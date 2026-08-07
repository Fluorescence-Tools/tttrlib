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
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "HistogramNd.h"

namespace tttrlib {
namespace data {

/*!
 * \brief An allocator whose vectors do not zero what they are about to overwrite.
 *
 * `std::vector<T>::resize` value-initialises, so growing a buffer for a reader
 * that is about to write every element writes it twice: once with zeros and
 * once with the data. On a 28-million-event file that is 458 MB of pointless
 * stores, and it is exactly what the malloc this replaced did not do.
 *
 * Only reachable through \ref Column::resize_uninitialized, which says what it
 * does. \ref Column::resize still zeroes, because a general table should.
 */
template<typename T>
struct DefaultInitAllocator : std::allocator<T> {
    using std::allocator<T>::allocator;

    template<typename U>
    struct rebind { using other = DefaultInitAllocator<U>; };

    template<typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        ::new(static_cast<void*>(p)) U(std::forward<Args>(args)...);
    }
    template<typename U>
    void construct(U* p) {
        ::new(static_cast<void*>(p)) U;      // default-init: scalars left alone
    }
};

template<typename T>
using RawVector = std::vector<T, DefaultInitAllocator<T>>;

/// What a column holds. String is stored but never binned -- see \ref Column.
enum class ColumnType {
    Float64,
    Float32,
    Int64,
    Int32,
    // The narrow integer types exist because the photon stream is made of them:
    // a routing channel is one byte and a micro time is two, and widening them
    // to int64 would quadruple the largest arrays in the library for nothing.
    Int16,
    Int8,
    UInt64,
    UInt32,
    UInt16,
    UInt8,
    Bool,
    String
};

/// Whether a column type can hold a NaN or an infinity at all.
inline bool is_floating(ColumnType t) {
    return t == ColumnType::Float64 || t == ColumnType::Float32;
}

/// The dtype's name, as the bindings spell it. For error messages that have to
/// say which two types would not combine.
inline const char* column_type_name(ColumnType t) {
    switch (t) {
        case ColumnType::Float64: return "float64";
        case ColumnType::Float32: return "float32";
        case ColumnType::Int64:   return "int64";
        case ColumnType::Int32:   return "int32";
        case ColumnType::Int16:   return "int16";
        case ColumnType::Int8:    return "int8";
        case ColumnType::UInt64:  return "uint64";
        case ColumnType::UInt32:  return "uint32";
        case ColumnType::UInt16:  return "uint16";
        case ColumnType::UInt8:   return "uint8";
        case ColumnType::Bool:    return "bool";
        case ColumnType::String:  return "str";
    }
    return "float64";
}

/// Bytes per element of a stored column, for reporting memory use.
inline int column_type_size(ColumnType t) {
    switch (t) {
        case ColumnType::Float64: return 8;
        case ColumnType::Float32: return 4;
        case ColumnType::Int64:   return 8;
        case ColumnType::Int32:   return 4;
        case ColumnType::Int16:   return 2;
        case ColumnType::Int8:    return 1;
        case ColumnType::UInt64:  return 8;
        case ColumnType::UInt32:  return 4;
        case ColumnType::UInt16:  return 2;
        case ColumnType::UInt8:   return 1;
        case ColumnType::Bool:    return 1;   // bit-packed; see Column::nbytes
        case ColumnType::String:  return 4;   // the code; the dictionary is extra
    }
    return 8;
}

/*!
 * \brief The msgpack encoding of a column description held as JSON text.
 *
 * Storage, not API: \ref Column::metadata keeps a text face because a string
 * crosses four bindings with no typemap and is what a human reads in a
 * debugger. This is what a *file* holds, for four reasons -- types survive
 * (JSON has one number type, so an integer row index comes back as a double),
 * binary values need no base64, the description is parsed on every open whether
 * or not anyone looks at it, and it is about a quarter smaller.
 *
 * Empty in, empty out: a column with no description costs one length prefix.
 *
 * \throws std::invalid_argument if the text does not parse, matching
 *         \ref Column::set_metadata rather than silently storing nothing.
 */
std::vector<unsigned char> metadata_to_msgpack(const std::string& json_text);

/*!
 * \brief JSON text from what \ref metadata_to_msgpack wrote.
 *
 * Returns ``""`` for a byte range that is not msgpack. A corrupt description is
 * not a reason to refuse the column -- the values are still exactly what was
 * measured, and losing the units is the smaller loss.
 */
std::string metadata_from_msgpack(const unsigned char* bytes, std::size_t n);

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

    /// Raw words, for building a mask 64 rows at a time rather than bit by bit.
    std::uint64_t* words() { return words_.data(); }
    const std::uint64_t* words() const { return words_.data(); }
    std::size_t n_words() const { return words_.size(); }

    /// The inverse of \ref words: take a mask back from packed words, for a
    /// loader that read them out of a file rather than computing them. Keeps
    /// the packing an implementation detail on both sides.
    void assign_words(const std::uint64_t* w, std::size_t n_bits) {
        n_ = n_bits;
        words_.assign(w, w + (n_bits + 63) / 64);
    }

    // --- set algebra ------------------------------------------------------
    //
    // A selection is built by combining conditions, and doing it on bits works
    // 64 rows at a time. The alternative -- a bool array per condition, ORed
    // with numpy -- moves 64x the memory and is what this exists to replace.

    void and_with(const BitMask& o) {
        const std::size_t n = std::min(words_.size(), o.words_.size());
        for (std::size_t i = 0; i < n; i++) words_[i] &= o.words_[i];
        for (std::size_t i = n; i < words_.size(); i++) words_[i] = 0;
    }
    void or_with(const BitMask& o) {
        const std::size_t n = std::min(words_.size(), o.words_.size());
        for (std::size_t i = 0; i < n; i++) words_[i] |= o.words_[i];
        trim();
    }
    /// Clear every bit that is set in `o`.
    void andnot_with(const BitMask& o) {
        const std::size_t n = std::min(words_.size(), o.words_.size());
        for (std::size_t i = 0; i < n; i++) words_[i] &= ~o.words_[i];
    }
    void invert() {
        for (std::uint64_t& w : words_) w = ~w;
        trim();
    }
    bool any() const {
        for (std::uint64_t w : words_) if (w != 0) return true;
        return false;
    }

    /*!
     * Set from a byte-per-row array, which is what numpy hands over.
     *
     * A word at a time. Setting one bit at a time is a read-modify-write of a
     * whole word per row, and this is on the interactive path -- every redraw
     * hands over a fresh mask of every row in the table.
     */
    void from_bytes(const unsigned char* b, std::size_t n) {
        assign(n, false);
        const std::size_t full = n / 64;
        for (std::size_t k = 0; k < full; k++) {
            const unsigned char* q = b + k * 64;
            std::uint64_t bits = 0;
            for (int i = 0; i < 64; i++)
                bits |= static_cast<std::uint64_t>(q[i] != 0 ? 1 : 0) << i;
            words_[k] = bits;
        }
        std::uint64_t bits = 0;
        for (std::size_t i = full * 64; i < n; i++)
            bits |= static_cast<std::uint64_t>(b[i] != 0 ? 1 : 0) << (i - full * 64);
        if (full < words_.size()) words_[full] = bits;
    }
    /// Expand into a caller-provided byte-per-row array. Takes a length
    /// because a language binding cannot pass a bare pointer safely.
    void to_bytes(unsigned char* out_bytes, int n_out) const {
        const std::size_t n = std::min<std::size_t>(n_, static_cast<std::size_t>(n_out));
        for (std::size_t i = 0; i < n; i++) out_bytes[i] = test(i) ? 1 : 0;
    }

private:
    /// Clear the bits past n_ in the last word, so count() and any() cannot see
    /// bits that are not rows.
    void trim() {
        if (n_ % 64 && !words_.empty()) words_.back() &= (1ULL << (n_ % 64)) - 1ULL;
    }

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

    /*!
     * \brief The column's name, and the key it is looked up by.
     *
     * An attribute of \ref metadata rather than a field beside it, cached here
     * because lookup is the hot path and parsing JSON per lookup would not be.
     * Returned by reference: no allocation, no parse.
     */
    const std::string& name() const { return name_; }

    /// Set the name, and the ``name`` attribute of \ref metadata with it.
    void set_name(std::string s);

    /*!
     * \brief Everything known about the column, as a JSON object.
     *
     * A column carries one extensible description instead of a growing list of
     * members: units, the dictionary item it corresponds to, a longer label.
     * Adding one is another key rather than a change to this class, to the
     * `.dstore` directory record, to four bindings and to the format version.
     *
     * The store neither validates nor interprets what is in here. ``units`` is
     * conventional and is what this exists for -- a burst duration in
     * milliseconds and a lifetime in nanoseconds otherwise say so only in their
     * column names, when whoever wrote them remembered.
     *
     * Empty when the column has none; nothing fabricates an object.
     */
    const std::string& metadata() const { return metadata_; }

    /*!
     * \brief Replace the description.
     *
     * \param json a JSON **object**, or the empty string for none.
     * \throws std::invalid_argument if it does not parse, or is not an object.
     *         Rejecting here rather than storing it means the failure is at the
     *         call that got it wrong, not at some later read.
     */
    void set_metadata(const std::string& json);

    /// One attribute, or ``""`` when absent, so a caller never has to parse.
    std::string attribute(const std::string& key) const;

