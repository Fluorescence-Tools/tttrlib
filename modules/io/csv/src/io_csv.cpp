// SPDX-License-Identifier: BSD-3-Clause
#include "io_csv.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <atomic>
#include <chrono>
#include <functional>
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

// --- writing straight into the final columns --------------------------------

/*!
 * Where one column's values go, and the block-local state a text column needs.
 *
 * Exactly one pointer is set, matching the column's inferred kind, and it points
 * at the column's own buffer offset by the block's first row. There is no
 * per-block copy of the data at all: a block parses into the memory the column
 * will keep.
 */
struct ColumnSink {
    CsvColumnKind kind = CsvColumnKind::Text;
    long long* i64 = nullptr;
    double* f64 = nullptr;
    float* f32 = nullptr;
    int* codes = nullptr;
    unsigned char* bool_bytes = nullptr;   // staged: bits cannot be written in parallel
    unsigned char* valid = nullptr;        // staged for the same reason
};

/// A block's private string dictionary. Merged and remapped after the parse,
/// because sharing one during it would need a lock on every text field.
struct BlockDict {
    std::vector<std::string> dict;
    std::unordered_map<std::string, int> lookup;

    int code_for(const std::string& s) {
        auto it = lookup.find(s);
        if (it != lookup.end()) return it->second;
        const int code = static_cast<int>(dict.size());
        dict.push_back(s);
        lookup.emplace(s, code);
        return code;
    }
};

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

/*!
 * Count the records in a block, so the block after it knows where it starts.
 *
 * This is what makes writing into final positions possible, and it has to be
 * cheap or it eats the saving. For the common file it is a memchr loop -- the
 * bytes are scanned but almost nothing is done per byte -- against a parse that
 * converts every field. A blank line is not a record, matching what parse_block
 * does, or the offsets would drift.
 */
std::size_t count_rows(const FileBytes& buf, std::size_t begin, std::size_t end,
                       const CsvOptions& o, bool quote_aware) {
    const char* p = buf.data();
    if (!quote_aware) {
        std::size_t rows = 0, i = begin;
        while (i < end) {
            const void* nl = std::memchr(p + i, '\n', end - i);
            const std::size_t line_end = nl != nullptr
                    ? static_cast<std::size_t>(static_cast<const char*>(nl) - p) : end;
            const std::size_t len = line_end - i;
            if (!(len == 0 || (len == 1 && p[i] == '\r'))) rows++;
            if (nl == nullptr) break;
            i = line_end + 1;
        }
        return rows;
    }
    std::vector<Field> fields;
    std::size_t rows = 0, pos = begin;
    while (pos < end) {
        pos = split_record(p, pos, end, o.delimiter, o.quote, fields);
        if (fields.size() == 1 && fields[0].n == 0) continue;
        rows++;
    }
    return rows;
}

