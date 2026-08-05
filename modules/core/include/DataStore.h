// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_DATASTORE_H
#define TTTRLIB_DATASTORE_H

/*!
 * \file DataStore.h
 * \brief A columnar in-memory table: typed columns, row masks, and histograms
 *        filled straight out of it.
 *
 * This exists because a histogram is only half of what a data explorer needs.
 * The other half is somewhere to keep the columns -- one per measured quantity,
 * a few million rows, mixed types, some of them text that will never be
 * histogrammed but has to be carried alongside and shown.
 *
 * \section ds_compression Does in-memory compression make sense?
 *
 * Not as block compression. A histogram fill is a random scatter over the whole
 * array at roughly a nanosecond per point; decompressing a block to touch one
 * value would be several orders of magnitude worse, and a column being filtered
 * is read in whatever order the mask says.
 *
 * What does make sense is compression that stays QUERYABLE, and this uses three
 * kinds:
 *
 * - **Native dtypes.** A column held as float32 or int32 is half the size of the
 *   float64 it would otherwise be promoted to, and stays directly readable. The
 *   histogram fill is templated on the column type, so nothing is converted on
 *   the way in either.
 * - **Dictionary-encoded strings.** A text column in this kind of data is a few
 *   dozen distinct values repeated millions of times. Stored as unique strings
 *   plus int32 codes it is typically 10-100x smaller AND becomes directly
 *   histogrammable, because the codes are already a category axis.
 * - **Bit-packed masks.** One bit per row instead of one byte: 8x, and the fill
 *   reads it a word at a time.
 *
 * \section ds_mask Masking
 *
 * Every column may carry a validity mask, and the store a row mask. A fill
 * skips a row when either says to. That is how "these bursts are selected" and
 * "this value was not measured" are expressed without inventing a sentinel
 * value that later gets histogrammed by accident.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "HistogramNd.h"

namespace tttrlib {
namespace data {

/// What a column holds. String is stored but never binned -- see \ref Column.
enum class ColumnType {
    Float64,
    Float32,
    Int64,
    Int32,
    Bool,
    String
};

/// Bytes per element of a stored column, for reporting memory use.
inline int column_type_size(ColumnType t) {
    switch (t) {
        case ColumnType::Float64: return 8;
        case ColumnType::Float32: return 4;
        case ColumnType::Int64:   return 8;
        case ColumnType::Int32:   return 4;
        case ColumnType::Bool:    return 1;   // bit-packed; see Column::nbytes
        case ColumnType::String:  return 4;   // the code; the dictionary is extra
    }
    return 8;
}

/*!
 * \brief A bit per row.
 *
 * One eighth the size of a byte array, which matters at ten million rows, and
 * read a word at a time when a fill is scanning it.
 */
class BitMask {
public:
    BitMask() = default;
    explicit BitMask(std::size_t n, bool value = true) { assign(n, value); }

    void assign(std::size_t n, bool value) {
        n_ = n;
        words_.assign((n + 63) / 64, value ? ~0ULL : 0ULL);
        if (value && n % 64) words_.back() = (1ULL << (n % 64)) - 1ULL;
    }
    void clear() { n_ = 0; words_.clear(); }
    bool empty() const { return n_ == 0; }
    std::size_t size() const { return n_; }

    inline bool test(std::size_t i) const {
        return (words_[i >> 6] >> (i & 63)) & 1ULL;
    }
    inline void set(std::size_t i, bool v) {
        const std::uint64_t bit = 1ULL << (i & 63);
        if (v) words_[i >> 6] |= bit; else words_[i >> 6] &= ~bit;
    }

    /// Number of set bits.
    std::size_t count() const {
        std::size_t c = 0;
        for (std::uint64_t w : words_) {
            // popcount without <bit>, which is C++20.
            while (w) { w &= w - 1; c++; }
        }
        return c;
    }
    std::size_t nbytes() const { return words_.size() * sizeof(std::uint64_t); }