    /*!
     * \brief One attribute as JSON **text**, or ``""`` when absent.
     *
     * The difference from \ref attribute is quoting, and it is what makes the
     * round trip exact: a stored string comes back as ``"ns"`` with its quotes,
     * a stored array as ``[[2,4]]``. \ref attribute unquotes a string so a
     * caller reading ``units`` need not parse, which leaves it unable to say
     * whether ``[[2,4]]`` was an array or a string that looked like one.
     */
    std::string attribute_json(const std::string& key) const;

    /*!
     * \brief Set one attribute to a string value, creating the description if
     *        there was none.
     *
     * The value is stored as a JSON **string**, whatever it looks like:
     * ``set_attribute("na", "[[2,4]]")`` stores the seven characters, not an
     * array of ranges. Use \ref set_attribute_json for anything structured.
     */
    void set_attribute(const std::string& key, const std::string& value);

    /*!
     * \brief Set one attribute to a JSON value given as text.
     *
     * \param json_value a JSON value -- ``42``, ``[[2,4]]``, ``{"of":"run.ptu"}``,
     *        ``"ns"`` **with** its quotes for a string. The empty string erases
     *        the key, matching \ref set_attribute.
     * \throws std::invalid_argument if it does not parse.
     *
     * Text rather than a variant because a string crosses four bindings with no
     * typemap; the type is preserved from here on, and \ref metadata is stored
     * as msgpack, so an integer written as an integer stays one.
     */
    void set_attribute_json(const std::string& key, const std::string& json_value);

    /// The ``units`` attribute -- what this was built for. ``""`` when unsaid.
    const std::string& units() const { return units_; }