/// Parse one block directly into the columns, starting at absolute row `row0`.
void parse_block(const FileBytes& buf, std::size_t begin, std::size_t end,
                 const std::vector<CsvColumnKind>& kinds, const CsvOptions& o,
                 std::vector<ColumnSink>& sinks, std::vector<BlockDict>& dicts,
                 std::size_t row0, std::vector<unsigned char>& overflowed) {
    const std::size_t n_cols = kinds.size();
    std::vector<Field> fields;
    std::size_t pos = begin;
    std::size_t r = row0;

    auto missing = [&](std::size_t c, std::size_t row) {
        ColumnSink& s = sinks[c];
        if (s.valid != nullptr) s.valid[row] = 0;
        switch (kinds[c]) {
            case CsvColumnKind::Integer: s.i64[row] = 0; break;
            case CsvColumnKind::Boolean: s.bool_bytes[row] = 0; break;
            case CsvColumnKind::Real:
                // NaN, not zero: the mask is authoritative but the value has to
                // be something, and zero is a number somebody will plot.
                if (s.f64 != nullptr) s.f64[row] = std::nan("");
                else s.f32[row] = std::nanf("");
                break;
            default: s.codes[row] = -1; break;
        }
    };

    while (pos < end) {
        pos = split_record(buf.data(), pos, end, o.delimiter, o.quote, fields);
        if (fields.size() == 1 && fields[0].n == 0) continue;   // blank line
        for (std::size_t c = 0; c < n_cols; c++) {
            ColumnSink& s = sinks[c];
            if (c >= fields.size() || is_na(fields[c], o)) { missing(c, r); continue; }
            const Field& f = fields[c];
            switch (kinds[c]) {
                case CsvColumnKind::Integer: {
                    long long v;
                    if (parse_int64(f.p, f.n, v)) s.i64[r] = v;
                    else { overflowed[c] = 1; missing(c, r); }
                    break;
                }
                case CsvColumnKind::Boolean: {
                    bool v;
                    if (parse_bool(f.p, f.n, v)) s.bool_bytes[r] = v ? 1 : 0;
                    else { overflowed[c] = 1; missing(c, r); }
                    break;
                }
                case CsvColumnKind::Real: {
                    double v;
                    if (parse_double(f.p, f.n, v)) {
                        if (s.f64 != nullptr) s.f64[r] = v;
                        else s.f32[r] = static_cast<float>(v);
                    } else { overflowed[c] = 1; missing(c, r); }
                    break;
                }
                default:
                    s.codes[r] = dicts[c].code_for(
                            f.needs_unquote ? unquote(f, o.quote) : std::string(f.p, f.n));
                    break;
            }
        }
        r++;
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
    // Phase timings, printed when TTTRLIB_CSV_PROFILE is set. Kept because the
    // two things that looked like the bottleneck were not, twice.
    const bool profile = std::getenv("TTTRLIB_CSV_PROFILE") != nullptr;
    auto clock_now = [] { return std::chrono::steady_clock::now(); };
    auto since = [&](std::chrono::steady_clock::time_point t0) {
        return std::chrono::duration<double, std::milli>(clock_now() - t0).count();
    };
    auto t_all = clock_now();

    auto t0 = clock_now();
    const FileBytes buf(filename);
    if (profile) std::fprintf(stderr, "  open/map   %6.1f ms\n", since(t0));
    store = data::DataStore();
    Parsed p = read_header(buf, options);
    if (p.names.empty()) return;
    const std::size_t n_cols = p.names.size();

    t0 = clock_now();
    // A smaller sample than it looks like it should be. Inference is serial, so
    // every row of it is on the critical path, and a column that changes shape
    // after the first few thousand rows is caught by the loosen-and-retry loop
    // below anyway. 65536 rows cost 11 ms of a 90 ms read for no extra safety.
    std::vector<CsvColumnKind> kinds = infer(buf, p.data_start, n_cols, options, 8192);
    if (profile) std::fprintf(stderr, "  infer      %6.1f ms\n", since(t0));
    for (const std::string& forced : options.force_text_columns) {
        for (std::size_t c = 0; c < n_cols; c++)
            if (p.names[c] == forced) kinds[c] = CsvColumnKind::Text;
    }

    auto thread_count = [&](std::size_t n_items) {
        if (options.threads > 0) return static_cast<unsigned>(options.threads);
        if (options.threads < 0) return 1u;
        unsigned n = std::max(1u, std::thread::hardware_concurrency());
        return static_cast<unsigned>(std::min<std::size_t>(n, std::max<std::size_t>(1, n_items)));
    };
    /*!
     * Run `body(b)` for every block, claimed from a shared counter.
     *
     * Not one strided slice per thread. Blocks differ in cost -- a block of
     * short numeric rows parses faster than one full of quoted text -- and on a
     * machine with performance and efficiency cores they differ again by which
     * core drew them. With a fixed assignment everybody waits for the slowest
     * slice; claiming the next block when free spreads both kinds of imbalance
     * without anybody tuning a constant.
     */
    auto run_blocks = [&](unsigned n_threads, std::size_t n_blocks,
                          const std::function<void(std::size_t)>& body) {
        if (n_threads <= 1 || n_blocks <= 1) {
            for (std::size_t b = 0; b < n_blocks; b++) body(b);
            return;
        }
        std::atomic<std::size_t> next{0};
        auto worker = [&] {
            for (;;) {
                const std::size_t b = next.fetch_add(1, std::memory_order_relaxed);
                if (b >= n_blocks) break;
                body(b);
            }
        };
        std::vector<std::thread> workers;
        workers.reserve(n_threads - 1);
        for (unsigned t = 1; t < n_threads; t++) workers.emplace_back(worker);
        worker();
        for (auto& w : workers) w.join();
    };

    // Parse, and if a block meets a value the inferred type cannot hold, loosen
    // that column and parse again. Inference looks at the first 65536 rows, so
    // this is the tail case -- a column that is integral for a million rows and
    // then is not. It converges because loosening only moves toward Text.
    std::vector<std::size_t> bounds;
    std::vector<std::size_t> row0;
    std::size_t total = 0;
    std::vector<std::vector<BlockDict>> dicts;
    std::vector<unsigned char> bool_stage, valid_stage;
    std::vector<data::ColumnType> col_types(n_cols);

    // Block size is chosen from the file and the machine, not taken as given.
    // A 16 MB block on a 50 MB file is four blocks, so four threads, and the
    // parse -- which is 70% of the read -- could not use the other four however
    // many were asked for. Several blocks per thread also lets a thread that
    // draws a cheap block pick up another instead of finishing early.
    std::size_t block_size = options.block_size;
    {
        const unsigned hw = std::max(1u, options.threads > 0
                ? static_cast<unsigned>(options.threads)
                : std::thread::hardware_concurrency());
        const std::size_t bytes = buf.size() - std::min(buf.size(), p.data_start);
        const std::size_t want = bytes / (static_cast<std::size_t>(hw) * 4 + 1);
        block_size = std::min(block_size, std::max<std::size_t>(256u << 10, want));
    }

    for (int attempt = 0; attempt < 5; attempt++) {
        bounds = find_boundaries(buf, p.data_start, block_size,
                                 options.quote, options.newlines_in_values);
        const std::size_t n_blocks = bounds.size() - 1;
        const bool quote_aware = options.newlines_in_values;

        // Pass 1: how many records in each block. This is what lets pass 2 write
        // into final positions instead of into per-block buffers that then have
        // to be concatenated -- and the concatenation was the serial half that
        // would not scale, about 60% of the read at four threads.
        std::vector<std::size_t> counts(n_blocks, 0);
        const unsigned nt = thread_count(n_blocks);
        t0 = clock_now();
        run_blocks(nt, n_blocks, [&](std::size_t b) {
            counts[b] = count_rows(buf, bounds[b], bounds[b + 1], options, quote_aware);
        });
        if (profile) std::fprintf(stderr, "  count      %6.1f ms (%zu blocks, %u threads)\n",
                                  since(t0), n_blocks, nt);
        row0.assign(n_blocks + 1, 0);
        for (std::size_t b = 0; b < n_blocks; b++) row0[b + 1] = row0[b] + counts[b];
        total = row0[n_blocks];

        // Allocate the columns once, at their final size.
        t0 = clock_now();
        store = data::DataStore();
        store.set_n_rows(total);
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
            const int idx = store.add_column(p.names[c], col_types[c]);
            store.column(idx).resize(total);
        }

        // Bool columns and the validity flags are staged as bytes. Bits cannot
        // be written from several threads at once -- two blocks whose rows fall
        // in the same 64-bit word would race -- so they are packed in one pass
        // afterwards.
        std::size_t n_bool = 0;
        for (std::size_t c = 0; c < n_cols; c++) if (kinds[c] == CsvColumnKind::Boolean) n_bool++;
        bool_stage.assign(n_bool * total, 0);
        valid_stage.assign(n_cols * total, 1);

        if (profile) std::fprintf(stderr, "  allocate   %6.1f ms\n", since(t0));
        std::vector<std::vector<ColumnSink>> block_sinks(n_blocks);
        dicts.assign(n_blocks, std::vector<BlockDict>(n_cols));
        std::vector<std::vector<unsigned char>> block_over(n_blocks,
                std::vector<unsigned char>(n_cols, 0));

        for (std::size_t b = 0; b < n_blocks; b++) {
            block_sinks[b].assign(n_cols, ColumnSink());
            std::size_t bool_slot = 0;
            for (std::size_t c = 0; c < n_cols; c++) {
                ColumnSink& s = block_sinks[b][c];
                s.kind = kinds[c];
                data::Column& col = store.column(static_cast<int>(c));
                switch (kinds[c]) {
                    case CsvColumnKind::Integer: s.i64 = col.i64_data(); break;
                    case CsvColumnKind::Boolean:
                        s.bool_bytes = bool_stage.data() + bool_slot * total; break;
                    case CsvColumnKind::Real:
                        if (col_types[c] == data::ColumnType::Float32) s.f32 = col.f32_data();
                        else s.f64 = col.f64_data();
                        break;
                    default: s.codes = col.codes_data(); break;
                }
                s.valid = valid_stage.data() + c * total;
                if (kinds[c] == CsvColumnKind::Boolean) bool_slot++;
            }
        }

        // Pass 2: parse into place.
        t0 = clock_now();
        run_blocks(nt, n_blocks, [&](std::size_t b) {
            parse_block(buf, bounds[b], bounds[b + 1], kinds, options,
                        block_sinks[b], dicts[b], row0[b], block_over[b]);
        });

        if (profile) std::fprintf(stderr, "  parse      %6.1f ms\n", since(t0));
        bool again = false;
        for (std::size_t c = 0; c < n_cols; c++) {
            bool over = false;
            for (const auto& bo : block_over) if (bo[c]) over = true;
            if (over && kinds[c] != CsvColumnKind::Text) { kinds[c] = loosen(kinds[c]); again = true; }
        }
        if (!again) break;
    }

    const std::size_t n_blocks = bounds.size() - 1;
    t0 = clock_now();

    // Merge the per-block string dictionaries and remap the codes in place. Each
    // block owns a disjoint range of rows, so the remap is parallel and needs no
    // locking -- and it walks the codes once rather than rebuilding the column.
    for (std::size_t c = 0; c < n_cols; c++) {
        if (col_types[c] != data::ColumnType::String) continue;
        std::vector<std::string> merged;
        std::unordered_map<std::string, int> merged_lookup;
        std::vector<std::vector<int>> remap(n_blocks);
        for (std::size_t b = 0; b < n_blocks; b++) {
            const std::vector<std::string>& d = dicts[b][c].dict;
            remap[b].resize(d.size());
            for (std::size_t k = 0; k < d.size(); k++) {
                auto it = merged_lookup.find(d[k]);
                if (it != merged_lookup.end()) { remap[b][k] = it->second; continue; }
                const int code = static_cast<int>(merged.size());
                merged.push_back(d[k]);
                merged_lookup.emplace(d[k], code);
                remap[b][k] = code;
            }
        }
        int* codes = store.column(static_cast<int>(c)).codes_data();

        // A missing text value needs SOME code, and an empty label is the
        // natural one -- but only add it when there is actually a missing
        // value. Adding it regardless put a phantom "" in the dictionary of
        // every complete text column, which then showed up as a category with
        // no rows in it.
        bool any_missing = false;
        for (std::size_t r = 0; r < total; r++) if (codes[r] < 0) { any_missing = true; break; }
        int empty_code = 0;
        if (any_missing) {
            auto it = merged_lookup.find(std::string());
            if (it != merged_lookup.end()) empty_code = it->second;
            else { empty_code = static_cast<int>(merged.size()); merged.emplace_back(); }
        }
        const unsigned nt = thread_count(n_blocks);
        run_blocks(nt, n_blocks, [&](std::size_t b) {
            const std::vector<int>& rm = remap[b];
            for (std::size_t r = row0[b]; r < row0[b + 1]; r++) {
                const int v = codes[r];
                codes[r] = (v < 0) ? empty_code : rm[v];
            }
        });
        store.column(static_cast<int>(c)).set_dictionary(merged);
    }

    if (profile) std::fprintf(stderr, "  dict remap %6.1f ms\n", since(t0));
    t0 = clock_now();

    // Pack the staged bool columns and the validity masks. One pass each, and a
    // mask is only attached when something was actually missing -- the common
    // case is a complete column, and an all-valid mask costs memory and a branch
    // per row for nothing.
    std::size_t bool_slot = 0;
    for (std::size_t c = 0; c < n_cols; c++) {
        data::Column& col = store.column(static_cast<int>(c));
        if (col_types[c] == data::ColumnType::Bool) {
            col.set_bool(bool_stage.data() + bool_slot * total, static_cast<int>(total));
            bool_slot++;
        }
        const unsigned char* v = valid_stage.data() + c * total;
        bool any_missing = false;
        for (std::size_t r = 0; r < total; r++) if (!v[r]) { any_missing = true; break; }
        if (any_missing) col.set_mask(v, static_cast<int>(total));
    }
    if (profile) {
        std::fprintf(stderr, "  pack/mask  %6.1f ms\n", since(t0));
        std::fprintf(stderr, "  TOTAL      %6.1f ms\n", since(t_all));
    }
}

}  // namespace io
}  // namespace tttrlib