    /// Set from a byte-per-row array, which is what numpy hands over.
    void from_bytes(const unsigned char* b, std::size_t n) {
        assign(n, false);
        for (std::size_t i = 0; i < n; i++) if (b[i]) set(i, true);
    }
    /// Expand into a caller-provided byte-per-row array. Takes a length
    /// because a language binding cannot pass a bare pointer safely.
    void to_bytes(unsigned char* out_bytes, int n_out) const {
        const std::size_t n = std::min<std::size_t>(n_, static_cast<std::size_t>(n_out));
        for (std::size_t i = 0; i < n; i++) out_bytes[i] = test(i) ? 1 : 0;
    }

private:
    std::size_t n_ = 0;
    std::vector<std::uint64_t> words_;
};

/*!
 * \brief One column.
 *
 * Exactly one of the typed vectors is populated, chosen by \ref type. Nothing
 * is promoted on the way in: a float32 column stays float32, because half the
 * memory is the point and the histogram fill is templated on the type anyway.
 *
 * \section col_string Strings are stored, not binned
 *
 * A text column is dictionary-encoded: the distinct values once, plus an int32
 * code per row. That is the compression, and it is also what makes such a
 * column usable -- the codes ARE a category axis, so "how many rows per label"
 * needs no separate pass. The strings themselves never reach a histogram.
 */
class Column {
public:
    Column() = default;
    Column(std::string name, ColumnType type) : name_(std::move(name)), type_(type) {}

    const std::string& name() const { return name_; }
    void set_name(std::string s) { name_ = std::move(s); }
    ColumnType type() const { return type_; }
    std::size_t size() const { return n_; }
    bool is_numeric() const { return type_ != ColumnType::String; }

    /*!
     * Release any capacity beyond what is stored.
     *
     * Bulk-loading a column through push_string leaves a vector with up to
     * twice the capacity it needs, which on a hundred-million-row column is
     * hundreds of megabytes of nothing. nbytes() reports capacity rather than
     * size precisely so that this is visible.
     */
    void shrink_to_fit() {
        f64_.shrink_to_fit(); f32_.shrink_to_fit();
        i64_.shrink_to_fit(); i32_.shrink_to_fit();
        codes_.shrink_to_fit(); dictionary_.shrink_to_fit();
    }

    /// Bytes actually held -- CAPACITY, not size -- dictionary and mask included.
    std::size_t nbytes() const {
        std::size_t b = 0;
        switch (type_) {
            case ColumnType::Float64: b = f64_.capacity() * 8; break;
            case ColumnType::Float32: b = f32_.capacity() * 4; break;
            case ColumnType::Int64:   b = i64_.capacity() * 8; break;
            case ColumnType::Int32:   b = i32_.capacity() * 4; break;
            case ColumnType::Bool:    b = bits_.nbytes(); break;
            case ColumnType::String:
                b = codes_.capacity() * 4;
                for (const std::string& s : dictionary_) b += s.capacity() + sizeof(std::string);
                break;
        }
        return b + mask_.nbytes();
    }

    // --- setting ----------------------------------------------------------

    // Plain int / long long / unsigned char rather than the <cstdint> spellings:
    // SWIG matches typemaps on the type as written, and (int* IN_ARRAY1, int DIM1)
    // does not apply to a parameter declared std::int32_t. The types are the same
    // where it matters and the binding is the point of these overloads.
    void set_f64(const double* v, int n) { f64_.assign(v, v + n); n_ = n; type_ = ColumnType::Float64; }
    void set_f32(const float* v, int n) { f32_.assign(v, v + n); n_ = n; type_ = ColumnType::Float32; }
    void set_i64(const long long* v, int n) { i64_.assign(v, v + n); n_ = n; type_ = ColumnType::Int64; }
    void set_i32(const int* v, int n) { i32_.assign(v, v + n); n_ = n; type_ = ColumnType::Int32; }
    void set_bool(const unsigned char* v, int n) { bits_.from_bytes(v, n); n_ = n; type_ = ColumnType::Bool; }

    /*!
     * Append one string, dictionary-encoded.
     *
     * Repeated values cost four bytes, not the string. A column of ten million
     * rows drawn from twenty labels is 40 MB of codes plus a few hundred bytes,
     * rather than several hundred megabytes of std::string.
     */
    void push_string(const std::string& s) {
        type_ = ColumnType::String;
        auto it = lookup_.find(s);
        int code;
        if (it == lookup_.end()) {
            code = static_cast<int>(dictionary_.size());
            dictionary_.push_back(s);
            lookup_.emplace(s, code);
        } else {
            code = it->second;
        }
        codes_.push_back(code);
        n_ = codes_.size();
    }