    /// Set the ``units`` attribute.
    void set_units(std::string s);

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
        i16_.shrink_to_fit(); i8_.shrink_to_fit();
        u64_.shrink_to_fit(); u32_.shrink_to_fit();
        u16_.shrink_to_fit(); u8_.shrink_to_fit();
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
            case ColumnType::Int16:   b = i16_.capacity() * 2; break;
            case ColumnType::Int8:    b = i8_.capacity(); break;
            case ColumnType::UInt64:  b = u64_.capacity() * 8; break;
            case ColumnType::UInt32:  b = u32_.capacity() * 4; break;
            case ColumnType::UInt16:  b = u16_.capacity() * 2; break;
            case ColumnType::UInt8:   b = u8_.capacity(); break;
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
    void set_i16(const short* v, int n) { i16_.assign(v, v + n); n_ = n; type_ = ColumnType::Int16; }
    void set_i8(const signed char* v, int n) { i8_.assign(v, v + n); n_ = n; type_ = ColumnType::Int8; }
    void set_u64(const unsigned long long* v, int n) { u64_.assign(v, v + n); n_ = n; type_ = ColumnType::UInt64; }
    void set_u32(const unsigned int* v, int n) { u32_.assign(v, v + n); n_ = n; type_ = ColumnType::UInt32; }
    void set_u16(const unsigned short* v, int n) { u16_.assign(v, v + n); n_ = n; type_ = ColumnType::UInt16; }
    void set_u8(const unsigned char* v, int n) { u8_.assign(v, v + n); n_ = n; type_ = ColumnType::UInt8; }
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
            case ColumnType::Int16:   i16_.reserve(n); break;
            case ColumnType::Int8:    i8_.reserve(n); break;
            case ColumnType::UInt64:  u64_.reserve(n); break;
            case ColumnType::UInt32:  u32_.reserve(n); break;
            case ColumnType::UInt16:  u16_.reserve(n); break;
            case ColumnType::UInt8:   u8_.reserve(n); break;
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
            case ColumnType::Int16:   i16_.assign(n, 0); break;
            case ColumnType::Int8:    i8_.assign(n, 0); break;
            case ColumnType::UInt64:  u64_.assign(n, 0); break;
            case ColumnType::UInt32:  u32_.assign(n, 0); break;
            case ColumnType::UInt16:  u16_.assign(n, 0); break;
            case ColumnType::UInt8:   u8_.assign(n, 0); break;
            case ColumnType::String:  codes_.assign(n, 0); break;
            case ColumnType::Bool:    bits_.assign(n, false); break;
        }
    }
    /*!
     * \brief Size the column WITHOUT initialising it.
     *
     * For a reader that is about to write every element. resize() zeroes first,
     * which on a large file is a full extra pass over the memory; this is what
     * the malloc it replaced did. Anything not written is whatever was in the
     * page, so a caller that does not fill the column must use resize().
     */
    void resize_uninitialized(std::size_t n) {
        n_ = n;
        switch (type_) {
            case ColumnType::Float64: f64_.resize(n); break;
            case ColumnType::Float32: f32_.resize(n); break;
            case ColumnType::Int64:   i64_.resize(n); break;
            case ColumnType::Int32:   i32_.resize(n); break;
            case ColumnType::Int16:   i16_.resize(n); break;
            case ColumnType::Int8:    i8_.resize(n); break;
            case ColumnType::UInt64:  u64_.resize(n); break;
            case ColumnType::UInt32:  u32_.resize(n); break;
            case ColumnType::UInt16:  u16_.resize(n); break;
            case ColumnType::UInt8:   u8_.resize(n); break;
            case ColumnType::String:  codes_.resize(n); break;
            case ColumnType::Bool:    bits_.assign(n, false); break;
        }
    }

    /*!
     * Grow to `n`, KEEPING what is already there.
     *
     * resize() clears, which is what a loader filling a column wants; this is
     * what a container growing one wants. Two names because getting the wrong
     * one silently loses data rather than failing.
     */
    void grow(std::size_t n) {
        if (n < n_) return;
        n_ = n;
        switch (type_) {
            case ColumnType::Float64: f64_.resize(n, 0.0); break;
            case ColumnType::Float32: f32_.resize(n, 0.0f); break;
            case ColumnType::Int64:   i64_.resize(n, 0); break;
            case ColumnType::Int32:   i32_.resize(n, 0); break;
            case ColumnType::Int16:   i16_.resize(n, 0); break;
            case ColumnType::Int8:    i8_.resize(n, 0); break;
            case ColumnType::UInt64:  u64_.resize(n, 0); break;
            case ColumnType::UInt32:  u32_.resize(n, 0); break;
            case ColumnType::UInt16:  u16_.resize(n, 0); break;
            case ColumnType::UInt8:   u8_.resize(n, 0); break;
            case ColumnType::String:  codes_.resize(n, 0); break;
            case ColumnType::Bool:    { BitMask b(n, false);
                                        for (std::size_t i = 0; i < std::min(n, bits_.size()); i++)
                                            b.set(i, bits_.test(i));
                                        bits_ = b; break; }
        }
    }

    /*!
     * Keep the first `n` elements and drop the rest, without reallocating.
     *
     * For a reader that allocated for the record count in the file and then
     * found fewer valid events -- which is every TTTR reader, because invalid
     * and overflow records are not events.
     */
    void truncate(std::size_t n) {
        if (n > n_) return;
        n_ = n;
        // ONLY the active type. Resizing all of them looks harmless because the
        // others are empty -- and it is the opposite: resize() on an empty
        // vector GROWS it, so every column allocated and zeroed one array per
        // type it does not hold. It cost 60% of the test suite's runtime.
        switch (type_) {
            case ColumnType::Float64: f64_.resize(n); break;
            case ColumnType::Float32: f32_.resize(n); break;
            case ColumnType::Int64:   i64_.resize(n); break;
            case ColumnType::Int32:   i32_.resize(n); break;
            case ColumnType::Int16:   i16_.resize(n); break;
            case ColumnType::Int8:    i8_.resize(n); break;
            case ColumnType::UInt64:  u64_.resize(n); break;
            case ColumnType::UInt32:  u32_.resize(n); break;
            case ColumnType::UInt16:  u16_.resize(n); break;
            case ColumnType::UInt8:   u8_.resize(n); break;
            case ColumnType::String:  codes_.resize(n); break;
            case ColumnType::Bool:    break;   // bit-packed; n_ is the length
        }
    }
    double* f64_data() { return f64_.data(); }
    short* i16_data() { return i16_.data(); }
    signed char* i8_data() { return i8_.data(); }
    unsigned long long* u64_data() { return u64_.data(); }
    unsigned int* u32_data() { return u32_.data(); }
    unsigned short* u16_data() { return u16_.data(); }
    unsigned char* u8_data() { return u8_.data(); }
    float* f32_data() { return f32_.data(); }
    long long* i64_data() { return reinterpret_cast<long long*>(i64_.data()); }
    int* codes_data() { return reinterpret_cast<int*>(codes_.data()); }

    /// Append raw values of the column's own type, without an intermediate copy.
    void append_f64(const double* v, std::size_t n) { f64_.insert(f64_.end(), v, v + n); n_ = f64_.size(); }
    void append_f32(const float* v, std::size_t n) { f32_.insert(f32_.end(), v, v + n); n_ = f32_.size(); }
    void append_i64(const long long* v, std::size_t n) { i64_.insert(i64_.end(), v, v + n); n_ = i64_.size(); }
    void append_codes(const int* v, std::size_t n) { codes_.insert(codes_.end(), v, v + n); n_ = codes_.size(); }

    // --- combining and subsetting -----------------------------------------
    //
    // The three primitives DataStore::append_rows and DataStore::take are built
    // from. They live here because they need the typed vectors, and putting the
    // eleven-way switch in one place is the whole point: a twelfth column type
    // is one line in visit_pair rather than three loops to find.

    /*!
     * \brief Append `src`'s values to this column.
     *
     * \throws std::invalid_argument if the types differ. Promoting silently --
     *         a float32 column meeting a float64 one -- would lose the dtype
     *         this whole class exists to keep, and quietly.
     */
    void append_from(const Column& src) {
        if (src.type_ != type_)
            throw std::invalid_argument(
                    "cannot append a " + std::string(column_type_name(src.type_)) +
                    " column to a " + column_type_name(type_) + " one: '" + name_ + "'");
        const std::size_t before = n_, add = src.n_;
        if (type_ == ColumnType::String) {
            for (std::size_t i = 0; i < add; i++) push_string(src.string_at(i));
        } else if (type_ == ColumnType::Bool) {
            BitMask b = bits_;
            b.assign(before + add, false);
            for (std::size_t i = 0; i < before; i++) b.set(i, bits_.test(i));
            for (std::size_t i = 0; i < add; i++) b.set(before + i, src.bits_.test(i));
            bits_ = b;
            n_ = before + add;
        } else {
            visit_pair(*this, src, [&](auto& dst, const auto& s) {
                dst.insert(dst.end(), s.begin(), s.end());
            });
            n_ = before + add;
        }
        append_mask_from(src, before, add);
    }

    /*!
     * \brief Extend by `n` rows that were never measured.
     *
     * The values are zeroed and the validity mask says so, which is what lets a
     * column absent from one store keep its dtype through a concat instead of
     * being widened to hold a NaN.
     */
    void append_missing(std::size_t n) {
        const std::size_t before = n_;
        if (type_ == ColumnType::String) {
            for (std::size_t i = 0; i < n; i++) push_string(std::string());
        } else if (type_ == ColumnType::Bool) {
            BitMask b;
            b.assign(before + n, false);
            for (std::size_t i = 0; i < before; i++) b.set(i, bits_.test(i));
            bits_ = b;
            n_ = before + n;
        } else {
            visit_pair(*this, *this, [&](auto& dst, const auto&) {
                dst.resize(before + n);
            });
            n_ = before + n;
        }
        materialise_mask(before);
        for (std::size_t i = 0; i < n; i++) mask_.set(before + i, false);
    }

    /// Gather rows `rows[0..n)` of `src` into this column, which is emptied first.
    void take_from(const Column& src, const int* rows, std::size_t n) {
        metadata_ = src.metadata_;
        name_ = src.name_;
        units_ = src.units_;
        type_ = src.type_;
        if (type_ == ColumnType::String) {
            dictionary_.clear(); lookup_.clear(); codes_.clear(); n_ = 0;
            for (std::size_t k = 0; k < n; k++) push_string(src.string_at(rows[k]));
        } else if (type_ == ColumnType::Bool) {
            bits_.assign(n, false);
            for (std::size_t k = 0; k < n; k++) bits_.set(k, src.bits_.test(rows[k]));
            n_ = n;
        } else {
            visit_pair(*this, src, [&](auto& dst, const auto& s) {
                dst.clear();
                dst.resize(n);
                for (std::size_t k = 0; k < n; k++) dst[k] = s[rows[k]];
            });
            n_ = n;
        }
        if (src.mask_.empty()) {
            mask_.clear();
        } else {
            mask_.assign(n, true);
            for (std::size_t k = 0; k < n; k++) mask_.set(k, src.mask_.test(rows[k]));
        }
    }

    // --- reading ----------------------------------------------------------

    const std::vector<std::string>& dictionary() const { return dictionary_; }
    const RawVector<std::int32_t>& codes() const { return codes_; }

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
            case ColumnType::Int16:   return static_cast<double>(i16_[i]);
            case ColumnType::Int8:    return static_cast<double>(i8_[i]);
            // Above 2^53 a uint64 does not survive a double. Macro times reach
            // that on long acquisitions, so a caller that needs them exactly
            // must take the typed view rather than value_at.
            case ColumnType::UInt64:  return static_cast<double>(u64_[i]);
            case ColumnType::UInt32:  return static_cast<double>(u32_[i]);
            case ColumnType::UInt16:  return static_cast<double>(u16_[i]);
            case ColumnType::UInt8:   return static_cast<double>(u8_[i]);
            case ColumnType::Bool:    return bits_.test(i) ? 1.0 : 0.0;
            case ColumnType::String:  return static_cast<double>(codes_[i]);
        }
        return 0.0;
    }

    // Raw typed pointers, for a scan that wants the column in its own type
    // rather than one double at a time through a switch.
    const double* f64_ptr() const { return f64_.data(); }
    const float* f32_ptr() const { return f32_.data(); }
    const std::int64_t* i64_ptr() const { return i64_.data(); }
    const std::int32_t* i32_ptr() const { return i32_.data(); }
    const short* i16_ptr() const { return i16_.data(); }
    const signed char* i8_ptr() const { return i8_.data(); }
    const unsigned long long* u64_ptr() const { return u64_.data(); }
    const unsigned int* u32_ptr() const { return u32_.data(); }
    const unsigned short* u16_ptr() const { return u16_.data(); }
    const unsigned char* u8_ptr() const { return u8_.data(); }
    const std::int32_t* codes_ptr() const { return codes_.data(); }

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
    void get_i16_view(short** view, int* n) {
        *view = i16_.empty() ? nullptr : i16_.data(); *n = static_cast<int>(i16_.size());
    }
    void get_i8_view(signed char** view, int* n) {
        *view = i8_.empty() ? nullptr : i8_.data(); *n = static_cast<int>(i8_.size());
    }
    void get_u64_view(unsigned long long** view, int* n) {
        *view = u64_.empty() ? nullptr : u64_.data(); *n = static_cast<int>(u64_.size());
    }
    void get_u32_view(unsigned int** view, int* n) {
        *view = u32_.empty() ? nullptr : u32_.data(); *n = static_cast<int>(u32_.size());
    }
    void get_u16_view(unsigned short** view, int* n) {
        *view = u16_.empty() ? nullptr : u16_.data(); *n = static_cast<int>(u16_.size());
    }
    void get_u8_view(unsigned char** view, int* n) {
        *view = u8_.empty() ? nullptr : u8_.data(); *n = static_cast<int>(u8_.size());
    }
    void get_codes_view(int** view, int* n) {
        *view = codes_.empty() ? nullptr : reinterpret_cast<int*>(codes_.data());
        *n = static_cast<int>(codes_.size());
    }

    /*!
     * \brief The buffer itself, untyped.
     *
     * For a reader or writer that moves a whole column at once and already
     * knows its type -- an HDF5 dataset, a memory-mapped block. Untyped because
     * the alternative is a switch that names all eleven typed accessors at
     * every such call site, and the caller has just read \ref type to decide
     * which one it would name.
     *
     * Null for a Bool column, whose values are bit-packed and so do not exist
     * as elements to point at.
     */
    void* data_ptr() {
        switch (type_) {
            case ColumnType::Float64: return f64_.empty() ? nullptr : f64_.data();
            case ColumnType::Float32: return f32_.empty() ? nullptr : f32_.data();
            case ColumnType::Int64:   return i64_.empty() ? nullptr : i64_.data();
            case ColumnType::Int32:   return i32_.empty() ? nullptr : i32_.data();
            case ColumnType::Int16:   return i16_.empty() ? nullptr : i16_.data();
            case ColumnType::Int8:    return i8_.empty() ? nullptr : i8_.data();
            case ColumnType::UInt64:  return u64_.empty() ? nullptr : u64_.data();
            case ColumnType::UInt32:  return u32_.empty() ? nullptr : u32_.data();
            case ColumnType::UInt16:  return u16_.empty() ? nullptr : u16_.data();
            case ColumnType::UInt8:   return u8_.empty() ? nullptr : u8_.data();
            case ColumnType::String:  return codes_.empty() ? nullptr : codes_.data();
            case ColumnType::Bool:    return nullptr;
        }
        return nullptr;
    }
    /// \see data_ptr
    const void* data_ptr() const {
        return const_cast<Column*>(this)->data_ptr();
    }

    // --- validity ---------------------------------------------------------

    bool has_mask() const { return !mask_.empty(); }
    const BitMask& mask() const { return mask_; }

    /// The packed bits of a Bool column, for a writer that moves memory rather
    /// than values. Empty for every other type, which have \ref data_ptr.
    const BitMask& bits() const { return bits_; }

    /// \see bits. The inverse, for a loader reading packed words off disk --
    /// eight times less to read than a byte per row, and no expansion pass.
    void set_bits(const std::uint64_t* w, std::size_t n_bits) {
        type_ = ColumnType::Bool;
        bits_.assign_words(w, n_bits);
        n_ = n_bits;
    }
    /// \see mask. The inverse, same reason.
    void set_mask_bits(const std::uint64_t* w, std::size_t n_bits) {
        mask_.assign_words(w, n_bits);
    }
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
    /*!
     * \brief Call `f(a.vec, b.vec)` on the typed vector this column holds.
     *
     * The one place that knows the ten numeric cases. Bool and String are not
     * here on purpose: a bit-packed payload and a dictionary are not vectors of
     * values and every caller has to treat them separately anyway.
     */
    template <class F>
    static void visit_pair(Column& a, const Column& b, F&& f) {
        switch (a.type_) {
            case ColumnType::Float64: f(a.f64_, b.f64_); break;
            case ColumnType::Float32: f(a.f32_, b.f32_); break;
            case ColumnType::Int64:   f(a.i64_, b.i64_); break;
            case ColumnType::Int32:   f(a.i32_, b.i32_); break;
            case ColumnType::Int16:   f(a.i16_, b.i16_); break;
            case ColumnType::Int8:    f(a.i8_,  b.i8_);  break;
            case ColumnType::UInt64:  f(a.u64_, b.u64_); break;
            case ColumnType::UInt32:  f(a.u32_, b.u32_); break;
            case ColumnType::UInt16:  f(a.u16_, b.u16_); break;
            case ColumnType::UInt8:   f(a.u8_,  b.u8_);  break;
            default: break;
        }
    }

    /// Give the first `n` rows an explicit all-valid mask, so a later set()
    /// does not read as "everything before this was missing too".
    void materialise_mask(std::size_t n) {
        if (!mask_.empty()) { if (mask_.size() < n_) { BitMask m; m.assign(n_, true);
                for (std::size_t i = 0; i < mask_.size(); i++) m.set(i, mask_.test(i));
                mask_ = m; } return; }
        mask_.assign(n_, true);
        (void) n;
    }

    /// Carry `src`'s validity into rows [`at`, `at` + `n`) of this column.
    void append_mask_from(const Column& src, std::size_t at, std::size_t n) {
        if (mask_.empty() && src.mask_.empty()) return;   // all valid, stays implicit
        materialise_mask(at);
        for (std::size_t i = 0; i < n; i++)
            mask_.set(at + i, src.mask_.empty() ? true : src.mask_.test(i));
    }

    //: The authority. `name_` and `units_` are caches kept in step by the
    //: setters -- there are two ways in, and both have to update both, or a
    //: column reports one name and serialises another.
    std::string metadata_;
    std::string name_;
    std::string units_;
    ColumnType type_ = ColumnType::Float64;
    std::size_t n_ = 0;

    RawVector<double> f64_;
    RawVector<float> f32_;
    RawVector<std::int64_t> i64_;
    RawVector<std::int32_t> i32_;
    RawVector<short> i16_;
    RawVector<signed char> i8_;
    RawVector<unsigned long long> u64_;
    RawVector<unsigned int> u32_;
    RawVector<unsigned short> u16_;
    RawVector<unsigned char> u8_;
    BitMask bits_;                       // Bool columns

    std::vector<std::string> dictionary_;
    std::map<std::string, int> lookup_;
    RawVector<std::int32_t> codes_;

    BitMask mask_;                       // validity
};

