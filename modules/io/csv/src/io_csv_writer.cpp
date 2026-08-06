// SPDX-License-Identifier: BSD-3-Clause
#include "io_csv_writer.h"

#include "decimal_exact.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace tttrlib {
namespace io {

namespace {

using decimal::kPow10;
using data::Column;
using data::ColumnType;
using data::DataStore;

// --- the block buffer --------------------------------------------------------

/*!
 * \brief A growable byte buffer that is never zero-filled.
 *
 * std::vector<char>::resize writes a zero over every byte that is about to be
 * overwritten by the formatter; on a hundred megabytes of output that is a
 * hundred megabytes of stores for nothing. RawVector default-initialises, so
 * the capacity is taken without being touched.
 */
struct Out {
    data::RawVector<char> b;
    std::size_t n = 0;

    void reset() { n = 0; }
    const char* data() const { return b.data(); }
    std::size_t size() const { return n; }

    /// Space for `k` more bytes. The caller writes at most that many and says
    /// how many it used -- which is how a formatter can write into the buffer
    /// directly instead of into a scratch array and then copying.
    inline char* room(std::size_t k) {
        if (b.size() - n < k) grow(k);
        return b.data() + n;
    }
    inline void advance(std::size_t k) { n += k; }
    inline void put(char c) { *room(1) = c; n++; }
    inline void put(const char* s, std::size_t k) {
        if (k) std::memcpy(room(k), s, k);
        n += k;
    }
    inline void put(const std::string& s) { put(s.data(), s.size()); }

private:
    void grow(std::size_t k) {
        const std::size_t want = n + k;
        std::size_t cap = b.empty() ? std::size_t(64) << 10 : b.size() * 2;
        while (cap < want) cap *= 2;
        b.resize(cap);
    }
};

// --- formatting a double -----------------------------------------------------

/*
 * Writing a double as the shortest text that reads back.
 *
 * This is where a CSV writer spends its time, and the standard library is not
 * dependable here. `std::to_chars` for double does exactly this job in one
 * pass, but it is the last corner of <charconv> the implementations filled in:
 * libstdc++ has it from GCC 11, and libc++ puts it in the dylib behind an
 * availability guard, so a build with a macOS 10.15 deployment floor -- which
 * is what this library ships -- cannot call it at all. `snprintf("%.17g")`
 * always works and costs 280 ns a value on this platform, where to_chars costs
 * 40; it also takes the locale lock, so threads make it slower rather than
 * faster. Arrow does not have this problem because it vendors
 * double-conversion. Vendoring is not open to this library, so the fast path
 * is written out here instead.
 *
 * The idea is trial and exact verification, and the verification is what makes
 * it honest:
 *
 * 1. Guess the decimal exponent, and for a candidate digit count d compute
 *    m = round(v * 10^k) in ordinary double arithmetic, which may be a unit or
 *    two off.
 * 2. Ask whether the decimal m * 10^-k IS v -- not approximately, exactly --
 *    by converting it back. When m <= 2^53 and |k| <= 22, both the mantissa
 *    and the power of ten are doubles exactly, so a single multiply or divide
 *    is the only rounding and lands on the double any correct parser would
 *    give (Clinger). That is one instruction, and it is a proof rather than a
 *    tolerance.
 * 3. Take the first d that verifies, so the answer is the shortest text that
 *    reads back.
 *
 * Clinger's conditions stop at fifteen digits; sixteen and seventeen are
 * settled by the integer comparison in decimal_exact.h, which is what covers
 * the values that carry a full double's worth of information. What is left
 * over -- a decimal exponent past about ±22, where neither test can represent
 * what it would have to compare -- says so, and the caller falls back to
 * to_chars where the platform has it and snprintf where it does not.
 *
 * The reader's parse_double decides by the same two rules, which is not a
 * coincidence: it is what makes write-then-read the identity rather than a
 * near miss.
 */

/// Write the decimal `m * 10^-k`, choosing fixed or scientific by the rule
/// printf's %g uses at 17 significant digits, so the two paths agree on shape.
inline std::size_t emit_decimal(char* p, unsigned long long m, int k) {
    // 1200e-2 and 12e0 are the same number; the short spelling is the one
    // asked for. Stripping here also means the search can stop at the first d
    // that verifies without checking whether a shorter one would have.
    while (m % 10ULL == 0ULL) { m /= 10ULL; k--; }

    char digits[24];
    const int d = static_cast<int>(std::to_chars(digits, digits + 24, m).ptr - digits);
    const int e = d - 1 - k;                       // exponent of the leading digit
    char* q = p;

    if (e < -4 || e >= 17) {
        *q++ = digits[0];
        if (d > 1) {
            *q++ = '.';
            std::memcpy(q, digits + 1, static_cast<std::size_t>(d - 1));
            q += d - 1;
        }
        *q++ = 'e';
        *q++ = e < 0 ? '-' : '+';
        const unsigned ae = static_cast<unsigned>(e < 0 ? -e : e);
        if (ae < 10) { *q++ = '0'; *q++ = static_cast<char>('0' + ae); }
        else q = std::to_chars(q, q + 3, ae).ptr;
    } else if (k <= 0) {                           // an integer, plus its zeros
        std::memcpy(q, digits, static_cast<std::size_t>(d));
        q += d;
        for (int i = 0; i < -k; i++) *q++ = '0';
    } else if (k < d) {                            // the point falls inside
        std::memcpy(q, digits, static_cast<std::size_t>(d - k));
        q += d - k;
        *q++ = '.';
        std::memcpy(q, digits + d - k, static_cast<std::size_t>(k));
        q += k;
    } else {                                       // 0.000ddd
        *q++ = '0';
        *q++ = '.';
        for (int i = 0; i < k - d; i++) *q++ = '0';
        std::memcpy(q, digits, static_cast<std::size_t>(d));
        q += d;
    }
    return static_cast<std::size_t>(q - p);
}

/// \see the sections above. 0 means "cannot prove it", not "cannot do it".
inline std::size_t format_shortest(char* p, double v) {
    char* q = p;
    if (std::signbit(v)) { *q++ = '-'; v = -v; }
    if (v == 0.0) { *q++ = '0'; return static_cast<std::size_t>(q - p); }

    const int e10 = static_cast<int>(std::floor(std::log10(v)));
    for (int d = 1; d <= 15; d++) {
        const int k = d - 1 - e10;                 // v == m * 10^-k, m of d digits
        if (k < -22 || k > 22) return 0;           // outside what can be proven
        const double scaled = k >= 0 ? v * kPow10[k] : v / kPow10[-k];
        if (!(scaled >= 1.0 && scaled < 1e16)) continue;   // log10 was off by one
        const unsigned long long guess =
                static_cast<unsigned long long>(scaled + 0.5);
        // The guess can be a unit out either way, and trying its neighbours is
        // cheaper than computing it correctly: the verification decides.
        for (int adj = 0; adj < 3; adj++) {
            const unsigned long long m = adj == 0 ? guess : adj == 1 ? guess - 1 : guess + 1;
            if (m == 0ULL || m > (1ULL << 53)) continue;
            const double back = k >= 0 ? static_cast<double>(m) / kPow10[k]
                                       : static_cast<double>(m) * kPow10[-k];
            if (back == v) return static_cast<std::size_t>(q - p) + emit_decimal(q, m, k);
        }
    }

#ifdef TTTRLIB_CSV_HAVE_INT128
    unsigned long long M = 0;
    int F = 0;
    if (decimal::split_double(v, M, F)) {
        for (int d = 16; d <= 17; d++) {
            const int k = d - 1 - e10;
            if (k < 0 || k > 19) break;            // 10^k has to be a 64-bit integer
            // The correctly rounded d-digit decimal of v, computed rather than
            // guessed: m = round(M * 10^k / 2^F), all of it in integers.
            const decimal::u128 scaled =
                    static_cast<decimal::u128>(M) * decimal::kPow10Int[k] +
                    (static_cast<decimal::u128>(1) << (F - 1));
            const decimal::u128 rounded = scaled >> F;
            if (rounded >> 57) break;
            const unsigned long long m = static_cast<unsigned long long>(rounded);
            if (decimal::reads_back(m, k, M, F))
                return static_cast<std::size_t>(q - p) + emit_decimal(q, m, k);
        }
    }
#endif
    return 0;
}

/*!
 * Give an integral value its decimal point back.
 *
 * Every path above spells 12.0 as "12", because both the shortest form and
 * printf's %g drop a fractional part that is all zeros -- and so does Arrow.
 * For a reader that infers types from the text that turns an all-integral
 * float column into an integer column, which is a dtype lost in transit rather
 * than a value: the two spellings parse to the same double.
 *
 * Only for a value written as digits alone. "1e-07", "nan" and "inf" have
 * nothing to add a point to, and adding one would make each of them unreadable.
 */
inline std::size_t keep_the_point(char* p, std::size_t n, std::size_t cap) {
    if (n == 0 || n + 2 > cap) return n;
    for (std::size_t i = 0; i < n; i++) {
        const char c = p[i];
        if (c != '-' && (c < '0' || c > '9')) return n;      // a point, an e, a nan
    }
    p[n] = '.';
    p[n + 1] = '0';
    return n + 2;
}

/*!
 * \param precision significant digits (printf's %g), or 0 for the shortest
 *        text that reads back.
 * \param decimals digits after the point (printf's %f), or -1 to leave the
 *        choice to `precision`. The two count different things and a caller
 *        matching a file another program writes usually means this one: the
 *        burst companion formats are specified as %.6f, which is six DECIMALS
 *        -- `precision = 6` would give six significant digits and write
 *        1.23457e-05 where the format calls for 0.000012.
 * \param keep_dot \see keep_the_point. Ignored in fixed mode, where the number
 *        of decimals is exactly what the caller asked for.
 */
inline std::size_t format_double(char* p, std::size_t cap, double v, int precision,
                                 int decimals = -1, bool keep_dot = false) {
    if (std::isnan(v)) { std::memcpy(p, "nan", 3); return 3; }
    if (std::isinf(v)) {
        if (v < 0) { std::memcpy(p, "-inf", 4); return 4; }
        std::memcpy(p, "inf", 3);
        return 3;
    }
    if (decimals >= 0) {
        // snprintf, and deliberately: fixed-point has no shortest form to find
        // and no round trip to guarantee -- the caller has asked for exactly
        // these digits -- so the fast path has nothing to contribute. It is
        // also opt-in, which is what makes the cost acceptable.
        const int k = std::snprintf(p, cap, "%.*f", decimals, v);
        return k < 0 ? 0 : static_cast<std::size_t>(k);
    }
    if (precision <= 0) {
        const std::size_t n = format_shortest(p, v);
        if (n) return keep_dot ? keep_the_point(p, n, cap) : n;
    }
#ifdef TTTRLIB_HAVE_FP_TO_CHARS
    const std::to_chars_result r =
            precision > 0
                    ? std::to_chars(p, p + cap, v, std::chars_format::general, precision)
                    : std::to_chars(p, p + cap, v);
    const std::size_t n = static_cast<std::size_t>(r.ptr - p);
    return keep_dot ? keep_the_point(p, n, cap) : n;
#else
    // Seventeen significant digits always read back as the same double, so
    // there is no ladder and no verifying parse -- one call, and the only cost
    // of the platform's missing to_chars is a value spelled with more digits
    // than it strictly needs.
    const int k = std::snprintf(p, cap, "%.*g", precision > 0 ? precision : 17, v);
    if (k < 0) return 0;
    const std::size_t n = static_cast<std::size_t>(k);
    return keep_dot ? keep_the_point(p, n, cap) : n;
#endif
}

/// \see format_double. float32 goes through the same search, on the value
/// widened to double: a float needs at most nine digits, so the fifteen the
/// search can prove always cover it, and 0.1f comes out "0.1" rather than as
/// the seventeen digits of the double it widens to.
inline std::size_t format_float(char* p, std::size_t cap, float v, int precision,
                                int decimals = -1, bool keep_dot = false) {
    if (precision > 0 || decimals >= 0 || !std::isfinite(v))
        return format_double(p, cap, static_cast<double>(v), precision, decimals, keep_dot);
    if (v == 0.0f) {
        std::size_t n = 0;
        if (std::signbit(v)) p[n++] = '-';
        p[n++] = '0';
        return keep_dot ? keep_the_point(p, n, cap) : n;
    }
    // The shortest decimal for the FLOAT, which is shorter than the shortest
    // for the double it widens to: 0.1f is "0.1" and not 0.10000000149011612.
    char* q = p;
    float w = v;
    if (std::signbit(w)) { *q++ = '-'; w = -w; }
    const int e10 = static_cast<int>(std::floor(std::log10(static_cast<double>(w))));
    for (int d = 1; d <= 9; d++) {
        const int k = d - 1 - e10;
        if (k < -22 || k > 22) break;
        const double scaled = k >= 0 ? static_cast<double>(w) * kPow10[k]
                                     : static_cast<double>(w) / kPow10[-k];
        if (!(scaled >= 1.0 && scaled < 1e16)) continue;
        const unsigned long long guess = static_cast<unsigned long long>(scaled + 0.5);
        for (int adj = 0; adj < 3; adj++) {
            const unsigned long long m = adj == 0 ? guess : adj == 1 ? guess - 1 : guess + 1;
            if (m == 0ULL || m > (1ULL << 53)) continue;
            const double back = k >= 0 ? static_cast<double>(m) / kPow10[k]
                                       : static_cast<double>(m) * kPow10[-k];
            if (static_cast<float>(back) == w) {
                const std::size_t n = static_cast<std::size_t>(q - p) + emit_decimal(q, m, k);
                return keep_dot ? keep_the_point(p, n, cap) : n;
            }
        }
    }
    return format_double(p, cap, static_cast<double>(v), 0, -1, keep_dot);
}

// --- quoting -----------------------------------------------------------------

struct Fmt {
    char delimiter = ',';
    char quote = '"';
    bool quote_all = false;
    int precision = 0;
    int decimals = -1;
    bool keep_dot = false;
    const std::string* null_string = nullptr;
    const std::string* true_string = nullptr;
    const std::string* false_string = nullptr;
};

/// RFC 4180: a value holding the delimiter, a quote, or a line break has to be
/// quoted. Nothing else does, whatever a spreadsheet may prefer.
inline bool needs_quote(const std::string& s, const Fmt& f) {
    return s.find_first_of(std::string({f.delimiter, f.quote, '\n', '\r'})) != std::string::npos;
}

/// `s` as it appears in the file: wrapped, with every embedded quote doubled.
std::string quoted(const std::string& s, const Fmt& f) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back(f.quote);
    for (char c : s) {
        if (c == f.quote) out.push_back(f.quote);
        out.push_back(c);
    }
    out.push_back(f.quote);
    return out;
}

/// The finished text of one value: quoted when the mode or the content asks.
std::string render(const std::string& s, const Fmt& f, CsvQuoting mode) {
    if (mode == CsvQuoting::All) return quoted(s, f);
    if (mode == CsvQuoting::Needed && needs_quote(s, f)) return quoted(s, f);
    return s;
}

// --- one column, ready to write ----------------------------------------------

struct Plan;
using CellFn = void (*)(Out&, const Plan&, std::size_t, const Fmt&);

struct Plan {
    const void* data = nullptr;          ///< the column's own typed buffer
    const data::BitMask* mask = nullptr; ///< validity, or null when all valid
    const data::BitMask* bits = nullptr; ///< Bool only
    const std::int32_t* codes = nullptr; ///< String only
    /*!
     * String only: each distinct value, quoted and escaped once.
     *
     * The whole reason a text column is cheap to write. A column of a million
     * rows over four labels escapes four strings, and every row after that is
     * a memcpy of one of them.
     */
    std::vector<std::string> rendered;
    CellFn fn = nullptr;
};

/// Wrap what a formatter just wrote, when every value is being quoted. Numbers
/// never contain a delimiter or a quote, so this is the only case where they
/// are wrapped at all.
inline char* open_quote(char* q, const Fmt& f) {
    if (f.quote_all) *q++ = f.quote;
    return q;
}
inline char* close_quote(char* q, const Fmt& f) {
    if (f.quote_all) *q++ = f.quote;
    return q;
}

template<typename T>
void cell_integer(Out& o, const Plan& p, std::size_t r, const Fmt& f) {
    if (p.mask && !p.mask->test(r)) { o.put(*f.null_string); return; }
    char* const s = o.room(32);
    char* q = open_quote(s, f);
    // Promoted, because to_chars of a char type is a corner of the standard
    // that has been read both ways -- and a routing channel written as its
    // ASCII character rather than its number would be a silent disaster.
    q = std::to_chars(q, q + 24, +static_cast<const T*>(p.data)[r]).ptr;
    q = close_quote(q, f);
    o.advance(static_cast<std::size_t>(q - s));
}

void cell_f64(Out& o, const Plan& p, std::size_t r, const Fmt& f) {
    if (p.mask && !p.mask->test(r)) { o.put(*f.null_string); return; }
    char* const s = o.room(48);
    char* q = open_quote(s, f);
    q += format_double(q, 40, static_cast<const double*>(p.data)[r], f.precision,
                       f.decimals, f.keep_dot);
    q = close_quote(q, f);
    o.advance(static_cast<std::size_t>(q - s));
}

void cell_f32(Out& o, const Plan& p, std::size_t r, const Fmt& f) {
    if (p.mask && !p.mask->test(r)) { o.put(*f.null_string); return; }
    char* const s = o.room(48);
    char* q = open_quote(s, f);
    q += format_float(q, 40, static_cast<const float*>(p.data)[r], f.precision,
                      f.decimals, f.keep_dot);
    q = close_quote(q, f);
    o.advance(static_cast<std::size_t>(q - s));
}

void cell_bool(Out& o, const Plan& p, std::size_t r, const Fmt& f) {
    if (p.mask && !p.mask->test(r)) { o.put(*f.null_string); return; }
    const std::string& v = p.bits->test(r) ? *f.true_string : *f.false_string;
    if (f.quote_all) o.put(f.quote);
    o.put(v);
    if (f.quote_all) o.put(f.quote);
}

void cell_text(Out& o, const Plan& p, std::size_t r, const Fmt& f) {
    if (p.mask && !p.mask->test(r)) { o.put(*f.null_string); return; }
    o.put(p.rendered[static_cast<std::size_t>(p.codes[r])]);
}

CellFn cell_writer(ColumnType t) {
    switch (t) {
        case ColumnType::Float64: return cell_f64;
        case ColumnType::Float32: return cell_f32;
        case ColumnType::Int64:   return cell_integer<std::int64_t>;
        case ColumnType::Int32:   return cell_integer<std::int32_t>;
        case ColumnType::Int16:   return cell_integer<short>;
        case ColumnType::Int8:    return cell_integer<signed char>;
        case ColumnType::UInt64:  return cell_integer<unsigned long long>;
        case ColumnType::UInt32:  return cell_integer<unsigned int>;
        case ColumnType::UInt16:  return cell_integer<unsigned short>;
        case ColumnType::UInt8:   return cell_integer<unsigned char>;
        case ColumnType::Bool:    return cell_bool;
        case ColumnType::String:  return cell_text;
    }
    return nullptr;
}

// --- the write itself --------------------------------------------------------

/// Everything the row loop needs, prepared once, so that nothing about the
/// options or the column types is looked at again per cell.
struct Job {
    std::vector<Plan> plans;
    std::vector<const Column*> columns;
    Fmt fmt;
    std::string header;
    std::string eol;
    std::size_t n_rows = 0;
    const data::BitMask* row_mask = nullptr;
    std::size_t block_rows = 16384;
    unsigned threads = 1;
};

/// Builds \ref Job, or explains why it cannot. Everything that can fail fails
/// here: once the row loop starts there is nothing left to check per row.
Job prepare(const DataStore& store, const CsvWriteOptions& options) {
    Job j;
    j.fmt.delimiter = options.delimiter;
    j.fmt.quote = options.quote;
    j.fmt.quote_all = options.quoting == CsvQuoting::All;
    j.fmt.precision = options.float_precision;
    j.fmt.decimals = options.float_decimals;
    j.fmt.keep_dot = options.keep_decimal_point;
    j.fmt.null_string = &options.null_string;
    j.fmt.true_string = &options.true_string;
    j.fmt.false_string = &options.false_string;
    j.eol = options.eol;

    if (options.delimiter == options.quote)
        throw std::runtime_error("write_csv: the delimiter and the quote are the same character");
    if (options.eol.empty())
        throw std::runtime_error("write_csv: the line terminator is empty");

    const std::vector<std::string> bad = store.inconsistent_columns();
    if (!bad.empty()) {
        std::string names;
        for (const std::string& s : bad) names += (names.empty() ? "" : ", ") + s;
        throw std::runtime_error("write_csv: these columns do not have the table's row count: " + names);
    }

    if (options.columns.empty()) {
        for (int i = 0; i < store.n_columns(); i++) j.columns.push_back(&store.column(i));
    } else {
        for (const std::string& name : options.columns) {
            const int i = store.find(name);
            if (i < 0) throw std::runtime_error("write_csv: no column named '" + name + "'");
            j.columns.push_back(&store.column(i));
        }
    }

    // Under CsvQuoting::Never a value that needs quoting cannot be written at
    // all -- the file would read back as a different table. Every such value is
    // knowable before the first row: the numbers cannot contain a delimiter,
    // and the text is a dictionary.
    const CsvQuoting mode = options.quoting;
    auto check_none = [&](const std::string& s, const char* what) {
        if (mode == CsvQuoting::Never && needs_quote(s, j.fmt))
            throw std::runtime_error(std::string("write_csv: ") + what + " needs quoting ('" + s +
                                     "'), and quoting is off");
    };
    check_none(options.null_string, "the null string");
    check_none(options.true_string, "the true string");
    check_none(options.false_string, "the false string");

    j.plans.resize(j.columns.size());
    for (std::size_t k = 0; k < j.columns.size(); k++) {
        const Column& c = *j.columns[k];
        Plan& p = j.plans[k];
        p.fn = cell_writer(c.type());
        p.mask = c.has_mask() ? &c.mask() : nullptr;
        if (c.type() == ColumnType::Bool) {
            p.bits = &c.bits();
        } else if (c.type() == ColumnType::String) {
            p.codes = c.codes_ptr();
            p.rendered.reserve(c.dictionary().size());
            for (const std::string& s : c.dictionary()) {
                check_none(s, ("a value of column '" + c.name() + "'").c_str());
                p.rendered.push_back(render(s, j.fmt, mode));
            }
        } else {
            p.data = c.data_ptr();
        }
    }

    if (options.has_header) {
        Out h;
        for (std::size_t k = 0; k < j.columns.size(); k++) {
            if (k) h.put(j.fmt.delimiter);
            const std::string& name = j.columns[k]->name();
            check_none(name, "a column name");
            h.put(render(name, j.fmt, mode));
        }
        h.put(j.eol);
        j.header.assign(h.data(), h.size());
    }

    j.n_rows = store.n_rows();
    if (options.selected_only && store.has_row_mask()) j.row_mask = &store.row_mask();
    j.block_rows = std::max<std::size_t>(1, options.block_rows);

    const std::size_t n_blocks = (j.n_rows + j.block_rows - 1) / j.block_rows;
    unsigned want = 1;
    if (options.threads > 0) want = static_cast<unsigned>(options.threads);
    else if (options.threads == 0) want = std::max(1u, std::thread::hardware_concurrency());
    j.threads = static_cast<unsigned>(std::max<std::size_t>(1, std::min<std::size_t>(want, n_blocks)));
    return j;
}

/// Format rows [r0, r1) into `o`. The one loop that runs per row; everything it
/// touches was decided in \ref prepare.
void format_block(const Job& j, std::size_t r0, std::size_t r1, Out& o) {
    const std::size_t n_cols = j.plans.size();
    const Plan* const plans = j.plans.data();
    for (std::size_t r = r0; r < r1; r++) {
        if (j.row_mask && !j.row_mask->test(r)) continue;
        for (std::size_t k = 0; k < n_cols; k++) {
            if (k) o.put(j.fmt.delimiter);
            plans[k].fn(o, plans[k], r, j.fmt);
        }
        o.put(j.eol);
    }
}

/*!
 * Run the blocks and hand each finished buffer to `sink`, in order.
 *
 * A wave of `threads` blocks is formatted at once and then drained. Waves cost
 * a little at the boundary -- the wave is as long as its slowest block -- and
 * buy the property that matters more: the memory held is the wave, not the
 * file, so a table that does not fit in memory still writes.
 */
bool run(const Job& j, const std::function<bool(const char*, std::size_t)>& sink) {
    if (!j.header.empty() && !sink(j.header.data(), j.header.size())) return false;
    if (j.n_rows == 0 || j.plans.empty()) return true;

    const std::size_t n_blocks = (j.n_rows + j.block_rows - 1) / j.block_rows;
    std::vector<Out> buffers(j.threads);

    for (std::size_t base = 0; base < n_blocks; base += j.threads) {
        const std::size_t wave = std::min<std::size_t>(j.threads, n_blocks - base);
        auto work = [&](std::size_t w) {
            const std::size_t r0 = (base + w) * j.block_rows;
            const std::size_t r1 = std::min(r0 + j.block_rows, j.n_rows);
            buffers[w].reset();
            format_block(j, r0, r1, buffers[w]);
        };
        if (wave == 1) {
            work(0);
        } else {
            std::vector<std::thread> workers;
            workers.reserve(wave - 1);
            for (std::size_t w = 1; w < wave; w++) workers.emplace_back(work, w);
            work(0);
            for (std::thread& t : workers) t.join();
        }
        for (std::size_t w = 0; w < wave; w++)
            if (!sink(buffers[w].data(), buffers[w].size())) return false;
    }
    return true;
}

}  // namespace

