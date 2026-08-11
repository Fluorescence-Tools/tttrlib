// SPDX-License-Identifier: BSD-3-Clause
#include "FileIO.h"
#include "io_hdf5_table.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef BUILD_PHOTON_HDF
#include <hdf5.h>
#endif

namespace tttrlib {
namespace io {

#ifndef BUILD_PHOTON_HDF

bool hdf5_table_available() { return false; }

void read_hdf5_table_into(data::DataStore&, const std::string&, const std::string&, bool) {
    throw std::runtime_error("not built with Photon HDF interface");
}

void read_hdf5_table_into(data::DataStore&, const std::string&, const std::string&,
                          bool, const std::vector<std::string>&,
                          std::uint64_t, std::uint64_t) {
    throw std::runtime_error("not built with Photon HDF interface");
}

std::uint64_t hdf5_bytes_read() { return 0; }

data::DataStore read_hdf5_table(const std::string&, const std::string&, bool) {
    throw std::runtime_error("not built with Photon HDF interface");
}

std::vector<std::string> read_hdf5_table_columns(const std::string&, const std::string&) {
    std::cerr << "Not built with Photon HDF interface." << std::endl;
    return {};
}

bool write_hdf5_table(const std::string&, const data::DataStore&,
                      const std::string&, int, Hdf5WriteMode) {
    std::cerr << "Not built with Photon HDF interface." << std::endl;
    return false;
}

// The queries are silent even here. A caller probes with these to find out
// whether a file is worth opening, often in a loop, and a predicate that
// narrates is a predicate nobody can use.
std::vector<std::string> hdf5_table_groups(const std::string&) { return {}; }
bool hdf5_table_has(const std::string&, const std::string&) { return false; }
bool hdf5_table_remove(const std::string&, const std::string&) { return false; }


#else

bool hdf5_table_available() { return true; }

namespace {

/// The DataStore type a stored HDF5 type maps to, or String for a text column.
bool store_type_of(hid_t type, data::ColumnType* out) {
    const H5T_class_t cls = H5Tget_class(type);
    if (cls == H5T_STRING) { *out = data::ColumnType::String; return true; }
    if (cls == H5T_FLOAT) {
        *out = H5Tget_size(type) <= 4 ? data::ColumnType::Float32
                                      : data::ColumnType::Float64;
        return true;
    }
    if (cls != H5T_INTEGER) return false;
    const bool is_signed = H5Tget_sign(type) == H5T_SGN_2;
    switch (H5Tget_size(type)) {
        case 1: *out = is_signed ? data::ColumnType::Int8 : data::ColumnType::UInt8; break;
        case 2: *out = is_signed ? data::ColumnType::Int16 : data::ColumnType::UInt16; break;
        case 4: *out = is_signed ? data::ColumnType::Int32 : data::ColumnType::UInt32; break;
        default: *out = is_signed ? data::ColumnType::Int64 : data::ColumnType::UInt64; break;
    }
    return true;
}

/// The in-memory HDF5 type for a column type. Reading through it is what keeps
/// a float32 column float32 instead of widening every value on the way in.
hid_t mem_type_of(data::ColumnType type) {
    switch (type) {
        case data::ColumnType::Float64: return H5T_NATIVE_DOUBLE;
        case data::ColumnType::Float32: return H5T_NATIVE_FLOAT;
        case data::ColumnType::Int64:   return H5T_NATIVE_INT64;
        case data::ColumnType::Int32:   return H5T_NATIVE_INT32;
        case data::ColumnType::Int16:   return H5T_NATIVE_INT16;
        case data::ColumnType::Int8:    return H5T_NATIVE_INT8;
        case data::ColumnType::UInt64:  return H5T_NATIVE_UINT64;
        case data::ColumnType::UInt32:  return H5T_NATIVE_UINT32;
        case data::ColumnType::UInt16:  return H5T_NATIVE_UINT16;
        case data::ColumnType::UInt8:   return H5T_NATIVE_UINT8;
        case data::ColumnType::Bool:    return H5T_NATIVE_UINT8;
        case data::ColumnType::String:  return -1;
    }
    return H5T_NATIVE_DOUBLE;
}

/// The stored type to write a column as.
hid_t file_type_of(data::ColumnType type) {
    switch (type) {
        case data::ColumnType::Float64: return H5T_IEEE_F64LE;
        case data::ColumnType::Float32: return H5T_IEEE_F32LE;
        case data::ColumnType::Int64:   return H5T_STD_I64LE;
        case data::ColumnType::Int32:   return H5T_STD_I32LE;
        case data::ColumnType::Int16:   return H5T_STD_I16LE;
        case data::ColumnType::Int8:    return H5T_STD_I8LE;
        case data::ColumnType::UInt64:  return H5T_STD_U64LE;
        case data::ColumnType::UInt32:  return H5T_STD_U32LE;
        case data::ColumnType::UInt16:  return H5T_STD_U16LE;
        case data::ColumnType::UInt8:   return H5T_STD_U8LE;
        case data::ColumnType::Bool:    return H5T_STD_U8LE;
        case data::ColumnType::String:  return -1;
    }
    return H5T_IEEE_F64LE;
}

/*!
 * \brief How many bytes this reader has moved. \see hdf5_bytes_read
 *
 * Counted where the read is issued rather than estimated afterwards, because
 * the claim being made is "the rows outside the window were never read" and an
 * estimate cannot support it.
 */
std::uint64_t g_bytes_read = 0;

/*!
 * \brief A window on one column's dataset, as a pair of dataspaces.
 *
 * `H5S_ALL` for both when the whole column is wanted, which is the same
 * argument the reader passed before there was a window -- so the unwindowed
 * path is byte for byte what it was.
 *
 * HDF5 selects natively: a hyperslab on the file space means the library reads
 * the chunks the range falls in and no others. Nothing is read and discarded,
 * which is the entire reason this is here rather than in a slicing layer above.
 */
class Slab {
public:
    Slab(hid_t ds, std::uint64_t first, std::uint64_t count) {
        if (count == 0) return;                      // the whole dataset
        file_ = H5Dget_space(ds);
        if (file_ < 0) { file_ = H5S_ALL; return; }
        const hsize_t start[1] = {static_cast<hsize_t>(first)};
        const hsize_t n[1] = {static_cast<hsize_t>(count)};
        H5Sselect_hyperslab(file_, H5S_SELECT_SET, start, nullptr, n, nullptr);
        mem_ = H5Screate_simple(1, n, nullptr);
    }
    ~Slab() {
        if (file_ != H5S_ALL) H5Sclose(file_);
        if (mem_ != H5S_ALL) H5Sclose(mem_);
    }
    Slab(const Slab&) = delete;
    Slab& operator=(const Slab&) = delete;

    hid_t file() const { return file_; }
    hid_t mem() const { return mem_; }
private:
    hid_t file_ = H5S_ALL;
    hid_t mem_ = H5S_ALL;
};

/// What a caller asked for, carried down to the per-column reads.
struct ReadOptions {
    /// Null or empty for every column. Matched per node, so a name absent from
    /// one group does not make that group an error.
    const std::vector<std::string>* columns = nullptr;
    std::uint64_t first_row = 0;
    /// 0 for "to the end", which is also what "no window was asked for" means.
    std::uint64_t n_rows = 0;