/*!
 * \brief What one live store is, for the registry.
 */
struct DataStoreInfo {
    int id = 0;
    std::string label;
    std::size_t n_rows = 0;
    int n_columns = 0;
    /// The whole tree: this store's columns plus every group under it.
    std::size_t nbytes = 0;
    std::size_t n_selected = 0;
    /*!
     * Direct children, so a root that looks empty is not mistaken for one.
     *
     * Only a count. A per-group breakdown belongs to
     * `DataStore.memory_report()`, which is where a caller who wants it is
     * already looking -- putting one here would mean allocating a second vector
     * per store while the registry mutex is held.
     */
    int n_groups = 0;
};

class DataStore;

/*!
 * \brief Every store currently alive in the process.
 *
 * A session accumulates these without meaning to: a TTTR file is one, the
 * bursts extracted from it are another, an SMLM localisation table a third, and
 * each is potentially most of the memory in the process. Without somewhere to
 * look, "why is this using 12 GB" has no answer short of a profiler.
 *
 * Stores register themselves and deregister on destruction, so the list is
 * always what is actually there rather than what someone remembered to record.
 * It holds raw pointers deliberately -- a registry that kept the stores alive
 * would be the leak it exists to diagnose.
 */
class DataStoreRegistry {
public:
    /*!
     * The one registry in the process.
     *
     * Defined in DataStore.cpp, NOT inline here. A function-local static in an
     * inline function is merged across translation units only when the symbol
     * is exported, and the Python extension is built with hidden visibility --
     * so core and the extension each got their own registry, and a TTTR created
     * through one was invisible to a listing taken through the other. It looked
     * exactly like the registration not happening.
     */
    static DataStoreRegistry& instance();

    int add(DataStore* s) {
        std::lock_guard<std::mutex> lock(mutex_);
        const int id = ++next_id_;
        stores_.emplace_back(id, s);
        return id;
    }
    void remove(int id) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t i = 0; i < stores_.size(); i++) {
            if (stores_[i].first == id) { stores_.erase(stores_.begin() + i); return; }
        }
    }
    /// Snapshot of what is live. Defined after DataStore, which it inspects.
    std::vector<DataStoreInfo> list() const;
    std::size_t total_bytes() const;
    /// Number of live stores.
    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stores_.size();
    }

private:
    DataStoreRegistry() = default;
    mutable std::mutex mutex_;
    std::vector<std::pair<int, DataStore*>> stores_;
    int next_id_ = 0;
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
    /*!
     * Registration is per INSTANCE, including copies.
     *
     * A copy is a second table holding a second lot of memory, and the registry
     * exists to show exactly that. Move transfers the identity instead, because
     * a moved-from store holds nothing.
     */
    DataStore() { id_ = DataStoreRegistry::instance().add(this); }
    explicit DataStore(std::string label) : label_(std::move(label)) {
        id_ = DataStoreRegistry::instance().add(this);
    }
    ~DataStore() { if (id_ != 0) DataStoreRegistry::instance().remove(id_); }

    DataStore(const DataStore& o)
            : columns_(o.columns_), n_rows_(o.n_rows_), row_mask_(o.row_mask_),
              label_(o.label_) {
        clone_groups_from(o);               // deep: a group belongs to one parent
        id_ = DataStoreRegistry::instance().add(this);
    }
    DataStore& operator=(const DataStore& o) {
        if (this != &o) {
            // Copying an ancestor into one of its own groups would read the tree
            // it is in the middle of rewriting. One pointer compare for a store
            // with no groups, which is every store that never uses them.
            if (o.contains(this))
                throw std::invalid_argument(
                        "assigning a store into its own subtree would make a cycle");
            columns_ = o.columns_; n_rows_ = o.n_rows_;
            row_mask_ = o.row_mask_; label_ = o.label_;
            groups_.clear();
            clone_groups_from(o);
        }
        return *this;                       // keeps its own registry identity
    }
    DataStore(DataStore&& o) noexcept
            : columns_(std::move(o.columns_)), n_rows_(o.n_rows_),
              row_mask_(std::move(o.row_mask_)), label_(std::move(o.label_)),
              groups_(std::move(o.groups_)) {
        o.n_rows_ = 0;
        id_ = DataStoreRegistry::instance().add(this);
    }
    // No cycle guard here, deliberately: this is noexcept, so throwing would
    // terminate. See operator=(const DataStore&), which is the reachable route.
    DataStore& operator=(DataStore&& o) noexcept {
        if (this != &o) {
            columns_ = std::move(o.columns_); n_rows_ = o.n_rows_;
            row_mask_ = std::move(o.row_mask_); label_ = std::move(o.label_);
            groups_ = std::move(o.groups_);
            o.n_rows_ = 0;
        }
        return *this;
    }

    /// Registry identity. Stable for the life of this instance.
    int id() const { return id_; }
    /// What this store is, for someone reading the registry listing.
    const std::string& label() const { return label_; }
    void set_label(std::string s) { label_ = std::move(s); }

    /*!
     * \brief Drop every column and free the memory, keeping the object.
     *
     * For a caller who knows a table is finished with but cannot drop the
     * handle -- a cache eviction, a plot that has been closed. Views handed out
     * before this DO keep their own memory alive, because they hold a reference
     * to the column; what is freed is this store's claim on it.
     *
     * **Drops the groups too**, so nbytes() really does go to zero. A release
     * that left most of a tree alive would defeat the one thing it is for.
     * Handles into the tree die with it, exactly as for clear_groups().
     */
    void release() {
        columns_.clear();
        columns_.shrink_to_fit();
        row_mask_.clear();
        n_rows_ = 0;
        groups_.clear();
    }

    std::size_t n_rows() const { return n_rows_; }
    int n_columns() const { return static_cast<int>(columns_.size()); }

    Column& column(int i) { return columns_.at(i); }
    const Column& column(int i) const { return columns_.at(i); }

    /*!
     * \brief The index of a column of this store, or -1.
     *
     * Never looks in a group, and must not start. This is the hottest lookup in
     * the class -- column_by_name goes through it, and so does every
     * where/region/interval/histogram call from a binding -- and a group is not
     * a column, so a hit here could not be returned anyway. It is also what
     * keeps `store["meta"]` unambiguous when there is both a column and a group
     * called meta: the accessors differ, so nothing has to guess.
     */
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

    /// The columns of this store. A group is not a column and never appears
    /// here; \see group_names.
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
     * \brief Drop a column and free it.
     *
     * Indices of the columns after it shift down by one, which is the price of
     * keeping them contiguous; a caller holding indices must adjust them.
     */
    void remove_column(int i) {
        if (i < 0 || i >= n_columns()) return;
        columns_.erase(columns_.begin() + i);
    }

    /*!
     * Declare the row count.
     *
     * Columns are checked against it rather than resized: a column of the wrong
     * length is a mistake upstream, and silently padding it turns that into a
     * plot nobody can explain.
     */
    void set_n_rows(std::size_t n) { n_rows_ = n; }

    /*!
     * \brief Every column of THIS store that disagrees with n_rows(), by name.
     *
     * Does not look into the groups, and must not: a group with a different row
     * count from its parent is the normal shape, not a fault. A results table
     * has one row per pixel and the meta beside it has one row, and reporting
     * that would make the arrangement this feature exists for look broken.
     */
    std::vector<std::string> inconsistent_columns() const {
        std::vector<std::string> bad;
        for (const Column& c : columns_)
            if (c.size() != n_rows_) bad.push_back(c.name());
        return bad;
    }

    // --- combining and subsetting ------------------------------------------
    //
    // The two binary operations `concat` is a fold over, and the one that
    // materialises a selection. Binary rather than variadic because a fold is
    // the same thing and is far easier to wrap for four languages; the
    // variadic `concat(stores, axis=..., join=...)` lives in each binding.
    //
    // They act on THIS store's columns, not on the tree. Concatenating two
    // roots that each hold a `results` group is ambiguous -- are the results
    // tables being stacked, or the roots? -- and row counts differ per group
    // anyway. Concatenating groups is `a.group("r").append_rows(b.group("r"))`,
    // said out loud. Same rule as the histogram fill, which also takes one
    // store: there is no cross-group operation anywhere in this class, and this
    // adds none.

    /// How a column present in one store and not the other is treated.
    enum class Join {
        Outer,   ///< keep it; the rows that had no value are marked not-measured
        Inner,   ///< keep only the columns both stores have
    };

    /// What to do when both stores have a column of the same name.
    enum class OnDuplicate {
        Refuse,     ///< say which name clashed, and change nothing
        KeepFirst,  ///< keep this store's, drop the other's
    };

    /*!
     * \brief Append `other`'s rows to this store.
     *
     * Columns line up by NAME, not by position: two burst tables written by
     * different runs need not have listed their columns in the same order. A
     * column whose type differs between the two throws rather than being
     * promoted -- widening a float32 to meet a float64 loses the dtype the
     * store exists to keep, and does it silently.
     *
     * With \ref Join::Outer a column missing from one side keeps its dtype and
     * the rows that had no value are marked not-measured. That is the advantage
     * over a data frame here, and it is not a small one: pandas has to widen an
     * int64 column to float64 to hold a NaN, and the dtype cannot be recovered
     * afterwards.
     *
     * \throws std::invalid_argument on a type conflict, naming the column.
     */
    void append_rows(const DataStore& other, Join join = Join::Outer);

    /*!
     * \brief Put `other`'s columns beside this store's.
     *
     * For files describing the SAME rows -- several analyses of one burst
     * table. Row counts must match: a mismatch throws and names both counts
     * rather than skipping the file, because a merge that quietly drops a
     * measurement is worse than one that stops.
     *
     * \throws std::invalid_argument on a row-count mismatch, or on a duplicate
     *         column name unless \ref OnDuplicate::KeepFirst is asked for.
     */
    void append_columns(const DataStore& other,
                        OnDuplicate on_duplicate = OnDuplicate::Refuse);

    /*!
     * \brief Fill `out` with rows `take_rows[0..n)` of this store.
     *
     * A copy, not a view: a `Column` owns its buffer and cannot borrow one, and
     * making it able to is a far larger change than this is worth. Column
     * order, dtypes, metadata, dictionaries and validity all come across. The
     * row selection does not, because the result IS the selection.
     *
     * Groups are not descended into -- their row counts are their own, so one
     * index list cannot mean anything across them.
     */
    void take_into(DataStore& out, const int* take_rows, int n_take_rows) const;

    /// \see take_into, for the rows the selection currently keeps. What every
    /// `select_*` was missing: 29 ways to express a gate and not one that
    /// yielded a store you could hand on or write back out.
    void compact_into(DataStore& out) const;

    // --- selection --------------------------------------------------------

    bool has_row_mask() const { return !row_mask_.empty(); }
    const BitMask& row_mask() const { return row_mask_; }
    void set_row_mask(const unsigned char* m, int n) { row_mask_.from_bytes(m, n); }
    /// \see BitMask::assign_words -- for a loader taking the selection back off
    /// disk in the form it is held in.
    void set_row_mask_bits(const std::uint64_t* w, std::size_t n_bits) {
        row_mask_.assign_words(w, n_bits);
    }
    void clear_row_mask() { row_mask_.clear(); }
    std::size_t n_selected() const {
        return row_mask_.empty() ? n_rows_ : row_mask_.count();
    }
    inline bool row_selected(std::size_t i) const {
        return row_mask_.empty() || row_mask_.test(i);
    }

    // --- building a selection ---------------------------------------------

