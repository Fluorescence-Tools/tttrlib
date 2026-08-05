// SPDX-License-Identifier: BSD-3-Clause
#include "io_csv.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#include "FileIO.h"

#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace tttrlib {
namespace io {

namespace {

// --- fast scalar parsers ----------------------------------------------------
//
// strtod and strtol both consult the locale and both take a NUL-terminated
// string, which a field inside a memory-mapped block is not. Hand-rolled
// parsers over (pointer, length) avoid a copy per field, and a CSV of ten
// million rows has a hundred million fields.

/// Parse a decimal integer. Returns false on anything else, including overflow.
bool parse_int64(const char* p, std::size_t n, long long& out) {
    if (n == 0) return false;
    std::size_t i = 0;
    bool neg = false;
    if (p[0] == '+' || p[0] == '-') { neg = p[0] == '-'; i = 1; }
    if (i >= n) return false;
    unsigned long long v = 0;
    for (; i < n; i++) {
        const char c = p[i];
        if (c < '0' || c > '9') return false;
        const unsigned long long nv = v * 10ULL + static_cast<unsigned long long>(c - '0');
        if (nv < v) return false;                       // wrapped
        v = nv;
    }
    if (!neg && v > 9223372036854775807ULL) return false;
    if (neg && v > 9223372036854775808ULL) return false;
    out = neg ? -static_cast<long long>(v) : static_cast<long long>(v);
    return true;
}

/*!
 * Parse a decimal real.
 *
 * The straightforward digits-and-exponent path covers what a data file
 * contains; anything else -- hex floats, "infinity", very long mantissas where
 * the naive accumulation would lose the last bits -- falls through to strtod on
 * a stack copy, which is correct and rare enough not to matter.
 */
bool parse_double(const char* p, std::size_t n, double& out) {
    if (n == 0) return false;
    std::size_t i = 0;
    bool neg = false;
    if (p[0] == '+' || p[0] == '-') { neg = p[0] == '-'; i = 1; }

    unsigned long long mant = 0;
    int digits = 0, exp10 = 0;
    bool any = false;
    for (; i < n && p[i] >= '0' && p[i] <= '9'; i++) {
        if (digits < 19) { mant = mant * 10ULL + static_cast<unsigned>(p[i] - '0'); digits++; }
        else exp10++;                                    // beyond precision, scale instead
        any = true;
    }
    if (i < n && p[i] == '.') {
        i++;
        for (; i < n && p[i] >= '0' && p[i] <= '9'; i++) {
            if (digits < 19) { mant = mant * 10ULL + static_cast<unsigned>(p[i] - '0'); digits++; exp10--; }
            any = true;
        }
    }
    if (!any) goto slow;
    if (i < n && (p[i] == 'e' || p[i] == 'E')) {
        i++;
        bool eneg = false;
        if (i < n && (p[i] == '+' || p[i] == '-')) { eneg = p[i] == '-'; i++; }
        if (i >= n) goto slow;
        int e = 0;
        for (; i < n && p[i] >= '0' && p[i] <= '9'; i++) {
            e = e * 10 + (p[i] - '0');
            if (e > 100000) goto slow;
        }
        exp10 += eneg ? -e : e;
    }
    if (i != n) goto slow;
    {
        double v = static_cast<double>(mant);
        // std::pow once rather than a loop: one rounding, not exp10 of them.
        if (exp10 != 0) v *= std::pow(10.0, static_cast<double>(exp10));
        out = neg ? -v : v;
        return true;
    }
slow: {
        char buf[512];
        if (n >= sizeof(buf)) return false;
        std::memcpy(buf, p, n);
        buf[n] = '\0';
        char* end = nullptr;
        errno = 0;
        const double v = std::strtod(buf, &end);
        if (end != buf + n || errno == ERANGE) return false;
        out = v;
        return true;
    }
}

bool parse_bool(const char* p, std::size_t n, bool& out) {
    if (n == 1) {
        if (p[0] == '1' || p[0] == 'T' || p[0] == 't') { out = true; return true; }
        if (p[0] == '0' || p[0] == 'F' || p[0] == 'f') { out = false; return true; }
        return false;
    }
    auto eq = [&](const char* s, std::size_t m) {
        if (n != m) return false;
        for (std::size_t i = 0; i < m; i++)
            if (std::tolower(static_cast<unsigned char>(p[i])) != s[i]) return false;
        return true;
    };
    if (eq("true", 4)) { out = true; return true; }
    if (eq("false", 5)) { out = false; return true; }
    return false;
}

// --- field splitting --------------------------------------------------------

/// One field of one row, as a view into the file buffer. `needs_unquote` marks
/// the rare field containing a doubled quote, which cannot be used in place.
struct Field {
    const char* p = nullptr;
    std::size_t n = 0;
    bool needs_unquote = false;
};

/*!
 * Split one record, starting at `pos`, into `out`. Returns the offset just past
 * the record's newline, or `end` at the end of the buffer.
 *
 * Fields are views into the buffer: no allocation, no copy, and a numeric field
 * is parsed straight out of the file's own bytes.
 */
std::size_t split_record(const char* buf, std::size_t pos, std::size_t end,
                         char delim, char quote, std::vector<Field>& out) {
    out.clear();
    while (pos <= end) {
        Field f;
        if (pos < end && buf[pos] == quote) {
            pos++;
            const std::size_t start = pos;
            bool doubled = false;
            while (pos < end) {
                if (buf[pos] == quote) {
                    if (pos + 1 < end && buf[pos + 1] == quote) { doubled = true; pos += 2; continue; }
                    break;
                }
                pos++;
            }
            f.p = buf + start;
            f.n = pos - start;
            f.needs_unquote = doubled;
            if (pos < end && buf[pos] == quote) pos++;
        } else {
            const std::size_t start = pos;
            while (pos < end && buf[pos] != delim && buf[pos] != '\n' && buf[pos] != '\r') pos++;
            f.p = buf + start;
            f.n = pos - start;
        }
        out.push_back(f);

        if (pos >= end) return end;
        if (buf[pos] == delim) { pos++; continue; }
        if (buf[pos] == '\r') { pos++; if (pos < end && buf[pos] == '\n') pos++; return pos; }
        if (buf[pos] == '\n') { return pos + 1; }
        // A stray character after a closing quote: skip to the record end
        // rather than losing sync with the rest of the file.
        while (pos < end && buf[pos] != '\n') pos++;
        return pos < end ? pos + 1 : end;
    }
    return end;
}

std::string unquote(const Field& f, char quote) {
    std::string s;
    s.reserve(f.n);
    for (std::size_t i = 0; i < f.n; i++) {
        s.push_back(f.p[i]);
        if (f.needs_unquote && f.p[i] == quote && i + 1 < f.n && f.p[i + 1] == quote) i++;
    }
    return s;
}

// --- file reading -----------------------------------------------------------

/*!
 * The file's bytes, mapped where possible.
 *
 * Reading a CSV into a heap buffer costs a full copy of it before parsing even
 * starts -- fifty megabytes of memcpy for a fifty megabyte file, and twice the
 * peak. Mapping it hands the parser the page cache directly, which is what the
 * readers this is competing with do. Falls back to a read on Windows and if the
 * mapping fails.
 */
class FileBytes {
public:
    explicit FileBytes(const std::string& filename) {
#ifndef _WIN32
        fd_ = ::open(filename.c_str(), O_RDONLY);
        if (fd_ >= 0) {
            struct stat st;
            if (::fstat(fd_, &st) == 0 && st.st_size > 0) {
                void* m = ::mmap(nullptr, static_cast<std::size_t>(st.st_size),
                                 PROT_READ, MAP_PRIVATE, fd_, 0);
                if (m != MAP_FAILED) {
                    mapped_ = static_cast<const char*>(m);
                    size_ = static_cast<std::size_t>(st.st_size);
                    // WILLNEED only. MADV_SEQUENTIAL also tells the kernel it
                    // may drop pages behind the read, which is right for a
                    // one-shot scan and wrong here -- reading the same file
                    // again re-faulted it from disk and took four times as
                    // long. Blocks are also read by several threads at once,
                    // which is not the sequential access it describes.
                    ::madvise(m, size_, MADV_WILLNEED);
                    return;
                }
            }
            ::close(fd_);
            fd_ = -1;
        }
#endif
        owned_ = read_into_vector(filename);
        size_ = owned_.size();
    }