    /*!
     * \brief Install a dictionary and codes wholesale.
     *
     * For a bulk loader that already knows the distinct values. push_string does
     * a hash lookup per row, which is right when values arrive one at a time and
     * catastrophic when a million of them arrive at once: merging per-block
     * dictionaries this way turned a million lookups into one per distinct
     * value.
     */
    void set_dictionary(const std::vector<std::string>& dict) {
        type_ = ColumnType::String;
        dictionary_ = dict;
        lookup_.clear();
        for (std::size_t i = 0; i < dictionary_.size(); i++)
            lookup_.emplace(dictionary_[i], static_cast<int>(i));
    }
    /// \see set_dictionary. Codes must index into it.
    void set_codes(const int* v, int n) {
        type_ = ColumnType::String;
        codes_.assign(v, v + n);
        n_ = codes_.size();
    }
    /// Room for `n` elements of the column's own type, for an appending loader.
    void reserve(std::size_t n) {
        switch (type_) {
            case ColumnType::Float64: f64_.reserve(n); break;
            case ColumnType::Float32: f32_.reserve(n); break;
            case ColumnType::Int64:   i64_.reserve(n); break;
            case ColumnType::Int32:   i32_.reserve(n); break;
            case ColumnType::String:  codes_.reserve(n); break;
            default: break;
        }
    }
    /*!
     * \brief Size the column and hand out its buffer.
     *
     * For a loader that knows where each of its pieces belongs and wants to
     * write them straight into place. That is the difference between a parser
     * that produces the column and one that produces something which is then
     * copied into the column -- at a hundred million rows the copy is the
     * dominant cost and the peak is twice what it needs to be.
     *
     * Bool and String have no raw buffer here on purpose: bool is bit-packed,
     * so two writers can collide inside one word, and a string needs its
     * dictionary. Both take a staging array and one pass at the end.
     */
    void resize(std::size_t n) {
        n_ = n;
        switch (type_) {
            case ColumnType::Float64: f64_.assign(n, 0.0); break;
            case ColumnType::Float32: f32_.assign(n, 0.0f); break;
            case ColumnType::Int64:   i64_.assign(n, 0); break;
            case ColumnType::Int32:   i32_.assign(n, 0); break;
            case ColumnType::String:  codes_.assign(n, 0); break;
            case ColumnType::Bool:    bits_.assign(n, false); break;
        }
    }
    double* f64_data() { return f64_.data(); }
    float* f32_data() { return f32_.data(); }
    long long* i64_data() { return reinterpret_cast<long long*>(i64_.data()); }
    int* codes_data() { return reinterpret_cast<int*>(codes_.data()); }

    /// Append raw values of the column's own type, without an intermediate copy.
    void append_f64(const double* v, std::size_t n) { f64_.insert(f64_.end(), v, v + n); n_ = f64_.size(); }
    void append_f32(const float* v, std::size_t n) { f32_.insert(f32_.end(), v, v + n); n_ = f32_.size(); }
    void append_i64(const long long* v, std::size_t n) { i64_.insert(i64_.end(), v, v + n); n_ = i64_.size(); }
    void append_codes(const int* v, std::size_t n) { codes_.insert(codes_.end(), v, v + n); n_ = codes_.size(); }

    // --- reading ----------------------------------------------------------

    const std::vector<std::string>& dictionary() const { return dictionary_; }
    const std::vector<std::int32_t>& codes() const { return codes_; }

    const std::string& string_at(std::size_t i) const {
        static const std::string empty;
        if (type_ != ColumnType::String || i >= codes_.size()) return empty;
        return dictionary_[codes_[i]];
    }

    /*!
     * \brief The value of row `i` as a double, whatever the column holds.
     *
     * For a string column this is the dictionary CODE, not the text -- which is
     * the only numeric thing a string has, and is what a category axis wants.
     */
    inline double value_at(std::size_t i) const {
        switch (type_) {
            case ColumnType::Float64: return f64_[i];
            case ColumnType::Float32: return static_cast<double>(f32_[i]);
            case ColumnType::Int64:   return static_cast<double>(i64_[i]);
            case ColumnType::Int32:   return static_cast<double>(i32_[i]);
            case ColumnType::Bool:    return bits_.test(i) ? 1.0 : 0.0;
            case ColumnType::String:  return static_cast<double>(codes_[i]);
        }
        return 0.0;
    }