private:
    /*!
     * Evaluate `p` over a typed column, writing 64 results at a time.
     *
     * The obvious loop -- value_at(i) then mask.set(i, ...) -- costs a switch on
     * the column type and a read-modify-write of a word for EVERY row, and
     * measured four times slower than the numpy expression it is meant to
     * replace. Reading the column in its own type and accumulating a word in a
     * register before storing it once is what makes it cheaper instead: the
     * scan becomes bound by reading the column, which is the least any
     * implementation can do.
     */
    template<typename T, typename Pred>
    static void scan_typed(const T* v, std::size_t n, BitMask& m, Pred p) {
        std::uint64_t* w = m.words();
        const std::size_t nw = m.n_words();
        // Branchless, and that is the whole trick. `if (p(v[i])) bits |= ...`
        // is a branch on a comparison over unsorted measurement data, so it
        // mispredicts about half the time -- which on data this size costs more
        // than the comparison, the load and the store put together. Turning the
        // predicate into a 0 or 1 and shifting it leaves a loop with no branches
        // at all, which the compiler can also unroll.
        const std::size_t full = n / 64;
        for (std::size_t k = 0; k < full; k++) {
            const T* q = v + k * 64;
            std::uint64_t bits = 0;
            for (int i = 0; i < 64; i++) {
                bits |= static_cast<std::uint64_t>(p(q[i]) ? 1 : 0) << i;
            }
            w[k] = bits;
        }
        if (full < nw) {                       // the partial last word
            std::uint64_t bits = 0;
            for (std::size_t i = full * 64; i < n; i++) {
                bits |= static_cast<std::uint64_t>(p(v[i]) ? 1 : 0) << (i - full * 64);
            }
            w[full] = bits;
            for (std::size_t k = full + 1; k < nw; k++) w[k] = 0;
        }
    }

    /*!
     * Evaluate a predicate on a PAIR of columns, word at a time.
     *
     * The two-dimensional counterpart of scan_column, and what a region drawn
     * on a scatter plot needs. Both columns are read in their own type; the
     * predicate sees doubles because that is what a shape is defined in.
     */
    template<typename Pred>
    void scan_xy(const Column& cx, const Column& cy, BitMask& m, Pred p) const {
        const std::size_t n = std::min(n_rows_, std::min(cx.size(), cy.size()));
        std::uint64_t* w = m.words();
        const std::size_t nw = m.n_words();
        const std::size_t full = n / 64;
        for (std::size_t k = 0; k < full; k++) {
            std::uint64_t bits = 0;
            const std::size_t base = k * 64;
            for (int i = 0; i < 64; i++) {
                bits |= static_cast<std::uint64_t>(
                        p(cx.value_at(base + i), cy.value_at(base + i)) ? 1 : 0) << i;
            }
            w[k] = bits;
        }
        if (full < nw) {
            std::uint64_t bits = 0;
            for (std::size_t i = full * 64; i < n; i++) {
                bits |= static_cast<std::uint64_t>(p(cx.value_at(i), cy.value_at(i)) ? 1 : 0)
                        << (i - full * 64);
            }
            w[full] = bits;
            for (std::size_t k = full + 1; k < nw; k++) w[k] = 0;
        }
        // A point whose position is unknown cannot be shown to be inside a
        // shape, and admitting it would quietly widen every selection.
        if (cx.has_mask()) m.and_with(cx.mask());
        if (cy.has_mask()) m.and_with(cy.mask());
    }

    /*!
     * Dispatch `p` over whatever type the column holds. One switch per COLUMN.
     *
     * `invalid_selected` says what a row the column marks as missing means. The
     * default -- false -- is the library's rule: "not measured" cannot satisfy a
     * condition. True is for a front end whose gates are written as comparisons
     * in a language where every comparison against a missing value is false, so
     * a missing value passes through the gate untouched. Both are defensible and
     * they are not interchangeable, so it is a parameter rather than a policy.
     */
    template<typename Pred>
    void scan_column(const Column& c, BitMask& m, Pred p,
                     bool invalid_selected = false) const {
        const std::size_t n = std::min(n_rows_, c.size());
        switch (c.type()) {
            case ColumnType::Float64: scan_typed(c.f64_ptr(), n, m, p); break;
            case ColumnType::Float32: scan_typed(c.f32_ptr(), n, m, p); break;
            case ColumnType::Int64:   scan_typed(c.i64_ptr(), n, m, p); break;
            case ColumnType::Int32:   scan_typed(c.i32_ptr(), n, m, p); break;
            case ColumnType::Int16:   scan_typed(c.i16_ptr(), n, m, p); break;
            case ColumnType::Int8:    scan_typed(c.i8_ptr(), n, m, p); break;
            case ColumnType::UInt64:  scan_typed(c.u64_ptr(), n, m, p); break;
            case ColumnType::UInt32:  scan_typed(c.u32_ptr(), n, m, p); break;
            case ColumnType::UInt16:  scan_typed(c.u16_ptr(), n, m, p); break;
            case ColumnType::UInt8:   scan_typed(c.u8_ptr(), n, m, p); break;
            case ColumnType::String:  scan_typed(c.codes_ptr(), n, m, p); break;
            case ColumnType::Bool: {
                for (std::size_t i = 0; i < n; i++) m.set(i, p(c.value_at(i) != 0.0));
                break;
            }
        }
        if (c.has_mask()) {
            if (invalid_selected) {
                // The missing rows pass the gate whatever the predicate made of
                // whatever was in the buffer for them.
                BitMask missing = c.mask();
                missing.invert();
                m.or_with(missing);
            } else {
                // A column that says a value is missing cannot satisfy any
                // condition.
                m.and_with(c.mask());
            }
        }
    }