bool write_csv(const std::string& filename, const DataStore& store,
               const CsvWriteOptions& options) {
    Job j;
    try {
        j = prepare(store, options);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return false;
    }

    // Beside the target, so the rename that follows stays on one filesystem and
    // is therefore atomic. A failed write leaves the old file untouched.
    const std::string temp = filename + ".tttrlib-tmp";
    std::FILE* f = std::fopen(temp.c_str(), "wb");
    if (f == nullptr) {
        std::cerr << "write_csv: could not open " << temp << " for writing" << std::endl;
        return false;
    }

    bool ok = run(j, [&](const char* p, std::size_t n) {
        return n == 0 || std::fwrite(p, 1, n, f) == n;
    });
    if (!ok) std::cerr << "write_csv: could not write " << temp << std::endl;
    if (std::fclose(f) != 0) ok = false;

    if (ok && std::rename(temp.c_str(), filename.c_str()) != 0) {
        std::cerr << "write_csv: could not rename " << temp << " over " << filename << std::endl;
        ok = false;
    }
    if (!ok) std::remove(temp.c_str());
    return ok;
}

std::string write_csv_string(const DataStore& store, const CsvWriteOptions& options) {
    const Job j = prepare(store, options);
    std::string out;
    run(j, [&](const char* p, std::size_t n) {
        out.append(p, n);
        return true;
    });
    return out;
}

}  // namespace io
}  // namespace tttrlib
