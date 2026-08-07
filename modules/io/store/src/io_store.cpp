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

/// 2 added a per-column description; a version 1 file reads under 2 with an
/// empty one. 3 stores that description as msgpack rather than as JSON text,
/// in the same length-prefixed slot -- \ref data::metadata_to_msgpack says
/// why. A version 2 file still reads: the slot is there and holds text, so
/// only the decode differs.
const std::uint32_t kVersion = 3;
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
    /// Format version of the file this directory came from. The directory is
    /// positional, so a field added in a later version has to be consumed
    /// exactly when it is present -- including for a column being skipped.
    std::uint32_t version = kVersion;

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
    std::FILE* h = nullptr;
    /// Where in the file this store begins. Every offset the directory records
    /// is relative to it, so a store written into the middle of a container is
    /// still a self-contained store.
    std::uint64_t base = 0;
    std::uint64_t pos = 0;
    bool ok = true;

    BlobStream(std::FILE* file, std::uint64_t base_) : h(file), base(base_) {
        ok = h != nullptr;
    }

    void bytes(const void* data, std::size_t n) {
        if (!ok || n == 0) return;
        if (std::fwrite(data, 1, n, h) != n) ok = false;
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

        // The column's description, as msgpack. The name is written above as a
        // field of its own as well, even though it is an attribute of this: a
        // subset read decides whether to skip a column before it has any reason
        // to decode, and store_columns() promises the names without reading
        // data.
        const std::vector<unsigned char> meta =
            data::metadata_to_msgpack(column.metadata());
        dir.u32(static_cast<std::uint32_t>(meta.size()));
        if (!meta.empty()) dir.raw(meta.data(), meta.size());

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

/// Every byte the store reader has moved. \see store_bytes_read.
std::uint64_t g_bytes_read = 0;

struct Blobs {
    std::FILE* f = nullptr;
    std::uint64_t file_bytes = 0;
    std::uint64_t base = 0;

    /// Read a blob, or throw. An offset past the end of the file is the shape a
    /// truncated or doctored file takes, and reading whatever is there instead
    /// would turn it into wrong numbers rather than an error.
    void read(const BlobRef& r, void* into) const {
        if (r.bytes == 0) return;
        if (r.offset + r.bytes > file_bytes)
            throw std::runtime_error("store file: a column points past the end");
        if (std::fseek(f, static_cast<long>(base + r.offset), SEEK_SET) != 0 ||
            std::fread(into, 1, static_cast<std::size_t>(r.bytes), f) != r.bytes)
            throw std::runtime_error("store file: could not read a column");
        g_bytes_read += r.bytes;
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

/*!
 * \brief Which rows to read, and how much of a column that turns out to be.
 *
 * A node applies this to its own length: a group shorter than `first` comes
 * back empty rather than throwing, because the tree is one file and its tables
 * need not agree on how long they are.
 */
struct RowRange {
    bool everything = true;
    std::uint64_t first = 0;
    std::uint64_t count = 0;      ///< 0 with `everything` false means "to the end"

    /// How many of `have` rows this range selects, and where it starts.
    std::uint64_t take(std::uint64_t have) const {
        if (everything) return have;
        if (first >= have) return 0;
        const std::uint64_t left = have - first;
        return count == 0 || count > left ? left : count;
    }
    std::uint64_t start() const { return everything ? 0 : first; }
};

/// The part of a fixed-width blob that holds rows [first, first + n).
BlobRef slice(const BlobRef& r, std::uint64_t first, std::uint64_t n,
              std::size_t width) {
    BlobRef s;
    s.offset = r.offset + first * width;
    s.bytes = n * width;
    return s;
}

/*!
 * \brief Bits [first, first + count) of a bit-packed blob, repacked from bit 0.
 *
 * Only the words the range falls in are read, which is the point -- but a range
 * that does not start on a word boundary has to be shifted down, so the result
 * is a new buffer rather than the file's own bytes.
 */
std::vector<std::uint64_t> read_bits(const Blobs& blobs, const BlobRef& r,
                                     std::uint64_t first, std::uint64_t count) {
    const std::size_t out_words = static_cast<std::size_t>((count + 63) / 64);
    std::vector<std::uint64_t> out(out_words, 0);
    if (count == 0) return out;

    const std::uint64_t first_word = first / 64;
    const std::uint64_t last_word = (first + count + 63) / 64;
    const std::uint64_t have_words = r.bytes / 8;
    if (first_word >= have_words) return out;
    const std::uint64_t stop = last_word < have_words ? last_word : have_words;

    BlobRef part;
    part.offset = r.offset + first_word * 8;
    part.bytes = (stop - first_word) * 8;
    std::vector<std::uint64_t> raw(static_cast<std::size_t>(stop - first_word));
    blobs.read(part, raw.data());

    const unsigned shift = static_cast<unsigned>(first % 64);
    for (std::size_t w = 0; w < out_words; w++) {
        std::uint64_t lo = w < raw.size() ? raw[w] : 0;
        std::uint64_t value = shift == 0 ? lo : (lo >> shift);
        if (shift != 0 && w + 1 < raw.size()) value |= raw[w + 1] << (64 - shift);
        out[w] = value;
    }
    // Whatever the last word carried past the end of the range is not part of
    // it; a mask that kept those bits would report rows that were not asked for.
    const unsigned tail = static_cast<unsigned>(count % 64);
    if (tail != 0) out[out_words - 1] &= (1ULL << tail) - 1;
    return out;
}

void read_node(Reader& dir, const Blobs& blobs, data::DataStore& store,
               const ColumnFilter& want, const RowRange& rows) {
    store.set_label(dir.str());
    const std::uint64_t node_rows = dir.u64();
    const std::uint64_t take = rows.take(node_rows);
    const std::uint64_t from = rows.start();
    store.set_n_rows(static_cast<std::size_t>(take));

    const std::uint64_t row_bits = dir.u64();
    const BlobRef row_blob = dir.blob();
    if (row_bits > 0) {
        const std::uint64_t bits = rows.everything ? row_bits : take;
        std::vector<std::uint64_t> words = read_bits(blobs, row_blob, from, bits);
        if (bits > 0)
            store.set_row_mask_bits(words.data(), static_cast<std::size_t>(bits));
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
        // Read even for a column about to be skipped: the directory is
        // positional, so not consuming it here misaligns every column after.
        // Version 3 holds msgpack in the slot version 2 held JSON text in.
        std::string meta;
        if (dir.version >= 2) {
            const std::string stored = dir.str();
            meta = dir.version >= 3
                       ? data::metadata_from_msgpack(
                             reinterpret_cast<const unsigned char*>(stored.data()),
                             stored.size())
                       : stored;
        }
        if (!known_column_type(raw_type))
            throw std::runtime_error("store file: unknown column type for '" + name + "'");
        const data::ColumnType type = static_cast<data::ColumnType>(raw_type);
        BlobRef dict_blob;
        if (type == data::ColumnType::String) dict_blob = dir.blob();
        if (!want(name)) continue;

        // A column's own length, not the node's: they agree in a file this
        // library wrote, and clamping to both costs nothing if they ever do not.
        const std::uint64_t got = rows.take(n);
        const std::uint64_t at = rows.start() < n ? rows.start() : n;

        data::Column& column = store.column(store.add_column(name, type));
        if (!meta.empty()) column.set_metadata(meta);
        if (type == data::ColumnType::Bool) {
            std::vector<std::uint64_t> words = read_bits(blobs, data_blob, at, got);
            column.set_bits(words.data(), static_cast<std::size_t>(got));
        } else if (type == data::ColumnType::String) {
            // The whole dictionary, whatever the range: it is the labels, not
            // the rows, and is small by construction.
            const std::vector<unsigned char> raw = blobs.read(dict_blob);
            Reader d{raw.data(), raw.size(), 0};
            std::vector<std::string> dictionary(d.u32());
            for (std::string& s : dictionary) s = d.str();
            column.set_dictionary(dictionary);
            std::vector<int> codes(static_cast<std::size_t>(got));
            blobs.read(slice(data_blob, at, got, 4), codes.data());
            column.set_codes(codes.data(), static_cast<int>(got));
        } else {
            // Straight into the column's own buffer. resize_uninitialized does
            // not fill it first, so nothing is written twice.
            column.resize_uninitialized(static_cast<std::size_t>(got));
            blobs.read(slice(data_blob, at, got, element_bytes(type)),
                       column.data_ptr());
        }

        if (flags & kColumnHasMask) {
            const std::uint64_t bits = rows.everything ? mask_bits : got;
            std::vector<std::uint64_t> words = read_bits(blobs, mask_blob, at, bits);
            column.set_mask_bits(words.data(), static_cast<std::size_t>(bits));
        }
    }

    const std::uint32_t n_groups = dir.u32();
    for (std::uint32_t g = 0; g < n_groups; g++) {
        const std::string name = dir.str();
        read_node(dir, blobs, store.add_group(name), want, rows);
    }
}

/// Open a file and read its directory, or throw saying why not.
struct OpenStore {
    File f;
    std::vector<unsigned char> directory;
    std::uint64_t file_bytes = 0;
    std::uint64_t base = 0;
    std::uint32_t format_version = kVersion;

    OpenStore(const std::string& filename, std::uint64_t base_ = 0,
              std::uint64_t region = 0)
            : f(filename.c_str(), "rb"), base(base_) {
        if (!f.ok()) throw std::runtime_error("cannot open " + filename);
        if (base != 0 && std::fseek(f.get(), static_cast<long>(base), SEEK_SET) != 0)
            throw std::runtime_error(filename + " is shorter than the store in it");

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

        format_version = version;
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
        const std::uint64_t whole = static_cast<std::uint64_t>(std::ftell(f.get()));
        file_bytes = region != 0 ? region : (whole > base ? whole - base : 0);
        if (declared != file_bytes)
            throw std::runtime_error(filename + " is truncated or was appended to");
        if (dir_offset + dir_bytes > file_bytes)
            throw std::runtime_error(filename + " has no directory where it says");

        directory.resize(static_cast<std::size_t>(dir_bytes));
        if (std::fseek(f.get(), static_cast<long>(base + dir_offset), SEEK_SET) != 0 ||
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
        // The description, in the same place the reader expects it, and never
        // decoded -- the slot is length-prefixed either way, so stepping over
        // it costs the same in v2 and v3. This walk touches no blob, but it
        // still has to step over every field: the directory is positional, and
        // one unconsumed slot here shifts every column and group after it.
        if (dir.version >= 2) dir.str();
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

namespace {

/// Header, blobs, directory, then the header again with the real offsets.
/// Shared by the by-name and the into-an-open-file forms.
bool emit_store(BlobStream& out, const data::DataStore& store) {
    unsigned char head[kHeaderBytes];
    std::memset(head, 0, kHeaderBytes);
    out.bytes(head, kHeaderBytes);

    Writer dir;
    write_node(out, dir, store);

    out.pad_to_8();
    const std::uint64_t dir_offset = out.pos;
    out.bytes(dir.b.data(), dir.b.size());
    const std::uint64_t file_bytes = out.pos;

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

    if (std::fseek(out.h, static_cast<long>(out.base), SEEK_SET) != 0 ||
        std::fwrite(head, 1, kHeaderBytes, out.h) != kHeaderBytes)
        out.ok = false;
    if (out.ok && std::fseek(out.h, static_cast<long>(out.base + file_bytes),
                             SEEK_SET) != 0)
        out.ok = false;
    return out.ok;
}

}  // namespace

std::uint64_t write_store_at(std::FILE* f, const data::DataStore& store) {
    if (f == nullptr) return 0;
    const long here = std::ftell(f);
    if (here < 0) return 0;
    BlobStream out(f, static_cast<std::uint64_t>(here));
    if (!emit_store(out, store)) return 0;
    return out.pos;
}

bool write_store(const std::string& filename, const data::DataStore& store) {
    // Beside the target, so the rename that follows stays on one filesystem and
    // is therefore atomic: a failure never leaves half a file where a good one
    // was.
    const std::string temp = filename + ".tttrlib-tmp";

    File owned(temp.c_str(), "wb");
    BlobStream out(owned.get(), 0);
    if (!out.ok) {
        std::cerr << "store file: could not create " << temp << std::endl;
        return false;
    }

    bool ok = emit_store(out, store);
    if (!owned.close()) ok = false;

    if (ok && std::rename(temp.c_str(), filename.c_str()) != 0) {
        std::cerr << "store file: could not rename " << temp << " over "
                  << filename << std::endl;
        ok = false;
    }
    if (!ok) std::remove(temp.c_str());
    return ok;
}

namespace {

/*!
 * \brief The one reader. Both knobs, both independent, both optional.
 *
 * The four public overloads are this call with two arguments set differently;
 * they were four copies of these six lines until the one combination that
 * mattered -- a column subset of a store embedded in a container -- turned out
 * to be the one nobody had written down.
 */
void read_region(data::DataStore& out, const std::string& filename,
                 std::uint64_t base, std::uint64_t bytes,
                 const ColumnFilter& want, const RowRange& rows) {
    OpenStore file(filename, base, bytes);
    Reader dir{file.directory.data(), file.directory.size(), 0, file.format_version};
    Blobs blobs{file.f.get(), file.file_bytes, file.base};
    out.release();
    read_node(dir, blobs, out, want, rows);
}

}  // namespace

void read_store_into(data::DataStore& out, const std::string& filename) {
    read_region(out, filename, 0, 0, ColumnFilter(), RowRange());
}

void read_store_into(data::DataStore& out, const std::string& filename,
                     const std::vector<std::string>& columns) {
    ColumnFilter want;
    want.everything = false;
    want.wanted = &columns;
    read_region(out, filename, 0, 0, want, RowRange());
}

void read_store_into(data::DataStore& out, const std::string& filename,
                     std::uint64_t base, std::uint64_t bytes) {
    read_region(out, filename, base, bytes, ColumnFilter(), RowRange());
}

void read_store_into(data::DataStore& out, const std::string& filename,
                     std::uint64_t base, std::uint64_t bytes,
                     const std::vector<std::string>& columns) {
    ColumnFilter want;
    want.everything = false;
    want.wanted = &columns;
    read_region(out, filename, base, bytes, want, RowRange());
}

void read_store_into(data::DataStore& out, const std::string& filename,
                     std::uint64_t base, std::uint64_t bytes,
                     const std::vector<std::string>& columns,
                     std::uint64_t first_row, std::uint64_t n_rows) {
    ColumnFilter want;
    // An empty list here means every column, not none: the row range is what
    // the caller came for, and asking for a window of nothing is not a thing
    // anyone means.
    want.everything = columns.empty();
    want.wanted = &columns;
    RowRange rows;
    rows.everything = false;
    rows.first = first_row;
    rows.count = n_rows;
    read_region(out, filename, base, bytes, want, rows);
}

std::uint64_t store_bytes_read() { return g_bytes_read; }

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
                                       std::uint64_t base, std::uint64_t bytes,
                                       const std::string& group) {
    std::vector<std::string> columns;
    try {
        OpenStore file(filename, base, bytes);
        Reader dir{file.directory.data(), file.directory.size(), 0, file.format_version};
        std::vector<std::string> paths;
        walk_paths(dir, std::string(), paths, &columns, group, group.empty());
    } catch (const std::exception&) {
        return {};
    }
    return columns;
}

std::vector<std::string> store_columns(const std::string& filename,
                                       const std::string& group) {
    return store_columns(filename, 0, 0, group);
}

std::vector<std::string> store_groups(const std::string& filename,
                                      std::uint64_t base, std::uint64_t bytes) {
    std::vector<std::string> paths;
    try {
        OpenStore file(filename, base, bytes);
        Reader dir{file.directory.data(), file.directory.size(), 0, file.format_version};
        walk_paths(dir, std::string(), paths, nullptr, std::string(), false);
    } catch (const std::exception&) {
        return {};
    }
    return paths;
}

std::vector<std::string> store_groups(const std::string& filename) {
    return store_groups(filename, 0, 0);
}

}  // namespace io
}  // namespace tttrlib