public:

    /// How a new condition combines with the selection already there.
    enum class Combine { Replace, And, Or, AndNot };

    /*!
     * \brief Select the rows whose value in column `col` lies in [lo, hi).
     *
     * Evaluated in C++ over the column's own type -- a float32 column is
     * compared as float32 -- and written straight into the bit-packed
     * selection. The pattern this replaces is a bool array per condition,
     * combined with numpy and then turned into an index array: eight times the
     * memory for the mask, plus a second array of indices, plus the copy that
     * fancy-indexing makes.
     *
     * Rows the column marks invalid are never selected: "not measured" cannot
     * satisfy a range.
     */
    void select_range(int col, double lo, double hi, Combine how = Combine::Replace) {
        BitMask m(n_rows_, false);
        scan_column(column(col), m, [lo, hi](auto v) {
            const double d = static_cast<double>(v);
            return d >= lo && d < hi;
        });
        apply(m, how);
    }

    /// Select the rows whose value in `col` equals `value`. For categories and
    /// integer columns, where a range is the wrong question.
    void select_equal(int col, double value, Combine how = Combine::Replace) {
        BitMask m(n_rows_, false);
        scan_column(column(col), m,
                    [value](auto v) { return static_cast<double>(v) == value; });
        apply(m, how);
    }

    /*!
     * \brief Select the rows whose value in `col` lies in an interval whose
     *        endpoints may each be open or closed.
     *
     * \ref select_range is the library's own rule -- half-open, missing values
     * dropped -- and is unchanged. This is for reproducing a gate defined
     * somewhere else, where those two decisions were made differently and
     * changing them would change which points a published figure contains:
     *
     *  - **Closed endpoints.** Most gates a scientist draws are `lo <= v <= hi`.
     *    A half-open interval quietly drops the points exactly on the upper
     *    edge, which for integer-valued or binned columns is not a rounding
     *    detail but a visible bite out of the population.
     *  - **`invalid_selected`.** A front end that writes its gate as
     *    `(v >= lo) & (v <= hi)` in numpy keeps every NaN, because both
     *    comparisons are false and the point is never excluded by THIS gate --
     *    it is left for a separate "drop the non-finite" step to decide. Passing
     *    true reproduces that; the default reproduces the library's rule.
     *
     * `lo` and `hi` may be infinite, which is how a one-sided gate is written.
     * An infinite bound is a real comparison, not a missing value: with
     * `hi = +inf` a stored `+inf` is inside a closed interval and outside an
     * open one, exactly as the arithmetic says.
     */
    void select_interval(int col, double lo, double hi,
                         bool lo_closed = true, bool hi_closed = true,
                         bool invalid_selected = false,
                         Combine how = Combine::Replace) {
        BitMask m(n_rows_, false);
        scan_column(column(col), m,
                    [lo, hi, lo_closed, hi_closed, invalid_selected](auto v) {
                        const double d = static_cast<double>(v);
                        // NaN fails every comparison, so the two conventions
                        // differ on it and only on it.
                        if (d != d) return invalid_selected;
                        const bool above = lo_closed ? (d >= lo) : (d > lo);
                        const bool below = hi_closed ? (d <= hi) : (d < hi);
                        return above && below;
                    },
                    invalid_selected);
        apply(m, how);
    }

    /// Select the rows where every listed column has a finite, valid value.
    void select_finite(const std::vector<int>& cols, Combine how = Combine::Replace) {
        BitMask acc(n_rows_, true);
        for (int ci : cols) {
            const Column& c = column(ci);
            // An integer cannot be a NaN or an infinity, so there is nothing to
            // scan for: only what the column itself marks missing can exclude a
            // row. Worth the branch because a pixel coordinate is an integer
            // column and this runs on every redraw.
            if (!is_floating(c.type())) {
                if (c.has_mask()) acc.and_with(c.mask());
                continue;
            }
            BitMask m(n_rows_, false);
            scan_column(c, m,
                        [](auto v) { return std::isfinite(static_cast<double>(v)); });
            acc.and_with(m);
        }
        apply(acc, how);
    }

    // --- regions -----------------------------------------------------------
    //
    // The shapes a user draws on a scatter plot: a rectangle, an ellipse, a
    // lasso, a painted mask. Evaluated here rather than in the front end
    // because the data is here -- the alternative is handing out two columns,
    // testing them elsewhere, and handing back a mask the size of the table.

    /// Rows inside the axis-aligned rectangle [x0, x1) x [y0, y1).
    void select_rectangle(int col_x, int col_y, double x0, double y0,
                          double x1, double y1, Combine how = Combine::Replace) {
        if (x1 < x0) std::swap(x0, x1);
        if (y1 < y0) std::swap(y0, y1);
        BitMask m(n_rows_, false);
        scan_xy(column(col_x), column(col_y), m, [=](double x, double y) {
            return x >= x0 && x < x1 && y >= y0 && y < y1;
        });
        apply(m, how);
    }

    /*!
     * Rows inside an ellipse centred at (cx, cy) with semi-axes (rx, ry),
     * rotated by `angle` radians.
     *
     * The rotation is folded into two constants so the inner test is a pair of
     * multiply-adds and a comparison -- no trigonometry per point.
     */
    void select_ellipse(int col_x, int col_y, double cx, double cy,
                        double rx, double ry, double angle = 0.0,
                        Combine how = Combine::Replace) {
        const double ca = std::cos(-angle), sa = std::sin(-angle);
        const double irx = rx != 0.0 ? 1.0 / rx : 0.0;
        const double iry = ry != 0.0 ? 1.0 / ry : 0.0;
        BitMask m(n_rows_, false);
        scan_xy(column(col_x), column(col_y), m, [=](double x, double y) {
            const double dx = x - cx, dy = y - cy;
            const double u = (dx * ca - dy * sa) * irx;
            const double v = (dx * sa + dy * ca) * iry;
            return u * u + v * v <= 1.0;
        });
        apply(m, how);
    }

    /*!
     * \brief Rows where a quadratic form about (cx, cy) is at most `threshold`.
     *
     * `(dx, dy) M (dx, dy)^T <= threshold` with `M = [[a, b], [b, c]]`. With `M`
     * the inverse of a covariance matrix and `threshold` the square of a number
     * of standard deviations, this is a Mahalanobis gate -- the ellipse drawn
     * around a fitted 2-D Gaussian population.
     *
     * \ref select_ellipse says the same thing in centre-radii-angle form and is
     * what a drawn shape has. This says it in the form a FIT has, and the
     * difference is not presentation: converting one to the other means
     * diagonalising `M`, and a comparison against a boundary computed through
     * two square roots and an arctangent does not agree bit for bit with one
     * computed from the coefficients directly. For a gate that decides which
     * points appear in a published population, reproducing the arithmetic is
     * worth a second entry point. It also stays meaningful when `M` is not
     * positive definite -- a covariance from a failed fit -- where the radii
     * form has no answer at all.
     */
    void select_quadratic(int col_x, int col_y, double cx, double cy,
                          double a, double b, double c, double threshold,
                          Combine how = Combine::Replace) {
        BitMask m(n_rows_, false);
        scan_xy(column(col_x), column(col_y), m, [=](double x, double y) {
            const double dx = x - cx, dy = y - cy;
            return a * dx * dx + 2.0 * b * dx * dy + c * dy * dy <= threshold;
        });
        apply(m, how);
    }

    /*!
     * \brief Combine a row-per-byte boolean mask into the selection.
     *
     * The way in for a condition the store has no primitive for. A caller can
     * read the one or two columns it needs as zero-copy views, decide in
     * whatever language it is written in, and combine the answer here -- rather
     * than taking the selection out, combining outside, and putting a whole new
     * one back, which is where a second representation of the selection starts.
     */
    void select_mask_rows(const unsigned char* m, int n,
                          Combine how = Combine::Replace) {
        BitMask b(n_rows_, false);
        const std::size_t k = std::min<std::size_t>(static_cast<std::size_t>(n < 0 ? 0 : n),
                                                    n_rows_);
        for (std::size_t i = 0; i < k; i++) if (m[i]) b.set(i, true);
        apply(b, how);
    }

    /*!
     * Rows inside a polygon, by the crossing-number rule.
     *
     * A lasso has a hundred vertices and the test is O(vertices) per point, so
     * the bounding box is checked first: a drawn region covers a small part of
     * the plane, most points fail four comparisons and never touch the edge
     * loop, and that is the difference between this being usable and not.
     */
    void select_polygon(int col_x, int col_y,
                        const double* xs, int n_xs, const double* ys, int n_ys,
                        Combine how = Combine::Replace) {
        const int nv = std::min(n_xs, n_ys);
        BitMask m(n_rows_, false);
        if (nv < 3) { apply(m, how); return; }

        double bx0 = xs[0], bx1 = xs[0], by0 = ys[0], by1 = ys[0];
        for (int i = 1; i < nv; i++) {
            bx0 = std::min(bx0, xs[i]); bx1 = std::max(bx1, xs[i]);
            by0 = std::min(by0, ys[i]); by1 = std::max(by1, ys[i]);
        }
        scan_xy(column(col_x), column(col_y), m, [=](double x, double y) {
            if (x < bx0 || x > bx1 || y < by0 || y > by1) return false;
            bool in = false;
            for (int i = 0, j = nv - 1; i < nv; j = i++) {
                if ((ys[i] > y) != (ys[j] > y) &&
                    x < (xs[j] - xs[i]) * (y - ys[i]) / (ys[j] - ys[i]) + xs[i]) {
                    in = !in;
                }
            }
            return in;
        });
        apply(m, how);
    }

    /*!
     * Rows whose (x, y) falls on a set pixel of a painted mask.
     *
     * `image` is `ny` rows of `nx` bytes -- row-major, y varying slowest --
     * covering [x0, x1) x [y0, y1). The parameter order says so: a 2-D numpy
     * array binds its first dimension to the first length, and that dimension
     * is the row count. Naming the row count `nx` and then indexing
     * `iy * nx + ix` transposes every non-square mask, silently and only for
     * non-square masks, which is exactly the bug this order exists to prevent.
     *
     * One multiply-add and a load per point, whatever the shape painted --
     * which is why an arbitrary drawing is no more expensive than a rectangle.
     */
    void select_mask_image(int col_x, int col_y,
                           const unsigned char* image, int ny, int nx,
                           double x0, double y0, double x1, double y1,
                           Combine how = Combine::Replace) {
        BitMask m(n_rows_, false);
        if (image == nullptr || nx < 1 || ny < 1 || x1 <= x0 || y1 <= y0) {
            apply(m, how);
            return;
        }
        const double sx = nx / (x1 - x0), sy = ny / (y1 - y0);
        scan_xy(column(col_x), column(col_y), m, [=](double x, double y) {
            // floor, not a cast: casting truncates towards zero, so a point one
            // half-pixel to the LEFT of the extent lands in column 0 instead of
            // -1 and is accepted by the range check below.
            const double fx = std::floor((x - x0) * sx);
            const double fy = std::floor((y - y0) * sy);
            if (!(fx >= 0.0 && fx < nx && fy >= 0.0 && fy < ny)) return false;
            const std::size_t ix = static_cast<std::size_t>(fx);
            const std::size_t iy = static_cast<std::size_t>(fy);
            return image[iy * static_cast<std::size_t>(nx) + ix] != 0;
        });
        apply(m, how);
    }

    /// Everything selected.
    void select_all() { row_mask_.clear(); }
    /// Nothing selected.
    void select_none() { row_mask_.assign(n_rows_, false); }

    /*!
     * \brief Ungate this store and every group under it.
     *
     * A selection does NOT propagate down a tree: groups have different row
     * counts, so a mask over one of them means nothing over another. Clearing
     * every gate at once is the exception, because it is the one tree-wide
     * operation callers actually want -- "show me all of it again" -- and it is
     * well defined whatever the row counts are.
     *
     * This is the one that also frees the masks, select_all() being a clear
     * rather than a fill.
     */
    void select_all_recursive() {
        select_all();
        for (const auto& g : groups_) g.second->select_all_recursive();
    }

    /// \see select_all_recursive. Selects nothing, everywhere; keeps the masks.
    void select_none_recursive() {
        select_none();
        for (const auto& g : groups_) g.second->select_none_recursive();
    }
    /*!
     * Flip the selection.
     *
     * Every row flips, including rows whose coordinates a region could not be
     * evaluated on because a column marks them missing. That is what "flip"
     * means and it is the only thing a mask operation can mean, but it is
     * usually not what a caller inverting a REGION wants: a point whose position
     * is unknown cannot be shown to be outside a shape any more than inside it.
     * Express that as `select_finite({x, y})` followed by the region with
     * Combine::AndNot, which keeps the missing rows out under both polarities.
     */
    void invert_selection() {
        if (row_mask_.empty()) { select_none(); return; }
        row_mask_.invert();
    }

    // -- groups ---------------------------------------------------------------
    //
    // A store is both a table (its own columns) and a container (its groups);
    // either may be empty. Each group is a full store -- its own columns, row
    // count, selection and label -- because that is what the data needs: a
    // results table has one row per pixel and the meta beside it has one row.
    //
    // Column and group namespaces are separate. A store may have a column and a
    // group of the same name, and nothing has to disambiguate, because the
    // accessors differ. In particular find() and column_by_name() never see a
    // group: they are the hottest lookup in the class and are not touched.
    //
    // Paths: "" and "/" mean this store, a leading and a trailing separator are
    // both optional, and "a/b" nests. add_group takes a NAME, the rest take a
    // path. An empty component ("a//b"), "." or ".." as a component, and a NUL
    // are rejected by throwing rather than by being reinterpreted.

    /// Direct children only.
    int n_groups() const { return static_cast<int>(groups_.size()); }

    /// Drop every group, and everything under them. Invalidates any handle into
    /// the tree; the store's own columns are untouched.
    void clear_groups() { groups_.clear(); }

    bool has_group(const std::string& path) const { return resolve(path) != nullptr; }

    /*!
     * \brief The group at `path`.
     *
     * A borrowed reference into the tree, owned by the root. It survives any
     * number of later add_group calls and the removal of unrelated siblings;
     * what invalidates it is removing it, or an ancestor.
     *
     * \note A group has no registry identity -- `id()` is 0 and it never appears
     *       in the live-store listing. The root's entry reports the whole tree.
     * \throws std::invalid_argument if there is no such group.
     */
    DataStore& group(const std::string& path) {
        DataStore* g = resolve(path);
        if (g == nullptr) throw std::invalid_argument("no group '" + path + "'");
        return *g;
    }
    const DataStore& group(const std::string& path) const {
        const DataStore* g = resolve(path);
        if (g == nullptr) throw std::invalid_argument("no group '" + path + "'");
        return *g;
    }

    /*!
     * \brief Add an empty group as a direct child.
     *
     * \param name a name, not a path -- a separator in it throws, because
     *        add_group("a/b") reads as "make b inside a" and does not.
     * \throws std::invalid_argument if a group of that name is already there.
     *         Replacing one is remove_group then add_group, said out loud.
     */
    DataStore& add_group(const std::string& name) {
        check_component(name, name);
        if (name.find(kGroupSeparator) != std::string::npos)
            throw std::invalid_argument(
                    "add_group takes a name, not a path: '" + name + "'");
        if (child(name) != nullptr)
            throw std::invalid_argument("group '" + name + "' is already there");
        groups_.emplace_back(name, std::unique_ptr<DataStore>(new DataStore(ChildTag{})));
        return *groups_.back().second;
    }

    /// The group at `path`, creating it and any intermediate it needs.
    /// Idempotent: calling it twice gives the same group, not two.
    DataStore& ensure_group(const std::string& path) {
        DataStore* cur = this;
        for (const std::string& name : split_path(path)) {
            DataStore* next = cur->child(name);
            cur = next != nullptr ? next : &cur->add_group(name);
        }
        return *cur;
    }

    /// False when there was no such group. Handles into the removed subtree
    /// die with it; handles to anything else survive.
    bool remove_group(const std::string& path) {
        const std::vector<std::string> parts = split_path(path);
        if (parts.empty()) return false;            // "" and "/" are this store
        DataStore* parent = this;
        for (std::size_t i = 0; i + 1 < parts.size(); i++) {
            parent = parent->child(parts[i]);
            if (parent == nullptr) return false;
        }
        for (auto it = parent->groups_.begin(); it != parent->groups_.end(); ++it)
            if (it->first == parts.back()) { parent->groups_.erase(it); return true; }
        return false;
    }

    /// Direct children, in the order they were added. Not alphabetical:
    /// insertion order is what a round trip has to preserve.
    std::vector<std::string> group_names() const {
        std::vector<std::string> out;
        out.reserve(groups_.size());
        for (const auto& g : groups_) out.push_back(g.first);
        return out;
    }

    /// Every descendant, depth first and parent before child, so every
    /// intermediate appears before anything under it.
    std::vector<std::string> group_paths() const {
        std::vector<std::string> out;
        append_paths(std::string(), out);
        return out;
    }

    /*!
     * \brief Total bytes held, by this store and every group under it.
     *
     * Columns and row masks only -- not the group names, not the container --
     * so the total stays exactly the sum of the per-column figures that
     * memory_report() lists. A subtotal that counted anything else would make
     * the two disagree.
     *
     * \warning Called by DataStoreRegistry::list() and total_bytes() while they
     *          hold a non-recursive mutex. Nothing this reaches -- including the
     *          recursion into groups -- may touch the registry, or the first
     *          data_store_report() deadlocks. Groups are unregistered, which is
     *          what makes that safe.
     */
    std::size_t nbytes() const {
        std::size_t b = row_mask_.nbytes();
        for (const Column& c : columns_) b += c.nbytes();
        for (const auto& g : groups_) b += g.second->nbytes();
        return b;
    }