    bool wants(const std::string& name) const {
        if (columns == nullptr || columns->empty()) return true;
        for (const std::string& s : *columns) if (s == name) return true;
        return false;
    }
    /// The rows of a dataset of `n` this window covers, clamped to it. A group
    /// shorter than `first_row` comes back empty rather than throwing, matching
    /// what the native format does with the same knob.
    std::uint64_t take(std::uint64_t n) const {
        if (n_rows == 0 && first_row == 0) return n;
        if (first_row >= n) return 0;
        const std::uint64_t left = n - first_row;
        return n_rows == 0 || n_rows > left ? left : n_rows;
    }
    std::uint64_t start(std::uint64_t n) const {
        return first_row < n ? first_row : n;
    }
    bool windowed() const { return first_row != 0 || n_rows != 0; }
};

/// Write the column buffer straight into the column, with no per-value loop.
void read_numeric_column(hid_t ds, data::Column& column,
                         data::ColumnType type, std::size_t n,
                         std::uint64_t at, std::uint64_t got) {
    column.resize_uninitialized(got);
    if (got == 0) return;
    const Slab slab(ds, at, got);
    if (type == data::ColumnType::Bool) {
        // Bit-packed in the store, a byte per row on disk.
        std::vector<unsigned char> bytes(got, 0);
        H5Dread(ds, H5T_NATIVE_UINT8, slab.mem(), slab.file(), H5P_DEFAULT,
                bytes.data());
        column.set_bool(bytes.data(), static_cast<int>(got));
        g_bytes_read += got;
        return;
    }
    H5Dread(ds, mem_type_of(type), slab.mem(), slab.file(), H5P_DEFAULT,
            column.data_ptr());
    g_bytes_read += got * static_cast<std::uint64_t>(data::column_type_size(type));
    (void)n;
}

void read_string_column(hid_t ds, hid_t type, data::Column& column,
                        std::uint64_t at, std::uint64_t got) {
    const std::size_t n = static_cast<std::size_t>(got);
    const Slab slab(ds, at, got);
    const hid_t mem = H5Tcopy(H5T_C_S1);
    H5Tset_size(mem, H5T_VARIABLE);
    H5Tset_cset(mem, H5T_CSET_UTF8);
    if (H5Tis_variable_str(type)) {
        std::vector<char*> raw(n, nullptr);
        if (n > 0) H5Dread(ds, mem, slab.mem(), slab.file(), H5P_DEFAULT, raw.data());
        for (std::size_t i = 0; i < n; i++) {
            column.push_string(raw[i] != nullptr ? std::string(raw[i]) : std::string());
            g_bytes_read += raw[i] != nullptr ? std::strlen(raw[i]) : 0;
        }
        if (n > 0) {
            const hid_t space = slab.mem() != H5S_ALL ? slab.mem() : H5Dget_space(ds);
            H5Dvlen_reclaim(mem, space, H5P_DEFAULT, raw.data());
            if (slab.mem() == H5S_ALL) H5Sclose(space);
        }
    } else {
        // Fixed-length strings, which is what a NumPy "S8" column becomes.
        const std::size_t width = H5Tget_size(type);
        std::vector<char> buffer(n * width + 1, '\0');
        if (n > 0) {
            const hid_t fixed = H5Tcopy(H5T_C_S1);
            H5Tset_size(fixed, width);
            H5Dread(ds, fixed, slab.mem(), slab.file(), H5P_DEFAULT, buffer.data());
            H5Tclose(fixed);
            g_bytes_read += n * width;
        }
        for (std::size_t i = 0; i < n; i++) {
            const char* start = buffer.data() + i * width;
            std::size_t len = 0;
            while (len < width && start[len] != '\0') len++;
            column.push_string(std::string(start, len));
        }
    }
    H5Tclose(mem);
}

/*!
 * \brief The attribute that turns a column of integers back into text.
 *
 * A text column is dictionary-encoded in the store -- the distinct labels once,
 * plus an int32 code per row -- and writing it as one string per row throws
 * that away at the file boundary. On a burst table the difference is the whole
 * comparison: a million rows drawn from four labels cost 4 MB as codes and
 * 44 MB as strings, which is what put a written store *past* the DataFrame it
 * replaces rather than under it.
 *
 * So the codes are the dataset and the dictionary is an attribute on it. The
 * file stays self-describing -- everything needed to read the column is on the
 * column -- and a reader that ignores the attribute still gets a valid integer
 * column of category codes rather than nothing.
 */
const char* kDictionaryAttribute = "dictionary";

/*!
 * \brief The attribute carrying the column's description.
 *
 * A `.dstore` keeps one per column and HDF5 dropped it entirely, which made the
 * two formats disagree about what a column *is*: a lifetime written in
 * nanoseconds through HDF5 came back saying nothing about nanoseconds. It rides
 * as an attribute on the column's own dataset, like the dictionary and for the
 * same reason -- everything needed to read the column is on the column.
 *
 * Bytes rather than text: the value is msgpack, exactly what a `.dstore` holds,
 * so the two formats store the identical encoding and neither has to escape
 * anything. \see data::metadata_to_msgpack.
 */
const char* kMetadataAttribute = "tttrlib_metadata";

/// Whether an object carries a named attribute at all, which is not the same
/// question as whether it carries a non-empty one: a text column with no rows
/// has an empty dictionary and is still a text column.
bool has_attribute(hid_t obj, const char* attribute) {
    return H5Aexists(obj, attribute) > 0;
}

/// A string-list attribute, or empty when the object does not carry one.
std::vector<std::string> read_string_attribute(hid_t obj, const char* attribute) {
    std::vector<std::string> out;
    if (H5Aexists(obj, attribute) <= 0) return out;
    const hid_t attr = H5Aopen(obj, attribute, H5P_DEFAULT);
    if (attr < 0) return out;
    const hid_t space = H5Aget_space(attr);
    hsize_t dims[1] = {0};
    H5Sget_simple_extent_dims(space, dims, nullptr);
    const hid_t mem = H5Tcopy(H5T_C_S1);
    H5Tset_size(mem, H5T_VARIABLE);
    H5Tset_cset(mem, H5T_CSET_UTF8);
    std::vector<char*> raw(static_cast<std::size_t>(dims[0]), nullptr);
    if (dims[0] > 0 && H5Aread(attr, mem, raw.data()) >= 0) {
        for (char* s : raw) out.push_back(s != nullptr ? std::string(s) : std::string());
        H5Dvlen_reclaim(mem, space, H5P_DEFAULT, raw.data());
    }
    H5Tclose(mem);
    H5Sclose(space);
    H5Aclose(attr);
    return out;
}

/// A byte-array attribute as JSON text, or "" when the object has none.
/// \see kMetadataAttribute
std::string read_metadata_attribute(hid_t obj) {
    if (H5Aexists(obj, kMetadataAttribute) <= 0) return std::string();
    const hid_t attr = H5Aopen(obj, kMetadataAttribute, H5P_DEFAULT);
    if (attr < 0) return std::string();
    const hid_t space = H5Aget_space(attr);
    hsize_t dims[1] = {0};
    H5Sget_simple_extent_dims(space, dims, nullptr);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(dims[0]));
    std::string out;
    if (!bytes.empty() && H5Aread(attr, H5T_NATIVE_UINT8, bytes.data()) >= 0)
        out = data::metadata_from_msgpack(bytes.data(), bytes.size());
    H5Sclose(space);
    H5Aclose(attr);
    return out;
}

/// The `columns` attribute, if the writer left one.
std::vector<std::string> declared_order(hid_t group) {
    return read_string_attribute(group, "columns");
}

/// Whether a column type is one an integer dataset reads back as, i.e. one that
/// could be carrying dictionary codes. \see kDictionaryAttribute.
bool is_integer_type(data::ColumnType type) {
    switch (type) {
        case data::ColumnType::Int64:  case data::ColumnType::Int32:
        case data::ColumnType::Int16:  case data::ColumnType::Int8:
        case data::ColumnType::UInt64: case data::ColumnType::UInt32:
        case data::ColumnType::UInt16: case data::ColumnType::UInt8:
            return true;
        default:
            return false;
    }
}

/*!
 * \brief The dictionary codes of a text column, or nothing.
 *
 * Answers "no" -- so the caller reads the dataset as the integers it literally
 * holds -- when a code does not index the dictionary. That is a corrupt or
 * foreign file rather than a text column, and taking it as text would build a
 * column whose every access is out of bounds. Asked *before* the column is
 * created, because the answer is what decides its type.
 */
bool read_codes(hid_t ds, const std::vector<std::string>& dictionary,
                std::uint64_t at, std::uint64_t got,
                std::vector<std::int32_t>* out) {
    const std::size_t n = static_cast<std::size_t>(got);
    out->assign(n, 0);
    const Slab slab(ds, at, got);
    if (n > 0 && H5Dread(ds, H5T_NATIVE_INT32, slab.mem(), slab.file(), H5P_DEFAULT,
                         out->data()) < 0)
        return false;
    g_bytes_read += n * 4;
    const std::int32_t limit = static_cast<std::int32_t>(dictionary.size());
    for (std::int32_t code : *out)
        if (code < 0 || code >= limit) return false;
    return true;
}

/*!
 * Every dataset directly in the group, in the order HDF5 lists them.
 *
 * Whether a link is a dataset is decided by opening it, not by asking: the
 * call that answers that question changed signature between HDF5 1.10 and 1.12,
 * and this has to build against both.
 */
std::vector<std::string> dataset_names(hid_t group) {
    std::vector<std::string> out;
    H5G_info_t info;
    if (H5Gget_info(group, &info) < 0) return out;
    for (hsize_t i = 0; i < info.nlinks; i++) {
        const ssize_t len = H5Lget_name_by_idx(group, ".", H5_INDEX_NAME, H5_ITER_INC,
                                               i, nullptr, 0, H5P_DEFAULT);
        if (len <= 0) continue;
        std::string name(static_cast<std::size_t>(len), '\0');
        H5Lget_name_by_idx(group, ".", H5_INDEX_NAME, H5_ITER_INC, i,
                           &name[0], static_cast<std::size_t>(len) + 1, H5P_DEFAULT);
        const hid_t ds = H5Dopen2(group, name.c_str(), H5P_DEFAULT);
        if (ds < 0) continue;
        H5Dclose(ds);
        out.push_back(name);
    }
    return out;
}

/// Every link in the group that is itself a group, in the order HDF5 lists
/// them. \see dataset_names for why this opens rather than asks.
std::vector<std::string> subgroup_names(hid_t group) {
    std::vector<std::string> out;
    H5G_info_t info;
    if (H5Gget_info(group, &info) < 0) return out;
    for (hsize_t i = 0; i < info.nlinks; i++) {
        const ssize_t len = H5Lget_name_by_idx(group, ".", H5_INDEX_NAME, H5_ITER_INC,
                                               i, nullptr, 0, H5P_DEFAULT);
        if (len <= 0) continue;
        std::string name(static_cast<std::size_t>(len), '\0');
        H5Lget_name_by_idx(group, ".", H5_INDEX_NAME, H5_ITER_INC, i,
                           &name[0], static_cast<std::size_t>(len) + 1, H5P_DEFAULT);
        const hid_t child = H5Gopen2(group, name.c_str(), H5P_DEFAULT);
        if (child < 0) continue;
        H5Gclose(child);
        out.push_back(name);
    }
    return out;
}

/// The scratch group a replacement writes into. Never part of the answer to
/// what a file holds -- if one is ever visible, a write died mid-flight.
const char* kTempPrefix = "__tttrlib_tmp__";

bool is_temp_name(const std::string& name) {
    return name.compare(0, std::strlen(kTempPrefix), kTempPrefix) == 0;
}

const char* kMaskSuffix = "__mask";

/*!
 * A column name as an HDF5 dataset name.
 *
 * "/" separates path components in HDF5, so a column called "Sg/Sr" -- an
 * ordinary name for a signal ratio -- silently becomes a GROUP called "Sg"
 * holding a dataset called "Sr". The column then does not come back, and
 * nothing reports that it went missing.
 *
 * Percent-encoding the two characters that matter keeps the name reversible
 * and still readable in an HDF5 browser: "Sg/Sr" is stored as "Sg%2FSr". The
 * true name is what the `columns` attribute carries, so a reader that does not
 * know about the encoding still gets the right names, just not the right
 * datasets -- which is why the reader below tries both.
 */
std::string encode_name(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (c == '%') out += "%25";
        else if (c == '/') out += "%2F";
        else out += c;
    }
    return out;
}

/*!
 * Silence HDF5's automatic error printing for the lifetime of the object.
 *
 * Asking whether an optional dataset exists is a normal thing to do here -- the
 * `columns` attribute and the per-column masks are both optional -- and HDF5
 * answers "no" by printing a twelve-frame error stack to stderr before
 * returning the answer. The stack is not an error report, it is the
 * implementation narrating a lookup that failed, and there is no way to ask
 * without triggering it.
 *
 * It silences the WRITE paths too, which is why those report their own failures
 * through \ref write_failed rather than relying on the stack: a caller who gets
 * `false` back would otherwise have nothing to go on.
 *
 * `H5E_DEFAULT` is process-global. Nesting is safe -- the save and restore are
 * stack-like -- but this does not defend against another thread using HDF5 at
 * the same time, which would have its diagnostics suppressed for the duration.
 */
class QuietHdf5 {
public:
    QuietHdf5() { H5Eget_auto2(H5E_DEFAULT, &fn_, &data_);
                  H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr); }
    ~QuietHdf5() { H5Eset_auto2(H5E_DEFAULT, fn_, data_); }
    QuietHdf5(const QuietHdf5&) = delete;
    QuietHdf5& operator=(const QuietHdf5&) = delete;
private:
    H5E_auto2_t fn_ = nullptr;
    void* data_ = nullptr;
};