    /// Zero-copy views, valid while the column is alive and unmodified.
    void get_f64_view(double** view, int* n) {
        *view = f64_.empty() ? nullptr : f64_.data(); *n = static_cast<int>(f64_.size());
    }
    void get_f32_view(float** view, int* n) {
        *view = f32_.empty() ? nullptr : f32_.data(); *n = static_cast<int>(f32_.size());
    }
    void get_i64_view(long long** view, int* n) {
        *view = i64_.empty() ? nullptr : reinterpret_cast<long long*>(i64_.data());
        *n = static_cast<int>(i64_.size());
    }
    void get_i32_view(int** view, int* n) {
        *view = i32_.empty() ? nullptr : reinterpret_cast<int*>(i32_.data());
        *n = static_cast<int>(i32_.size());
    }
    void get_codes_view(int** view, int* n) {
        *view = codes_.empty() ? nullptr : reinterpret_cast<int*>(codes_.data());
        *n = static_cast<int>(codes_.size());
    }

    // --- validity ---------------------------------------------------------

    bool has_mask() const { return !mask_.empty(); }
    const BitMask& mask() const { return mask_; }
    void set_mask(const unsigned char* m, int n) { mask_.from_bytes(m, n); }
    void clear_mask() { mask_.clear(); }
    inline bool valid(std::size_t i) const { return mask_.empty() || mask_.test(i); }

    /*!
     * Mark every non-finite value invalid.
     *
     * A NaN in a measured column means "not measured", and the alternative to
     * saying so is that it silently lands in a flow bin and is counted as data
     * that was merely off-scale.
     */
    std::size_t mask_non_finite() {
        if (type_ != ColumnType::Float64 && type_ != ColumnType::Float32) return 0;
        if (mask_.empty()) mask_.assign(n_, true);
        std::size_t n_masked = 0;
        for (std::size_t i = 0; i < n_; i++) {
            if (!std::isfinite(value_at(i))) { mask_.set(i, false); n_masked++; }
        }
        return n_masked;
    }

private:
    std::string name_;
    ColumnType type_ = ColumnType::Float64;
    std::size_t n_ = 0;

    std::vector<double> f64_;
    std::vector<float> f32_;
    std::vector<std::int64_t> i64_;
    std::vector<std::int32_t> i32_;
    BitMask bits_;                       // Bool columns

    std::vector<std::string> dictionary_;
    std::map<std::string, int> lookup_;
    std::vector<std::int32_t> codes_;

    BitMask mask_;                       // validity
};

/*!
 * \brief A table of columns, all the same length.
 *
 * Plus a row mask, which is the selection: "the bursts currently shown". A fill
 * honours it and the column's own validity mask together, so neither a
 * deselected row nor an unmeasured value can reach a histogram.
 */
class DataStore {
public:
    DataStore() = default;

    std::size_t n_rows() const { return n_rows_; }
    int n_columns() const { return static_cast<int>(columns_.size()); }

    Column& column(int i) { return columns_.at(i); }
    const Column& column(int i) const { return columns_.at(i); }

    int find(const std::string& name) const {
        for (std::size_t i = 0; i < columns_.size(); i++)
            if (columns_[i].name() == name) return static_cast<int>(i);
        return -1;
    }
    Column& column_by_name(const std::string& name) {
        const int i = find(name);
        if (i < 0) throw std::invalid_argument("no column named " + name);
        return columns_[i];
    }

    std::vector<std::string> column_names() const {
        std::vector<std::string> out;
        out.reserve(columns_.size());
        for (const Column& c : columns_) out.push_back(c.name());
        return out;
    }

    /// Add an empty column and return its index.
    int add_column(const std::string& name, ColumnType type) {
        if (find(name) >= 0) throw std::invalid_argument("duplicate column " + name);
        columns_.emplace_back(name, type);
        return static_cast<int>(columns_.size()) - 1;
    }

    /*!
     * Declare the row count.
     *
     * Columns are checked against it rather than resized: a column of the wrong
     * length is a mistake upstream, and silently padding it turns that into a
     * plot nobody can explain.
     */
    void set_n_rows(std::size_t n) { n_rows_ = n; }