private:
    /*!
     * \brief A store that belongs to a parent rather than to the registry.
     *
     * The registry holds ROOTS. A child that registered itself would make
     * total_bytes() double-count the moment nbytes() recurses, and would put an
     * entry in the listing that nobody can drop independently. Leaving id_ at 0
     * is all it takes -- the destructor already reads that as "nothing to
     * deregister".
     */
    struct ChildTag {};
    explicit DataStore(ChildTag) {}
    DataStore(ChildTag, const DataStore& o)
            : columns_(o.columns_), n_rows_(o.n_rows_), row_mask_(o.row_mask_),
              label_(o.label_) {
        clone_groups_from(o);
    }

    /// Deep-copy o's groups into this store's, as children.
    void clone_groups_from(const DataStore& o) {
        if (o.groups_.empty()) return;      // the common case, and it costs nothing
        groups_.reserve(o.groups_.size());
        for (const auto& g : o.groups_)
            // Not make_unique: it is not a friend and cannot see ChildTag.
            groups_.emplace_back(g.first, std::unique_ptr<DataStore>(
                    new DataStore(ChildTag{}, *g.second)));
    }

    /// Is p this store, or anywhere under it? Walks nothing when there are no
    /// groups, which is the only case the hot paths ever reach.
    bool contains(const DataStore* p) const {
        if (this == p) return true;
        for (const auto& g : groups_)
            if (g.second->contains(p)) return true;
        return false;
    }

    static const char kGroupSeparator = '/';

    /// One path component, or the name handed to add_group.
    static void check_component(const std::string& c, const std::string& whole) {
        if (c.empty())
            throw std::invalid_argument("empty group name in '" + whole + "'");
        if (c == "." || c == "..")
            throw std::invalid_argument("'" + c + "' is not a group name");
        if (c.find('\0') != std::string::npos)
            throw std::invalid_argument("a group name cannot contain a NUL");
    }

    /// The components of a path. Empty for "" and "/", which mean this store.
    static std::vector<std::string> split_path(const std::string& path) {
        std::vector<std::string> out;
        std::size_t b = 0, e = path.size();
        if (b < e && path[b] == kGroupSeparator) b++;              // leading, optional
        if (e > b && path[e - 1] == kGroupSeparator) e--;          // trailing, optional
        if (b >= e) return out;
        while (b < e) {
            std::size_t cut = path.find(kGroupSeparator, b);
            if (cut == std::string::npos || cut > e) cut = e;
            const std::string part = path.substr(b, cut - b);
            check_component(part, path);
            out.push_back(part);
            b = cut + 1;
        }
        return out;
    }

    DataStore* child(const std::string& name) {
        for (const auto& g : groups_)
            if (g.first == name) return g.second.get();
        return nullptr;
    }
    const DataStore* child(const std::string& name) const {
        return const_cast<DataStore*>(this)->child(name);
    }

    /// The store a path names, or nullptr. The single-component case is the one
    /// that happens, so it does not allocate a vector to find one child.
    const DataStore* resolve(const std::string& path) const {
        if (path.empty() || path == std::string(1, kGroupSeparator)) return this;
        if (path.find(kGroupSeparator) == std::string::npos) {
            check_component(path, path);
            return child(path);
        }
        const DataStore* cur = this;
        for (const std::string& name : split_path(path)) {
            cur = cur->child(name);
            if (cur == nullptr) return nullptr;
        }
        return cur;
    }
    DataStore* resolve(const std::string& path) {
        return const_cast<DataStore*>(
                static_cast<const DataStore*>(this)->resolve(path));
    }

    void append_paths(const std::string& prefix, std::vector<std::string>& out) const {
        for (const auto& g : groups_) {
            // A local copy, not out.back(): the recursive call push_backs into
            // the same vector and reallocates the reference away.
            const std::string path = prefix + g.first;
            out.push_back(path);
            g.second->append_paths(path + kGroupSeparator, out);
        }
    }

    void apply(BitMask& m, Combine how) {
        if (row_mask_.empty() && how != Combine::Replace) row_mask_.assign(n_rows_, true);
        switch (how) {
            case Combine::Replace: row_mask_ = m; break;
            case Combine::And:     row_mask_.and_with(m); break;
            case Combine::Or:      row_mask_.or_with(m); break;
            case Combine::AndNot:  row_mask_.andnot_with(m); break;
        }
    }

    // A deque, not a vector: adding a column must not invalidate a Column
    // reference already handed out. Nothing here needs the columns contiguous.
    std::deque<Column> columns_;
    std::size_t n_rows_ = 0;
    BitMask row_mask_;
    std::string label_;
    int id_ = 0;

    /*!
     * A vector of unique_ptr, not of DataStore.
     *
     * A handle to a group must survive a sibling being added AND removed, and
     * must survive the parent itself being moved -- and behind a pointer the
     * child never moves for any of the three. This is the bug the columns deque
     * exists to avoid, one level up: a reference handed out and then quietly
     * reallocated away reads freed memory and reports an empty name rather than
     * raising.
     *
     * (deque<pair<string, DataStore>> is not an option in any case: DataStore is
     * incomplete inside its own definition, and deque -- unlike vector -- may
     * not be instantiated on an incomplete type.)
     *
     * A vector because groups are few and ordered; lookup is a linear scan on
     * the name. The public API would be identical over any container, so if
     * sizeof(DataStore) ever matters this member and the special members above
     * are the whole of what would change.
     */
    std::vector<std::pair<std::string, std::unique_ptr<DataStore>>> groups_;
};