bool is_mask_name(const std::string& name) {
    const std::size_t k = std::strlen(kMaskSuffix);
    return name.size() > k && name.compare(name.size() - k, k, kMaskSuffix) == 0;
}

}  // namespace

namespace {

/// One group's datasets into one store. Takes an hid_t and decides nothing
/// about the file, so the tree walk can call it per node.
void read_table_into(hid_t group, data::DataStore& out, const std::string& filename,
                     const ReadOptions& opts) {
    std::vector<std::string> names = declared_order(group);
    const std::vector<std::string> present = dataset_names(group);
    if (names.empty()) {
        // No declared order: take the datasets as they come, minus the masks.
        for (const std::string& n : present) if (!is_mask_name(n)) names.push_back(n);
    }

    std::size_t n_rows = 0;
    bool first = true;
    for (const std::string& name : names) {
        // Matched per node, not per file: a name that is in one group and not
        // another leaves that other group with fewer columns rather than making
        // the read an error. Skipped before the dataset is opened, which is
        // what makes a subset read cost the subset.
        if (!opts.wants(name)) continue;
        hid_t ds = H5Dopen2(group, name.c_str(), H5P_DEFAULT);
        std::string dataset_name = name;
        if (ds < 0) {
            // A name the writer had to encode -- see encode_name.
            dataset_name = encode_name(name);
            ds = H5Dopen2(group, dataset_name.c_str(), H5P_DEFAULT);
        }
        if (ds < 0) {
            std::cerr << "hdf5 table: no dataset for column " << name << std::endl;
            continue;
        }
        const hid_t space = H5Dget_space(ds);
        const hid_t type = H5Dget_type(ds);
        hsize_t dims[2] = {0, 0};
        const int rank = H5Sget_simple_extent_ndims(space);
        if (rank == 1) H5Sget_simple_extent_dims(space, dims, nullptr);

        data::ColumnType column_type = data::ColumnType::Float64;
        const std::size_t n = static_cast<std::size_t>(dims[0]);
        const bool usable = rank == 1 && store_type_of(type, &column_type) &&
                            (first || opts.take(n) == n_rows);
        const std::uint64_t at = opts.start(n);
        const std::uint64_t got = opts.take(n);
        if (usable) {
            if (first) { n_rows = static_cast<std::size_t>(got); first = false; }
            // Codes plus a dictionary attribute is a text column. Both have to
            // be read before the column exists, because whether they are
            // consistent is what decides the column's type.
            std::vector<std::string> dictionary;
            std::vector<std::int32_t> codes;
            bool encoded = is_integer_type(column_type) &&
                           has_attribute(ds, kDictionaryAttribute);
            if (encoded) {
                dictionary = read_string_attribute(ds, kDictionaryAttribute);
                encoded = read_codes(ds, dictionary, at, got, &codes);
                if (!encoded)
                    std::cerr << "hdf5 table: " << name
                              << " has a dictionary attribute its values do not "
                                 "index; reading it as integers" << std::endl;
            }

            const int index = out.add_column(
                name, encoded ? data::ColumnType::String : column_type);
            data::Column& column = out.column(index);
            if (encoded) {
                column.set_dictionary(dictionary);
                column.set_codes(codes.data(), static_cast<int>(got));
            } else if (column_type == data::ColumnType::String) {
                read_string_column(ds, type, column, at, got);
            } else {
                read_numeric_column(ds, column, column_type, n, at, got);
            }

            // After the column exists and before its mask: the description may
            // carry a `name`, and restoring it is how a name HDF5 could not
            // hold as a link -- one with a `/` in it -- comes back whole.
            const std::string meta = read_metadata_attribute(ds);
            if (!meta.empty()) column.set_metadata(meta);

            // A per-column validity mask, if one was written. This is the only
            // way an INTEGER column can say a value is missing -- a float has
            // NaN, an integer has nothing to spare.
            const std::string mask_name = dataset_name + kMaskSuffix;
            if (H5Lexists(group, mask_name.c_str(), H5P_DEFAULT) > 0) {
                const hid_t mds = H5Dopen2(group, mask_name.c_str(), H5P_DEFAULT);
                if (mds >= 0) {
                    std::vector<unsigned char> bytes(static_cast<std::size_t>(got), 1);
                    if (got > 0) {
                        const Slab mask_slab(mds, at, got);
                        H5Dread(mds, H5T_NATIVE_UINT8, mask_slab.mem(),
                                mask_slab.file(), H5P_DEFAULT, bytes.data());
                        g_bytes_read += got;
                    }
                    column.set_mask(bytes.data(), static_cast<int>(got));
                    H5Dclose(mds);
                }
            }
        } else if (rank != 1) {
            std::cerr << "hdf5 table: skipping " << name
                      << " (not one-dimensional)" << std::endl;
        } else if (!first) {
            std::cerr << "hdf5 table: skipping " << name << " (" << n
                      << " rows, expected " << n_rows << ")" << std::endl;
        }

        H5Tclose(type);
        H5Sclose(space);
        H5Dclose(ds);
    }

    out.set_n_rows(n_rows);
    if (out.label().empty()) out.set_label(filename);
}

}  // namespace