    /// Every column that disagrees with n_rows(), by name.
    std::vector<std::string> inconsistent_columns() const {
        std::vector<std::string> bad;
        for (const Column& c : columns_)
            if (c.size() != n_rows_) bad.push_back(c.name());
        return bad;
    }

    // --- selection --------------------------------------------------------

    bool has_row_mask() const { return !row_mask_.empty(); }
    const BitMask& row_mask() const { return row_mask_; }
    void set_row_mask(const unsigned char* m, int n) { row_mask_.from_bytes(m, n); }
    void clear_row_mask() { row_mask_.clear(); }
    std::size_t n_selected() const {
        return row_mask_.empty() ? n_rows_ : row_mask_.count();
    }
    inline bool row_selected(std::size_t i) const {
        return row_mask_.empty() || row_mask_.test(i);
    }

    /// Total bytes held, so a caller can size a cache.
    std::size_t nbytes() const {
        std::size_t b = row_mask_.nbytes();
        for (const Column& c : columns_) b += c.nbytes();
        return b;
    }

private:
    std::vector<Column> columns_;
    std::size_t n_rows_ = 0;
    BitMask row_mask_;
};

/*!
 * \brief Fill a histogram from store columns.
 *
 * The columns keep their own types -- a float32 column is read as float32, a
 * dictionary-encoded string column as its codes -- because the fill is
 * templated on the accessor. Nothing is converted to double first, so a
 * ten-million-row float32 column does not become an 80 MB temporary on the way
 * into a plot.
 *
 * A row is counted only if the store's selection allows it AND every column
 * involved says the value is valid. That is one rule covering both "this burst
 * is not in the current selection" and "this quantity was not measured here",
 * and it means neither can be mistaken for data that merely fell off the axis.
 *
 * \param h        the histogram, whose rank must equal the number of columns
 * \param store    the table the columns come from
 * \param columns  one column index per axis
 * \param weight   a column to weight by, or -1
 * \param n_threads 0 to decide, 1 to force serial, or an explicit count
 */
inline void fill_histogram(hist::HistogramNd& h, const DataStore& store,
                           const std::vector<int>& columns, int weight = -1,
                           int n_threads = 0) {
    if (static_cast<int>(columns.size()) != h.rank())
        throw std::invalid_argument("fill_histogram: one column per axis is required");

    std::vector<const Column*> cols;
    cols.reserve(columns.size());
    for (int i : columns) cols.push_back(&store.column(i));
    const Column* w = (weight >= 0) ? &store.column(weight) : nullptr;

    // Weights have to be doubles by the time they reach the accumulator, and
    // there is one per row rather than one per axis, so this is the single
    // place a conversion is worth doing.
    std::vector<double> weights;
    if (w != nullptr) {
        weights.resize(store.n_rows());
        for (std::size_t i = 0; i < weights.size(); i++) weights[i] = w->value_at(i);
    }

    const Column* const* cp = cols.data();
    const DataStore* sp = &store;
    const std::size_t n_cols = cols.size();

    h.fill_with(
            [cp](int d, long long i) { return cp[d]->value_at(static_cast<std::size_t>(i)); },
            [cp, sp, n_cols, w](long long i) {
                const std::size_t r = static_cast<std::size_t>(i);
                if (!sp->row_selected(r)) return false;
                for (std::size_t d = 0; d < n_cols; d++)
                    if (!cp[d]->valid(r)) return false;
                if (w != nullptr && !w->valid(r)) return false;
                return true;
            },
            static_cast<long long>(store.n_rows()),
            weights.empty() ? nullptr : weights.data(), n_threads);
}

/*!
 * \brief A category axis over a string column's dictionary.
 *
 * The bridge that lets a text column be histogrammed without the strings ever
 * reaching the histogram: the axis bins the integer codes, and the labels come
 * back from the dictionary for the tick marks.
 */
inline hist::Axis category_axis_for(const Column& c) {
    if (c.type() != ColumnType::String)
        throw std::invalid_argument("category_axis_for: not a string column");
    std::vector<int> codes(c.dictionary().size());
    for (std::size_t i = 0; i < codes.size(); i++) codes[i] = static_cast<int>(i);
    return hist::Axis::category(codes.data(), static_cast<int>(codes.size()),
                                hist::AxisOptions(), c.name());
}

}  // namespace data
}  // namespace tttrlib

#endif  // TTTRLIB_DATASTORE_H