inline std::vector<DataStoreInfo> DataStoreRegistry::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DataStoreInfo> out;
    out.reserve(stores_.size());
    for (const auto& kv : stores_) {
        const DataStore* s = kv.second;
        DataStoreInfo i;
        i.id = kv.first;
        i.label = s->label();
        i.n_rows = s->n_rows();
        i.n_columns = s->n_columns();
        i.nbytes = s->nbytes();          // the whole tree; groups are not listed
        i.n_selected = s->n_selected();
        i.n_groups = s->n_groups();
        out.push_back(i);
    }
    return out;
}

inline std::size_t DataStoreRegistry::total_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t b = 0;
    for (const auto& kv : stores_) b += kv.second->nbytes();
    return b;
}

/// Every store currently alive, largest first.
inline std::vector<DataStoreInfo> live_data_stores() {
    std::vector<DataStoreInfo> v = DataStoreRegistry::instance().list();
    std::sort(v.begin(), v.end(),
              [](const DataStoreInfo& a, const DataStoreInfo& b) { return a.nbytes > b.nbytes; });
    return v;
}

/// Total bytes held by every live store.
inline std::size_t live_data_store_bytes() { return DataStoreRegistry::instance().total_bytes(); }

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
            // The weight is read out of its column, in its own type, exactly
            // like a coordinate. Materialising it as a double array first --
            // which is what this did -- allocated and filled one double per ROW
            // per fill: 14 MB on a two-million-row table, three times per
            // redraw, to widen values that are added up and discarded.
            [w](long long i) {
                // Guarded rather than relying on the caller never asking: the
                // guard is one predictable branch, and a null dereference here
                // would be a crash in a fill loop.
                return w != nullptr ? w->value_at(static_cast<std::size_t>(i)) : 1.0;
            },
            w != nullptr,
            static_cast<long long>(store.n_rows()), n_threads);
}

/*!
 * \brief Fill a Mean or WeightedMean histogram with a SAMPLE column, binned by
 *        the coordinate columns.
 *
 * The parameter map: each bin holds the mean of `sample` over the rows that
 * landed in it, rather than how many landed there. Two axis columns and a
 * lifetime column make a lifetime image; the same call with one axis makes a
 * profile along it.
 *
 * A row is used only if the selection allows it and every column involved --
 * each axis, the sample, and the weight -- says its value is valid. The sample
 * carries a stricter rule than the axes do: a non-finite sample is skipped even
 * if the column does not mark it missing, because Welford's update is recursive
 * and a single NaN leaves the bin NaN for every row after it. Dropping the row
 * loses one measurement; keeping it loses the pixel.
 *
 * \param h         the histogram; its storage must be Mean or WeightedMean
 * \param store     the table the columns come from
 * \param columns   one column index per axis
 * \param sample    the column being averaged
 * \param weight    a column to weight by, or -1 (WeightedMean only)
 */
inline void fill_histogram_sample(hist::HistogramNd& h, const DataStore& store,
                                  const std::vector<int>& columns, int sample,
                                  int weight = -1) {
    if (static_cast<int>(columns.size()) != h.rank())
        throw std::invalid_argument(
                "fill_histogram_sample: one column per axis is required");

    std::vector<const Column*> cols;
    cols.reserve(columns.size());
    for (int i : columns) cols.push_back(&store.column(i));
    const Column& s = store.column(sample);
    const Column* w = (weight >= 0) ? &store.column(weight) : nullptr;

    std::vector<double> weights;
    if (w != nullptr) {
        weights.resize(store.n_rows());
        for (std::size_t i = 0; i < weights.size(); i++) weights[i] = w->value_at(i);
    }

    const Column* const* cp = cols.data();
    const Column* sp_col = &s;
    const DataStore* sp = &store;
    const std::size_t n_cols = cols.size();

    h.fill_sample_with(
            [cp](int d, long long i) { return cp[d]->value_at(static_cast<std::size_t>(i)); },
            [cp, sp, sp_col, n_cols, w](long long i) {
                const std::size_t r = static_cast<std::size_t>(i);
                if (!sp->row_selected(r)) return false;
                for (std::size_t d = 0; d < n_cols; d++)
                    if (!cp[d]->valid(r)) return false;
                if (!sp_col->valid(r)) return false;
                if (!std::isfinite(sp_col->value_at(r))) return false;
                if (w != nullptr && !w->valid(r)) return false;
                return true;
            },
            [sp_col](long long i) { return sp_col->value_at(static_cast<std::size_t>(i)); },
            static_cast<long long>(store.n_rows()),
            weights.empty() ? nullptr : weights.data());
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