data::DataStore read_hdf5_table(const std::string& filename,
                                const std::string& group_name, bool with_groups) {
    data::DataStore out;
    read_hdf5_table_into(out, filename, group_name, with_groups);
    return out;
}

std::vector<std::string> read_hdf5_table_columns(const std::string& filename,
                                                 const std::string& group_name) {
    const QuietHdf5 quiet;
    const hid_t file = H5Fopen(filename.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file < 0) return {};
    const hid_t group = H5Gopen2(file, group_name.c_str(), H5P_DEFAULT);
    if (group < 0) { H5Fclose(file); return {}; }
    std::vector<std::string> names = declared_order(group);
    if (names.empty())
        for (const std::string& n : dataset_names(group))
            if (!is_mask_name(n)) names.push_back(n);
    H5Gclose(group);
    H5Fclose(file);
    return names;
}

namespace {

/*!
 * \brief Say that a write failed, once, naming what and where.
 *
 * The QuietHdf5 guard silences HDF5's own error stack, which is right for the
 * queries -- probing a foreign file is a normal thing to do and should not
 * narrate. It is wrong for a write: a caller who gets `false` back has no other
 * way to find out which column stopped it.
 */
bool write_failed(const char* what, const std::string& name) {
    std::cerr << "hdf5 table: could not " << what << " '" << name << "'" << std::endl;
    return false;
}

/// One column, chunked and optionally deflated. Chunking is not decoration: an
/// unchunked dataset cannot be compressed, and a chunk of the whole column
/// would have to be inflated in full to read any of it.
bool write_column_dataset(hid_t group, const std::string& name, hid_t file_type,
                          hid_t mem_type, std::size_t n, const void* data,
                          int compression) {
    const hsize_t dims[1] = {static_cast<hsize_t>(n)};
    const hid_t space = H5Screate_simple(1, dims, nullptr);
    if (space < 0) return write_failed("describe", name);
    hid_t plist = H5P_DEFAULT;
    const std::size_t element = std::max<std::size_t>(1, H5Tget_size(file_type));
    if (n > 0 && compression > 0) {
        const hsize_t chunk[1] = {
            std::min<hsize_t>(dims[0], std::max<hsize_t>(1, 1u << 20) / element)};
        plist = H5Pcreate(H5P_DATASET_CREATE);
        H5Pset_chunk(plist, 1, chunk);
        H5Pset_deflate(plist, std::min(9, compression));
    }
    const hid_t ds = H5Dcreate2(group, name.c_str(), file_type, space,
                                H5P_DEFAULT, plist, H5P_DEFAULT);
    bool ok = ds >= 0;
    if (ok) {
        if (n > 0 && H5Dwrite(ds, mem_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, data) < 0)
            ok = false;
        H5Dclose(ds);
    }
    if (plist != H5P_DEFAULT) H5Pclose(plist);
    H5Sclose(space);
    return ok ? true : write_failed("write column", name);
}

/// A list of strings as one attribute -- the `columns` order, and later the
/// group order. Variable-length UTF-8, so a name is whatever the caller called
/// it.
bool write_string_attribute(hid_t obj, const char* attribute,
                            const std::vector<std::string>& values) {
    std::vector<const char*> pointers;
    pointers.reserve(values.size());
    for (const std::string& v : values) pointers.push_back(v.c_str());

    const hsize_t dims[1] = {static_cast<hsize_t>(pointers.size())};
    const hid_t space = H5Screate_simple(1, dims, nullptr);
    if (space < 0) return write_failed("describe attribute", attribute);
    const hid_t vlen = H5Tcopy(H5T_C_S1);
    H5Tset_size(vlen, H5T_VARIABLE);
    H5Tset_cset(vlen, H5T_CSET_UTF8);

    const hid_t attr = H5Acreate2(obj, attribute, vlen, space, H5P_DEFAULT, H5P_DEFAULT);
    bool ok = attr >= 0;
    if (ok) {
        if (!pointers.empty() && H5Awrite(attr, vlen, pointers.data()) < 0) ok = false;
        H5Aclose(attr);
    }
    H5Tclose(vlen);
    H5Sclose(space);
    return ok ? true : write_failed("write attribute", attribute);
}

/// The column's description as one byte attribute. A column with none gets no
/// attribute rather than an empty one -- nothing should acquire a description
/// merely by being written. \see kMetadataAttribute
bool write_metadata_attribute(hid_t obj, const std::string& json_text) {
    if (json_text.empty()) return true;
    const std::vector<unsigned char> bytes = data::metadata_to_msgpack(json_text);
    if (bytes.empty()) return true;
    hsize_t dims[1] = {static_cast<hsize_t>(bytes.size())};
    const hid_t space = H5Screate_simple(1, dims, nullptr);
    if (space < 0) return write_failed("describe attribute", kMetadataAttribute);
    const hid_t attr = H5Acreate2(obj, kMetadataAttribute, H5T_STD_U8LE, space,
                                  H5P_DEFAULT, H5P_DEFAULT);
    bool ok = attr >= 0;
    if (ok) {
        if (H5Awrite(attr, H5T_NATIVE_UINT8, bytes.data()) < 0) ok = false;
        H5Aclose(attr);
    }
    H5Sclose(space);
    return ok ? true : write_failed("write attribute", kMetadataAttribute);
}

/// Gather the selected rows into a contiguous buffer of the column's OWN type.
///
/// The shortcut this replaces -- gather into a double and let HDF5 narrow it --
/// is wrong for the two widest integers. A double carries 53 bits of mantissa,
/// so an Int64 or UInt64 above 2^53 was written as a different number. It could
/// only ever show on the gated path, because an ungated write hands HDF5 the
/// column's buffer untouched, which is why it went unnoticed: the values
/// changed the moment a selection was set and not before.
template <typename T>
bool write_gathered(hid_t group, const std::string& name, const data::Column& c,
                    const std::vector<std::size_t>& rows, bool gated,
                    std::size_t n_out, int compression) {
    std::vector<T> values(n_out);
    const T* src = static_cast<const T*>(c.data_ptr());
    if (src != nullptr)
        for (std::size_t i = 0; i < n_out; i++)
            values[i] = src[gated ? rows[i] : i];
    return write_column_dataset(group, name, file_type_of(c.type()),
                                mem_type_of(c.type()), n_out, values.data(),
                                compression);
}

/// \see write_gathered. Bool and String have no contiguous buffer to gather
/// from -- one is bit-packed, the other dictionary-encoded -- and the caller
/// writes them its own way, so reaching them here is a bug rather than a case.
bool write_gathered_typed(hid_t group, const std::string& name,
                          const data::Column& c,
                          const std::vector<std::size_t>& rows, bool gated,
                          std::size_t n_out, int compression) {
    switch (c.type()) {
        case data::ColumnType::Float64:
            return write_gathered<double>(group, name, c, rows, gated, n_out,
                                     compression);
        case data::ColumnType::Float32:
            return write_gathered<float>(group, name, c, rows, gated, n_out,
                                     compression);
        case data::ColumnType::Int64:
            return write_gathered<std::int64_t>(group, name, c, rows, gated, n_out,
                                     compression);
        case data::ColumnType::Int32:
            return write_gathered<std::int32_t>(group, name, c, rows, gated, n_out,
                                     compression);
        case data::ColumnType::Int16:
            return write_gathered<std::int16_t>(group, name, c, rows, gated, n_out,
                                     compression);
        case data::ColumnType::Int8:
            return write_gathered<std::int8_t>(group, name, c, rows, gated, n_out,
                                     compression);
        case data::ColumnType::UInt64:
            return write_gathered<std::uint64_t>(group, name, c, rows, gated, n_out,
                                     compression);
        case data::ColumnType::UInt32:
            return write_gathered<std::uint32_t>(group, name, c, rows, gated, n_out,
                                     compression);
        case data::ColumnType::UInt16:
            return write_gathered<std::uint16_t>(group, name, c, rows, gated, n_out,
                                     compression);
        case data::ColumnType::UInt8:
            return write_gathered<std::uint8_t>(group, name, c, rows, gated, n_out,
                                     compression);
        case data::ColumnType::Bool:
        case data::ColumnType::String:
            return false;
    }
    return false;
}

/*!
 * \brief Which rows a write covers.
 *
 * Only the selected ones, when the store has a selection. Writing the whole
 * table and expecting the reader to gate it again would put the selection in
 * two places, and the point of exporting a subset is that it IS the subset.
 */
struct RowGate {
    bool gated = false;
    std::vector<std::size_t> rows;   ///< source row of each output row; empty when not gated
    std::size_t n_out = 0;

