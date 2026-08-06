// SPDX-License-Identifier: BSD-3-Clause
#include "io_hdf5_table.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#ifdef BUILD_PHOTON_HDF
#include <hdf5.h>
#endif

namespace tttrlib {
namespace io {

#ifndef BUILD_PHOTON_HDF

bool hdf5_table_available() { return false; }

void read_hdf5_table_into(data::DataStore&, const std::string&, const std::string&) {
    throw std::runtime_error("not built with Photon HDF interface");
}

data::DataStore read_hdf5_table(const std::string&, const std::string&) {
    throw std::runtime_error("not built with Photon HDF interface");
}

std::vector<std::string> read_hdf5_table_columns(const std::string&, const std::string&) {
    std::cerr << "Not built with Photon HDF interface." << std::endl;
    return {};
}

bool write_hdf5_table(const std::string&, const data::DataStore&,
                      const std::string&, int) {
    std::cerr << "Not built with Photon HDF interface." << std::endl;
    return false;
}


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

/// Write the column buffer straight into the column, with no per-value loop.
void read_numeric_column(hid_t ds, data::Column& column,
                         data::ColumnType type, std::size_t n) {
    column.resize_uninitialized(n);
    if (n == 0) return;
    if (type == data::ColumnType::Bool) {
        // Bit-packed in the store, a byte per row on disk.
        std::vector<unsigned char> bytes(n, 0);
        H5Dread(ds, H5T_NATIVE_UINT8, H5S_ALL, H5S_ALL, H5P_DEFAULT, bytes.data());
        column.set_bool(bytes.data(), static_cast<int>(n));
        return;
    }
    H5Dread(ds, mem_type_of(type), H5S_ALL, H5S_ALL, H5P_DEFAULT, column.data_ptr());
}

void read_string_column(hid_t ds, hid_t type, data::Column& column, std::size_t n) {
    const hid_t mem = H5Tcopy(H5T_C_S1);
    H5Tset_size(mem, H5T_VARIABLE);
    H5Tset_cset(mem, H5T_CSET_UTF8);
    if (H5Tis_variable_str(type)) {
        std::vector<char*> raw(n, nullptr);
        if (n > 0) H5Dread(ds, mem, H5S_ALL, H5S_ALL, H5P_DEFAULT, raw.data());
        for (std::size_t i = 0; i < n; i++)
            column.push_string(raw[i] != nullptr ? std::string(raw[i]) : std::string());
        if (n > 0) {
            const hid_t space = H5Dget_space(ds);
            H5Dvlen_reclaim(mem, space, H5P_DEFAULT, raw.data());
            H5Sclose(space);
        }
    } else {
        // Fixed-length strings, which is what a NumPy "S8" column becomes.
        const std::size_t width = H5Tget_size(type);
        std::vector<char> buffer(n * width + 1, '\0');
        if (n > 0) {
            const hid_t fixed = H5Tcopy(H5T_C_S1);
            H5Tset_size(fixed, width);
            H5Dread(ds, fixed, H5S_ALL, H5S_ALL, H5P_DEFAULT, buffer.data());
            H5Tclose(fixed);
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

/// The `columns` attribute, if the writer left one.
std::vector<std::string> declared_order(hid_t group) {
    std::vector<std::string> out;
    if (H5Aexists(group, "columns") <= 0) return out;
    const hid_t attr = H5Aopen(group, "columns", H5P_DEFAULT);
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

void read_hdf5_table_into(data::DataStore& out, const std::string& filename,
                          const std::string& group_name) {
    const QuietHdf5 quiet;
    const hid_t file = H5Fopen(filename.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file < 0) throw std::runtime_error("cannot open " + filename);
    const hid_t group = H5Gopen2(file, group_name.c_str(), H5P_DEFAULT);
    if (group < 0) {
        H5Fclose(file);
        throw std::runtime_error("no group " + group_name + " in " + filename);
    }

    std::vector<std::string> names = declared_order(group);
    const std::vector<std::string> present = dataset_names(group);
    if (names.empty()) {
        // No declared order: take the datasets as they come, minus the masks.
        for (const std::string& n : present) if (!is_mask_name(n)) names.push_back(n);
    }

    std::size_t n_rows = 0;
    bool first = true;
    for (const std::string& name : names) {
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
                            (first || n == n_rows);
        if (usable) {
            if (first) { n_rows = n; first = false; }
            const int index = out.add_column(name, column_type);
            data::Column& column = out.column(index);
            if (column_type == data::ColumnType::String) {
                read_string_column(ds, type, column, n);
            } else {
                read_numeric_column(ds, column, column_type, n);
            }

            // A per-column validity mask, if one was written. This is the only
            // way an INTEGER column can say a value is missing -- a float has
            // NaN, an integer has nothing to spare.
            const std::string mask_name = dataset_name + kMaskSuffix;
            if (H5Lexists(group, mask_name.c_str(), H5P_DEFAULT) > 0) {
                const hid_t mds = H5Dopen2(group, mask_name.c_str(), H5P_DEFAULT);
                if (mds >= 0) {
                    std::vector<unsigned char> bytes(n, 1);
                    if (n > 0)
                        H5Dread(mds, H5T_NATIVE_UINT8, H5S_ALL, H5S_ALL,
                                H5P_DEFAULT, bytes.data());
                    column.set_mask(bytes.data(), static_cast<int>(n));
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
    H5Gclose(group);
    H5Fclose(file);
}

data::DataStore read_hdf5_table(const std::string& filename,
                                const std::string& group_name) {
    data::DataStore out;
    read_hdf5_table_into(out, filename, group_name);
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

/// One column, chunked and optionally deflated. Chunking is not decoration: an
/// unchunked dataset cannot be compressed, and a chunk of the whole column
/// would have to be inflated in full to read any of it.
void write_column_dataset(hid_t group, const std::string& name, hid_t file_type,
                          hid_t mem_type, std::size_t n, const void* data,
                          int compression) {
    const hsize_t dims[1] = {static_cast<hsize_t>(n)};
    const hid_t space = H5Screate_simple(1, dims, nullptr);
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
    if (ds >= 0) {
        if (n > 0) H5Dwrite(ds, mem_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
        H5Dclose(ds);
    }
    if (plist != H5P_DEFAULT) H5Pclose(plist);
    H5Sclose(space);
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
void write_gathered(hid_t group, const std::string& name, const data::Column& c,
                    const std::vector<std::size_t>& rows, bool gated,
                    std::size_t n_out, int compression) {
    std::vector<T> values(n_out);
    const T* src = static_cast<const T*>(c.data_ptr());
    if (src != nullptr)
        for (std::size_t i = 0; i < n_out; i++)
            values[i] = src[gated ? rows[i] : i];
    write_column_dataset(group, name, file_type_of(c.type()), mem_type_of(c.type()),
                         n_out, values.data(), compression);
}

/// \see write_gathered. False for the two types with no contiguous buffer to
/// gather from -- Bool is bit-packed, String is dictionary-encoded -- which the
/// caller writes its own way.
bool write_gathered_typed(hid_t group, const std::string& name,
                          const data::Column& c,
                          const std::vector<std::size_t>& rows, bool gated,
                          std::size_t n_out, int compression) {
    switch (c.type()) {
        case data::ColumnType::Float64:
            write_gathered<double>(group, name, c, rows, gated, n_out, compression);
            return true;
        case data::ColumnType::Float32:
            write_gathered<float>(group, name, c, rows, gated, n_out, compression);
            return true;
        case data::ColumnType::Int64:
            write_gathered<std::int64_t>(group, name, c, rows, gated, n_out, compression);
            return true;
        case data::ColumnType::Int32:
            write_gathered<std::int32_t>(group, name, c, rows, gated, n_out, compression);
            return true;
        case data::ColumnType::Int16:
            write_gathered<std::int16_t>(group, name, c, rows, gated, n_out, compression);
            return true;
        case data::ColumnType::Int8:
            write_gathered<std::int8_t>(group, name, c, rows, gated, n_out, compression);
            return true;
        case data::ColumnType::UInt64:
            write_gathered<std::uint64_t>(group, name, c, rows, gated, n_out, compression);
            return true;
        case data::ColumnType::UInt32:
            write_gathered<std::uint32_t>(group, name, c, rows, gated, n_out, compression);
            return true;
        case data::ColumnType::UInt16:
            write_gathered<std::uint16_t>(group, name, c, rows, gated, n_out, compression);
            return true;
        case data::ColumnType::UInt8:
            write_gathered<std::uint8_t>(group, name, c, rows, gated, n_out, compression);
            return true;
        case data::ColumnType::Bool:
        case data::ColumnType::String:
            return false;
    }
    return false;
}

}  // namespace

bool write_hdf5_table(const std::string& filename, const data::DataStore& store,
                      const std::string& group_name, int compression) {
    const QuietHdf5 quiet;
    const hid_t file = H5Fcreate(filename.c_str(), H5F_ACC_TRUNC,
                                 H5P_DEFAULT, H5P_DEFAULT);
    if (file < 0) return false;
    hid_t group = file;
    bool own_group = false;
    if (group_name != "/" && !group_name.empty()) {
        group = H5Gcreate2(file, group_name.c_str(), H5P_DEFAULT, H5P_DEFAULT,
                           H5P_DEFAULT);
        if (group < 0) { H5Fclose(file); return false; }
        own_group = true;
    }

    // Only the selected rows. Writing the whole table and expecting the reader
    // to gate it again would put the selection in two places, and the point of
    // exporting a subset is that it IS the subset.
    const bool gated = store.has_row_mask();
    std::vector<std::size_t> rows;
    if (gated) {
        rows.reserve(store.n_selected());
        for (std::size_t i = 0; i < store.n_rows(); i++)
            if (store.row_selected(i)) rows.push_back(i);
    }
    const std::size_t n_out = gated ? rows.size() : store.n_rows();

    std::vector<std::string> names;
    for (int c = 0; c < store.n_columns(); c++) {
        const data::Column& column = store.column(c);
        names.push_back(column.name());

        if (column.type() == data::ColumnType::String) {
            std::vector<std::string> values(n_out);
            for (std::size_t i = 0; i < n_out; i++)
                values[i] = column.string_at(gated ? rows[i] : i);
            std::vector<const char*> pointers(n_out);
            for (std::size_t i = 0; i < n_out; i++) pointers[i] = values[i].c_str();
            const hid_t vlen = H5Tcopy(H5T_C_S1);
            H5Tset_size(vlen, H5T_VARIABLE);
            H5Tset_cset(vlen, H5T_CSET_UTF8);
            write_column_dataset(group, encode_name(column.name()), vlen, vlen,
                                 n_out, pointers.data(), 0);
            H5Tclose(vlen);
        } else if (!gated && column.type() != data::ColumnType::Bool &&
                   column.data_ptr() != nullptr) {
            // The whole column, straight out of its buffer.
            write_column_dataset(group, encode_name(column.name()),
                                 file_type_of(column.type()),
                                 mem_type_of(column.type()), n_out, column.data_ptr(),
                                 compression);
        } else if (!write_gathered_typed(group, encode_name(column.name()), column,
                                         rows, gated, n_out, compression)) {
            // Bool: bit-packed in the store, a byte per row on disk, which is
            // what the reader's Bool branch expects.
            std::vector<unsigned char> values(n_out);
            for (std::size_t i = 0; i < n_out; i++)
                values[i] = column.value_at(gated ? rows[i] : i) != 0.0 ? 1 : 0;
            write_column_dataset(group, encode_name(column.name()),
                                 file_type_of(column.type()),
                                 H5T_NATIVE_UINT8, n_out, values.data(), compression);
        }

        if (column.has_mask()) {
            std::vector<unsigned char> bytes(n_out, 1);
            for (std::size_t i = 0; i < n_out; i++)
                bytes[i] = column.valid(gated ? rows[i] : i) ? 1 : 0;
            write_column_dataset(group, encode_name(column.name()) + kMaskSuffix,
                                 H5T_STD_U8LE, H5T_NATIVE_UINT8, n_out,
                                 bytes.data(), compression);
        }
    }

    // The column order, so a reader does not have to guess it from however HDF5
    // happens to list the group.
    {
        std::vector<const char*> pointers;
        pointers.reserve(names.size());
        for (const std::string& n : names) pointers.push_back(n.c_str());
        const hsize_t dims[1] = {static_cast<hsize_t>(pointers.size())};
        const hid_t space = H5Screate_simple(1, dims, nullptr);
        const hid_t vlen = H5Tcopy(H5T_C_S1);
        H5Tset_size(vlen, H5T_VARIABLE);
        H5Tset_cset(vlen, H5T_CSET_UTF8);
        const hid_t attr = H5Acreate2(group, "columns", vlen, space,
                                      H5P_DEFAULT, H5P_DEFAULT);
        if (attr >= 0) {
            if (!pointers.empty()) H5Awrite(attr, vlen, pointers.data());
            H5Aclose(attr);
        }
        H5Tclose(vlen);
        H5Sclose(space);
    }

    if (own_group) H5Gclose(group);
    H5Fclose(file);
    return true;
}

#endif  // BUILD_PHOTON_HDF

}  // namespace io
}  // namespace tttrlib
