// SPDX-License-Identifier: BSD-3-Clause
#include "io_store.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace tttrlib {
namespace io {

const char* const kStoreMagic = "TTTRSTOR";
const char* const kStoreExtension = ".dstore";

namespace {

const std::uint32_t kVersion = 1;
const std::uint32_t kFlagLittleEndian = 1u << 0;
const std::size_t kMagicBytes = 8;
const std::size_t kHeaderBytes = 48;

bool host_is_little_endian() {
    const std::uint32_t one = 1;
    unsigned char first;
    std::memcpy(&first, &one, 1);
    return first == 1;
}

/// FNV-1a over the directory. Not a cryptographic claim -- it is there to catch
/// a file that was cut short or scribbled on, which is what actually happens.
std::uint32_t fnv1a(const unsigned char* p, std::size_t n) {
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

/*!
 * \brief A stdio file, closed when it goes out of scope.
 *
 * std::FILE rather than a fstream, and the difference is not stylistic:
 * measured on this platform's libc++, ofstream::write moves 21 MB in 0.10 s
 * where fwrite does it in 0.007 s -- thirteen times slower, and enough on its
 * own to make this format lose to HDF5 at the one thing it exists to win.
 */
class File {
public:
    File(const char* path, const char* mode) : f_(std::fopen(path, mode)) {}
    ~File() { close(); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    bool ok() const { return f_ != nullptr; }
    std::FILE* get() const { return f_; }
    bool close() {
        if (f_ == nullptr) return true;
        const bool fine = std::fclose(f_) == 0;
        f_ = nullptr;
        return fine;
    }
private:
    std::FILE* f_ = nullptr;
};

/// Where a run of bytes sits in the file. Eight-byte aligned, always, so a
/// future reader can map the file and point a column straight at it.
struct BlobRef {
    std::uint64_t offset = 0;
    std::uint64_t bytes = 0;
};

// --- the directory, as bytes -------------------------------------------------

struct Writer {
    std::vector<unsigned char> b;

    void raw(const void* p, std::size_t n) {
        const unsigned char* c = static_cast<const unsigned char*>(p);
        b.insert(b.end(), c, c + n);
    }
    void u8(std::uint8_t v) { raw(&v, 1); }
    void u32(std::uint32_t v) { raw(&v, 4); }
    void u64(std::uint64_t v) { raw(&v, 8); }
    void str(const std::string& s) {
        u32(static_cast<std::uint32_t>(s.size()));
        if (!s.empty()) raw(s.data(), s.size());
    }
    void blob(const BlobRef& r) { u64(r.offset); u64(r.bytes); }
};

/// Reads what Writer wrote, and refuses to run off the end. A truncated
/// directory is a corrupt file, not a reason to read whatever follows.
struct Reader {
    const unsigned char* p = nullptr;
    std::size_t n = 0, i = 0;

    void need(std::size_t k) const {
        if (i + k > n) throw std::runtime_error("store file: the directory is truncated");
    }
    void raw(void* out, std::size_t k) { need(k); std::memcpy(out, p + i, k); i += k; }
    std::uint8_t u8() { std::uint8_t v; raw(&v, 1); return v; }
    std::uint32_t u32() { std::uint32_t v; raw(&v, 4); return v; }
    std::uint64_t u64() { std::uint64_t v; raw(&v, 8); return v; }
    std::string str() {
        const std::uint32_t k = u32();
        need(k);
        std::string s(reinterpret_cast<const char*>(p + i), k);
        i += k;
        return s;
    }
    BlobRef blob() { BlobRef r; r.offset = u64(); r.bytes = u64(); return r; }
};

// --- writing -----------------------------------------------------------------

/// Streams blobs forward, recording where each landed. Only the 48-byte header
/// is written twice: once as a placeholder, once with the real offsets.
struct BlobStream {
    File f;
    std::uint64_t pos = 0;
    bool ok = true;

    BlobStream(const std::string& path) : f(path.c_str(), "wb") { ok = f.ok(); }

    void bytes(const void* data, std::size_t n) {
        if (!ok || n == 0) return;
        if (std::fwrite(data, 1, n, f.get()) != n) ok = false;
        pos += n;
    }
    void pad_to_8() {
        static const char zeros[8] = {0};
        const std::size_t slack = static_cast<std::size_t>(pos % 8);
        if (slack) bytes(zeros, 8 - slack);
    }
    BlobRef blob(const void* data, std::size_t n) {
        pad_to_8();
        BlobRef r;
        r.offset = pos;
        r.bytes = n;
        bytes(data, n);
        return r;
    }
};

/// Element width of a column type, or 0 for the two that are not a plain array.
std::size_t element_bytes(data::ColumnType t) {
    switch (t) {
        case data::ColumnType::Float64: return 8;
        case data::ColumnType::Float32: return 4;
        case data::ColumnType::Int64:   return 8;
        case data::ColumnType::Int32:   return 4;
        case data::ColumnType::Int16:   return 2;
        case data::ColumnType::Int8:    return 1;
        case data::ColumnType::UInt64:  return 8;
        case data::ColumnType::UInt32:  return 4;
        case data::ColumnType::UInt16:  return 2;
        case data::ColumnType::UInt8:   return 1;
        case data::ColumnType::String:  return 4;   // the codes
        case data::ColumnType::Bool:    return 0;   // bit-packed
    }
    return 0;
}

const std::uint8_t kColumnHasMask = 1u << 0;

/*!
 * \brief Is this byte a column type this build knows?
 *
 * A switch rather than a range check, so reordering the enum cannot quietly
 * turn one type into another here, and so adding an enumerator is a compiler
 * warning rather than a file that reads back as the wrong dtype.
 */
bool known_column_type(std::uint8_t raw) {
    switch (static_cast<data::ColumnType>(raw)) {
        case data::ColumnType::Float64:
        case data::ColumnType::Float32:
        case data::ColumnType::Int64:
        case data::ColumnType::Int32:
        case data::ColumnType::Int16:
        case data::ColumnType::Int8:
        case data::ColumnType::UInt64:
        case data::ColumnType::UInt32:
        case data::ColumnType::UInt16:
        case data::ColumnType::UInt8:
        case data::ColumnType::Bool:
        case data::ColumnType::String:
            return true;
    }
    return false;
}

// The numeric value of a ColumnType is written into the file, so it is part of
// the format. Appending a new type is fine; renumbering the existing ones would
// silently change the dtype of every column in every file already written.
static_assert(static_cast<int>(data::ColumnType::Float64) == 0 &&
              static_cast<int>(data::ColumnType::Bool) == 10 &&
              static_cast<int>(data::ColumnType::String) == 11,
              "ColumnType values are part of the .dstore format; see kVersion");

void write_node(BlobStream& out, Writer& dir, const data::DataStore& store) {
    dir.str(store.label());
    dir.u64(store.n_rows());

    // The selection travels as data. This format's contract is that a store
    // comes back as it went in, so unlike the HDF5 writer it does not export a
    // gated subset -- it saves the gate.
    const data::BitMask& rows = store.row_mask();
    dir.u64(rows.size());
    dir.blob(out.blob(rows.words(), rows.nbytes()));

    dir.u32(static_cast<std::uint32_t>(store.n_columns()));
    for (int c = 0; c < store.n_columns(); c++) {
        const data::Column& column = store.column(c);
        dir.str(column.name());
        dir.u8(static_cast<std::uint8_t>(column.type()));
        dir.u64(column.size());
        dir.u8(column.has_mask() ? kColumnHasMask : 0);

        if (column.type() == data::ColumnType::Bool) {
            dir.blob(out.blob(column.bits().words(), column.bits().nbytes()));
        } else {
            const std::size_t width = element_bytes(column.type());
            const void* data = column.type() == data::ColumnType::String
                    ? static_cast<const void*>(column.codes().data())
                    : column.data_ptr();
            dir.blob(out.blob(data, data == nullptr ? 0 : column.size() * width));
        }

        const data::BitMask& mask = column.mask();
        dir.u64(mask.size());
        dir.blob(out.blob(mask.words(), mask.nbytes()));

        // The dictionary of a text column, as its own little block: a count and
        // then each string with its length. Written through the same blob path
        // so it is aligned and addressable like everything else.
        if (column.type() == data::ColumnType::String) {
            Writer d;
            d.u32(static_cast<std::uint32_t>(column.dictionary().size()));
            for (const std::string& s : column.dictionary()) d.str(s);
            dir.blob(out.blob(d.b.data(), d.b.size()));
        }
    }

    const std::vector<std::string> names = store.group_names();
    dir.u32(static_cast<std::uint32_t>(names.size()));
    for (const std::string& name : names) {
        dir.str(name);
        write_node(out, dir, store.group(name));
    }
}

// --- reading -----------------------------------------------------------------

struct Blobs {
    std::FILE* f = nullptr;
    std::uint64_t file_bytes = 0;

    /// Read a blob, or throw. An offset past the end of the file is the shape a
    /// truncated or doctored file takes, and reading whatever is there instead
    /// would turn it into wrong numbers rather than an error.
    void read(const BlobRef& r, void* into) const {
        if (r.bytes == 0) return;
        if (r.offset + r.bytes > file_bytes)
            throw std::runtime_error("store file: a column points past the end");
        if (std::fseek(f, static_cast<long>(r.offset), SEEK_SET) != 0 ||
            std::fread(into, 1, static_cast<std::size_t>(r.bytes), f) != r.bytes)
            throw std::runtime_error("store file: could not read a column");
    }
    std::vector<unsigned char> read(const BlobRef& r) const {
        std::vector<unsigned char> out(static_cast<std::size_t>(r.bytes));
        read(r, out.data());
        return out;
    }
};

/// Whether to read a column's data at all, for the partial form.
struct ColumnFilter {
    bool everything = true;
    const std::vector<std::string>* wanted = nullptr;

    bool operator()(const std::string& name) const {
        if (everything) return true;
        for (const std::string& w : *wanted) if (w == name) return true;
        return false;
    }
};

void read_node(Reader& dir, const Blobs& blobs, data::DataStore& store,
               const ColumnFilter& want) {
    store.set_label(dir.str());
    store.set_n_rows(static_cast<std::size_t>(dir.u64()));

    const std::uint64_t row_bits = dir.u64();
    const BlobRef row_blob = dir.blob();
    if (row_bits > 0) {
        std::vector<std::uint64_t> words(static_cast<std::size_t>(row_blob.bytes / 8));
        blobs.read(row_blob, words.data());
        store.set_row_mask_bits(words.data(), static_cast<std::size_t>(row_bits));
    }

    const std::uint32_t n_columns = dir.u32();
    for (std::uint32_t c = 0; c < n_columns; c++) {
        const std::string name = dir.str();
        const std::uint8_t raw_type = dir.u8();
        const std::uint64_t n = dir.u64();
        const std::uint8_t flags = dir.u8();
        const BlobRef data_blob = dir.blob();
        const std::uint64_t mask_bits = dir.u64();
        const BlobRef mask_blob = dir.blob();
        if (!known_column_type(raw_type))
            throw std::runtime_error("store file: unknown column type for '" + name + "'");
        const data::ColumnType type = static_cast<data::ColumnType>(raw_type);
        BlobRef dict_blob;
        if (type == data::ColumnType::String) dict_blob = dir.blob();
        if (!want(name)) continue;

        data::Column& column = store.column(store.add_column(name, type));
        if (type == data::ColumnType::Bool) {
            std::vector<std::uint64_t> words(static_cast<std::size_t>(data_blob.bytes / 8));
            blobs.read(data_blob, words.data());
            column.set_bits(words.data(), static_cast<std::size_t>(n));
        } else if (type == data::ColumnType::String) {
            const std::vector<unsigned char> raw = blobs.read(dict_blob);
            Reader d{raw.data(), raw.size(), 0};
            std::vector<std::string> dictionary(d.u32());
            for (std::string& s : dictionary) s = d.str();
            column.set_dictionary(dictionary);
            std::vector<int> codes(static_cast<std::size_t>(n));
            blobs.read(data_blob, codes.data());
            column.set_codes(codes.data(), static_cast<int>(n));
        } else {
            // Straight into the column's own buffer. resize_uninitialized does
            // not fill it first, so nothing is written twice.
            column.resize_uninitialized(static_cast<std::size_t>(n));
            blobs.read(data_blob, column.data_ptr());
        }

        if (flags & kColumnHasMask) {
            std::vector<std::uint64_t> words(static_cast<std::size_t>(mask_blob.bytes / 8));
            blobs.read(mask_blob, words.data());
            column.set_mask_bits(words.data(), static_cast<std::size_t>(mask_bits));
        }
    }

    const std::uint32_t n_groups = dir.u32();
    for (std::uint32_t g = 0; g < n_groups; g++) {
        const std::string name = dir.str();
        read_node(dir, blobs, store.add_group(name), want);
    }
}

/// Open a file and read its directory, or throw saying why not.
struct OpenStore {
    File f;
    std::vector<unsigned char> directory;
    std::uint64_t file_bytes = 0;

    explicit OpenStore(const std::string& filename) : f(filename.c_str(), "rb") {
        if (!f.ok()) throw std::runtime_error("cannot open " + filename);

        unsigned char head[kHeaderBytes];
        if (std::fread(head, 1, kHeaderBytes, f.get()) != kHeaderBytes ||
            std::memcmp(head, kStoreMagic, kMagicBytes) != 0)
            throw std::runtime_error(filename + " is not a tttrlib store file");

        std::uint32_t version, flags, checksum;
        std::uint64_t dir_offset, dir_bytes, declared;
        std::memcpy(&version, head + 8, 4);
        std::memcpy(&flags, head + 12, 4);
        std::memcpy(&dir_offset, head + 16, 8);
        std::memcpy(&dir_bytes, head + 24, 8);
        std::memcpy(&declared, head + 32, 8);
        std::memcpy(&checksum, head + 40, 4);

        if (version > kVersion)
            throw std::runtime_error(filename + " was written by a newer tttrlib"
                                     " (store format version " +
                                     std::to_string(version) + ")");
        // Byte order is recorded rather than converted. Swapping on the way in
        // would put back the conversion layer this format exists to avoid, and
        // no machine this runs on is big-endian.
        if (((flags & kFlagLittleEndian) != 0) != host_is_little_endian())
            throw std::runtime_error(filename + " was written on a machine of the"
                                     " opposite byte order, which is not supported");

        std::fseek(f.get(), 0, SEEK_END);
        file_bytes = static_cast<std::uint64_t>(std::ftell(f.get()));
        if (declared != file_bytes)
            throw std::runtime_error(filename + " is truncated or was appended to");
        if (dir_offset + dir_bytes > file_bytes)
            throw std::runtime_error(filename + " has no directory where it says");

        directory.resize(static_cast<std::size_t>(dir_bytes));
        if (std::fseek(f.get(), static_cast<long>(dir_offset), SEEK_SET) != 0 ||
            std::fread(directory.data(), 1, directory.size(), f.get()) != directory.size())
            throw std::runtime_error(filename + ": could not read the directory");
        if (fnv1a(directory.data(), directory.size()) != checksum)
            throw std::runtime_error(filename + ": the directory is corrupt");
    }
};

/// The paths of a node and its descendants, without touching any blob.
void walk_paths(Reader& dir, const std::string& prefix,
                std::vector<std::string>& out, std::vector<std::string>* columns,
                const std::string& want, bool here) {
    dir.str();                                       // label
    dir.u64();                                       // n_rows
    dir.u64(); dir.blob();                           // row mask
    const std::uint32_t n_columns = dir.u32();
    for (std::uint32_t c = 0; c < n_columns; c++) {
        const std::string name = dir.str();
        const std::uint8_t type = dir.u8();
        dir.u64(); dir.u8(); dir.blob(); dir.u64(); dir.blob();
        if (type == static_cast<std::uint8_t>(data::ColumnType::String)) dir.blob();
        if (columns != nullptr && here) columns->push_back(name);
    }
    const std::uint32_t n_groups = dir.u32();
    for (std::uint32_t g = 0; g < n_groups; g++) {
        const std::string name = dir.str();
        const std::string path = prefix.empty() ? name : prefix + "/" + name;
        out.push_back(path);
        walk_paths(dir, path, out, columns, want, path == want);
    }
}

}  // namespace

bool write_store(const std::string& filename, const data::DataStore& store) {
    // Beside the target, so the rename that follows stays on one filesystem and
    // is therefore atomic: a failure never leaves half a file where a good one
    // was.
    const std::string temp = filename + ".tttrlib-tmp";

    BlobStream out(temp);
    if (!out.ok) {
        std::cerr << "store file: could not create " << temp << std::endl;
        return false;
    }

    unsigned char head[kHeaderBytes];
    std::memset(head, 0, kHeaderBytes);
    out.bytes(head, kHeaderBytes);

    Writer dir;
    write_node(out, dir, store);

    out.pad_to_8();
    const std::uint64_t dir_offset = out.pos;
    out.bytes(dir.b.data(), dir.b.size());
    const std::uint64_t file_bytes = out.pos;

    // The one place anything is written twice: the header could not be filled in
    // until the directory had somewhere to live.
    const std::uint32_t version = kVersion;
    const std::uint32_t flags = host_is_little_endian() ? kFlagLittleEndian : 0;
    const std::uint64_t dir_bytes = dir.b.size();
    const std::uint32_t checksum = fnv1a(dir.b.data(), dir.b.size());
    std::memcpy(head, kStoreMagic, kMagicBytes);
    std::memcpy(head + 8, &version, 4);
    std::memcpy(head + 12, &flags, 4);
    std::memcpy(head + 16, &dir_offset, 8);
    std::memcpy(head + 24, &dir_bytes, 8);
    std::memcpy(head + 32, &file_bytes, 8);
    std::memcpy(head + 40, &checksum, 4);
    if (std::fseek(out.f.get(), 0, SEEK_SET) != 0 ||
        std::fwrite(head, 1, kHeaderBytes, out.f.get()) != kHeaderBytes)
        out.ok = false;
    if (!out.f.close()) out.ok = false;

    if (out.ok && std::rename(temp.c_str(), filename.c_str()) != 0) {
        std::cerr << "store file: could not rename " << temp << " over "
                  << filename << std::endl;
        out.ok = false;
    }
    if (!out.ok) std::remove(temp.c_str());
    return out.ok;
}

void read_store_into(data::DataStore& out, const std::string& filename) {
    ColumnFilter want;
    OpenStore file(filename);
    Reader dir{file.directory.data(), file.directory.size(), 0};
    Blobs blobs{file.f.get(), file.file_bytes};
    out.release();
    read_node(dir, blobs, out, want);
}

void read_store_into(data::DataStore& out, const std::string& filename,
                     const std::vector<std::string>& columns) {
    ColumnFilter want;
    want.everything = false;
    want.wanted = &columns;
    OpenStore file(filename);
    Reader dir{file.directory.data(), file.directory.size(), 0};
    Blobs blobs{file.f.get(), file.file_bytes};
    out.release();
    read_node(dir, blobs, out, want);
}

data::DataStore read_store(const std::string& filename) {
    data::DataStore out;
    read_store_into(out, filename);
    return out;
}

bool is_store_file(const std::string& filename) {
    File f(filename.c_str(), "rb");
    if (!f.ok()) return false;
    char magic[kMagicBytes];
    return std::fread(magic, 1, kMagicBytes, f.get()) == kMagicBytes &&
           std::memcmp(magic, kStoreMagic, kMagicBytes) == 0;
}

std::vector<std::string> store_columns(const std::string& filename,
                                       const std::string& group) {
    std::vector<std::string> columns;
    try {
        OpenStore file(filename);
        Reader dir{file.directory.data(), file.directory.size(), 0};
        std::vector<std::string> paths;
        walk_paths(dir, std::string(), paths, &columns, group, group.empty());
    } catch (const std::exception&) {
        return {};
    }
    return columns;
}

std::vector<std::string> store_groups(const std::string& filename) {
    std::vector<std::string> paths;
    try {
        OpenStore file(filename);
        Reader dir{file.directory.data(), file.directory.size(), 0};
        walk_paths(dir, std::string(), paths, nullptr, std::string(), false);
    } catch (const std::exception&) {
        return {};
    }
    return paths;
}

}  // namespace io
}  // namespace tttrlib