    std::size_t source(std::size_t i) const { return gated ? rows[i] : i; }
};

RowGate row_gate_of(const data::DataStore& store) {
    RowGate g;
    g.gated = store.has_row_mask();
    if (g.gated) {
        g.rows.reserve(store.n_selected());
        for (std::size_t i = 0; i < store.n_rows(); i++)
            if (store.row_selected(i)) g.rows.push_back(i);
    }
    g.n_out = g.gated ? g.rows.size() : store.n_rows();
    return g;
}

/*!
 * \brief A store's columns into an already-open group.
 *
 * Takes an hid_t and makes no decision about the file: whether that group was
 * created, opened, or is a temporary about to be moved into place is the
 * caller's business.
 */
bool write_table_into(hid_t dest, const data::DataStore& store, int compression) {
    const RowGate gate = row_gate_of(store);
    const std::size_t n_out = gate.n_out;

    std::vector<std::string> names;
    for (int c = 0; c < store.n_columns(); c++) {
        const data::Column& column = store.column(c);
        const std::string stored = encode_name(column.name());
        names.push_back(column.name());
        bool ok = true;

        if (column.type() == data::ColumnType::String) {
            // The codes, not the labels. \see kDictionaryAttribute. Ungated,
            // that is the column's own buffer and there is nothing to gather.
            const data::RawVector<std::int32_t>& source_codes = column.codes();
            std::vector<std::int32_t> gathered;
            if (gate.gated) {
                gathered.resize(n_out);
                for (std::size_t i = 0; i < n_out; i++)
                    gathered[i] = source_codes[gate.source(i)];
            }
            ok = write_column_dataset(
                dest, stored, H5T_STD_I32LE, H5T_NATIVE_INT32, n_out,
                gate.gated ? gathered.data() : source_codes.data(), compression);
            if (ok) {
                const hid_t ds = H5Dopen2(dest, stored.c_str(), H5P_DEFAULT);
                if (ds < 0) {
                    ok = write_failed("reopen for its dictionary", stored);
                } else {
                    ok = write_string_attribute(ds, kDictionaryAttribute,
                                                column.dictionary());
                    H5Dclose(ds);
                }
            }
        } else if (!gate.gated && column.type() != data::ColumnType::Bool &&
                   column.data_ptr() != nullptr) {
            // The whole column, straight out of its buffer.
            ok = write_column_dataset(dest, stored, file_type_of(column.type()),
                                      mem_type_of(column.type()), n_out,
                                      column.data_ptr(), compression);
        } else if (column.type() != data::ColumnType::Bool) {
            ok = write_gathered_typed(dest, stored, column, gate.rows, gate.gated,
                                      n_out, compression);
        } else {
            // Bool: bit-packed in the store, a byte per row on disk, which is
            // what the reader's Bool branch expects.
            std::vector<unsigned char> values(n_out);
            for (std::size_t i = 0; i < n_out; i++)
                values[i] = column.value_at(gate.source(i)) != 0.0 ? 1 : 0;
            ok = write_column_dataset(dest, stored, file_type_of(column.type()),
                                      H5T_NATIVE_UINT8, n_out, values.data(),
                                      compression);
        }
        if (!ok) return false;

        // The description, on the column it describes. Only opened when there
        // is one, so a store that carries none pays nothing for the feature.
        if (!column.metadata().empty()) {
            const hid_t ds = H5Dopen2(dest, stored.c_str(), H5P_DEFAULT);
            if (ds < 0) return write_failed("reopen for its description", stored);
            ok = write_metadata_attribute(ds, column.metadata());
            H5Dclose(ds);
            if (!ok) return false;
        }

        // `has_missing`, not `has_mask`: a column whose gaps are recorded as
        // ranges has no bit mask and still has gaps. The description carries
        // the ranges as well, so a tttrlib reader gets the reason back -- but
        // this format exists to hand a table to something that is NOT tttrlib,
        // and that reader needs the per-row answer written down.
        if (column.has_missing()) {
            std::vector<unsigned char> bytes(n_out, 1);
            for (std::size_t i = 0; i < n_out; i++)
                bytes[i] = column.valid(gate.source(i)) ? 1 : 0;
            if (!write_column_dataset(dest, stored + kMaskSuffix, H5T_STD_U8LE,
                                      H5T_NATIVE_UINT8, n_out, bytes.data(),
                                      compression))
                return false;
        }
    }

    // The column order, so a reader does not have to guess it from however HDF5
    // happens to list the group.
    return write_string_attribute(dest, "columns", names);
}

/*!
 * \brief The first place a column and a group share a name, or empty.
 *
 * HDF5 has one link namespace per group, so a dataset `meta` and a group `meta`
 * cannot both exist. In memory they can, deliberately -- a DataStore keeps the
 * two namespaces apart because nothing there has to disambiguate them -- so the
 * refusal belongs here, and has to happen before anything is written rather
 * than halfway through.
 */
std::string first_name_collision(const data::DataStore& store,
                                 const std::string& prefix) {
    for (const std::string& name : store.group_names()) {
        if (store.find(name) >= 0) return prefix + name;
        const std::string deeper =
                first_name_collision(store.group(name), prefix + name + "/");
        if (!deeper.empty()) return deeper;
    }
    return std::string();
}

/*!
 * \brief A store and its groups into an already-open group, recursively.
 *
 * The child groups are listed in a `groups` attribute, for the same reason the
 * columns are: HDF5 lists a group's links in whatever order it likes, and
 * insertion order is what a round trip has to give back. Keeping the order as
 * data also makes it immune to the H5Lmove that replacing a group performs --
 * an attribute belongs to the object and travels with it, where HDF5's own
 * creation-order index would put the moved group back at the end.
 *
 * A store with no groups writes no attribute, so its file stays byte-for-byte
 * what the previous version produced.
 */
bool write_tree_into(hid_t dest, const data::DataStore& store, int compression) {
    if (!write_table_into(dest, store, compression)) return false;

    const std::vector<std::string> names = store.group_names();
    if (names.empty()) return true;

    for (const std::string& name : names) {
        const hid_t child = H5Gcreate2(dest, encode_name(name).c_str(),
                                       H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        if (child < 0) return write_failed("create group", name);
        const bool ok = write_tree_into(child, store.group(name), compression);
        H5Gclose(child);
        if (!ok) return false;
    }
    return write_string_attribute(dest, "groups", names);
}

/// A path with no leading or trailing separator. Empty means the root.
std::string normalise_group(const std::string& path) {
    std::size_t b = 0, e = path.size();
    while (b < e && path[b] == '/') b++;
    while (e > b && path[e - 1] == '/') e--;
    return path.substr(b, e - b);
}

bool file_exists(const std::string& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    return f.good();
}

/*!
 * \brief Is this an HDF5 file?
 *
 * Asked directly rather than inferred from H5Fopen failing, which also fails on
 * a permission error -- and truncating then would destroy a file the caller
 * cannot even read.
 *
 * H5Fis_hdf5 moved behind H5_NO_DEPRECATED_SYMBOLS in HDF5 2.0, and
 * H5Fis_accessible has been there since 1.12; the module already builds against
 * both 1.10 and 1.12, so pick per version rather than per build flag.
 */
bool looks_like_hdf5(const std::string& path) {
#if defined(H5_VERSION_GE) && H5_VERSION_GE(1, 12, 0)
    return H5Fis_accessible(path.c_str(), H5P_DEFAULT) > 0;
#else
    return H5Fis_hdf5(path.c_str()) > 0;
#endif
}

/// Creates missing intermediates, so "/a/b" does not need "/a" to exist first.
hid_t lcpl_intermediate() {
    const hid_t lcpl = H5Pcreate(H5P_LINK_CREATE);
    if (lcpl >= 0) H5Pset_create_intermediate_group(lcpl, 1);
    return lcpl;
}

/// A name no caller would choose, free in this group.
std::string free_temp_name(hid_t parent) {
    for (int i = 0; i < 1000; i++) {
        std::string name = std::string(kTempPrefix) + std::to_string(i);
        if (H5Lexists(parent, name.c_str(), H5P_DEFAULT) <= 0) return name;
    }
    return std::string();
}

/*!
 * \brief Replace one group of an open file, or leave it exactly as it was.
 *
 * HDF5 has no transactions, so this is as close as the format allows: the new
 * contents go into a sibling temporary, and only once every column is safely
 * written is the old group unlinked and the temporary moved into its place.
 * A failure anywhere before that leaves the original readable.
 *
 * The target has to be unlinked rather than emptied -- deleting its datasets one
 * by one leaves the group behind, and H5Gcreate2 on an existing group fails.
 */
bool replace_group(hid_t file, const std::string& path,
                   const data::DataStore& store, int compression) {
    const std::size_t cut = path.rfind('/');
    const std::string parent_path = cut == std::string::npos ? std::string()
                                                             : path.substr(0, cut);
    const std::string leaf = cut == std::string::npos ? path : path.substr(cut + 1);

    const hid_t lcpl = lcpl_intermediate();
    hid_t parent = file;
    bool own_parent = false;
    if (!parent_path.empty()) {
        parent = H5Gopen2(file, parent_path.c_str(), H5P_DEFAULT);
        if (parent < 0)
            parent = H5Gcreate2(file, parent_path.c_str(), lcpl, H5P_DEFAULT,
                                H5P_DEFAULT);
        if (parent < 0) {
            if (lcpl >= 0) H5Pclose(lcpl);
            return write_failed("create group", parent_path);
        }
        own_parent = true;
    }

    bool ok = false;
    const std::string temp = free_temp_name(parent);
    if (temp.empty()) {
        write_failed("find a free temporary name in", path);
    } else {
        const hid_t scratch = H5Gcreate2(parent, temp.c_str(), H5P_DEFAULT,
                                         H5P_DEFAULT, H5P_DEFAULT);
        if (scratch < 0) {
            write_failed("create group", path);
        } else {
            ok = write_tree_into(scratch, store, compression);
            H5Gclose(scratch);
        }
        if (ok) {
            if (H5Lexists(parent, leaf.c_str(), H5P_DEFAULT) > 0 &&
                H5Ldelete(parent, leaf.c_str(), H5P_DEFAULT) < 0) {
                ok = write_failed("remove the previous", path);
            } else if (H5Lmove(parent, temp.c_str(), parent, leaf.c_str(),
                               lcpl, H5P_DEFAULT) < 0) {
                ok = write_failed("move the new group into", path);
            }
        }
        // Whatever went wrong, the scratch group must not survive it.
        if (!ok && H5Lexists(parent, temp.c_str(), H5P_DEFAULT) > 0)
            H5Ldelete(parent, temp.c_str(), H5P_DEFAULT);
    }

    if (own_parent) H5Gclose(parent);
    if (lcpl >= 0) H5Pclose(lcpl);
    if (ok) H5Fflush(file, H5F_SCOPE_GLOBAL);
    return ok;
}

/*!
 * \brief Replace the whole file, without a window in which it is broken.
 *
 * The root group is the one group that cannot be swapped from inside: H5Lmove
 * needs a sibling and the root has none. Writing a temporary file beside it and
 * renaming over the original gives the same guarantee more cheaply -- rename is
 * atomic -- and it is also the only way the file does not grow, since HDF5 never
 * reuses the space a replaced group leaves behind.
 */
bool replace_whole_file(const std::string& filename, const data::DataStore& store,
                        int compression) {
    const std::string temp = filename + ".tttrlib-tmp";
    const hid_t file = H5Fcreate(temp.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (file < 0) return write_failed("create", temp);

    bool ok = write_tree_into(file, store, compression);
    if (H5Fclose(file) < 0) ok = false;

    if (ok && !replace_file(temp, filename))
        ok = write_failed("rename the new file over", filename);
    if (!ok) std::remove(temp.c_str());
    return ok;
}

/*!
 * \brief Does this group hold a table?
 *
 * At least one 1-D dataset, and every 1-D dataset the same length. Sub-groups
 * are not datasets and are ignored, so a root table with a `/meta` group beside
 * it is still a table -- that is the imaging layout and it has to work.
 *
 * A `__mask` sidecar is counted in the length check but does not by itself make
 * a group a table: a group holding nothing but masks describes columns that are
 * not there.
 */
bool group_is_table(hid_t group) {
    bool any_column = false, any_length = false;
    hsize_t length = 0;
    for (const std::string& name : dataset_names(group)) {
        const hid_t ds = H5Dopen2(group, name.c_str(), H5P_DEFAULT);
        if (ds < 0) continue;
        const hid_t space = H5Dget_space(ds);
        hsize_t dims[1] = {0};
        const int rank = H5Sget_simple_extent_ndims(space);
        if (rank == 1) H5Sget_simple_extent_dims(space, dims, nullptr);
        H5Sclose(space);
        H5Dclose(ds);
        if (rank != 1) continue;

        if (!any_length) { any_length = true; length = dims[0]; }
        else if (dims[0] != length) return false;
        if (!is_mask_name(name)) any_column = true;
    }
    return any_column;
}

/*!
 * \brief The child groups, in the order the file says.
 *
 * The `groups` attribute where there is one, then anything else by name. The
 * merge matters for a file something else has added a group to: ours keep their
 * order and the newcomer lands after them, rather than the whole listing
 * falling back to alphabetical.
 */
std::vector<std::string> ordered_subgroups(hid_t group) {
    std::vector<std::string> found = subgroup_names(group);
    std::vector<std::string> out;
    for (const std::string& declared : read_string_attribute(group, "groups")) {
        const auto it = std::find(found.begin(), found.end(), declared);
        if (it == found.end()) continue;
        out.push_back(*it);
        found.erase(it);
    }
    out.insert(out.end(), found.begin(), found.end());
    out.erase(std::remove_if(out.begin(), out.end(), is_temp_name), out.end());
    return out;
}

/// Every group at or under this one that holds a table, depth first.
void collect_tables(hid_t group, const std::string& path,
                    std::vector<std::string>& out) {
    if (group_is_table(group)) out.push_back(path.empty() ? "/" : path);
    for (const std::string& name : ordered_subgroups(group)) {
        const hid_t child = H5Gopen2(group, name.c_str(), H5P_DEFAULT);
        if (child < 0) continue;
        collect_tables(child, path + "/" + name, out);
        H5Gclose(child);
    }
}

/// Open the file for a question. Negative for anything that is not one of ours
/// to read, and silent about it: probing is a normal thing for a caller to do.
hid_t open_for_query(const std::string& filename) {
    if (!file_exists(filename) || !looks_like_hdf5(filename)) return -1;
    return H5Fopen(filename.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
}

/// The table in a group -- its datasets and its `columns` attribute -- leaving
/// any sub-group alone. What "remove" means at the root, which cannot itself be
/// unlinked.
bool clear_table(hid_t group) {
    bool ok = true;
    for (const std::string& name : dataset_names(group))
        if (H5Ldelete(group, name.c_str(), H5P_DEFAULT) < 0) ok = false;
    if (H5Aexists(group, "columns") > 0 && H5Adelete(group, "columns") < 0) ok = false;
    return ok;
}

}  // namespace

namespace {

/*!
 * \brief Does this group, or anything under it, hold a table?
 *
 * What decides whether a foreign sub-group is descended into. A Photon-HDF5 or
 * pandas file read at the root should give back the parts that are tables and
 * quietly skip the rest -- reading foreign layouts is not this module's job,
 * but failing on them is not either.
 */
bool has_table_below(hid_t group) {
    if (group_is_table(group)) return true;
    for (const std::string& name : subgroup_names(group)) {
        if (is_temp_name(name)) continue;
        const hid_t child = H5Gopen2(group, name.c_str(), H5P_DEFAULT);
        if (child < 0) continue;
        const bool found = has_table_below(child);
        H5Gclose(child);
        if (found) return true;
    }
    return false;
}

/// The tree at `group`, rebuilt under `out`.
void read_tree_into(hid_t group, data::DataStore& out, const std::string& filename,
                    const ReadOptions& opts) {
    read_table_into(group, out, filename, opts);

    // The true names the writer recorded, so a percent-encoded link comes back
    // under the name the store used. Anything not in the attribute is a group
    // something else put there, and keeps its name as it is on disk.
    const std::vector<std::string> declared = read_string_attribute(group, "groups");
    for (const std::string& link : ordered_subgroups(group)) {
        std::string name = link;
        for (const std::string& d : declared)
            if (encode_name(d) == link) { name = d; break; }

        const hid_t child = H5Gopen2(group, link.c_str(), H5P_DEFAULT);
        if (child < 0) continue;
        // A group we named is always taken, so a container group with no
        // columns of its own survives the round trip. One we did not is taken
        // only if there is a table somewhere in it.
        const bool ours = std::find(declared.begin(), declared.end(), name) != declared.end();
        if (ours || has_table_below(child)) {
            data::DataStore& into = out.ensure_group(name);
            read_tree_into(child, into, filename, opts);
        }
        H5Gclose(child);
    }
}

}  // namespace

void read_hdf5_table_into(data::DataStore& out, const std::string& filename,
                          const std::string& group_name, bool with_groups) {
    read_hdf5_table_into(out, filename, group_name, with_groups,
                         std::vector<std::string>(), 0, 0);
}

void read_hdf5_table_into(data::DataStore& out, const std::string& filename,
                          const std::string& group_name, bool with_groups,
                          const std::vector<std::string>& columns,
                          std::uint64_t first_row, std::uint64_t n_rows) {
    const QuietHdf5 quiet;
    const hid_t file = H5Fopen(filename.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file < 0) throw std::runtime_error("cannot open " + filename);
    const std::string path = normalise_group(group_name);
    const hid_t group = path.empty() ? file
                                     : H5Gopen2(file, path.c_str(), H5P_DEFAULT);
    if (group < 0) {
        H5Fclose(file);
        throw std::runtime_error("no group " + group_name + " in " + filename);
    }

    ReadOptions opts;
    opts.columns = &columns;
    opts.first_row = first_row;
    opts.n_rows = n_rows;
    if (with_groups) read_tree_into(group, out, filename, opts);
    else read_table_into(group, out, filename, opts);

    if (!path.empty()) H5Gclose(group);
    H5Fclose(file);

    // Nothing at all -- no columns and no groups -- is a group that does not
    // hold a table, and it has to be distinguishable from a table that
    // legitimately has none of its rows. It used to come back as an empty
    // store, so a caller that trusted the result opened a foreign file, saw a
    // table with nothing in it, and reported success.
    //
    // A table with columns and no rows does NOT come through here, which is the
    // whole point: zero rows is an answer, zero columns is a refusal.
    if (out.n_columns() == 0 && out.n_groups() == 0)
        throw std::runtime_error("no table in " + group_name + " of " + filename);
}

std::uint64_t hdf5_bytes_read() { return g_bytes_read; }


bool write_hdf5_table(const std::string& filename, const data::DataStore& store,
                      const std::string& group_name, int compression,
                      Hdf5WriteMode mode) {
    const QuietHdf5 quiet;
    const std::string path = normalise_group(group_name);
    const bool present = file_exists(filename);

    // Before the file is touched at all: a store that cannot be represented
    // must not take the old one with it.
    const std::string clash = first_name_collision(store, std::string());
    if (!clash.empty()) {
        std::cerr << "hdf5 table: '" << clash << "' is both a column and a group,"
                  << " which one HDF5 group cannot hold; nothing was written"
                  << std::endl;
        return false;
    }

    // Do not truncate someone else's file by inference. A caller that means
    // "replace whatever is there" says Truncate.
    if (mode == Hdf5WriteMode::Update && present && !looks_like_hdf5(filename)) {
        std::cerr << "hdf5 table: " << filename << " exists and is not an HDF5 file; "
                  << "pass Hdf5WriteMode::Truncate to replace it" << std::endl;
        return false;
    }

    // The root is replaced wholesale either way -- writing a group replaces
    // everything under it, and everything is under the root.
    if (path.empty()) return replace_whole_file(filename, store, compression);

    const bool keep = mode == Hdf5WriteMode::Update && present;
    const hid_t file = keep
            ? H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT)
            : H5Fcreate(filename.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (file < 0) return write_failed(keep ? "open for writing" : "create", filename);

    const bool ok = replace_group(file, path, store, compression);
    H5Fclose(file);
    return ok;
}

std::vector<std::string> hdf5_table_groups(const std::string& filename) {
    const QuietHdf5 quiet;
    std::vector<std::string> out;
    const hid_t file = open_for_query(filename);
    if (file < 0) return out;
    collect_tables(file, std::string(), out);
    H5Fclose(file);
    return out;
}

bool hdf5_table_has(const std::string& filename, const std::string& group_name) {
    const QuietHdf5 quiet;
    const hid_t file = open_for_query(filename);
    if (file < 0) return false;

    const std::string path = normalise_group(group_name);
    const hid_t group = path.empty() ? file
                                     : H5Gopen2(file, path.c_str(), H5P_DEFAULT);
    const bool ok = group >= 0 && group_is_table(group);
    if (group >= 0 && !path.empty()) H5Gclose(group);
    H5Fclose(file);
    return ok;
}

bool hdf5_table_remove(const std::string& filename, const std::string& group_name) {
    const QuietHdf5 quiet;
    if (!file_exists(filename) || !looks_like_hdf5(filename)) return false;
    const hid_t file = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
    if (file < 0) return false;

    const std::string path = normalise_group(group_name);
    bool ok;
    if (path.empty()) {
        // The root cannot be unlinked, so removing it means removing the table
        // it holds. Its sub-groups are their own tables and are left alone.
        ok = group_is_table(file) && clear_table(file);
    } else {
        ok = H5Lexists(file, path.c_str(), H5P_DEFAULT) > 0 &&
             H5Ldelete(file, path.c_str(), H5P_DEFAULT) >= 0;
    }
    if (ok) H5Fflush(file, H5F_SCOPE_GLOBAL);
    H5Fclose(file);
    return ok;
}

#endif  // BUILD_PHOTON_HDF

}  // namespace io
}  // namespace tttrlib