    ~FileBytes() {
#ifndef _WIN32
        if (mapped_ != nullptr) ::munmap(const_cast<char*>(mapped_), size_);
        if (fd_ >= 0) ::close(fd_);
#endif
    }
    FileBytes(const FileBytes&) = delete;
    FileBytes& operator=(const FileBytes&) = delete;

    const char* data() const { return mapped_ != nullptr ? mapped_ : owned_.data(); }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    char operator[](std::size_t i) const { return data()[i]; }

private:
    static std::vector<char> read_into_vector(const std::string& filename);

    const char* mapped_ = nullptr;
    std::vector<char> owned_;
    std::size_t size_ = 0;
    int fd_ = -1;
};

std::vector<char> read_whole_file_impl(const std::string& filename) {
    FILE* fp = open_file(filename, "rb");
    if (fp == nullptr) throw std::runtime_error("cannot open " + filename);
    if (fseek64(fp, 0, SEEK_END) != 0) { std::fclose(fp); throw std::runtime_error("cannot seek " + filename); }
    const long long size = ftell64(fp);
    std::rewind(fp);
    if (size < 0) { std::fclose(fp); throw std::runtime_error("cannot size " + filename); }
    std::vector<char> buf(static_cast<std::size_t>(size));
    const std::size_t got = buf.empty() ? 0 : std::fread(buf.data(), 1, buf.size(), fp);
    std::fclose(fp);
    buf.resize(got);
    return buf;
}

std::vector<char> FileBytes::read_into_vector(const std::string& filename) {
    return read_whole_file_impl(filename);
}

/*!
 * Record boundaries at roughly `block_size` intervals.
 *
 * The fast path -- a file with no quote character anywhere, or one promising no
 * newlines inside values -- moves each candidate offset forward to the next
 * newline, which is a memchr. Otherwise the file is scanned once tracking quote
 * state, which is correct and is the price of a file that needs it.
 */
std::vector<std::size_t> find_boundaries(const FileBytes& buf, std::size_t start,
                                         std::size_t block_size, char quote,
                                         bool newlines_in_values) {
    std::vector<std::size_t> bounds{start};
    const std::size_t n = buf.size();
    if (start >= n) { bounds.push_back(n); return bounds; }

    const bool has_quote =
            std::memchr(buf.data() + start, quote, n - start) != nullptr;

    if (!newlines_in_values || !has_quote) {
        std::size_t pos = start;
        while (pos < n) {
            std::size_t target = pos + block_size;
            if (target >= n) break;
            const void* nl = std::memchr(buf.data() + target, '\n', n - target);
            if (nl == nullptr) break;
            pos = static_cast<std::size_t>(static_cast<const char*>(nl) - buf.data()) + 1;
            bounds.push_back(pos);
        }
        bounds.push_back(n);
        return bounds;
    }

    bool in_quote = false;
    std::size_t next_target = start + block_size;
    for (std::size_t i = start; i < n; i++) {
        const char c = buf[i];
        if (c == quote) {
            if (in_quote && i + 1 < n && buf[i + 1] == quote) { i++; continue; }
            in_quote = !in_quote;
        } else if (c == '\n' && !in_quote && i + 1 >= next_target) {
            bounds.push_back(i + 1);
            next_target = i + 1 + block_size;
        }
    }
    bounds.push_back(n);
    return bounds;
}

// --- per-block column buffers -----------------------------------------------

/// One column's worth of one block. Only the vector matching `kind` is filled.
struct BlockColumn {
    std::vector<long long> i64;
    std::vector<double> f64;
    std::vector<unsigned char> b;
    std::vector<int> codes;
    std::vector<std::string> local_dict;
    std::unordered_map<std::string, int> local_lookup;
    std::vector<unsigned char> valid;      // 0 = missing
    bool overflowed = false;               // the inferred type did not fit
};

int local_code(BlockColumn& c, const std::string& s) {
    auto it = c.local_lookup.find(s);
    if (it != c.local_lookup.end()) return it->second;
    const int code = static_cast<int>(c.local_dict.size());
    c.local_dict.push_back(s);
    c.local_lookup.emplace(s, code);
    return code;
}

CsvColumnKind loosen(CsvColumnKind k) {
    switch (k) {
        case CsvColumnKind::Null:    return CsvColumnKind::Integer;
        case CsvColumnKind::Integer: return CsvColumnKind::Boolean;
        case CsvColumnKind::Boolean: return CsvColumnKind::Real;
        case CsvColumnKind::Real:    return CsvColumnKind::Text;
        default:                     return CsvColumnKind::Text;
    }
}

}  // namespace

// --- the reader -------------------------------------------------------------

namespace {

struct Parsed {
    std::vector<std::string> names;
    std::vector<CsvColumnKind> kinds;
    std::size_t data_start = 0;
};

/// Header row and column names.
Parsed read_header(const FileBytes& buf, const CsvOptions& o) {
    Parsed p;
    if (buf.empty()) return p;
    std::vector<Field> fields;
    const std::size_t after = split_record(buf.data(), 0, buf.size(), o.delimiter, o.quote, fields);
    if (o.has_header) {
        for (const Field& f : fields) p.names.push_back(unquote(f, o.quote));
        p.data_start = after;
    } else {
        for (std::size_t i = 0; i < fields.size(); i++) p.names.push_back("f" + std::to_string(i));
        p.data_start = 0;
    }
    return p;
}

bool is_na(const Field& f, const CsvOptions& o) {
    for (const std::string& s : o.na_values) {
        if (f.n == s.size() && std::memcmp(f.p, s.data(), f.n) == 0) return true;
    }
    return false;
}

/// Infer each column's type from the first `max_rows` records.
std::vector<CsvColumnKind> infer(const FileBytes& buf, std::size_t start,
                                 std::size_t n_cols, const CsvOptions& o,
                                 std::size_t max_rows) {
    std::vector<CsvColumnKind> kinds(n_cols, CsvColumnKind::Null);
    std::vector<Field> fields;
    std::size_t pos = start;
    std::size_t rows = 0;
    while (pos < buf.size() && rows < max_rows) {
        pos = split_record(buf.data(), pos, buf.size(), o.delimiter, o.quote, fields);
        rows++;
        for (std::size_t c = 0; c < n_cols && c < fields.size(); c++) {
            const Field& f = fields[c];
            if (is_na(f, o)) continue;                   // a missing value tells us nothing
            // Loosen until the value fits. At most four steps, and it only ever
            // moves in one direction, so the column converges.
            for (;;) {
                bool ok = false;
                long long i; double d; bool b;
                switch (kinds[c]) {
                    case CsvColumnKind::Null:    ok = false; break;
                    case CsvColumnKind::Integer: ok = parse_int64(f.p, f.n, i); break;
                    case CsvColumnKind::Boolean: ok = parse_bool(f.p, f.n, b); break;
                    case CsvColumnKind::Real:    ok = parse_double(f.p, f.n, d); break;
                    case CsvColumnKind::Text:    ok = true; break;
                }
                if (ok) break;
                kinds[c] = loosen(kinds[c]);
            }
        }
    }
    // A column that was only ever missing has no evidence; text is the choice
    // that cannot be wrong about the values it never saw.
    for (CsvColumnKind& k : kinds) if (k == CsvColumnKind::Null) k = CsvColumnKind::Text;
    for (const std::string& name : o.force_text_columns) (void) name;
    return kinds;
}

/// Parse one block into per-column buffers.
void parse_block(const FileBytes& buf, std::size_t begin, std::size_t end,
                 const std::vector<CsvColumnKind>& kinds, const CsvOptions& o,
                 std::vector<BlockColumn>& cols, std::size_t& n_rows) {
    const std::size_t n_cols = kinds.size();
    cols.assign(n_cols, BlockColumn());
    std::vector<Field> fields;
    std::size_t pos = begin;
    n_rows = 0;
    while (pos < end) {
        pos = split_record(buf.data(), pos, end, o.delimiter, o.quote, fields);
        // A blank trailing line is not a row.
        if (fields.size() == 1 && fields[0].n == 0) continue;
        n_rows++;
        for (std::size_t c = 0; c < n_cols; c++) {
            BlockColumn& col = cols[c];
            if (c >= fields.size()) {                    // ragged row: missing
                col.valid.push_back(0);
                switch (kinds[c]) {
                    case CsvColumnKind::Integer: col.i64.push_back(0); break;
                    case CsvColumnKind::Boolean: col.b.push_back(0); break;
                    // NaN rather than 0 for a missing real. The mask is the
                    // authoritative record, but a value has to be stored too,
                    // and 0 is a number somebody will plot. NaN survives being
                    // handed to code that drops the mask, which is what numpy
                    // and pyarrow both do here.
                    case CsvColumnKind::Real:    col.f64.push_back(std::nan("")); break;
                    default:                     col.codes.push_back(-1); break;
                }
                continue;
            }
            const Field& f = fields[c];
            if (is_na(f, o)) {
                col.valid.push_back(0);
                switch (kinds[c]) {
                    case CsvColumnKind::Integer: col.i64.push_back(0); break;
                    case CsvColumnKind::Boolean: col.b.push_back(0); break;
                    case CsvColumnKind::Real:    col.f64.push_back(std::nan("")); break;
                    default:                     col.codes.push_back(-1); break;
                }
                continue;
            }
            switch (kinds[c]) {
                case CsvColumnKind::Integer: {
                    long long v;
                    if (parse_int64(f.p, f.n, v)) { col.i64.push_back(v); col.valid.push_back(1); }
                    else { col.overflowed = true; col.i64.push_back(0); col.valid.push_back(0); }
                    break;
                }
                case CsvColumnKind::Boolean: {
                    bool v;
                    if (parse_bool(f.p, f.n, v)) { col.b.push_back(v ? 1 : 0); col.valid.push_back(1); }
                    else { col.overflowed = true; col.b.push_back(0); col.valid.push_back(0); }
                    break;
                }
                case CsvColumnKind::Real: {
                    double v;
                    if (parse_double(f.p, f.n, v)) { col.f64.push_back(v); col.valid.push_back(1); }
                    else { col.overflowed = true; col.f64.push_back(std::nan("")); col.valid.push_back(0); }
                    break;
                }
                default: {
                    col.codes.push_back(local_code(col, f.needs_unquote ? unquote(f, o.quote)
                                                                        : std::string(f.p, f.n)));
                    col.valid.push_back(1);
                    break;
                }
            }
        }
    }
}

}  // namespace

std::vector<std::string> read_csv_column_names(const std::string& filename,
                                               const CsvOptions& options) {
    const FileBytes buf(filename);
    return read_header(buf, options).names;
}

std::vector<CsvColumnKind> infer_csv_columns(const std::string& filename,
                                             const CsvOptions& options) {
    const FileBytes buf(filename);
    const Parsed p = read_header(buf, options);
    if (p.names.empty()) return {};
    return infer(buf, p.data_start, p.names.size(), options, 65536);
}

data::DataStore read_csv(const std::string& filename, const CsvOptions& options) {
    data::DataStore store;
    read_csv_into(store, filename, options);
    return store;
}

// The in-place form is the implementation and the by-value one delegates to it,
// not the other way round: building a store and then assigning it into the
// caller's holds the whole table twice for the length of the assignment, which
// for the tables this is for is the difference between opening a file and not.
void read_csv_into(data::DataStore& store, const std::string& filename,
                   const CsvOptions& options) {
    const FileBytes buf(filename);
    store = data::DataStore();
    Parsed p = read_header(buf, options);
    if (p.names.empty()) return;
    const std::size_t n_cols = p.names.size();

    std::vector<CsvColumnKind> kinds = infer(buf, p.data_start, n_cols, options, 65536);
    for (const std::string& forced : options.force_text_columns) {
        for (std::size_t c = 0; c < n_cols; c++)
            if (p.names[c] == forced) kinds[c] = CsvColumnKind::Text;
    }

    // Parse, and if a block meets a value the inferred type cannot hold, loosen
    // that column and parse again. Inference looks at the first 65536 rows, so
    // this is the tail case -- a column that is integral for a million rows and
    // then is not. At most a few passes, and it converges because loosening only
    // ever moves toward Text.
    std::vector<std::vector<BlockColumn>> block_cols;
    std::vector<std::size_t> block_rows;
    std::vector<std::size_t> bounds;
    for (int attempt = 0; attempt < 5; attempt++) {
        bounds = find_boundaries(buf, p.data_start, options.block_size,
                                 options.quote, options.newlines_in_values);
        const std::size_t n_blocks = bounds.size() - 1;
        block_cols.assign(n_blocks, {});
        block_rows.assign(n_blocks, 0);

        unsigned n_threads = 1;
        if (options.threads > 0) n_threads = static_cast<unsigned>(options.threads);
        else if (options.threads == 0) {
            n_threads = std::max(1u, std::thread::hardware_concurrency());
            n_threads = static_cast<unsigned>(std::min<std::size_t>(n_threads, n_blocks));
        }

        if (n_threads > 1 && n_blocks > 1) {
            std::vector<std::thread> workers;
            workers.reserve(n_threads - 1);
            auto body = [&](unsigned t) {
                for (std::size_t bidx = t; bidx < n_blocks; bidx += n_threads)
                    parse_block(buf, bounds[bidx], bounds[bidx + 1], kinds, options,
                                block_cols[bidx], block_rows[bidx]);
            };
            for (unsigned t = 1; t < n_threads; t++) workers.emplace_back([&, t] { body(t); });
            body(0);
            for (auto& w : workers) w.join();
        } else {
            for (std::size_t bidx = 0; bidx < n_blocks; bidx++)
                parse_block(buf, bounds[bidx], bounds[bidx + 1], kinds, options,
                            block_cols[bidx], block_rows[bidx]);
        }

        bool again = false;
        for (std::size_t c = 0; c < n_cols; c++) {
            bool over = false;
            for (const auto& bc : block_cols) if (c < bc.size() && bc[c].overflowed) over = true;
            if (over && kinds[c] != CsvColumnKind::Text) { kinds[c] = loosen(kinds[c]); again = true; }
        }
        if (!again) break;
    }

    std::size_t total = 0;
    for (std::size_t r : block_rows) total += r;
    store.set_n_rows(total);

    // Columns are added serially -- adding one mutates the store's vector -- and
    // then filled in parallel, because each column's concatenation touches only
    // its own buffers. This half is a straight memory copy of the whole table
    // and was most of what did not scale: at four threads the parse had dropped
    // to a third and this had not moved at all.
    std::vector<data::ColumnType> col_types(n_cols);
    for (std::size_t c = 0; c < n_cols; c++) {
        switch (kinds[c]) {
            case CsvColumnKind::Integer: col_types[c] = data::ColumnType::Int64; break;
            case CsvColumnKind::Boolean: col_types[c] = data::ColumnType::Bool; break;
            case CsvColumnKind::Real:
                col_types[c] = options.use_float32 ? data::ColumnType::Float32
                                                   : data::ColumnType::Float64;
                break;
            default: col_types[c] = data::ColumnType::String; break;
        }
        store.add_column(p.names[c], col_types[c]);
    }

    // Concatenate the blocks into the store's columns.
    auto build_column = [&](std::size_t c) {
        const data::ColumnType ct = col_types[c];
        data::Column& out = store.column(static_cast<int>(c));
        (void) ct;

        if (ct == data::ColumnType::String) {
            // Merge the per-block dictionaries by remapping codes. Building one
            // shared dictionary during the parse would need a lock on every text
            // field, which is what threading was for; and feeding the rows back
            // through push_string would do a hash lookup per ROW rather than one
            // per distinct value -- on a million rows drawn from four labels
            // that was most of the read.
            std::vector<std::string> merged;
            std::unordered_map<std::string, int> merged_lookup;
            std::vector<std::vector<int>> remap(block_cols.size());
            for (std::size_t bi = 0; bi < block_cols.size(); bi++) {
                if (c >= block_cols[bi].size()) continue;
                const BlockColumn& b = block_cols[bi][c];
                remap[bi].resize(b.local_dict.size());
                for (std::size_t k = 0; k < b.local_dict.size(); k++) {
                    auto it = merged_lookup.find(b.local_dict[k]);
                    if (it == merged_lookup.end()) {
                        const int code = static_cast<int>(merged.size());
                        merged.push_back(b.local_dict[k]);
                        merged_lookup.emplace(b.local_dict[k], code);
                        remap[bi][k] = code;
                    } else {
                        remap[bi][k] = it->second;
                    }
                }
            }
            // A missing value still needs a code; give it an empty string.
            int empty_code = -1;
            for (const auto& bc : block_cols) {
                if (c >= bc.size()) continue;
                for (int code : bc[c].codes) if (code < 0) { empty_code = 0; break; }
                if (empty_code >= 0) break;
            }
            if (empty_code >= 0) {
                auto it = merged_lookup.find(std::string());
                if (it != merged_lookup.end()) empty_code = it->second;
                else { empty_code = static_cast<int>(merged.size()); merged.emplace_back(); }
            }
            out.set_dictionary(merged);
            out.reserve(total);
            std::vector<int> codes;
            for (std::size_t bi = 0; bi < block_cols.size(); bi++) {
                if (c >= block_cols[bi].size()) continue;
                const BlockColumn& b = block_cols[bi][c];
                codes.resize(b.codes.size());
                for (std::size_t r = 0; r < b.codes.size(); r++)
                    codes[r] = b.codes[r] < 0 ? empty_code : remap[bi][b.codes[r]];
                out.append_codes(codes.data(), codes.size());
            }
        } else if (ct == data::ColumnType::Int64) {
            out.reserve(total);
            for (const auto& bc : block_cols)
                if (c < bc.size()) out.append_i64(bc[c].i64.data(), bc[c].i64.size());
        } else if (ct == data::ColumnType::Bool) {
            std::vector<unsigned char> all;
            all.reserve(total);
            for (const auto& bc : block_cols)
                if (c < bc.size()) all.insert(all.end(), bc[c].b.begin(), bc[c].b.end());
            out.set_bool(all.data(), static_cast<int>(all.size()));
        } else if (ct == data::ColumnType::Float32) {
            out.reserve(total);
            std::vector<float> f;
            for (const auto& bc : block_cols) {
                if (c >= bc.size()) continue;
                f.resize(bc[c].f64.size());
                for (std::size_t i = 0; i < f.size(); i++) f[i] = static_cast<float>(bc[c].f64[i]);
                out.append_f32(f.data(), f.size());
            }
        } else {
            // Appended block by block, straight into the column. Collecting into
            // a temporary first and then handing that to a setter copied every
            // value twice and held both at the peak.
            out.reserve(total);
            for (const auto& bc : block_cols)
                if (c < bc.size()) out.append_f64(bc[c].f64.data(), bc[c].f64.size());
        }

        // Only attach a mask if something was actually missing: the common case
        // is a complete column, and a mask of all-ones costs memory and a branch
        // per row for nothing.
        bool any_missing = false;
        for (const auto& bc : block_cols)
            if (c < bc.size())
                for (unsigned char v : bc[c].valid) if (!v) { any_missing = true; break; }
        if (any_missing) {
            std::vector<unsigned char> valid;
            valid.reserve(total);
            for (const auto& bc : block_cols)
                if (c < bc.size()) valid.insert(valid.end(), bc[c].valid.begin(), bc[c].valid.end());
            out.set_mask(valid.data(), static_cast<int>(valid.size()));
        }
        out.shrink_to_fit();
    };

    // Serially, on purpose. Filling the columns on threads was tried and was
    // 60% SLOWER: each thread walks every block to pick out its own column, so
    // the block buffers are read n_columns times over with no locality, and the
    // copy is memory-bound to begin with. One pass in block order keeps the
    // reads sequential.
    for (std::size_t c = 0; c < n_cols; c++) build_column(c);
}

}  // namespace io
}  // namespace tttrlib
