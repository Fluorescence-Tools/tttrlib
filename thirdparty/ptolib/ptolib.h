// SPDX-License-Identifier: MIT
/*!
 * \file ptolib.h
 * \brief ptolib: PTO (Portable Tagged Objects), the DataStore and its `.dstore`
 *        encoding, in one header.
 * \author Thomas-Otavio Peulen
 * \copyright MIT License, Thomas-Otavio Peulen
 * \version 0.3.1
 *
 * PTO is a self-contained, packed data container: an EBML document (DocType
 * `"pto"`) that binds opaque payloads into one file, gives each a UID that
 * survives rewriting, lets typed tags describe them and reference each other,
 * and updates in place under a two-index commit that makes every change
 * atomic. The DataStore is the columnar table that is its natural payload,
 * with a compiled SIMD expression engine for gating it.
 * https://github.com/tpeulen/ptolib
 *
 * Usage: include this header wherever the types are needed, and in exactly ONE
 * translation unit per program (or shared library) write
 *
 *     #define PTOLIB_IMPLEMENTATION
 *     #include <ptolib/ptolib.h>
 *
 * Configuration macros, all optional:
 * - `PTOLIB_JSON_INCLUDE`: header that provides nlohmann::json (default
 *   `<nlohmann/json.hpp>`)
 * - `PTOLIB_API`: export/import decoration for the non-inline functions
 *   (default empty)
 */
#ifndef PTOLIB_H
#define PTOLIB_H

#define PTOLIB_VERSION_MAJOR 0
#define PTOLIB_VERSION_MINOR 3
#define PTOLIB_VERSION_PATCH 1
#define PTOLIB_VERSION_STRING "0.3.1"

#ifndef PTOLIB_API
#define PTOLIB_API
#endif
// `__restrict` on the pointers of an inline loop, for the compilers; nothing
// for SWIG, which parses this header for the bindings and does not know it.
#if defined(SWIG)
#define PTOLIB_RESTRICT
#else
#define PTOLIB_RESTRICT __restrict
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#ifndef SWIG
#include <cstdio>
#include <functional>
#endif

/*!
 * \namespace pto
 * \brief Everything ptolib declares: the DataStore, the `.dstore` encoding,
 *        the PTO container and the expression engine.
 */
namespace pto {

namespace detail {

/// Set bits in a word. The compiler's instruction where the target has one;
/// the parallel bit count otherwise, which is a dozen operations and no call.
inline int popcount64(std::uint64_t w) {
#if defined(__aarch64__) || defined(__POPCNT__) || (defined(__clang__) && !defined(_MSC_VER))
    return __builtin_popcountll(w);
#else
    w = w - ((w >> 1) & 0x5555555555555555ULL);
    w = (w & 0x3333333333333333ULL) + ((w >> 2) & 0x3333333333333333ULL);
    w = (w + (w >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return static_cast<int>((w * 0x0101010101010101ULL) >> 56);
#endif
}

/// Eight bytes to eight bits: bit k set where byte k is not zero. Branch-free,
/// and eight rows per step rather than one.
inline std::uint64_t pack8(std::uint64_t x) {
    const std::uint64_t low7 = x & 0x7F7F7F7F7F7F7F7FULL;
    const std::uint64_t nz = ((low7 + 0x7F7F7F7F7F7F7F7FULL) | x) & 0x8080808080808080ULL;
    return (nz * 0x0002040810204081ULL) >> 56;
}

/// The inverse: eight bits to eight bytes of 0 or 1, byte k from bit k.
inline std::uint64_t spread8(unsigned bits) {
    const std::uint64_t x = (static_cast<std::uint64_t>(bits & 0xFFu) * 0x0101010101010101ULL) & 0x8040201008040201ULL;
    return ((x + 0x7F7F7F7F7F7F7F7FULL) >> 7) & 0x0101010101010101ULL;
}

}  // namespace detail

// ===========================================================================
// DataStore -- columnar tables
// ===========================================================================

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
 * \brief A run of rows that were never measured, and why.
 *
 * The form a concat leaves behind: a column absent from one of twenty files is
 * missing for that file's whole contribution, which is one contiguous run, so a
 * bit per row would store a million copies of one fact. Half-open,
 * `[first, last)`.
 *
 * `why` is the part a bit cannot carry. "Not measured because that file did not
 * have this column" is information; a zero bit is the absence of it.
 *
 * At namespace scope rather than inside Column, where it belongs by meaning: a
 * nested class does not reach three of the four bindings, so it would have been
 * an opaque pointer everywhere but C++.
 */
struct NaRange {
    std::size_t first = 0;
    std::size_t last = 0;
    std::string why;
};

/*!
 * \brief A bit per row.
 *
 * One eighth the size of a byte array, which matters at ten million rows, and
 * read a word at a time when a fill is scanning it.
 */
// An out-of-range row index on a column accessor read past the allocation --
// silently at a small overrun, fatally at a large one. Out of line and
// noreturn so the guard costs a branch and nothing else in the caller.
[[noreturn]] void datastore_index_out_of_range(std::size_t index, std::size_t size);

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

    // Bounded, unlike the raw word indexing these used to do. `test` past the
    // end was a SIGSEGV straight from a binding -- BitMask().test(100000000)
    // -- and `set` is the worse half, ORing a bit into whatever it landed on.
    //
    // `n_` is the right bound and there is no capacity/count split to get wrong
    // here, unlike an event reader's arrays: `words_` is only ever assigned
    // alongside `n_`, always at (n_ + 63) / 64, and never grown on its own.
    //
    // These are per-row on every mask and filter, so the throw stays out of
    // line and the guard is a predicted compare.
    inline bool test(std::size_t i) const {
        if (i >= n_) datastore_index_out_of_range(i, n_);
        return (words_[i >> 6] >> (i & 63)) & 1ULL;
    }
    inline void set(std::size_t i, bool v) {
        if (i >= n_) datastore_index_out_of_range(i, n_);
        const std::uint64_t bit = 1ULL << (i & 63);
        if (v) words_[i >> 6] |= bit; else words_[i >> 6] &= ~bit;
    }

    /// Number of set bits.
    std::size_t count() const {
        std::size_t c = 0;
        for (std::uint64_t w : words_) c += static_cast<std::size_t>(detail::popcount64(w));
        return c;
    }
    std::size_t nbytes() const { return words_.size() * sizeof(std::uint64_t); }

    /// Raw words, for building a mask 64 rows at a time rather than bit by bit.
    std::uint64_t* words() { return words_.data(); }
    const std::uint64_t* words() const { return words_.data(); }
    std::size_t n_words() const { return words_.size(); }

    /// The inverse of \ref words -- take a mask back from packed words, for a
    /// loader that read them out of a file rather than computing them. Keeps
    /// the packing an implementation detail on both sides.
    void assign_words(const std::uint64_t* w, std::size_t n_bits) {
        n_ = n_bits;
        words_.assign(w, w + (n_bits + 63) / 64);
        trim();
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
            for (int j = 0; j < 8; j++) {
                std::uint64_t eight;
                std::memcpy(&eight, q + 8 * j, 8);
                bits |= detail::pack8(eight) << (8 * j);
            }
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
        const std::size_t n = std::min<std::size_t>(n_, static_cast<std::size_t>(n_out < 0 ? 0 : n_out));
        std::size_t i = 0;
        // Eight rows per step, straight from the word: no per-row bounds check
        // and no per-row read of the word.
        for (; i + 8 <= n; i += 8) {
            const std::uint64_t eight = detail::spread8(static_cast<unsigned>(words_[i >> 6] >> (i & 63)));
            std::memcpy(out_bytes + i, &eight, 8);
        }
        for (; i < n; i++) out_bytes[i] = (words_[i >> 6] >> (i & 63)) & 1ULL ? 1 : 0;
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

    /*!
     * \brief Bytes actually held -- CAPACITY, not size.
     *
     * The buffers: values, dictionary and mask. NOT the description, which is
     * how a column that records its gaps as ranges rather than as bits shows
     * that it allocated no mask -- and which is a description rather than a
     * buffer, so counting it would make "a float32 column is half a float64
     * one" false for a reason that has nothing to do with dtypes.
     */
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
     * found fewer valid events -- which is every photon-record reader, because invalid
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
     * The values are zeroed and the column says so, which is what lets a column
     * absent from one store keep its dtype through a concat instead of being
     * widened to hold a NaN.
     *
     * Recorded as one range, not as `n` zero bits: the rows are contiguous by
     * construction -- they are one store's whole contribution -- so a bit per
     * row would be a million copies of one fact. \param why travels with it,
     * which is the part a bit cannot carry. \see add_na_range, which decides
     * between the two representations.
     */
    void append_missing(std::size_t n, const std::string& why = std::string()) {
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
        add_na_range(before, before + n, why);
    }

    /// Gather rows `rows[0..n)` of `src` into this column, which is emptied first.
    void take_from(const Column& src, const int* rows, std::size_t n) {
        set_metadata(src.metadata_);
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
        // A gather reorders rows, so a range describing the SOURCE's rows says
        // nothing true about these. The validity is kept and the ranges are not:
        // the reason a row is missing survives only while the rows it names still
        // mean what they meant.
        if (!na_.empty()) { set_attribute_json("na", std::string()); }
        if (!src.has_missing()) {
            mask_.clear();
        } else {
            mask_.assign(n, true);
            for (std::size_t k = 0; k < n; k++)
                mask_.set(k, src.valid(static_cast<std::size_t>(rows[k])));
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
        // Bounded, unlike the raw indexing this used to do. An out-of-range row
        // read past the allocation: at a small overrun it returned 0.0 -- a
        // perfectly plausible measurement -- and far out it was a SIGSEGV
        // (BUGS 2026-08-11). `string_at` a dozen lines above has always
        // checked; this is the same class, the same shape, and did not. The
        // throw is out of line so the per-row callers (masking, comparisons,
        // the histogram fill callbacks) keep only a predicted branch.
        if (i >= n_) datastore_index_out_of_range(i, n_);
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

    /*!
     * \brief Rows `[first, first + n)` widened to double, into `out`.
     *
     * One switch on the type per call rather than one per row, which is what
     * makes a two-column scan run at the speed of the memory rather than of
     * the dispatch: \ref value_at is the right call for one value and the
     * wrong one for a million. For a String column the dictionary codes; for a
     * Bool column 0 or 1. Rows past the end are not written; the return value
     * says how many were.
     */
    std::size_t copy_f64(std::size_t first, std::size_t n, double* out) const {
        if (first >= n_) return 0;
        const std::size_t take = std::min(n, n_ - first);
        switch (type_) {
            case ColumnType::Float64: std::memcpy(out, f64_.data() + first, take * sizeof(double)); break;
            case ColumnType::Float32: widen(f32_.data() + first, take, out); break;
            case ColumnType::Int64:   widen(i64_.data() + first, take, out); break;
            case ColumnType::Int32:   widen(i32_.data() + first, take, out); break;
            case ColumnType::Int16:   widen(i16_.data() + first, take, out); break;
            case ColumnType::Int8:    widen(i8_.data() + first, take, out); break;
            case ColumnType::UInt64:  widen(u64_.data() + first, take, out); break;
            case ColumnType::UInt32:  widen(u32_.data() + first, take, out); break;
            case ColumnType::UInt16:  widen(u16_.data() + first, take, out); break;
            case ColumnType::UInt8:   widen(u8_.data() + first, take, out); break;
            case ColumnType::String:  widen(codes_.data() + first, take, out); break;
            case ColumnType::Bool:
                for (std::size_t i = 0; i < take; i++) out[i] = bits_.test(first + i) ? 1.0 : 0.0;
                break;
        }
        return take;
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

    /*!
     * \brief Whether a bit mask is allocated.
     *
     * Storage, not meaning: a column whose missing rows are recorded as ranges
     * answers `false` here and still has missing rows. Ask \ref has_missing for
     * the question that is usually meant, and \ref valid for one row.
     */
    bool has_mask() const { return !mask_.empty(); }
    const BitMask& mask() const { return mask_; }

    /// Whether any row is not valid, in whichever form this column stores it.
    bool has_missing() const { return !mask_.empty() || !na_.empty(); }

    /*!
     * \brief Validity as a bit mask, whatever the storage.
     *
     * Empty when every row is valid, which is the same convention \ref mask
     * uses. Ranges are expanded here rather than kept expanded, so a column
     * that nobody asks pays nothing: the whole point of the range form is that
     * a million rows of one fact do not become a million bits.
     */
    BitMask validity() const {
        if (na_.empty()) return mask_;
        BitMask m;
        m.assign(n_, true);
        for (const NaRange& r : na_) {
            const std::size_t last = r.last < n_ ? r.last : n_;
            for (std::size_t i = r.first; i < last; i++) m.set(i, false);
        }
        if (!mask_.empty())
            for (std::size_t i = 0; i < n_ && i < mask_.size(); i++)
                if (!mask_.test(i)) m.set(i, false);
        return m;
    }

    /// The runs recorded as not measured, in the order they were recorded.
    const std::vector<NaRange>& na_ranges() const { return na_; }

    /*!
     * \brief Record `[first, last)` as never measured, with a reason.
     *
     * Stored in \ref metadata under `na`, so it travels with the column through
     * any format that carries a description and costs about forty bytes rather
     * than one bit per row. A column that already carries a bit mask gets bits
     * instead -- mixing the two representations in one column would make every
     * reader carry both paths for no gain.
     */
    void add_na_range(std::size_t first, std::size_t last, const std::string& why);

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

    /*!
     * \brief Whether row `i` holds a measurement.
     *
     * The interface, whichever way the answer is stored. The range scan is a
     * loop over as many entries as there were files in the merge, and it is
     * reached only by a column that has ranges at all -- a column with neither
     * form costs the one branch it always cost.
     */
    inline bool valid(std::size_t i) const {
        if (!mask_.empty() && !mask_.test(i)) return false;
        for (std::size_t k = 0; k < na_.size(); k++)
            if (i >= na_[k].first && i < na_[k].last) return false;
        return true;
    }

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
    template <typename S>
    static void widen(const S* PTOLIB_RESTRICT src, std::size_t n, double* PTOLIB_RESTRICT dst) {
        for (std::size_t i = 0; i < n; i++) dst[i] = static_cast<double>(src[i]);
    }
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
        // The source's ranges shift by `at` and stay ranges, which is what keeps
        // a concat of twenty files from materialising twenty masks. Only when
        // one side already has bits does everything become bits -- see
        // add_na_range, which makes that decision in one place.
        if (mask_.empty() && !src.has_mask()) {
            for (const NaRange& r : src.na_) {
                const std::size_t last = (r.last < n ? r.last : n) + at;
                add_na_range(r.first + at, last, r.why);
            }
            return;                     // the appended rows are otherwise valid
        }
        if (!has_missing() && !src.has_missing()) return;  // all valid, stays implicit
        materialise_mask(at);
        for (std::size_t i = 0; i < n; i++)
            mask_.set(at + i, src.valid(i));
    }

    //: The authority. `name_`, `units_` and `na_` are caches kept in step by
    //: the setters -- there are two ways in, and both have to update all of
    //: them, or a column reports one name and serialises another.
    //:
    //: `na_` is a cache for a second reason: `valid()` is called once per row
    //: by every fill and every gather, and parsing JSON per row is not a thing
    //: that can happen.
    std::string metadata_;
    std::string name_;
    std::string units_;
    std::vector<NaRange> na_;
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
    std::unordered_map<std::string, int> lookup_;
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
 * A session accumulates these without meaning to: a photon stream is one, the
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
     * so core and the extension each got their own registry, and a store created
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

    //! A compiled expression together with the buffers it is bound to.
    /*! Held so a repeated gate -- which is what an interactive selection is --
        costs only its evaluation, and so the referenced columns are widened
        once rather than per call. */
    struct ExpressionProgram;
    mutable std::map<std::string, std::shared_ptr<ExpressionProgram> >
        expression_cache_;

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
    const Column& column_by_name(const std::string& name) const {
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

    /*!
     * \brief Fill `out` with an independent copy of this store, tree and all.
     *
     * Deep: every column owns its own buffer afterwards, so writing through one
     * store's values does not touch the other's. The group tree, dtypes,
     * dictionaries, validity, descriptions, labels and the row selection all
     * come across -- unlike \ref take_into and \ref compact_into, which drop
     * the selection because their result *is* one.
     *
     * The copy constructor did this already. It is a named method as well
     * because that is what a caller looks for, and because a constructor is not
     * how the other three bindings say it: `take_into` and `compact_into` are
     * reachable from all four and this has to be too, or "copy the store before
     * mutating it" is a Python-only idea.
     */
    void copy_into(DataStore& out) const;

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
            unsigned char hits[64];
            for (int i = 0; i < 64; i++) hits[i] = p(q[i]) ? 1 : 0;
            std::uint64_t bits = 0;
            for (int j = 0; j < 8; j++) {
                std::uint64_t eight;
                std::memcpy(&eight, hits + 8 * j, 8);
                bits |= detail::pack8(eight) << (8 * j);
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
        // A block of each column widened to double once, then the predicate
        // over the pair as a branchless loop: one type switch per 512 rows
        // instead of two per row, which measured five times faster than
        // value_at() per point.
        const std::size_t kBlock = 512;
        double bx[512], by[512];
        for (std::size_t base = 0; base < n; base += kBlock) {
            const std::size_t len = std::min(kBlock, n - base);
            cx.copy_f64(base, len, bx);
            cy.copy_f64(base, len, by);
            // The predicate writes a byte per row -- the loop shape a
            // vectoriser takes -- and the bytes pack eight at a time.
            unsigned char hits[512];
            for (std::size_t j = 0; j < len; j++) hits[j] = p(bx[j], by[j]) ? 1 : 0;
            if (len < kBlock) std::memset(hits + len, 0, kBlock - len);
            for (std::size_t i = 0; i < len; i += 64) {
                std::uint64_t bits = 0;
                for (int j = 0; j < 8; j++) {
                    std::uint64_t eight;
                    std::memcpy(&eight, hits + i + 8 * j, 8);
                    bits |= detail::pack8(eight) << (8 * j);
                }
                w[(base + i) >> 6] = bits;
            }
        }
        for (std::size_t k = (n + 63) / 64; k < nw; k++) w[k] = 0;
        // A point whose position is unknown cannot be shown to be inside a
        // shape, and admitting it would quietly widen every selection.
        if (cx.has_missing()) m.and_with(cx.validity());
        if (cy.has_missing()) m.and_with(cy.validity());
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
        if (c.has_missing()) {
            if (invalid_selected) {
                // The missing rows pass the gate whatever the predicate made of
                // whatever was in the buffer for them.
                BitMask missing = c.validity();
                missing.invert();
                m.or_with(missing);
            } else {
                // A column that says a value is missing cannot satisfy any
                // condition.
                m.and_with(c.validity());
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

    /*!
     * \brief Select the rows for which a boolean expression is true.
     *
     * The general form of a gate, where \ref select_range and the geometric
     * selectors are fixed shapes: `"(g-b)/(r-b) > 0.3"` names columns and is
     * compiled once, then evaluated over the store's own memory.
     *
     * Columns are referred to by name. Only the columns the expression names
     * are read, so a gate on two columns of a forty-column store touches two.
     * The expression is compiled by \ref ExpressionEngine and evaluated a block
     * of rows at a time, straight into the packed bits of the answer. An
     * expression over float32 columns is evaluated in single precision -- the
     * data's own -- so it agrees bit for bit with the same expression in numpy
     * or pandas; anything else is evaluated in double, with each column
     * converted as its block is read rather than widened into a copy first.
     *
     * Rows a referenced column marks invalid are never selected, the same rule
     * \ref select_range follows: "not measured" cannot satisfy a condition.
     * A row past the end of a referenced column is treated the same way.
     *
     * Python's `**` may be used for exponentiation, and `&`, `|`, `~` for the
     * elementwise boolean operators. A result that is not already boolean is
     * true where it is not zero, as `numpy.ndarray.astype(bool)` is.
     *
     * There is no second evaluator behind this one: an expression the engine
     * cannot compile is an error, stated, rather than an answer from a slower
     * path that might disagree. A Bool or String column reads as 0/1 or as
     * its dictionary code, the way \ref Column::value_at reports it.
     *
     * \param expr boolean expression over the column names
     * \param how how to combine the result with the current selection
     *
     * \throws std::invalid_argument if the expression does not compile, names
     *         an unknown column, or reads an Int64/UInt64 column whose values
     *         cannot be represented exactly. Python callers see this as a
     *         ValueError.
     */
    void select_expression(const std::string& expr,
                           Combine how = Combine::Replace);

    //! The rows an expression selects, without touching the selection.
    /*! Shared by \ref select_expression and \ref count_expression so the two
        cannot drift apart. */
    BitMask expression_mask(const std::string& expr) const;

    //! Drop compiled expressions; anything that moves a column must call this.
    void clear_expression_cache() const;

    //! Clear the bit of every row a referenced column marks as not measured.
    void apply_validity(const std::vector<int>& indices, BitMask& m) const;

    /*!
     * \brief How many rows an expression selects, without changing the
     *        selection or materialising a mask.
     *
     * The cheapest form of the question, for a caller that only wants the
     * count.
     */
    std::size_t count_expression(const std::string& expr) const;

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
                if (c.has_missing()) acc.and_with(c.validity());
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
// ===========================================================================
// ExpressionEngine -- compiled boolean/arithmetic expressions over columns
// ===========================================================================

/*!
 * \brief The layout of one column handed to the evaluator.
 *
 * Mirrors the numeric column types of \ref DataStore, kept as its own enum so
 * the engine does not depend on the store -- a caller with a plain array uses
 * it just as well.
 */
enum class ExprScalarType {
    Float64,
    Float32,
    Int64,
    Int32,
    Int16,
    Int8,
    UInt64,
    UInt32,
    UInt16,
    UInt8
};

/*!
 * \brief One input column, as a pointer and a type.
 *
 * The engine reads the caller's memory in place; nothing is copied into it and
 * nothing is widened ahead of time. A column whose \ref is_vector is false is a
 * single value at \ref data that broadcasts over every row, which is how a fit
 * parameter enters an equation without becoming an array.
 */
struct ExprColumn {
    /// First element of the column, or of the one value when broadcasting.
    const void* data = nullptr;
    /// How to read \ref data.
    ExprScalarType type = ExprScalarType::Float64;
    /// False for a single value broadcast over all rows.
    bool is_vector = true;
};

/*!
 * \brief An expression compiled to a block-vectorised program.
 *
 * Compile once, evaluate many times. The compiled form carries no per-call
 * state, so the same engine may be evaluated over different row counts and
 * different buffers without reparsing. Scratch buffers are kept between calls
 * -- allocating the block stack per call was measured to be most of the cost
 * for the few-hundred-point curves a fit evaluates -- which makes one engine
 * instance **not** safe to evaluate from two threads at once.
 *
 * \see DataStore::select_expression, which is this engine's first caller.
 */
class ExpressionEngine {
public:
    ExpressionEngine() = default;

    /*!
     * \brief Compile an expression, or report that this evaluator cannot.
     *
     * \param expression the expression, in the Python spelling a query is
     *        written in
     * \return true when a program was produced; false when the expression is
     *         malformed, names a function this evaluator does not implement, or
     *         nests deeper than the block stack allows. Nothing is thrown: a
     *         caller that has a fallback wants an answer, not an exception.
     *
     * On false the engine is left empty, and \ref ready answers false.
     */
    bool compile(const std::string& expression);

    /// The expression as it was handed to \ref compile.
    const std::string& expression() const { return expression_; }

    /*!
     * \brief The free names of the expression, in first-appearance order.
     *
     * These are the columns \ref compute_mask and \ref compute_values expect,
     * in the order they expect them. Names the engine resolves itself -- `pi`,
     * `e`, and every function it implements -- are not among them.
     */
    const std::vector<std::string>& variables() const { return variables_; }

    /// Whether a program is compiled and can be evaluated.
    bool ready() const { return !program_.empty(); }

    /// How many instructions the compiled program holds. For tests that pin
    /// what the folder and the subexpression pass actually did.
    std::size_t program_size() const { return program_.size(); }

    /// How many block-sized slots the shared-subexpression cache asked for.
    int cache_slots() const { return program_cache_size_; }

    /*!
     * \brief Evaluate as a gate, writing one bit per row.
     *
     * \param columns one per name in \ref variables, in that order
     * \param n_rows how many rows to evaluate
     * \param words destination, `(n_rows + 63) / 64` words, bit *i* of word
     *        *i/64* being row *i*. Every word written is overwritten whole, and
     *        bits past \p n_rows in the final word are cleared.
     *
     * The answer to a gate is a bit, and returning it as an array of doubles
     * costs eight bytes a row to say one. The block loop already carries its
     * booleans as one byte per row, so only the block's closing store differs
     * from \ref compute_values: the bytes are packed into words as the block
     * finishes, and no full-length intermediate exists at any point.
     *
     * A result that is not already boolean -- `"g"`, or `"g*2"` -- is taken as
     * true where it is not zero, which is numpy's cast to bool.
     *
     * \throws std::domain_error if no program is compiled, or if \p columns is
     *         not one per variable.
     */
    void compute_mask(const std::vector<ExprColumn>& columns,
                      std::size_t n_rows, std::uint64_t* words) const;

    /*!
     * \brief Evaluate as a curve, writing one double per row.
     *
     * \param columns one per name in \ref variables, in that order
     * \param n_rows how many rows to evaluate
     * \param out destination, \p n_rows doubles
     *
     * The arithmetic itself is done in whatever type the columns are -- float32
     * columns are evaluated in float32 -- and only the result is written as a
     * double.
     *
     * \throws std::domain_error if no program is compiled, or if \p columns is
     *         not one per variable.
     */
    void compute_values(const std::vector<ExprColumn>& columns,
                        std::size_t n_rows, double* out) const;

    /*!
     * \brief Rewrite the Python and pandas spellings into the engine's own.
     *
     * `**` becomes `^`, and `&`, `|`, `~` become `and`, `or`, `not`. Exposed
     * because a caller with a second evaluator behind this one has to hand it
     * the same string, and the two must not drift.
     *
     * \param s the expression as the user wrote it
     * \return the same expression in the engine's spelling
     */
    static std::string normalise(const std::string& s);

    /*!
     * \brief The free names of an already-normalised expression.
     *
     * \param s an expression, after \ref normalise
     * \return the names, deduplicated, in first-appearance order
     *
     * Exposed for the same reason as \ref normalise: a caller that resolves
     * columns before compiling needs exactly this list.
     */
    static std::vector<std::string> free_variables(const std::string& s);

private:
    //! One instruction of the block-vectorised program.
    struct VecOp {
        int kind = 0;       //!< constant, variable, operator, function, ...
        double value = 0.0; //!< the constant, or a constant exponent
        int index = 0;      //!< operator/function id, variable slot, cache slot
    };

    /*!
     * \brief Block scratch, one set per working type.
     *
     * Kept alive between calls. A six-deep program needs a 24 kB stack in
     * double, and allocating and freeing that on every call was most of the
     * cost at the curve lengths that actually occur.
     *
     * A stack slot is one of three things at any moment -- a block of numbers,
     * a block of one-byte booleans, or a single folded scalar -- and the flags
     * say which. Getting that reconciliation wrong at the boundaries is where
     * three real bugs lived, so the type is tracked rather than assumed.
     */
    template <typename T>
    struct Blocks {
        std::vector<T> stack;
        std::vector<unsigned char> bstack;
        std::vector<char> is_bool;
        std::vector<char> is_scalar;
        std::vector<T> scalar_value;
        std::vector<T> cache;
        std::vector<unsigned char> cache_bool;
        std::vector<char> cache_is_bool;
        std::vector<char> cache_is_scalar;
        std::vector<T> cache_scalar;
        std::vector<unsigned char> pack_scratch;
    };

    //! Tokenise and shunting-yard into \ref program_, then fold and share.
    bool compile_program(const std::string& normalised);

    //! Compute a repeated subtree once and reuse it, where that is cheaper.
    bool eliminate_common_subexpressions();

    //! The block loop. Exactly one of \p out and \p words is non-null.
    template <typename T>
    void run(const std::vector<ExprColumn>& columns, std::size_t n_rows,
             double* out, std::uint64_t* words, Blocks<T>& s) const;

    //! Check the columns and pick the working type, then \ref run.
    void dispatch(const std::vector<ExprColumn>& columns, std::size_t n_rows,
                  double* out, std::uint64_t* words) const;

    std::string expression_;
    std::string normalised_;
    std::vector<std::string> variables_;
    std::vector<VecOp> program_;
    int program_depth_ = 0;
    int program_cache_size_ = 0;

    mutable Blocks<double> scratch64_;
    mutable Blocks<float> scratch32_;
};

/*!
 * \brief The SIMD instruction set the expression engine evaluates with.
 *
 * `"avx2"`, `"sse2"`, `"neon"` or `"scalar"`. Chosen once, at the first
 * evaluation, from what the CPU reports: the header is compiled for the
 * baseline of its architecture and carries the wider tiers beside it, so one
 * binary runs everywhere and still uses AVX2 where it exists. Every tier
 * produces the same bits, so this is a statement about speed and never about
 * which rows a gate selects.
 */
const char* simd_tier();

/*!
 * \brief Choose the tier by name.
 *
 * For tests, which check every tier against the scalar one, and for
 * benchmarks. Returns false, changing nothing, for a name that is not one of
 * the four or a tier this CPU cannot run. Not for use while another thread is
 * evaluating: the choice is read at the start of each evaluation.
 */
bool set_simd_tier(const char* name);
#define PTOLIB_HAS_SIMD_TIER 1

// ===========================================================================
// Codecs -- compression, supplied by the consumer or wired in at build time
// ===========================================================================

/*!
 * \brief A compression codec, by name.
 *
 * ptolib carries no compressor of its own: the header stays std-only, and
 * which library a consumer links is the consumer's decision. What ptolib
 * fixes is the name -- `zstd`, `brotli`, `lz4`, `deflate` are the names the
 * formats record -- and the two calls a codec has to answer. A consumer that
 * links a library registers it once with \ref register_codec; a build with
 * `PTOLIB_WITH_ZSTD`, `PTOLIB_WITH_BROTLI` or `PTOLIB_WITH_LZ4` defined for
 * the implementation translation unit registers those itself: zstd as
 * single frames, brotli as plain streams, lz4 as frames (the `lz4` tool's
 * format, with the content size inside).
 *
 * Which to use: **zstd** for tables and streams -- on numeric columns it
 * gives the best ratio at every speed, level 3 (its default) at about
 * 400 MB/s in and 600 MB/s out on one core; **lz4** where read speed is
 * everything and a third less ratio is fine; **brotli** for text and JSON,
 * and for files other tools already write that way -- its modelling is
 * built for text and costs it five to twenty times zstd's time on numbers.
 *
 * `compress` writes the whole of `in` as one stream into `out`, at `level`
 * (`-1` for the codec's default: zstd 3, brotli 5, lz4 0 which is its fast
 * coder; lz4 3 and above is its HC coder). `decompress` writes exactly `raw_size`
 * bytes into `out`, or, when `raw_size` is 0 because the writer did not
 * record it, as many as the stream holds. Both return false on failure and
 * must never throw.
 */
#ifndef SWIG
struct Codec {
    std::string name;
    std::function<bool(const unsigned char* in, std::size_t n, int level,
                       std::vector<unsigned char>& out)> compress;
    std::function<bool(const unsigned char* in, std::size_t n, std::size_t raw_size,
                       std::vector<unsigned char>& out)> decompress;
};
/// Make a codec available under its name, replacing one of the same name.
void register_codec(const Codec& codec);
/// Forget a codec. For tests that need to see what a reader does without one.
void unregister_codec(const std::string& name);
/// Compress with a registered codec. False if the codec is unknown or failed.
bool compress_bytes(const std::string& codec, const unsigned char* in, std::size_t n,
                    int level, std::vector<unsigned char>& out);
/// \see Codec::decompress. False if the codec is unknown or the stream is bad.
bool decompress_bytes(const std::string& codec, const unsigned char* in, std::size_t n,
                      std::size_t raw_size, std::vector<unsigned char>& out);
#endif
/// Whether a codec of this name is registered.
bool has_codec(const std::string& name);
/// The registered codec names.
std::vector<std::string> codecs();

/*!
 * \brief An object encoding split at its codec suffix.
 *
 * `"dstore+zstd"` is a `.dstore` payload compressed with zstd; `"f32.col+brotli"`
 * a column of float32 under brotli; `"row16+zstd+delta"` rows that were
 * delta-coded by the application and then compressed with zstd. The part
 * before the first `+` says what the bytes are, the word after it names the
 * codec, and anything after that is the application's own transform, applied
 * before the codec and undone by the application after it -- the container
 * knows the codec and nothing else. An encoding with no `+` has an empty
 * `codec`.
 */
struct Encoding {
    std::string inner;      ///< what the bytes are once decoded: `dstore`, `row16`, `json`
    std::string codec;      ///< `zstd`, `brotli`, `lz4`, `deflate`, or empty
    std::string extra;      ///< the application's transforms after the codec: `delta`, or empty
};
/// \see Encoding
Encoding split_encoding(const std::string& encoding);

/*!
 * \brief How a store is written: which codec, if any, and how hard.
 *
 * With `codec` empty the file is what it always was, version 3, every blob
 * raw and mappable. With a codec named, blobs of at least `min_bytes` are
 * compressed and the file is version 4; a reader without that codec refuses
 * it, naming the codec. Columns are transformed first when `transform` is on
 * -- integers of 16 bits and wider are delta-coded, so a monotonic macro
 * time becomes a run of small numbers, and floats are byte-shuffled, so the
 * exponents and the mantissas each sit together -- which is most of the
 * ratio on measurement data. A column overrides all of this with a `codec`
 * attribute: a codec name, or `none` to stay raw.
 */
struct StoreOptions {
    std::string codec;          ///< "" for raw; "zstd", "brotli", ...
    int level = -1;             ///< the codec's default
    bool transform = true;      ///< delta for integers, shuffle for floats
    std::size_t min_bytes = 4096;   ///< smaller blobs stay raw
};

// ===========================================================================
// .dstore -- the native store file
// ===========================================================================

/// The magic at the head of a store file, and the customary extension.
// Inline constexpr, not extern data: MSVC cannot import data symbols
// across module DLLs, and SWIG wrappers reference these directly.
static constexpr const char* kStoreMagic = "TTTRSTOR";
static constexpr const char* kStoreExtension = ".dstore";

/*!
 * \brief Write a store, and everything under it, to `filename`.
 *
 * Goes to a temporary beside the target and is renamed into place, so a failed
 * write never leaves a half file where a good one was.
 *
 * \return false if the file could not be written. The reason goes to stderr.
 */
bool write_store(const std::string& filename, const DataStore& store);
/// \see write_store, with the codec and transforms \ref StoreOptions names.
/// False, with the reason on stderr, if the codec is not registered.
bool write_store(const std::string& filename, const DataStore& store,
                 const StoreOptions& options);

/*!
 * \brief Read a store file into `out`, replacing whatever it held.
 *
 * \throws std::runtime_error if the file is missing, not a store file, written
 *         by a newer version, byte-swapped, truncated, or corrupt in its
 *         directory. All of those are stated rather than guessed at: a reader
 *         that silently returns an empty table cannot be told apart from one
 *         that read an empty table.
 */
void read_store_into(DataStore& out, const std::string& filename);

/*!
 * \brief \see read_store_into, but only the named columns of each table.
 *
 * The directory says where every column is, so the ones not asked for are never
 * read -- two columns out of a four-gigabyte store costs two seeks. Names that
 * are not in the file are skipped silently; the group tree is rebuilt whole,
 * because it is the directory and costs nothing.
 */
void read_store_into(DataStore& out, const std::string& filename,
                     const std::vector<std::string>& columns);

/// \see read_store_into. Returns by value, which copies the whole tree at the
/// peak -- prefer the in-place form from a binding.
DataStore read_store(const std::string& filename);

/// Whether this file begins with the store magic. Silent on any input,
/// including a missing file: probing is a normal thing for a caller to do.
bool is_store_file(const std::string& filename);

#ifndef SWIG
/*!
 * \brief Write a store into an already-open file, at its current position.
 *
 * Every offset the directory records is relative to where the store starts, so
 * the result is a self-contained store file that happens to live inside
 * something bigger -- a PTO container, say. Nothing is buffered and no
 * temporary file is used, so a multi-gigabyte table costs its own bytes.
 *
 * Not exposed to the bindings: a FILE* is not something a binding can hold.
 *
 * \return bytes written, or 0 on failure.
 */
std::uint64_t write_store_at(std::FILE* f, const DataStore& store);
/// \see write_store_at, with \ref StoreOptions.
std::uint64_t write_store_at(std::FILE* f, const DataStore& store,
                             const StoreOptions& options);

/*!
 * \brief Read a store that is already in memory.
 *
 * What a store compressed as a whole inside a container comes back as, and
 * what a caller that fetched the bytes from somewhere that is not a file
 * has. The same reader, the same knobs: an empty `columns` means every
 * column, `n_rows` of 0 means to the end, an empty `group` the root.
 */
void read_store_into(DataStore& out, const unsigned char* bytes, std::size_t n);
void read_store_into(DataStore& out, const unsigned char* bytes, std::size_t n,
                     const std::vector<std::string>& columns,
                     std::uint64_t first_row = 0, std::uint64_t n_rows = 0,
                     const std::string& group = std::string());
/// \see store_columns, for a store in memory.
std::vector<std::string> store_columns(const unsigned char* bytes, std::size_t n,
                                       const std::string& group = "");
/// \see store_groups, for a store in memory.
std::vector<std::string> store_groups(const unsigned char* bytes, std::size_t n);
#endif
/// The format version word of a store file: 3 for a raw file, 4 for one with
/// compressed blobs. 0 for a file that is not a store.
std::uint32_t store_format_version(const std::string& filename);

/*!
 * \brief Read a store that begins `base` bytes into `filename`.
 *
 * \param bytes the length of the region, or 0 for "to the end of the file".
 * \see write_store_at, and \ref read_store_into for the whole-file case.
 */
void read_store_into(DataStore& out, const std::string& filename,
                     std::uint64_t base, std::uint64_t bytes);

/*!
 * \brief Both knobs at once: a column subset of a store embedded at `base`.
 *
 * The combination a container makes routine. A store inside a PTO is always the
 * `base`/`bytes` case, so without this a caller reading one could never ask for
 * a subset of its columns -- the single combination that matters was the single
 * one the API omitted.
 *
 * \see pto_read_store, which is the reason this exists.
 */
void read_store_into(DataStore& out, const std::string& filename,
                     std::uint64_t base, std::uint64_t bytes,
                     const std::vector<std::string>& columns);

/*!
 * \brief A column subset **and** a row range of an embedded store.
 *
 * A row range is a projection along the other axis from a column subset, and
 * costs the same kind of nothing: the directory records where every column's
 * blob begins and how wide its elements are, so a range is an offset and a
 * length per column. What a table viewer needs -- paging a million-row burst
 * table otherwise decodes a million rows to show fifty.
 *
 * Fixed-width columns are exact. A bit-packed column (bool, and every validity
 * mask) reads only the words its range falls in and is repacked to start at bit
 * zero. A dictionary-encoded text column reads its codes for the range and the
 * whole dictionary, which is small by construction.
 *
 * The range is applied to every table in the tree, each clamped to its own
 * length: a group with fewer rows than `first_row` comes back empty rather than
 * throwing. `n_rows` of 0 means "to the end".
 *
 * The store that comes back reports the range's length as its `n_rows`. It is a
 * window, not the file: writing it back would write the window.
 */
void read_store_into(DataStore& out, const std::string& filename,
                     std::uint64_t base, std::uint64_t bytes,
                     const std::vector<std::string>& columns,
                     std::uint64_t first_row, std::uint64_t n_rows,
                     const std::string& group = std::string());

/*!
 * \brief Read ONE group as the root of `out`.
 *
 * The third knob, and the cheapest of the three: the directory names every node
 * and every column's offset, so reaching a group is a scan of the directory --
 * a few kilobytes -- and not one byte of any group stepped over, however large
 * they are.
 *
 * A leading and a trailing separator are optional, as everywhere else a group
 * path is taken. The empty string is the root, which is the whole file.
 *
 * The tree BELOW the group comes back with it; the tree above it does not. That
 * is what makes the result a store in its own right rather than a view -- it
 * writes straight back out as a file whose root is the group asked for.
 *
 * \throws std::runtime_error if the group is not in the file. A caller who
 *         wants to ask rather than to read has \ref store_has.
 */
void read_store_into(DataStore& out, const std::string& filename,
                     const std::string& group);

/*!
 * \brief Whether the file holds this group.
 *
 * A directory read, touching no payload. Silent on any input, including files
 * that are not ours: probing is a normal thing to do.
 */
bool store_has(const std::string& filename, const std::string& group = "");

/// The column names of the root table, in order, without reading any data.
/// Empty for a file that is not one of ours.
std::vector<std::string> store_columns(const std::string& filename,
                                       const std::string& group = "");

/// \see store_columns, for a store that begins `base` bytes into `filename`.
std::vector<std::string> store_columns(const std::string& filename,
                                       std::uint64_t base, std::uint64_t bytes,
                                       const std::string& group = "");

/// Every group path in the file, depth first, without reading any data.
/// Empty for a file that is not one of ours.
std::vector<std::string> store_groups(const std::string& filename);

/// \see store_groups, for a store that begins `base` bytes into `filename`.
std::vector<std::string> store_groups(const std::string& filename,
                                      std::uint64_t base, std::uint64_t bytes);

/*!
 * \brief How many bytes the store reader has moved since the process started.
 *
 * A counter, because the claim "the reader did not touch those bytes" is not
 * one a wall clock can make: a warm page cache measures the cache. Every read
 * the store reader does goes through one place, and this is what that place
 * counts. Test scaffolding, and cheap enough to leave in.
 *
 * Not synchronised, because nothing in this library reads a store from more
 * than one thread. Take a difference around a single call and compare it to a
 * difference around another; the absolute value means nothing.
 */
std::uint64_t store_bytes_read();

// ===========================================================================
// PTO -- Portable Tagged Objects, the container
// ===========================================================================

/// What a \ref PtoTag carries. Covers PicoQuant's twelve header types, plus the
/// two object references that make provenance expressible.
enum class PtoType {
    Empty = 0,  ///< the tag's presence is the information
    UInt,       ///< PtoTag::u
    Int,        ///< PtoTag::i
    Float,      ///< PtoTag::d
    Date,       ///< PtoTag::i, nanoseconds since 2001-01-01 UTC (the EBML epoch)
    Text,       ///< PtoTag::text
    Bytes,      ///< PtoTag::bytes
    UID,        ///< PtoTag::u -- a reference to an object in this file
    UIDs,       ///< PtoTag::uids
    Floats,     ///< PtoTag::floats
    Ints,       ///< PtoTag::ints
};

/*!
 * \brief One piece of typed metadata, and what it is about.
 *
 * \par What PTO guarantees, and what it does not
 * A tag names its subject with \ref target, and may name another object as its
 * value with \ref PtoType::UID. That is a labelled edge, and it is the whole of
 * what the container provides. PTO does not define `derived_from`, does not
 * check that a referenced UID exists, and does not detect cycles: applications
 * disagree about all three, and a container that picks a winner is wrong for
 * everyone else.
 */
struct PtoTag {
    std::string name;               ///< opaque to the container; `pto.` is reserved
    PtoType type = PtoType::Empty;
    std::uint64_t target = 0;       ///< the object described; 0 means the file

    /*!
     * Position within an array, or -1 for a scalar.
     *
     * A PicoQuant header repeats a tag name once per element rather than
     * storing a list, and this is what lets such a header survive verbatim.
     */
    int index = -1;

    /// The type code this tag had in the format it came from, so a PTU header
    /// can be written back bit-exact. Zero when it came from nowhere.
    std::uint32_t source_type = 0;

    std::uint64_t u = 0;
    long long i = 0;
    double d = 0.0;
    std::string text;
    std::vector<unsigned char> bytes;
    std::vector<double> floats;
    std::vector<long long> ints;
    std::vector<std::uint64_t> uids;
};

/// A note somebody wrote down. Prose for people, as against \ref PtoTag, which
/// is values for programs.
struct PtoAnnotation {
    std::uint64_t target = 0;       ///< 0 means the file
    std::uint64_t first_row = 0;
    std::uint64_t last_row = 0;     ///< one past the end; both zero means all of it
    std::string text;
    std::string author;
    long long when = 0;             ///< nanoseconds since 2001-01-01 UTC, 0 if unset
};

/*!
 * \brief Reserved tag: the object this one accompanies, as a \ref PtoType::UID.
 *
 * A Becker & Hickl `.spc` keeps half its header in a `.set` beside it, so the
 * two have to travel together and be handed to the reader together. That makes
 * it container business rather than application business -- unlike
 * "derived from", which PTO deliberately leaves undefined -- and it is the one
 * relation the container names itself.
 */
static constexpr const char* kPtoSidecarTag = "pto.sidecar_of";

/// A run of free space inside the file. \see File::free_extents.
struct PtoExtent {
    std::uint64_t offset = 0;
    std::uint64_t bytes = 0;
};

/// What the container knows about one payload without decoding it.
struct PtoObject {
    std::uint64_t uid = 0;
    std::string kind;               ///< photons, table, spectrum, image, attachment
    std::string encoding;           ///< dstore, ptu, hdf5, tiff, raw, ...
    std::string name;
    std::string media_type;
    std::string description;
    std::uint64_t rows = 0;         ///< advisory; 0 if the writer did not say
    /// The decoded size, when \ref encoding carries a codec suffix and the
    /// writer recorded it; 0 otherwise. \see split_encoding
    std::uint64_t raw_size = 0;

    /// Where the payload is in the file, and how much room it has. `capacity`
    /// is what an in-place update has to fit inside; see \ref File::update.
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint64_t capacity = 0;
};

class File;

/*!
 * \brief What a file on disk would become inside a container.
 *
 * The three things an object needs before its bytes: what it is for, how to
 * decode it, and what to call it when it leaves again. \see pto_classify_path.
 */
struct PtoFileType {
    std::string kind;        ///< photons, table, spectrum, image, attachment
    std::string encoding;    ///< ptu, spc-130, csv, png, raw, ...
    std::string media_type;  ///< RFC 6838 type, empty when none is standard
};

/*!
 * \brief What a name alone says a file is.
 *
 * A table of extensions: `csv` is a `table`, `png` an `image`, `pdf` an
 * `attachment` with its media type, and anything the table does not know is
 * an `attachment` encoded `raw` -- carried, named, and left alone. Nothing here
 * opens the file. \ref File::classify is the hook an application overrides to
 * look at the bytes as well.
 */
PtoFileType classify_by_extension(const std::string& path);

// -- tables -----------------------------------------------------------------
//
// Declared before File so their default arguments are stated once: the
// friend declarations inside the class must not repeat them.

/*!
 * \brief Add a DataStore as an object, encoded as `dstore`.
 *
 * Serialised straight into the container with no intermediate copy and no
 * temporary file, so a photon stream of any size costs its own bytes and
 * nothing more.
 *
 * \param reserve extra bytes for a later in-place \ref File::update.
 * \return the new object's UID, or 0.
 */
std::uint64_t pto_add_store(File& file, const std::string& kind,
                            const std::string& name, const DataStore& store,
                            std::uint64_t reserve = 0);
/// \see pto_add_store, written with \ref StoreOptions: the object stays
/// encoded `dstore`, and the compression is inside it, per column, so the
/// directory and every partial read still work.
std::uint64_t pto_add_store(File& file, const std::string& kind,
                            const std::string& name, const DataStore& store,
                            std::uint64_t reserve, const StoreOptions& options);

/// Replace a `dstore` object's payload from a store, in place where it fits.
bool pto_update_store(File& file, std::uint64_t uid, const DataStore& store);
/// \see pto_update_store, with \ref StoreOptions.
bool pto_update_store(File& file, std::uint64_t uid, const DataStore& store,
                      const StoreOptions& options);

/// Record that `uid` accompanies `primary`. \see kPtoSidecarTag.
void pto_mark_sidecar(File& file, std::uint64_t uid, std::uint64_t primary);

/// Read a `dstore` object back. \throws std::runtime_error if it is not one.
void pto_read_store(const File& file, std::uint64_t uid, DataStore& out);

/*!
 * \brief \see pto_read_store, but only the named columns.
 *
 * The reason \ref read_store_into grew an overload taking both a region and a
 * column subset. An embedded store is always a region, so before that existed a
 * caller reading one had to take every column of it -- which is exactly the
 * all-or-nothing the container exists to avoid.
 *
 * Two columns out of a four-gigabyte table costs two seeks; the group tree comes
 * back whole either way, being the directory.
 */
void pto_read_store(const File& file, std::uint64_t uid, DataStore& out,
                    const std::vector<std::string>& columns);

/*!
 * \brief \see pto_read_store, for a window of rows as well as of columns.
 *
 * What a table viewer needs: paging a million-row burst table otherwise decodes
 * a million rows to show fifty. An empty `columns` means all of them.
 */
void pto_read_store(const File& file, std::uint64_t uid, DataStore& out,
                    const std::vector<std::string>& columns,
                    std::uint64_t first_row, std::uint64_t n_rows);

/*!
 * \brief Where a `dstore` object's payload lies, ready to be read.
 *
 * The seam between the container and the `.dstore` reader, made namable: a caller
 * with its own reason to reach a store hands the returned `offset` and `size`
 * to any of the region-taking \ref read_store_into overloads. Everything below
 * is this plus one call.
 *
 * \throws std::runtime_error if there is no such object, or it is not a store.
 */
PtoObject pto_store_region(const File& file, std::uint64_t uid);

/// An embedded store's column names, without reading a single column.
/// Empty if the object is not a `dstore`.
std::vector<std::string> pto_store_columns(const File& file, std::uint64_t uid,
                                           const std::string& group = "");

/// An embedded store's group paths, without reading any data.
std::vector<std::string> pto_store_groups(const File& file, std::uint64_t uid);

/*!
 * \brief One entry of a cue table: where an event ordinal sits in a payload.
 *
 * Advisory, always. A cue that is wrong must cost a slower decode and never a
 * wrong answer, which is why a reader seeks to the nearest cue *at or before*
 * what it wants and decodes forward from there.
 */
struct PtoCue {
    std::uint64_t event = 0;        ///< event ordinal within the payload
    std::uint64_t offset = 0;       ///< byte offset into the payload
    std::uint64_t time = 0;         ///< macro time at that event, 0 if unrecorded
};


/// One element of the file as \ref File::elements reports it.
struct Element {
    std::uint32_t id = 0;
    std::string name;                 ///< the element's name, or "" if unknown here
    std::uint64_t offset = 0;         ///< where its header begins
    std::uint64_t data_offset = 0;    ///< where its payload begins
    std::uint64_t size = 0;           ///< payload octets
    std::uint64_t total = 0;          ///< header plus payload
    int depth = 0;                    ///< 0 for a top-level element
};

/// One thing \ref File::verify found.
struct Problem {
    std::uint64_t offset = 0;
    std::string message;
    bool error = true;                ///< false for a warning (alignment, unknown ids)
};

/// The name of a known element id, or "" -- for tools that print a tree.
const char* element_name(std::uint32_t id);

/*!
 * \brief What \ref File::create writes into the banner by default.
 *
 * A container may outlive every program that reads it. The banner is a plain
 * text element near the head of the file saying how to decode the framing, so
 * that `strings run.pto | head` is enough to start. Applications append where
 * their reader lives.
 */
static constexpr const char* kDefaultBanner =
        "pto\n"
        "This is a .pto container -- PTO, Portable Tagged Objects: an EBML document, DocType \"pto\".\n"
        "Reader and specification: https://github.com/tpeulen/ptolib\n"
        "\n"
        "=== HOW TO DECODE THIS BINARY ===\n"
        "1. Framing: EBML Document (DocType \"pto\"). Header Magic: 0x1A45DFA3. Segment Magic: 0x18538067.\n"
        "2. VINT Integer Decoding (1-8 bytes): First byte's leading zero count N determines VINT byte width (N+1).\n"
        "   Mask highest 1-bit for sizes; preserve all bits for Element IDs.\n"
        "3. Target Payload Elements:\n"
        "   - AttachedFile (0x61A7): Container of one object.\n"
        "   - FileUID (0x46AE): 64-bit uint object handle.\n"
        "   - FileName (0x466E), PtoKind (0x1E54F001), PtoEncoding (0x1E54F002): ASCII strings.\n"
        "   - FileData (0x465C): Binary payload (8-byte aligned on disk).\n"
        "\n"
        "=== ASCII C99 DECODER PSEUDOCODE ===\n"
        "size_t read_vint(const uint8_t *b, uint64_t *v, int mask) {\n"
        "    int n = 1; uint8_t m = 0x80;\n"
        "    while ((b[0] & m) == 0) { m >>= 1; n++; }\n"
        "    *v = mask ? (b[0] & ~m) : b[0];\n"
        "    for (int i = 1; i < n; i++) *v = (*v << 8) | b[i];\n"
        "    return n;\n"
        "}\n"
        "/* Walk Segment -> Attachments -> AttachedFile (0x61A7) -> FileData (0x465C) */\n";

/*!
 * \brief A PTO file, open for reading or for writing.
 *
 * Everything except payload bytes is held in memory, which is a few kilobytes
 * for any realistic file, so listing objects and reading tags costs one open.
 * Payloads are read on demand and are never held.
 *
 * Changes are not visible until \ref commit. A crash before it leaves the file
 * exactly as it was; see the specification's account of the two SeekHeads.
 */
class File {
public:
    File();
    virtual ~File();
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    /*!
     * \brief Create an empty container, replacing anything already at `filename`.
     *
     * Takes the writer lock (see \ref open) *before* truncating, so being
     * refused cannot destroy a container someone else is writing.
     *
     * \param banner text written into the container's banner element, for
     *        whoever opens the file with no reader at hand. \ref kDefaultBanner
     *        says how to decode the framing; an application passes its own to
     *        add where a reader is found. Empty writes no banner.
     */
    bool create(const std::string& filename, const std::string& title = "",
                const std::string& banner = kDefaultBanner);

    /*!
     * \brief Open an existing one. \param writable false opens it read-only.
     *
     * \par One writer at a time
     * A writable open takes an exclusive advisory lock on the file and returns
     * false at once -- never blocking -- if another process or another
     * `File` already holds it; \ref error then says it is open for writing
     * elsewhere. Two writers each carry their own slot table, freelist and
     * generation counter and nothing is visible until \ref commit, so letting
     * both proceed means the second commit publishes an index over the first
     * writer's bytes. A read-only open never takes the lock, because a viewer
     * open during an analysis is the normal case and this format already has
     * the reader seeing the pre-commit state.
     *
     * The lock lives on the descriptor: \ref close drops it, so does a failed
     * open, and so does the process ending, however it ends.
     */
    bool open(const std::string& filename, bool writable = false);

    /*!
     * \brief How a container is open. \see open
     *
     * The three differ in what they lock and in when what they wrote becomes
     * readable, which is the distinction that matters during an acquisition.
     */
    enum class Mode {
        /*!
         * No lock. A viewer may open a container another process is measuring
         * into and will see the last committed state -- a consistent shorter
         * file, never a half-written one.
         */
        ReadOnly = 0,
        /*!
         * Exclusive advisory lock. Edits accumulate in memory and become
         * visible at \ref commit.
         */
        ReadWrite = 1,
        /*!
         * Exclusive advisory lock, and records are appended and committed as
         * they arrive. Reached through a streaming writer built on this class
         * rather than by opening directly, because a stream owns the object it
         * is filling.
         */
        Stream = 2,
    };

    /// How this container is currently open. \see Mode
    Mode mode() const;

    bool is_open() const;
    void close();
    const std::string& filename() const;

    /// Why the last call returned false, or empty.
    const std::string& error() const;

    // -- what the file says about itself --------------------------------------

    std::string title() const;
    void set_title(const std::string& s);
    std::string writing_app() const;
    /// The banner text near the head of the file, or "" if it has none.
    /// \ref compact carries it over; \ref create takes it as a parameter.
    std::string banner() const;
    void set_writing_app(const std::string& s);
    /// 16 random bytes, identifying this file across copies and renames.
    std::vector<unsigned char> uuid() const;
    /// Which commit this is. Rises by one each time; 0 for a file never committed.
    std::uint64_t generation() const;
    /// The `DocTypeVersion` the EBML header declares. This writer writes 2.
    std::uint64_t doctype_version() const;

    // -- objects ---------------------------------------------------------------

    int n_objects() const;
    /*!
     * \brief Every object, **in the order they were written**.
     *
     * The order is part of the contract, not an accident of the container
     * layout: a name is a label rather than an identity, so re-running an
     * analysis with a changed setting writes a second object with the same
     * `(kind, name)` and the older one deliberately stays reachable. Write
     * order is then the only thing that says which is which, and every reader
     * needs it to mean the same thing -- see \ref find and \ref find_all.
     */
    std::vector<PtoObject> objects() const;
    bool has(std::uint64_t uid) const;
    /// \throws std::invalid_argument if there is no such object.
    PtoObject object(std::uint64_t uid) const;
    /*!
     * \brief The **most recently written** object with this name, or 0.
     *
     * Names are labels, not identities: a container may hold several objects
     * with one name, and this resolves the tie the way a caller asking for
     * "the" object almost always means -- the newest, which is the result of
     * the latest run.
     *
     * \note This returned the *oldest* match before 2026-08-11, which silently
     *       handed back the stalest analysis in the container to whoever used
     *       the most obvious call. Use \ref find_all to see every one, and
     *       \ref objects for the full write order.
     */
    std::uint64_t find(const std::string& name) const;
    /*!
     * \brief Every object with this name, oldest first, empty if none.
     *
     * What \ref find hides. A reader that wants to compare runs, or to notice
     * that there is more than one, asks here rather than re-deriving
     * "newest wins" from \ref objects -- which is how two readers come to
     * disagree about which result a container is showing.
     */
    std::vector<std::uint64_t> find_all(const std::string& name) const;

    /*!
     * \brief Add an object, and return its UID.
     *
     * \param reserve bytes to leave after the payload so a later \ref update can
     *        grow into it without the object moving. Costs nothing but disk.
     * \return 0 on failure; see \ref error.
     */
    std::uint64_t add(const std::string& kind, const std::string& encoding,
                      const std::string& name, const unsigned char* data,
                      std::size_t n, std::uint64_t reserve = 0);


    /*!
     * \brief Replace an object's payload, keeping its UID.
     *
     * Rewrites in place when the new payload fits the room the old one had,
     * which is the point of the whole format: nothing else in the file moves,
     * whatever else is in it. When it does not fit, the object is written
     * elsewhere and the old space becomes free -- the UID survives, the offset
     * does not.
     */
    bool update(std::uint64_t uid, const unsigned char* data, std::size_t n);

    /// Drop an object. Tags targeting it are left alone, because an application
    /// may want to remember that something was there.
    bool remove(std::uint64_t uid);

    /*!
     * \brief Add an object whose payload is a file on disk.
     *
     * The mirror image of \ref extract, and the way in for something too big to
     * hold: \ref add takes a pointer and a length, so embedding a four-gigabyte
     * instrument file through it means having four gigabytes in hand first --
     * in Python, a `bytes` object the size of the file.
     *
     * This is not a second code path. \ref add already writes the header and
     * then streams the payload after it; this sizes the payload with
     * a stat and replaces that one write with a block
     * loop. Same header, same slot bookkeeping, same bytes on disk.
     *
     * \return 0 if the path is missing or unreadable, or on a write failure;
     *         see \ref error.
     */
    std::uint64_t add_file(const std::string& kind, const std::string& encoding,
                           const std::string& name, const std::string& path,
                           std::uint64_t reserve = 0);

    /*!
     * \brief Bundle a file on disk, letting the file say what it is.
     *
     * \ref add_file with \ref pto_classify_path in front of it: the caller
     * hands over a path and gets an object whose kind, encoding and media type
     * come from the file itself, named after it. The difference is who decides
     * -- `add_file` is for a caller that knows what it embedded, this is for
     * one that has a directory of files and wants them carried.
     *
     * Every argument after the path overrides what was inferred, so a caller
     * that knows better about one field does not lose the other two.
     *
     * \param name what the object is called, and what \ref disassemble writes
     *        it back out as. Defaults to the filename without its directory.
     * \return 0 if the path is missing or unreadable; see \ref error.
     */
    std::uint64_t attach(const std::string& path, const std::string& name = "",
                         const std::string& kind = "",
                         const std::string& encoding = "",
                         const std::string& media_type = "");


    /*!
     * \brief Add an object compressed with a registered codec.
     *
     * The object's encoding becomes `inner_encoding + "+" + codec` -- see
     * \ref split_encoding -- and its decoded size is recorded, so
     * \ref read hands the original bytes back and a listing can say how big
     * they are. \param level the codec's level, -1 for its default.
     * \return the uid, or 0 if the codec is not registered or failed; see
     *         \ref error.
     */
    std::uint64_t add_coded(const std::string& kind, const std::string& inner_encoding,
                            const std::string& codec, const std::string& name,
                            const unsigned char* data, std::size_t n,
                            int level = -1, std::uint64_t reserve = 0);
    /// \see update, for an object added with \ref add_coded: the new payload is
    /// compressed with the object's own codec, in place where it fits.
    bool update_coded(std::uint64_t uid, const unsigned char* data, std::size_t n,
                      int level = -1);
    /*!
     * \brief The payload, decoded.
     *
     * An object whose encoding carries a codec suffix comes back decompressed,
     * as the bytes it was added with; every other object comes back as
     * stored. \ref read_stored is the bytes on disk either way.
     *
     * \throws std::runtime_error if there is no such object, or its codec is
     *         not registered -- the message names the codec.
     */
    std::vector<unsigned char> read(std::uint64_t uid) const;
    /// The payload exactly as stored, compressed or not.
    /// \throws std::runtime_error if there is no such object.
    std::vector<unsigned char> read_stored(std::uint64_t uid) const;

    /*!
     * \brief `n` bytes of a payload, starting `at` bytes into it.
     *
     * The binding-facing half of \ref read_at: a caller pages through a payload
     * at whatever granularity suits, instead of materialising all of it to look
     * at part of it. Reads short at the end of the payload rather than
     * throwing, like a file read does. Pages the bytes as stored: a compressed
     * object is paged compressed, since a window of a compressed stream has no
     * meaning on its own -- \ref read decodes it whole.
     *
     * \throws std::runtime_error if there is no such object.
     */
    std::vector<unsigned char> read(std::uint64_t uid, std::uint64_t at,
                                    std::size_t n) const;

#ifndef SWIG
    /*!
     * \brief `n` bytes of a payload into a caller's buffer, no copy in between.
     *
     * \return how many bytes were actually read -- short at the end of the
     *         payload, and 0 for an object that is not there.
     */
    std::size_t read_at(std::uint64_t uid, std::uint64_t at,
                        void* into, std::size_t n) const;

    /*!
     * \brief Hand a payload to a sink in blocks, never holding it whole.
     *
     * The way to stream an object somewhere that is not a file -- a socket, a
     * hash, a decoder. \ref extract is this with a file-writing sink, which is
     * what it already was internally.
     *
     * \param sink called with each block in order; returning false stops the
     *        copy and makes this return false.
     */
    bool stream(std::uint64_t uid,
                const std::function<bool(const void*, std::size_t)>& sink) const;
    /// \see stream, for the bytes as stored: a coded payload arrives compressed.
    bool stream_stored(std::uint64_t uid,
                       const std::function<bool(const void*, std::size_t)>& sink) const;
#endif

    /*!
     * \brief Write an object's payload out as a file of its own.
     *
     * The way back out of the container: a measurement is saved as one `.pto`
     * holding the original instrument file and everything computed from it, and
     * this is how the instrument file becomes a `.ptu` again for something that
     * only reads those.
     *
     * Copied in blocks, so the payload is never held whole -- extracting an
     * eight-gigabyte stream costs eight gigabytes of disk and a few kilobytes
     * of memory.
     *
     * \return false if there is no such object, or the file could not be
     *         written; see \ref error.
     */
    bool extract(std::uint64_t uid, const std::string& filename) const;

    /*!
     * \brief Take the container apart: every object out into a directory.
     *
     * The way back to separate files. A measurement saved as one `.pto` holding
     * the instrument file and everything computed from it becomes a `.ptu` and
     * a table again, for tools that read only those.
     *
     * Sidecars land beside what they belong to, under their own names, which is
     * what a Becker & Hickl `.spc` needs: its reader looks for the `.set` next
     * to it on disk, and would otherwise silently read half a header.
     *
     * Each object is named by its \ref PtoObject::name, or by its UID when it has none
     * -- and when two share a name, the later ones get the UID as well, because
     * a name is a label and nothing stops two objects having the same one.
     *
     * A name is a relative path and is checked against \ref pto_object_names
     * before **anything** is written: one object that would land outside
     * `directory` fails the whole call, so a caller who sees the failure does
     * not also have half a directory. This is the check that stands between a
     * container somebody else wrote and the filesystem.
     *
     * \param on_written called with each path as it is written, for a caller
     *        that wants to report progress. Optional -- and the reason this
     *        exists: the CLI used to replicate the naming and the loop to get
     *        its progress ticks, which is how it came to be missing the check
     *        above. A second implementation is a second place to fix.
     *
     * \return the paths written, in object order. Empty if nothing could be;
     *         see \ref error for why.
     */
    std::vector<std::string> disassemble(
            const std::string& directory,
            const std::function<void(const std::string&)>& on_written =
                    std::function<void(const std::string&)>()) const;

    // -- metadata ---------------------------------------------------------------

    std::vector<PtoTag> tags() const;
    /// Tags whose target is `uid`. Pass 0 for the tags describing the file.
    std::vector<PtoTag> tags_for(std::uint64_t uid) const;
    /// Append a tag. A tag identical in every field to one already present is
    /// not appended again: re-describing an object must not make the container
    /// claim the same fact twice (a parent edge re-written on every re-run
    /// once accumulated one copy per analysis).
    void add_tag(const PtoTag& tag);
    /*!
     * \brief State a fact, replacing what was stated before.
     *
     * Removes every tag with the same `(target, name, index)`, then appends
     * `tag`. This is "the row grain IS x" as against \ref add_tag's "x is
     * also true" — the difference between the two is exactly the difference
     * between a scalar tag and an edge, and callers re-running an analysis
     * want this one for everything scalar. Tags describing other objects and
     * other names are untouched.
     */
    void set_tag(const PtoTag& tag);
    void set_tags(const std::vector<PtoTag>& tags);
    void clear_tags();
    /// Remove every tag with this `target` and `name`, any index.
    void clear_tags(std::uint64_t target, const std::string& name);

    std::vector<PtoAnnotation> annotations() const;
    void add_annotation(const PtoAnnotation& note);
    void clear_annotations();

    // -- making it stick ---------------------------------------------------------

    /*!
     * \brief Make every change since the last commit visible, atomically.
     *
     * Writes the index that is not currently live, with a higher generation and
     * a correct checksum, so a reader either sees all of this or none of it.
     */
    bool commit();

    // -- cues -----------------------------------------------------------------


    /// The cue table for an object, ascending by event. Empty when it has none.
    std::vector<PtoCue> cues(std::uint64_t uid) const;

    /// Drop an object's cues. They are also dropped when the object is removed
    /// or its payload replaced, because a cue into bytes that changed is worse
    /// than no cue at all.
    void clear_cues(std::uint64_t uid);

    /*!
     * \brief Replace an object's cue table wholesale.
     *
     * What a caller that indexed the payload itself hands back -- the
     * container knows nothing about what an "event" is, so building a cue
     * table is the application's job (a photon library walks its records to
     * do it) and storing one is this. Written on the next \ref commit.
     */
    void set_cues(std::uint64_t uid, const std::vector<PtoCue>& cues);

    /// Push buffered writes to the operating system, so a second handle on
    /// \ref File::filename sees what this one wrote.
    void flush() const;

    /// Where the EBML header begins: 0 for a plain container, more for an
    /// executable bundle that carries a stub in front of it.
    std::uint64_t ebml_offset() const;

    /*!
     * \brief Every element in the file, in order, with its depth.
     *
     * The framing only -- no payload is read -- so this is what a `tree`
     * command or an inspector shows, and what \ref verify walks.
     */
    std::vector<Element> elements() const;

    /*!
     * \brief Check the file against the format, without trusting this reader.
     *
     * Every element header must decode, no element may reach past its parent,
     * a master's children must fill it exactly, the Segment must end at the
     * end of the file, each index's CRC-32 must verify, and payloads should
     * start on an 8-octet boundary (a warning, or an error when
     * `strict_alignment`). What `pto verify` prints; empty means clean.
     */
    std::vector<Problem> verify(bool strict_alignment = false) const;

    /*!
     * \brief What a path would become inside this container. \see attach
     *
     * The default is \ref classify_by_extension: a table of names, and
     * `attachment`/`raw` for anything it does not know. An application that
     * can recognise a file by its bytes -- a photon library with format
     * sniffers -- overrides this, and \ref attach and \ref pto_bundle_files
     * then pick its answer up.
     */
    virtual PtoFileType classify(const std::string& path) const;

    /*!
     * \brief Where an object of size zero keeps its payload, if anywhere.
     *
     * A container may carry an object as a *reference* to a file beside it
     * rather than as bytes inside it; how that reference is recorded is an
     * application's vocabulary, not the container's. \ref stream and
     * \ref extract ask here when an object has no bytes of its own. The
     * default knows no such convention and returns the empty string.
     */
    virtual std::string external_payload_path(std::uint64_t uid) const;

    /// The free space in the file. For tests, and for deciding whether a file
    /// has accumulated enough holes to be worth compacting.
    std::vector<PtoExtent> free_extents() const;

    /*!
     * \brief Copy the live objects to a new file, dropping the free space.
     *
     * The only way space comes back — the same bargain HDF5 makes with
     * `h5repack`. UIDs are preserved and offsets are not, so nothing but the
     * index may hold an offset. Tags, annotations and cues come across: a cue
     * addresses a byte offset *into* a payload, and this moves payloads without
     * changing a byte inside one.
     *
     * Payloads are streamed, never held, so compacting an eight-gigabyte
     * container costs a megabyte of memory.
     *
     * The two knobs are the trade between a small file and a file that stays
     * small. Neither is right for everyone, which is why neither is the only
     * behaviour:
     *
     * \param tight drop the padding that puts each payload on an 8-byte
     *        boundary as well, so the result carries no reclaimable `Void` at
     *        all. The file is as small as the format allows and its payloads
     *        can no longer be mapped and used in place. For an archive or a
     *        copy that is about to be sent somewhere; alignment is a SHOULD, so
     *        the result is still conformant. \see \ref pto_align.
     * \param reserve room to leave after every object, as a fraction of its
     *        payload — 0.25 gives a 4 MiB table a megabyte to grow into. The
     *        opposite trade: a bigger file that absorbs the next few updates
     *        without relocating anything, which is what a container being
     *        edited wants. Default 0, which is what a container being archived
     *        wants.
     *
     * The default is neither: holes gone, payloads aligned, nothing reserved.
     */
    bool compact(const std::string& to, bool tight = false, double reserve = 0.0);

protected:
    /// Record why a call in a derived class failed, so \ref error reports it
    /// the way it reports the base class's own failures.
    void set_error(const std::string& why);

private:
    struct Impl;
    Impl* p_;

    // These three put a DataStore straight into the container, which needs the
    // layout, not the public API: a photon stream is serialised into the file
    // where it will live rather than into a buffer first.
    friend std::uint64_t pto_add_store(File&, const std::string&,
                                       const std::string&, const DataStore&,
                                       std::uint64_t);   // defaults: see above
    friend std::uint64_t pto_add_store(File&, const std::string&,
                                       const std::string&, const DataStore&,
                                       std::uint64_t, const StoreOptions&);
    friend bool pto_update_store(File&, std::uint64_t, const DataStore&);
    friend bool pto_update_store(File&, std::uint64_t, const DataStore&,
                                 const StoreOptions&);
    friend void pto_read_store(const File&, std::uint64_t, DataStore&);
    // The rest of the store entry points need no friendship: they go through
    // pto_store_region and filename(), which is the whole point of it existing.
    friend PtoObject pto_store_region(const File&, std::uint64_t);
};
/*!
 * \brief Bundle files and directories into an open container, one object each.
 *
 * The way a measurement scattered over a directory becomes one file: the
 * instrument file, its settings sidecar, the analysis that produced the burst
 * table, the protocol PDF and the note somebody left. Each keeps its name, so
 * \ref File::disassemble puts the directory back as it was.
 *
 * \par What it does that a loop over \ref File::attach does not
 * - **A directory means everything under it**, recursively, with each object
 *   named by its path relative to that directory -- `raw/m001.ptu`, not
 *   `m001.ptu` -- so two files of the same name in different folders stay two
 *   files. Entries are visited in sorted order, so the same directory bundles
 *   to the same object order twice running.
 * - **A `.set` is tied to the `.spc` it belongs to** with \ref
 *   kPtoSidecarTag, which is what makes the pair readable afterwards: a
 *   Becker & Hickl reader handed the `.spc` alone silently reads half a header.
 *
 * Nothing is committed. The caller decides when the container becomes visible,
 * because bundling is usually one step of building it -- see \ref
 * File::commit.
 *
 * \param link_sidecars false to bundle a `.set` as a plain object, for a caller
 *        that wants to say what accompanies what itself.
 * \return the objects made, in the order they were written. Short of `paths`
 *         if something could not be read; \ref File::error says what.
 * \throws std::runtime_error if a directory cannot be walked to the end. A walk
 *         that stopped early would bundle some of a directory and report that
 *         it bundled the directory, which is the one outcome nobody could
 *         detect afterwards.
 */
std::vector<PtoObject> pto_bundle_files(File& file,
                                        const std::vector<std::string>& paths,
                                        bool link_sidecars = true);
/// True for a file that begins with an EBML header whose DocType is "pto".
/// Silent on any input, including a missing file.
bool is_pto_file(const std::string& filename);
}  // namespace pto

// ===========================================================================
// ===========================================================================
//                              IMPLEMENTATION
// ===========================================================================
// ===========================================================================
#endif  // PTOLIB_H

// The implementation lives outside the include guard above, so that a
// translation unit which has already included the header without
// PTOLIB_IMPLEMENTATION -- a unity build, or a source that includes it before
// defining the macro -- still gets the implementation when it asks for it,
// and gets it exactly once.
#if defined(PTOLIB_IMPLEMENTATION) && !defined(PTOLIB_IMPLEMENTATION_DONE)
#define PTOLIB_IMPLEMENTATION_DONE
// The JSON header is only needed here, so its default is only set here: a
// translation unit that includes the declarations first and defines its own
// PTOLIB_JSON_INCLUDE before the implementation include is not second-guessed.
#ifndef PTOLIB_JSON_INCLUDE
#define PTOLIB_JSON_INCLUDE <nlohmann/json.hpp>
#endif

#include PTOLIB_JSON_INCLUDE

#include <cerrno>
#include <cctype>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <utility>

#include <system_error>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
#  include <share.h>
#  include <sys/stat.h>
#else
#  include <dirent.h>
#  include <fcntl.h>
#  include <sys/file.h>
#  include <unistd.h>
#endif

namespace pto {
namespace detail {

#ifdef _WIN32
#define PTOLIB_FSEEK64(fp, off, whence) _fseeki64((fp), static_cast<__int64>(off), (whence))
#define PTOLIB_FTELL64(fp) _ftelli64(fp)
inline std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), (int)s.size(), nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}
/// fopen taking a UTF-8 path. _SH_DENYNO because a container is legitimately
/// read through a second handle while another handle holds it.
inline std::FILE* fopen_utf8(const std::string& path, const char* mode, bool report = true) {
    std::wstring wpath = utf8_to_wide(path);
    std::wstring wmode = utf8_to_wide(std::string(mode ? mode : "rb"));
    std::FILE* f = nullptr;
#if defined(_MSC_VER)
    f = _wfsopen(wpath.c_str(), wmode.c_str(), _SH_DENYNO);
#else
    f = _wfopen(wpath.c_str(), wmode.c_str());
#endif
    if (!f && report) std::cerr << "Error opening file: " << path << std::endl;
    return f;
}
#else
#define PTOLIB_FSEEK64(fp, off, whence) fseeko((fp), static_cast<off_t>(off), (whence))
#define PTOLIB_FTELL64(fp) ftello(fp)
inline std::FILE* fopen_utf8(const std::string& path, const char* mode, bool report = true) {
    std::FILE* f = std::fopen(path.c_str(), mode);
    if (!f && report) std::cerr << "Error opening file: " << path << std::endl;
    return f;
}
#endif

/// Atomic rename-over: what makes "write to a temporary, then rename" safe.
inline bool replace_file(const std::string& from, const std::string& to) {
#ifdef _WIN32
    return MoveFileExW(utf8_to_wide(from).c_str(), utf8_to_wide(to).c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0;
#else
    return std::rename(from.c_str(), to.c_str()) == 0;
#endif
}

inline std::string lowered(std::string t) {
    for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return t;
}


// ---------------------------------------------------------------------------
// A small filesystem, so the header compiles as C++14 and on a
// macOS floor below 10.15, where std::filesystem is not available. Only the
// operations the container needs, with std::filesystem's names and shapes so
// the call sites read the same.
// ---------------------------------------------------------------------------
namespace fs {

#ifdef _WIN32
inline bool is_sep(char c) { return c == '/' || c == '\\'; }
#else
inline bool is_sep(char c) { return c == '/'; }
#endif

class path {
public:
    path() {}
    path(const std::string& s) : s_(s) {}       // NOLINT: implicit, like std's
    path(const char* s) : s_(s ? s : "") {}
    const std::string& string() const { return s_; }
    std::string generic_string() const {
        std::string g = s_;
        for (char& c : g) if (c == '\\') c = '/';
        return g;
    }
    bool empty() const { return s_.empty(); }
    /// Everything after the last separator.
    path filename() const {
        std::size_t i = s_.size();
        while (i > 0 && !is_sep(s_[i - 1])) i--;
        return path(s_.substr(i));
    }
    /// Everything before the last separator, without it; "/" stays "/".
    path parent_path() const {
        std::size_t i = s_.size();
        while (i > 0 && !is_sep(s_[i - 1])) i--;
        if (i == 0) return path();
        std::size_t j = i;
        while (j > 1 && is_sep(s_[j - 1])) j--;   // collapse trailing separators, keep a root
        return path(s_.substr(0, j));
    }
    /// The last dot of the filename onwards; none for ".", ".." and dot-files.
    path extension() const {
        const std::string f = filename().string();
        if (f == "." || f == "..") return path();
        const std::size_t d = f.rfind('.');
        if (d == std::string::npos || d == 0) return path();
        return path(f.substr(d));
    }
    path stem() const {
        const std::string f = filename().string();
        const std::string e = extension().string();
        return path(f.substr(0, f.size() - e.size()));
    }
    bool is_absolute() const {
        if (s_.empty()) return false;
        if (is_sep(s_[0])) return true;
#ifdef _WIN32
        if (s_.size() >= 3 && std::isalpha(static_cast<unsigned char>(s_[0])) && s_[1] == ':' && is_sep(s_[2]))
            return true;
#endif
        return false;
    }
    path operator/(const path& rhs) const {
        if (rhs.is_absolute() || s_.empty()) return rhs;
        if (rhs.empty()) return *this;
        if (is_sep(s_[s_.size() - 1])) return path(s_ + rhs.s_);
        return path(s_ + "/" + rhs.s_);
    }
    bool operator<(const path& o) const { return s_ < o.s_; }
    bool operator==(const path& o) const { return s_ == o.s_; }
    bool operator!=(const path& o) const { return s_ != o.s_; }

private:
    std::string s_;
};

inline path u8path(const std::string& s) { return path(s); }

enum copy_options { none = 0, overwrite_existing = 1 };

#ifdef _WIN32
inline bool stat_of(const path& p, struct _stat64* st) {
    return _wstat64(utf8_to_wide(p.string()).c_str(), st) == 0;
}
typedef struct _stat64 stat_t;
#else
inline bool stat_of(const path& p, struct stat* st) { return ::stat(p.string().c_str(), st) == 0; }
typedef struct stat stat_t;
#endif

inline bool exists(const path& p) { stat_t st; return stat_of(p, &st); }

inline bool is_directory(const path& p, std::error_code& ec) {
    stat_t st;
    ec.clear();
    if (!stat_of(p, &st)) { ec = std::error_code(errno, std::generic_category()); return false; }
    return (st.st_mode & S_IFMT) == S_IFDIR;
}

inline bool is_regular_file(const path& p, std::error_code& ec) {
    stat_t st;
    ec.clear();
    if (!stat_of(p, &st)) { ec = std::error_code(errno, std::generic_category()); return false; }
    return (st.st_mode & S_IFMT) == S_IFREG;
}

inline std::uintmax_t file_size(const path& p, std::error_code& ec) {
    stat_t st;
    ec.clear();
    if (!stat_of(p, &st)) {
        ec = std::error_code(errno, std::generic_category());
        return static_cast<std::uintmax_t>(-1);
    }
    return static_cast<std::uintmax_t>(st.st_size);
}

inline std::uintmax_t file_size(const path& p) {
    std::error_code ec;
    const std::uintmax_t n = file_size(p, ec);
    if (ec) throw std::runtime_error("cannot size " + p.string() + ": " + ec.message());
    return n;
}

inline bool make_one_directory(const path& p) {
#ifdef _WIN32
    return _wmkdir(utf8_to_wide(p.string()).c_str()) == 0 || errno == EEXIST;
#else
    return ::mkdir(p.string().c_str(), 0777) == 0 || errno == EEXIST;
#endif
}

/// Every missing directory on the way to `p`. True if it exists afterwards.
inline bool create_directories(const path& p, std::error_code& ec) {
    ec.clear();
    if (p.empty()) return false;
    std::error_code probe;
    if (is_directory(p, probe)) return true;
    const path parent = p.parent_path();
    if (!parent.empty() && parent != p && !create_directories(parent, ec)) return false;
    if (!make_one_directory(p)) { ec = std::error_code(errno, std::generic_category()); return false; }
    return true;
}

/// Do the two names reach the same file? By identity, not by spelling.
inline bool equivalent(const path& a, const path& b, std::error_code& ec) {
    ec.clear();
    stat_t sa, sb;
    if (!stat_of(a, &sa) || !stat_of(b, &sb)) {
        ec = std::error_code(errno, std::generic_category());
        return false;
    }
#ifdef _WIN32
    // st_ino is 0 on Windows; compare the resolved full paths instead.
    wchar_t fa[4096], fb[4096];
    const DWORD na = GetFullPathNameW(utf8_to_wide(a.string()).c_str(), 4096, fa, nullptr);
    const DWORD nb = GetFullPathNameW(utf8_to_wide(b.string()).c_str(), 4096, fb, nullptr);
    if (na == 0 || nb == 0 || na >= 4096 || nb >= 4096) return false;
    return _wcsicmp(fa, fb) == 0;
#else
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
#endif
}

inline bool copy_file(const path& from, const path& to, copy_options opts, std::error_code& ec) {
    ec.clear();
    if (opts != overwrite_existing && exists(to)) {
        ec = std::make_error_code(std::errc::file_exists);
        return false;
    }
    std::FILE* in = fopen_utf8(from.string(), "rb", false);
    if (in == nullptr) { ec = std::error_code(errno, std::generic_category()); return false; }
    std::FILE* out = fopen_utf8(to.string(), "wb", false);
    if (out == nullptr) { ec = std::error_code(errno, std::generic_category()); std::fclose(in); return false; }
    std::vector<unsigned char> buf(1u << 20);
    bool ok = true;
    for (;;) {
        const std::size_t n = std::fread(buf.data(), 1, buf.size(), in);
        if (n == 0) break;
        if (std::fwrite(buf.data(), 1, n, out) != n) { ok = false; break; }
    }
    if (std::ferror(in)) ok = false;
    std::fclose(in);
    if (std::fclose(out) != 0) ok = false;
    if (!ok) ec = std::make_error_code(std::errc::io_error);
    return ok;
}

inline std::vector<std::string> split_path(const std::string& s) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s) {
        if (is_sep(c)) { if (!cur.empty()) parts.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

/// `p` spelled from `base`, lexically: the common prefix dropped and a ".."
/// per remaining component of `base`. Both are taken as given, without
/// resolving symlinks -- what a directory walk needs, where `p` lies under
/// `base` by construction.
inline path relative(const path& p, const path& base, std::error_code& ec) {
    ec.clear();
    std::vector<std::string> a = split_path(p.string()), b = split_path(base.string());
    std::size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) i++;
    std::string out;
    for (std::size_t k = i; k < b.size(); k++) { if (b[k] == ".") continue; out += out.empty() ? ".." : "/.."; }
    for (std::size_t k = i; k < a.size(); k++) { if (a[k] == ".") continue; out += out.empty() ? a[k] : "/" + a[k]; }
    if (out.empty()) out = ".";
    return path(out);
}

/// Every regular file under `dir`, recursively. Sets `ec` and returns false
/// if a directory on the way could not be read, so a caller can refuse a
/// partial listing.
inline bool list_regular_files(const path& dir, std::vector<path>& out, std::error_code& ec) {
    ec.clear();
#ifdef _WIN32
    WIN32_FIND_DATAW fd;
    const std::wstring pattern = utf8_to_wide((dir / path("*")).string());
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) { ec = std::error_code(static_cast<int>(GetLastError()), std::system_category()); return false; }
    do {
        int n = WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, nullptr, 0, nullptr, nullptr);
        std::string name(n > 0 ? n - 1 : 0, '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, &name[0], n, nullptr, nullptr);
        if (name == "." || name == "..") continue;
        const path child = dir / path(name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!list_regular_files(child, out, ec)) { FindClose(h); return false; }
        } else {
            out.push_back(child);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#else
    DIR* d = ::opendir(dir.string().c_str());
    if (d == nullptr) { ec = std::error_code(errno, std::generic_category()); return false; }
    while (struct dirent* e = ::readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        const path child = dir / path(name);
        std::error_code kind_ec;
        if (is_directory(child, kind_ec)) {
            if (!list_regular_files(child, out, ec)) { ::closedir(d); return false; }
        } else if (is_regular_file(child, kind_ec)) {
            out.push_back(child);
        }
    }
    ::closedir(d);
#endif
    return true;
}

}  // namespace fs

}  // namespace detail
}  // namespace pto

#define fseek64 PTOLIB_FSEEK64
#define ftell64 PTOLIB_FTELL64
#define open_file ::pto::detail::fopen_utf8
#define utf8_to_wide_path ::pto::detail::utf8_to_wide


// ===========================================================================
// DataStore implementation
// ===========================================================================
namespace pto {

[[noreturn]] void datastore_index_out_of_range(std::size_t index, std::size_t size) {
    throw std::out_of_range(
            "DataStore: row index " + std::to_string(index) +
            " is out of range for a column with " + std::to_string(size) +
            " rows.");
}


// --- column description ---------------------------------------------------------
//
// A column carries one extensible description rather than a growing list of
// members, and `name` is an attribute of it. Parsing happens here, on the way
// in, so that `name()` and `units()` stay references to stored strings: lookup
// is the hot path, and a store is asked for a column far more often than it is
// told about one.

namespace {

/// Read one string attribute out of a JSON object, or "" if it is not there.
std::string attribute_of(const std::string& blob, const std::string& key) {
    if (blob.empty()) return std::string();
    const nlohmann::json j = nlohmann::json::parse(blob, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::string();
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) return std::string();
    return it->is_string() ? it->get<std::string>() : it->dump();
}

/// Set one attribute of a JSON object held as text, creating it if need be.
std::string with_attribute(const std::string& blob, const std::string& key,
                           const nlohmann::json& value) {
    nlohmann::json j = nlohmann::json::object();
    if (!blob.empty()) {
        nlohmann::json parsed = nlohmann::json::parse(blob, nullptr, false);
        if (!parsed.is_discarded() && parsed.is_object()) j = std::move(parsed);
    }
    if (value.is_null()) j.erase(key);
    else j[key] = value;
    return j.empty() ? std::string() : j.dump();
}

/// The string overload, where "" erases rather than storing an empty string.
std::string with_attribute(const std::string& blob, const std::string& key,
                           const std::string& value) {
    return with_attribute(blob, key,
                          value.empty() ? nlohmann::json() : nlohmann::json(value));
}

/*!
 * \brief The `na` attribute as ranges, or empty when there is none.
 *
 * Two spellings are read and one is written. `{"rows":[a,b],"why":"..."}` is
 * what this library writes, because the reason is the point. `[a,b]` is
 * accepted because it is what a person types by hand, and refusing it would
 * make the shorter form a silent no-op rather than an error.
 *
 * Anything else is ignored rather than rejected: `na` is one key of a free-form
 * description, and a caller who put something else under that name should not
 * find their column unreadable.
 */
std::vector<NaRange> na_ranges_of(const std::string& blob) {
    std::vector<NaRange> out;
    if (blob.empty()) return out;
    const nlohmann::json j = nlohmann::json::parse(blob, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return out;
    const auto it = j.find("na");
    if (it == j.end() || !it->is_array()) return out;

    for (const nlohmann::json& e : *it) {
        NaRange r;
        const nlohmann::json* rows = nullptr;
        if (e.is_array()) {
            rows = &e;
        } else if (e.is_object()) {
            const auto f = e.find("rows");
            if (f != e.end()) rows = &(*f);
            const auto w = e.find("why");
            if (w != e.end() && w->is_string()) r.why = w->get<std::string>();
        }
        if (rows == nullptr || !rows->is_array() || rows->size() != 2) continue;
        if (!(*rows)[0].is_number_unsigned() || !(*rows)[1].is_number_unsigned()) continue;
        r.first = (*rows)[0].get<std::size_t>();
        r.last = (*rows)[1].get<std::size_t>();
        if (r.last > r.first) out.push_back(r);
    }
    return out;
}

}  // namespace

std::vector<unsigned char> metadata_to_msgpack(const std::string& json_text) {
    if (json_text.empty()) return std::vector<unsigned char>();
    const nlohmann::json parsed = nlohmann::json::parse(json_text, nullptr, false);
    if (parsed.is_discarded())
        throw std::invalid_argument("column metadata is not JSON: " + json_text);
    return nlohmann::json::to_msgpack(parsed);
}

std::string metadata_from_msgpack(const unsigned char* bytes, std::size_t n) {
    if (bytes == nullptr || n == 0) return std::string();
    const nlohmann::json parsed =
        nlohmann::json::from_msgpack(bytes, bytes + n, true, false);
    if (parsed.is_discarded() || !parsed.is_object()) return std::string();
    return parsed.dump();
}

void Column::set_metadata(const std::string& json) {
    if (json.empty()) {
        metadata_.clear();
        units_.clear();
        na_.clear();
        return;
    }
    const nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);
    if (parsed.is_discarded())
        throw std::invalid_argument("column metadata is not JSON: " + json);
    if (!parsed.is_object())
        throw std::invalid_argument("column metadata must be a JSON object, got " +
                                    std::string(parsed.type_name()));
    metadata_ = parsed.dump();
    units_ = attribute_of(metadata_, "units");
    na_ = na_ranges_of(metadata_);
    const std::string named = attribute_of(metadata_, "name");
    if (!named.empty()) name_ = named;
}

void Column::set_name(std::string s) {
    name_ = std::move(s);
    // Only recorded in the description when there is one to record it in: a
    // column that was never described should not acquire a description merely
    // by being named, or every column in every file grows a JSON object.
    if (!metadata_.empty()) metadata_ = with_attribute(metadata_, "name", name_);
}

void Column::set_units(std::string s) {
    units_ = std::move(s);
    metadata_ = with_attribute(metadata_, "units", units_);
    if (!metadata_.empty() && !name_.empty())
        metadata_ = with_attribute(metadata_, "name", name_);
}

std::string Column::attribute(const std::string& key) const {
    return attribute_of(metadata_, key);
}

void Column::set_attribute(const std::string& key, const std::string& value) {
    metadata_ = with_attribute(metadata_, key, value);
    if (key == "units") units_ = value;
    if (key == "name" && !value.empty()) name_ = value;
    // A string value is never a range list, so this clears rather than parses --
    // set_attribute("na", "[[2,4]]") stores the characters, and the column has
    // no missing rows as a result of it.
    if (key == "na") na_ = na_ranges_of(metadata_);
}

std::string Column::attribute_json(const std::string& key) const {
    if (metadata_.empty()) return std::string();
    const nlohmann::json j = nlohmann::json::parse(metadata_, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::string();
    const auto it = j.find(key);
    if (it == j.end()) return std::string();
    return it->dump();
}

void Column::set_attribute_json(const std::string& key,
                                const std::string& json_value) {
    nlohmann::json value;                       // null, which erases
    if (!json_value.empty()) {
        value = nlohmann::json::parse(json_value, nullptr, false);
        if (value.is_discarded())
            throw std::invalid_argument("attribute '" + key +
                                        "' is not JSON: " + json_value);
    }
    metadata_ = with_attribute(metadata_, key, value);
    // The cached attributes stay in step however they were set. A non-string
    // value for either name is not what they mean, so it caches as empty rather
    // than as the text of a number.
    if (key == "units") units_ = value.is_string() ? value.get<std::string>() : std::string();
    if (key == "name" && value.is_string()) name_ = value.get<std::string>();
    if (key == "na") na_ = na_ranges_of(metadata_);
}

void Column::add_na_range(std::size_t first, std::size_t last,
                          const std::string& why) {
    if (last <= first) return;
    // A column that already carries bits gets bits. Two representations in one
    // column would put both paths in every reader for no gain -- and the bits
    // are already allocated, so the range would save nothing anyway.
    if (!mask_.empty()) {
        materialise_mask(n_);
        const std::size_t end = last < n_ ? last : n_;
        for (std::size_t i = first; i < end; i++) mask_.set(i, false);
        return;
    }

    nlohmann::json entry = nlohmann::json::object();
    entry["rows"] = nlohmann::json::array({first, last});
    if (!why.empty()) entry["why"] = why;

    nlohmann::json list = nlohmann::json::array();
    const std::string existing = attribute_json("na");
    if (!existing.empty()) {
        nlohmann::json parsed = nlohmann::json::parse(existing, nullptr, false);
        if (!parsed.is_discarded() && parsed.is_array()) list = std::move(parsed);
    }
    list.push_back(entry);
    metadata_ = with_attribute(metadata_, "na", list);
    na_ = na_ranges_of(metadata_);
}

// --- combining and subsetting ---------------------------------------------

void DataStore::append_rows(const DataStore& other, Join join) {
    if (&other == this)
        throw std::invalid_argument("append_rows: a store cannot be appended to itself");

    const std::size_t before = n_rows_, added = other.n_rows_;

    // Types first, over every column both sides have, so a conflict is found
    // BEFORE anything has been appended. Half-appending and then throwing would
    // leave a store no caller could put back.
    for (const Column& c : other.columns_) {
        const int i = find(c.name());
        if (i >= 0 && columns_[i].type() != c.type())
            throw std::invalid_argument(
                    "append_rows: column '" + c.name() + "' is " +
                    column_type_name(columns_[i].type()) + " here and " +
                    column_type_name(c.type()) + " there");
    }

    if (join == Join::Inner) {
        // Drop what the other side does not have, before appending, so the
        // result is the intersection rather than the union with holes in it.
        for (int i = n_columns() - 1; i >= 0; i--)
            if (other.find(columns_[i].name()) < 0) remove_column(i);
    }

    // The label is what a range can say and a zero bit cannot: which table did
    // not have this column. A store with no label falls back to saying that
    // much, since "absent in <nothing>" would read as a bug.
    const std::string source = other.label().empty()
            ? std::string("a store with no label")
            : ("'" + other.label() + "'");

    for (Column& mine : columns_) {
        const int j = other.find(mine.name());
        if (j >= 0) mine.append_from(other.column(j));
        // Outer; Inner dropped it above
        else mine.append_missing(added, "absent in " + source);
    }

    if (join == Join::Outer) {
        for (const Column& c : other.columns_) {
            if (find(c.name()) >= 0) continue;
            // New to us: the rows we already had were never measured for it.
            const int k = add_column(c.name(), c.type());
            columns_[k].set_metadata(c.metadata());
            columns_[k].append_missing(
                    before, "absent in " + (label_.empty()
                            ? std::string("a store with no label") : "'" + label_ + "'"));
            columns_[k].append_from(c);
        }
    }

    n_rows_ = before + added;
    // A gate over the old rows says nothing about the new ones, and silently
    // extending it either way would be a guess. Clearing is the honest move and
    // it is what select_all() means.
    row_mask_.clear();
}

void DataStore::append_columns(const DataStore& other, OnDuplicate on_duplicate) {
    if (&other == this)
        throw std::invalid_argument("append_columns: a store cannot be appended to itself");
    if (other.n_rows_ != n_rows_)
        throw std::invalid_argument(
                "append_columns: " + std::to_string(n_rows_) + " rows here and " +
                std::to_string(other.n_rows_) + " there; these describe different rows");

    if (on_duplicate == OnDuplicate::Refuse) {
        for (const Column& c : other.columns_)
            if (find(c.name()) >= 0)
                throw std::invalid_argument(
                        "append_columns: both stores have a column '" + c.name() +
                        "'; pass keep-first to keep this one");
    }

    for (const Column& c : other.columns_) {
        if (find(c.name()) >= 0) continue;        // KeepFirst
        const int k = add_column(c.name(), c.type());
        columns_[k].set_metadata(c.metadata());
        columns_[k].append_from(c);
    }
}

void DataStore::take_into(DataStore& out, const int* take_rows, int n_take_rows) const {
    const std::size_t n = n_take_rows < 0 ? 0 : static_cast<std::size_t>(n_take_rows);
    for (std::size_t k = 0; k < n; k++)
        if (take_rows[k] < 0 || static_cast<std::size_t>(take_rows[k]) >= n_rows_)
            throw std::invalid_argument(
                    "take: row " + std::to_string(take_rows[k]) + " is outside a store of " +
                    std::to_string(n_rows_) + " rows");

    out.release();
    out.set_label(label_);
    for (const Column& c : columns_) {
        const int k = out.add_column(c.name(), c.type());
        out.columns_[k].take_from(c, take_rows, n);
    }
    out.n_rows_ = n;
    // No row mask: the result IS the selection. Copying the old one across
    // would gate the gate.
}

void DataStore::compact_into(DataStore& out) const {
    std::vector<int> rows;
    rows.reserve(n_selected());
    for (std::size_t i = 0; i < n_rows_; i++)
        if (row_selected(i)) rows.push_back(static_cast<int>(i));
    take_into(out, rows.empty() ? nullptr : rows.data(), static_cast<int>(rows.size()));
}

void DataStore::copy_into(DataStore& out) const {
    // The copy constructor is the whole implementation; assigning through it
    // keeps one definition of what "a copy of a store" means rather than a
    // second walk of the tree that has to be kept in step with it.
    out = *this;
}

/*!
 * The single registry instance.
 *
 * Out of line on purpose: as an inline function-local static it was duplicated
 * between the core library and the Python extension, because the extension is
 * built with hidden visibility and the symbol could not be merged. Stores
 * created on one side were then invisible to a listing taken from the other.
 */
DataStoreRegistry& DataStoreRegistry::instance() {
    static DataStoreRegistry r;
    return r;
}

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <vector>

namespace {

//! Widening an Int64/UInt64 column to double is exact only to 2^53. Beyond
//! that an `==` would match the wrong rows, so refuse rather than answer
//! wrongly.
void check_exactly_representable(const pto::Column& c,
                                 const std::string& name, std::size_t n) {
    const double limit = 9007199254740992.0;  // 2^53
    if (c.type() == pto::ColumnType::Int64) {
        const std::int64_t* v = c.i64_ptr();
        for (std::size_t i = 0; i < n; ++i) {
            if (std::fabs(static_cast<double>(v[i])) > limit) {
                throw std::invalid_argument(
                    "select_expression: column '" + name +
                    "' holds Int64 values beyond 2^53, which double-precision "
                    "evaluation cannot represent exactly");
            }
        }
    } else if (c.type() == pto::ColumnType::UInt64) {
        // The accessor's own type: `std::uint64_t` is `unsigned long` on Linux
        // and `unsigned long long` on macOS, so naming it here would compile on
        // one and not the other.
        const unsigned long long* v = c.u64_ptr();
        for (std::size_t i = 0; i < n; ++i) {
            if (static_cast<double>(v[i]) > limit) {
                throw std::invalid_argument(
                    "select_expression: column '" + name +
                    "' holds UInt64 values beyond 2^53, which double-precision "
                    "evaluation cannot represent exactly");
            }
        }
    }
}

//! Whether the engine can read this column's memory directly.
/*! Bool is bit-packed and String is dictionary codes; neither is a numeric
    buffer the block loader can walk. Such a column is widened into the
    program's own double buffer instead -- correct, one copy slower, and both
    are rare in a gate. */
bool engine_readable(pto::ColumnType t) {
    return t != pto::ColumnType::Bool &&
           t != pto::ColumnType::String;
}

pto::ExprScalarType expr_type_of(pto::ColumnType t) {
    using pto::ColumnType;
    using pto::ExprScalarType;
    switch (t) {
        case ColumnType::Float64: return ExprScalarType::Float64;
        case ColumnType::Float32: return ExprScalarType::Float32;
        case ColumnType::Int64: return ExprScalarType::Int64;
        case ColumnType::Int32: return ExprScalarType::Int32;
        case ColumnType::Int16: return ExprScalarType::Int16;
        case ColumnType::Int8: return ExprScalarType::Int8;
        case ColumnType::UInt64: return ExprScalarType::UInt64;
        case ColumnType::UInt32: return ExprScalarType::UInt32;
        case ColumnType::UInt16: return ExprScalarType::UInt16;
        default: return ExprScalarType::UInt8;
    }
}

//! Widen one column into a double buffer through its own typed pointer.
/*! Used for the columns the engine cannot read in place (Bool, String); the
    engine-readable types convert a block at a time as the block loader walks
    them and never come here. The obvious loop, `value_at(i)` per row,
    dispatches on the column's type once per element, which for a few hundred
    thousand rows costs far more than the arithmetic it feeds. */
template<typename T>
inline void widen_typed(const T* v, std::size_t n, double* dst) {
    for (std::size_t i = 0; i < n; ++i) dst[i] = static_cast<double>(v[i]);
}

void widen_column(const pto::Column& c, std::size_t n, double* dst) {
    using pto::ColumnType;
    switch (c.type()) {
        case ColumnType::Float64: widen_typed(c.f64_ptr(), n, dst); break;
        case ColumnType::Float32: widen_typed(c.f32_ptr(), n, dst); break;
        case ColumnType::Int64:   widen_typed(c.i64_ptr(), n, dst); break;
        case ColumnType::Int32:   widen_typed(c.i32_ptr(), n, dst); break;
        case ColumnType::Int16:   widen_typed(c.i16_ptr(), n, dst); break;
        case ColumnType::Int8:    widen_typed(c.i8_ptr(),  n, dst); break;
        case ColumnType::UInt64:  widen_typed(c.u64_ptr(), n, dst); break;
        case ColumnType::UInt32:  widen_typed(c.u32_ptr(), n, dst); break;
        case ColumnType::UInt16:  widen_typed(c.u16_ptr(), n, dst); break;
        case ColumnType::UInt8:   widen_typed(c.u8_ptr(),  n, dst); break;
        default:
            // Bool and String have no numeric buffer to walk; the generic
            // accessor is right for them and they are short cases anyway.
            for (std::size_t i = 0; i < n; ++i) dst[i] = c.value_at(i);
            break;
    }
}

}  // namespace

//! A compiled query, and whatever the evaluator that took it needs.
/*! Held so a repeated gate -- which is what an interactive selection is --
    costs only its evaluation. */
struct DataStore::ExpressionProgram {
    //! The block-vectorised program.
    ExpressionEngine engine;
    //! Column index per free name, in the order the engine lists them.
    std::vector<int> indices;
    //! Rebuilt per evaluation: a column's buffer moves when it is resized.
    std::vector<ExprColumn> columns;

    //! Per-name widening buffer, filled only for the columns the engine
    //! cannot read in place (Bool, String).
    std::vector<std::vector<double> > values;

    std::size_t n = 0;
    //! The types the plan was built for. A column replaced by one of another
    //! type changes which evaluator is right, so the plan is rebuilt rather
    //! than reused against memory it would now read wrongly.
    std::vector<ColumnType> types;
};

namespace {

bool same_types(const std::vector<ColumnType>& want,
                const std::vector<ColumnType>& got) {
    return want.size() == got.size() &&
           std::equal(want.begin(), want.end(), got.begin());
}

}  // namespace

void DataStore::clear_expression_cache() const { expression_cache_.clear(); }

BitMask DataStore::expression_mask(const std::string& expr) const {
    BitMask m(n_rows_, false);
    if (n_rows_ == 0) return m;

    // The types of the columns this query would read now, so a plan built
    // against a column that has since been replaced by one of another type is
    // rebuilt rather than reused.
    std::vector<ColumnType> types_now;
    if (expression_cache_.count(expr)) {
        const std::shared_ptr<ExpressionProgram>& cached = expression_cache_[expr];
        if (cached) {
            types_now.reserve(cached->indices.size());
            for (int index : cached->indices) types_now.push_back(column(index).type());
        }
    }

    std::shared_ptr<ExpressionProgram>& slot = expression_cache_[expr];
    if (!slot || slot->n != n_rows_ || !same_types(slot->types, types_now)) {
        const std::string translated = ExpressionEngine::normalise(expr);
        const std::vector<std::string> names =
            ExpressionEngine::free_variables(translated);

        std::shared_ptr<ExpressionProgram> program =
            std::make_shared<ExpressionProgram>();
        program->n = n_rows_;
        program->indices.reserve(names.size());
        for (const std::string& name : names) {
            const int i = find(name);
            if (i < 0) {
                throw std::invalid_argument("select_expression: unknown column '" +
                                        name + "'");
            }
            program->indices.push_back(i);
            program->types.push_back(column(i).type());
        }
        // compile() reports rather than throws; a query the engine will not
        // take is refused here, loudly. There is no second evaluator to fall
        // through to -- the ExprTk fallback silently answered multi-argument
        // functions from element 0, and a gate that keeps the wrong rows is
        // worse than one that refuses (T-20260831-13).
        if (!program->engine.compile(expr) ||
            program->engine.variables().size() != names.size()) {
            throw std::invalid_argument(
                "select_expression: cannot compile '" + expr +
                "': the expression uses syntax or a function the evaluator "
                "does not implement");
        }
        program->values.resize(names.size());
        slot = program;
    }

    ExpressionProgram& program = *slot;
    // A column shorter than the store is read only as far as it goes; the rows
    // past its end have no value and so cannot satisfy a condition. Their bits
    // stay clear, which is what the mask was built with.
    std::size_t rows = n_rows_;
    for (int index : program.indices) {
        rows = std::min(rows, column(index).size());
    }
    if (rows == 0) return m;

    program.columns.clear();
    program.columns.reserve(program.indices.size());
    for (std::size_t k = 0; k < program.indices.size(); ++k) {
        const Column& c = column(program.indices[k]);
        check_exactly_representable(c, c.name(), rows);
        ExprColumn ec;
        if (engine_readable(c.type())) {
            ec.data = c.data_ptr();
            ec.type = expr_type_of(c.type());
        } else {
            // Bool and String have no numeric buffer the block loader can
            // walk; widen them into this program's own doubles. Correct, one
            // copy slower, and the whole query keeps the engine's semantics
            // rather than an interpreter's.
            program.values[k].resize(rows);
            widen_column(c, rows, program.values[k].data());
            ec.data = program.values[k].data();
            ec.type = ExprScalarType::Float64;
        }
        ec.is_vector = true;
        program.columns.push_back(ec);
    }
    // Straight into the mask's own words: no float per row, and no second
    // pass to pack what the block loop already carried as bytes.
    program.engine.compute_mask(program.columns, rows, m.words());

    apply_validity(program.indices, m);
    return m;
}

//! Clear the bit of every row a referenced column marks as not measured.
/*! The rule \ref select_range already follows: "not measured" cannot satisfy a
    condition. Where no referenced column has anything missing -- the common
    case -- nothing is read at all.
 *
 *  \ref Column::validity is asked rather than \ref Column::has_mask, so a
 *  column that records its gaps as ranges rather than bits is honoured too;
 *  asking has_mask() skipped those silently.
 */
void DataStore::apply_validity(const std::vector<int>& indices, BitMask& m) const {
    for (int index : indices) {
        const Column& c = column(index);
        if (!c.has_missing()) continue;
        // A validity mask shorter than the store leaves the tail clear, which
        // is the same answer as "that row has no value".
        m.and_with(c.validity());
    }
}

void DataStore::select_expression(const std::string& expr, Combine how) {
    BitMask m = expression_mask(expr);
    apply(m, how);
}

std::size_t DataStore::count_expression(const std::string& expr) const {
    return expression_mask(expr).count();
}

}  // namespace pto

// ===========================================================================
// ExpressionEngine implementation
// ===========================================================================
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <utility>

#include <atomic>

// Which SIMD tiers this build carries. SSE2 is the x86-64 baseline and NEON
// the aarch64 one, so the base tier is one of those wherever the target has
// them and plain scalar loops elsewhere.
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define PTOLIB_SIMD_X86 1
#else
#define PTOLIB_SIMD_X86 0
#endif
#if PTOLIB_SIMD_X86 && (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#define PTOLIB_SIMD_SSE2 1
#include <emmintrin.h>
#else
#define PTOLIB_SIMD_SSE2 0
#endif
// AVX2 is one of two things. Compiled in as the baseline (`-mavx2`,
// `/arch:AVX2`, `-march=native` on a machine that has it), it is simply what
// every loop in the header is vectorised to. Otherwise, with GCC or Clang, a
// second copy of the kernels is compiled with the AVX2 target attribute and
// picked at run time; MSVC has no per-function target, so an MSVC build gets
// AVX2 only as a baseline.
#if PTOLIB_SIMD_SSE2 && defined(__AVX2__)
#define PTOLIB_SIMD_AVX2_BASELINE 1
#define PTOLIB_SIMD_AVX2_DISPATCH 0
#include <immintrin.h>
#define PTOLIB_TARGET_AVX2
#elif PTOLIB_SIMD_SSE2 && (defined(__GNUC__) || defined(__clang__))
#define PTOLIB_SIMD_AVX2_BASELINE 0
#define PTOLIB_SIMD_AVX2_DISPATCH 1
#include <immintrin.h>
#include <cpuid.h>
#define PTOLIB_TARGET_AVX2 __attribute__((target("avx2")))
#else
#define PTOLIB_SIMD_AVX2_BASELINE 0
#define PTOLIB_SIMD_AVX2_DISPATCH 0
#define PTOLIB_TARGET_AVX2
#endif
#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(__aarch64__)
#define PTOLIB_SIMD_NEON 1
#include <arm_neon.h>
#else
#define PTOLIB_SIMD_NEON 0
#endif

namespace pto {

namespace {

// ---------------------------------------------------------------------------
// The instruction set
// ---------------------------------------------------------------------------

// OP_SAVE and OP_LOADC are what common subexpression elimination emits: a
// repeated subtree is computed once, copied into a cache slot, and every later
// occurrence becomes a load of that slot rather than the whole subtree again.
enum OpKind { OP_CONST = 0, OP_VAR, OP_BIN, OP_FUN, OP_POWC, OP_SAVE, OP_LOADC };
enum BinOp {
    B_ADD = 0, B_SUB, B_MUL, B_DIV, B_POW,
    B_LT, B_LE, B_GT, B_GE, B_EQ, B_NE, B_AND, B_OR
};
enum FunOp {
    F_NEG = 0, F_ABS, F_EXP, F_SQRT, F_LOG, F_LOG10,
    F_SIN, F_COS, F_TAN, F_NOT, F_POW2, F_MIN2, F_MAX2,
    // Added 2026-08-31 so a consumer could delete its own evaluator and the
    // vendored ExprTk behind it. These are the functions
    // that were reachable *and correct* in that fallback; without them,
    // dropping it would have been a capability regression, and `floor(x/2)`
    // is the case that proved it. Spelled as numpy spells them, because
    // numpy is what callers compare against.
    F_FLOOR, F_CEIL, F_ROUND, F_TRUNC, F_SIGN,
    F_ASIN, F_ACOS, F_ATAN, F_SINH, F_COSH, F_TANH,
    F_LOG2, F_EXPM1, F_LOG1P, F_DEG2RAD, F_RAD2DEG, F_ERF, F_ERFC,
    F_ATAN2, F_HYPOT,
    // Added 2026-09-02 so the DataStore could drop its ExprTk fallback
    // entirely (T-20260831-13): these were the last reachable-and-correct
    // arity-1/2 names that still forced a query onto it. The names that
    // remain reserved but unimplemented (clamp, inrange, if, avg, ...) now
    // refuse at compile time instead of being answered wrongly.
    F_ROOT, F_LOGN, F_FRAC
};

//! 512 rows: eight 64-bit mask words, and a double stack slot of 4 kB.
const std::size_t kBlock = 512;
//! Deepest expression the block stack will carry: 64 x 4 kB of doubles.
const int kMaxDepth = 64;
//! How many repeated subtrees may be kept aside at once.
const int kMaxCache = 16;

bool binary_function(int f) {
    return f == F_POW2 || f == F_MIN2 || f == F_MAX2 ||
           f == F_ATAN2 || f == F_HYPOT || f == F_ROOT || f == F_LOGN;
}

int function_id(const std::string& n) {
    if (n == "abs" || n == "fabs") return F_ABS;
    if (n == "exp") return F_EXP;
    if (n == "sqrt") return F_SQRT;
    if (n == "log") return F_LOG;
    if (n == "log10") return F_LOG10;
    if (n == "sin") return F_SIN;
    if (n == "cos") return F_COS;
    if (n == "tan") return F_TAN;
    if (n == "pow") return F_POW2;
    if (n == "min" || n == "minimum") return F_MIN2;
    if (n == "max" || n == "maximum") return F_MAX2;
    if (n == "floor") return F_FLOOR;
    if (n == "ceil") return F_CEIL;
    if (n == "round") return F_ROUND;
    if (n == "trunc") return F_TRUNC;
    if (n == "sign" || n == "sgn") return F_SIGN;
    if (n == "asin" || n == "arcsin") return F_ASIN;
    if (n == "acos" || n == "arccos") return F_ACOS;
    if (n == "atan" || n == "arctan") return F_ATAN;
    if (n == "sinh") return F_SINH;
    if (n == "cosh") return F_COSH;
    if (n == "tanh") return F_TANH;
    if (n == "log2") return F_LOG2;
    if (n == "expm1") return F_EXPM1;
    if (n == "log1p") return F_LOG1P;
    if (n == "deg2rad" || n == "radians") return F_DEG2RAD;
    if (n == "rad2deg" || n == "degrees") return F_RAD2DEG;
    if (n == "erf") return F_ERF;
    if (n == "erfc") return F_ERFC;
    if (n == "atan2" || n == "arctan2") return F_ATAN2;
    if (n == "hypot") return F_HYPOT;
    if (n == "root") return F_ROOT;
    if (n == "logn") return F_LOGN;
    if (n == "frac") return F_FRAC;
    return -1;
}

//! Names the engine resolves itself, so they are not free variables.
/*! Longer than the set of functions the evaluator implements: a name in here
    that the evaluator does not know makes \ref compile fail rather than turn
    into a column nobody can supply. */
bool is_reserved(const std::string& name) {
    static const char* kNames[] = {
        "abs", "fabs", "exp", "sqrt", "log", "log10", "log2", "sin", "cos",
        "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh", "pow",
        "min", "max", "minimum", "maximum", "avg", "sum", "floor", "ceil",
        "round", "sgn", "erf", "erfc", "frac", "trunc", "clamp", "inrange",
        "root", "hypot", "logn", "expm1", "log1p", "deg2rad", "rad2deg",
        "not", "and", "or", "xor", "nand", "nor", "if", "else", "while",
        "for", "true", "false", "pi", "epsilon", "inf", "e"};
    for (const char* n : kNames) {
        if (name == n) return true;
    }
    return false;
}

//! Python's precedence: or < and < not < comparison < +- < */ < unary- < **.
/*! Spaced by two so the two prefix operators fit between the binary ones: `not`
    binds looser than a comparison and tighter than `and`, unary minus binds
    looser than `**` and tighter than `*`. \see unary_precedence */
int precedence(int op) {
    switch (op) {
        case B_OR: return -4;
        case B_AND: return -2;
        case B_LT: case B_LE: case B_GT:
        case B_GE: case B_EQ: case B_NE: return 0;
        case B_ADD: case B_SUB: return 2;
        case B_MUL: case B_DIV: return 4;
        case B_POW: return 8;
        default: return -6;
    }
}

//! Where a pending prefix operator sits in the same ladder.
/*! Load-bearing, and it was missing: a pending unary minus used to be flushed
    only once its operand was complete, and never when the operand ended in a
    `**`. So `-g**2 < 0` compiled as `-((g**2) < 0)` -- a negated *boolean* --
    where Python means `(-(g**2)) < 0`. Anything of the shape
    "unary minus, then `**`, then something that binds looser" was wrong. */
int unary_precedence(int f) { return (f == F_NOT) ? -1 : 6; }

struct Token {
    int kind = 0;  //!< 0 number, 1 name, 2 operator, 3 '(', 4 ')', 5 ','
    double number = 0.0;
    std::string name;
    int op = 0;
    bool unary = false;
};

//! Tokenise, marking a leading +/- as unary.
/*! False for any character this evaluator does not know, which is how an
    expression it cannot represent is refused rather than half-parsed. */
bool tokenize(const std::string& s, std::vector<Token>* out) {
    std::size_t i = 0;
    bool value_before = false;
    while (i < s.size()) {
        const char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            char* end = nullptr;
            const double v = std::strtod(s.c_str() + i, &end);
            if (end == s.c_str() + i) return false;
            const std::size_t stop = static_cast<std::size_t>(end - s.c_str());
            // A number may not run straight into a name. Without this "1e+"
            // tokenises as 1, +, e and quietly evaluates to 3.718 -- the one
            // hole a separate validating parser used to cover.
            if (stop < s.size() &&
                (std::isalnum(static_cast<unsigned char>(s[stop])) ||
                 s[stop] == '_' || s[stop] == '.')) {
                return false;
            }
            Token t; t.kind = 0; t.number = v; out->push_back(t);
            i = stop;
            value_before = true;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t j = i;
            while (j < s.size() &&
                   (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) {
                ++j;
            }
            Token t; t.name = s.substr(i, j - i);
            if (t.name == "and" || t.name == "or") {
                t.kind = 2; t.op = (t.name == "and") ? B_AND : B_OR;
                value_before = false;
            } else if (t.name == "not") {
                // Unary; carried as a function so it binds looser than a
                // comparison, which is what makes "not x > 2" mean
                // "not (x > 2)" as it does in Python.
                t.kind = 2; t.op = B_AND; t.unary = true; t.name = "not";
                value_before = false;
            } else {
                t.kind = 1;
                value_before = true;
            }
            out->push_back(t);
            i = j;
            continue;
        }
        Token t;
        if (c == '(') { t.kind = 3; value_before = false; }
        else if (c == ')') { t.kind = 4; value_before = true; }
        else if (c == ',') { t.kind = 5; value_before = false; }
        else if (c == '+' || c == '-') {
            t.kind = 2; t.op = (c == '+') ? B_ADD : B_SUB;
            t.unary = !value_before; value_before = false;
        } else if (c == '^') { t.kind = 2; t.op = B_POW; value_before = false; }
        else if (c == '*') { t.kind = 2; t.op = B_MUL; value_before = false; }
        else if (c == '/') { t.kind = 2; t.op = B_DIV; value_before = false; }
        else if (c == '<') {
            t.kind = 2; t.op = B_LT;
            if (i + 1 < s.size() && s[i + 1] == '=') { t.op = B_LE; ++i; }
            value_before = false;
        } else if (c == '>') {
            t.kind = 2; t.op = B_GT;
            if (i + 1 < s.size() && s[i + 1] == '=') { t.op = B_GE; ++i; }
            value_before = false;
        } else if (c == '=' && i + 1 < s.size() && s[i + 1] == '=') {
            t.kind = 2; t.op = B_EQ; ++i; value_before = false;
        } else if (c == '!' && i + 1 < s.size() && s[i + 1] == '=') {
            t.kind = 2; t.op = B_NE; ++i; value_before = false;
        } else {
            return false;
        }
        out->push_back(t);
        ++i;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Scalar arithmetic
//
// The constant folder calls exactly these, so a folded subtree is bit-identical
// to what the evaluator's own scalar path would have produced at run time --
// only *when* the arithmetic happens changes.
// ---------------------------------------------------------------------------

//! numpy's `minimum`: a NaN in either operand propagates.
/*! The ternary form this replaces -- `(a > b) ? b : a` -- is neither numpy's
    rule nor C's `fmin`, and made `min` non-commutative under NaN:
    `min(y, nan)` was `y` while `min(nan, y)` was `nan`. */
template <typename T>
inline T numpy_min(T a, T b) {
    if (a != a) return a;
    if (b != b) return b;
    return (b < a) ? b : a;
}

template <typename T>
inline T numpy_max(T a, T b) {
    if (a != a) return a;
    if (b != b) return b;
    return (b > a) ? b : a;
}

inline double apply_binary_scalar(int f, double a, double b) {
    switch (f) {
        case F_POW2: return std::pow(a, b);
        case F_MIN2: return numpy_min(a, b);
        case F_ATAN2: return std::atan2(a, b);
        case F_HYPOT: return std::hypot(a, b);
        // ExprTk's spellings, kept at ExprTk's semantics so a query that used
        // the fallback answers the same after its removal.
        case F_ROOT: return std::pow(a, 1.0 / b);
        case F_LOGN: return std::log(a) / std::log(b);
        default: return numpy_max(a, b);
    }
}

inline double apply_scalar_fun(int f, double a) {
    switch (f) {
        case F_NEG: return -a;
        case F_ABS: return std::fabs(a);
        case F_EXP: return std::exp(a);
        case F_SQRT: return std::sqrt(a);
        case F_LOG: return std::log(a);
        case F_LOG10: return std::log10(a);
        case F_SIN: return std::sin(a);
        case F_COS: return std::cos(a);
        case F_TAN: return std::tan(a);
        // numpy's truthiness, not ExprTk's `> 0.5`: anything not zero is true,
        // which keeps a negative value and a NaN true.
        case F_NOT: return (a != 0.0) ? 0.0 : 1.0;
        case F_FLOOR: return std::floor(a);
        case F_CEIL: return std::ceil(a);
        // std::round is half-away-from-zero; numpy's `round` is half-to-even,
        // and a gate on a .5 boundary would disagree. std::nearbyint follows
        // the current rounding mode, which is round-to-nearest-even.
        case F_ROUND: return std::nearbyint(a);
        case F_TRUNC: return std::trunc(a);
        // numpy's sign: -1, 0 or 1, and NaN for NaN -- not copysign, which
        // has no zero case and would turn 0 into 1.
        case F_SIGN: return (a > 0.0) ? 1.0 : ((a < 0.0) ? -1.0
                                                        : (a != a ? a : 0.0));
        case F_ASIN: return std::asin(a);
        case F_ACOS: return std::acos(a);
        case F_ATAN: return std::atan(a);
        case F_SINH: return std::sinh(a);
        case F_COSH: return std::cosh(a);
        case F_TANH: return std::tanh(a);
        case F_LOG2: return std::log2(a);
        case F_EXPM1: return std::expm1(a);
        case F_LOG1P: return std::log1p(a);
        case F_DEG2RAD: return a * (3.14159265358979323846 / 180.0);
        case F_RAD2DEG: return a * (180.0 / 3.14159265358979323846);
        case F_ERF: return std::erf(a);
        case F_ERFC: return std::erfc(a);
        // Fractional part with ExprTk's (and C's) toward-zero convention:
        // frac(-1.25) is -0.25, not 0.75.
        case F_FRAC: return a - std::trunc(a);
        default: return a;
    }
}

// ---------------------------------------------------------------------------
// Block kernels, per SIMD tier
//
// Templated on the working type, because the two callers differ in it: a gate
// over float32 columns runs in float32 (twice the lanes, and bit-for-bit what
// numpy gives over the same columns) while everything else runs in double.
//
// The kernels are plain loops -- one loop per operator, `__restrict` on every
// pointer, no branch inside -- because that is the shape every compiler
// vectorises to the full width of whatever it is targeting, and it was
// measured to beat hand-written lane-by-lane intrinsics by a factor of two on
// the comparisons. Intrinsics remain in exactly two places where the compiler
// will not do it: the square root, which libm's errno contract keeps scalar,
// and the bit packing at the end of a gate, which has no loop form a
// vectoriser recognises.
//
// One set of kernel bodies is stamped out once per tier by PTOLIB_SIMD_KERNELS.
// The base tier is whatever the whole header is compiled for: SSE2 on x86-64,
// NEON on aarch64, the plain scalar loops elsewhere. On x86 with GCC or Clang
// an AVX2 tier is compiled beside it with `__attribute__((target("avx2")))`
// on every kernel, so the same loops come out 256 bits wide, and it is chosen
// at run time when the CPU reports AVX2 (see \ref simd_tier). The block loop
// reaches the chosen tier through a table of function pointers: one indirect
// call per 512-row block operation, which does not register.
//
// The tiers agree bit for bit. Every operation is one IEEE operation per lane
// (add, sub, mul, div, sqrt, compare, sign bit), no kernel contains a multiply
// feeding an add -- so there is nothing a compiler could contract into a fused
// multiply-add -- and no approximate reciprocal is used. A gate therefore
// selects the same rows whichever tier evaluated it, and test_simd proves it
// on every tier the machine has.
// ---------------------------------------------------------------------------

//! Whether this CPU and this OS can run AVX2 code: the AVX2 bit, and YMM
//! state enabled by the OS (without which the first 256-bit instruction traps).
#if PTOLIB_SIMD_AVX2_DISPATCH
inline bool cpu_has_avx2() {
    unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
#if defined(_MSC_VER)
    int r[4] = {0, 0, 0, 0};
    __cpuid(r, 0);
    if (r[0] < 7) return false;
    __cpuid(r, 1);
    ecx = static_cast<unsigned>(r[2]);
#else
    if (__get_cpuid_max(0, nullptr) < 7) return false;
    __cpuid(1, eax, ebx, ecx, edx);
#endif
    if (!(ecx & (1u << 27)) || !(ecx & (1u << 28))) return false;   // OSXSAVE, AVX
    unsigned xcr0 = 0;
#if defined(_MSC_VER)
    xcr0 = static_cast<unsigned>(_xgetbv(0));
#else
    unsigned xcr0_hi = 0;
    __asm__ volatile("xgetbv" : "=a"(xcr0), "=d"(xcr0_hi) : "c"(0));
#endif
    if ((xcr0 & 6u) != 6u) return false;                              // XMM and YMM state
#if defined(_MSC_VER)
    __cpuidex(r, 7, 0);
    ebx = static_cast<unsigned>(r[1]);
#else
    __cpuid_count(7, 0, eax, ebx, ecx, edx);
#endif
    return (ebx & (1u << 5)) != 0;                                    // AVX2
}
#endif

//! Whether 1/v is exact, so a divide may become a multiply.
/*! The reciprocal trick is worth real time -- division is the slowest
    arithmetic instruction on every machine this runs on -- but it is not
    exact, and a gate compares a computed value against a boundary where one
    ulp flips a row. So it is taken only where it changes nothing: a power of
    two, whose reciprocal is another power of two. */
template <typename T>
inline bool reciprocal_is_exact(T v) {
    if (!(v == v) || v == T(0)) return false;
    int e = 0;
    const T m = static_cast<T>(std::frexp(static_cast<double>(v), &e));
    if (m != T(0.5) && m != T(-0.5)) return false;
    // Denormal or huge: the reciprocal itself may not be representable.
    const T r = T(1) / v;
    return r == r && r != T(0) && std::isfinite(static_cast<double>(r));
}

//! The unary functions that are libm calls whatever the tier.
template <typename T>
inline void fun_libm(int f, T* __restrict a, std::size_t n) {
    switch (f) {
        case F_EXP: for (std::size_t i = 0; i < n; ++i) a[i] = std::exp(a[i]); break;
        case F_LOG: for (std::size_t i = 0; i < n; ++i) a[i] = std::log(a[i]); break;
        case F_LOG10: for (std::size_t i = 0; i < n; ++i) a[i] = std::log10(a[i]); break;
        case F_SIN: for (std::size_t i = 0; i < n; ++i) a[i] = std::sin(a[i]); break;
        case F_COS: for (std::size_t i = 0; i < n; ++i) a[i] = std::cos(a[i]); break;
        case F_TAN: for (std::size_t i = 0; i < n; ++i) a[i] = std::tan(a[i]); break;
        case F_FLOOR: for (std::size_t i = 0; i < n; ++i) a[i] = std::floor(a[i]); break;
        case F_CEIL: for (std::size_t i = 0; i < n; ++i) a[i] = std::ceil(a[i]); break;
        case F_ROUND: for (std::size_t i = 0; i < n; ++i) a[i] = std::nearbyint(a[i]); break;
        case F_TRUNC: for (std::size_t i = 0; i < n; ++i) a[i] = std::trunc(a[i]); break;
        case F_SIGN: for (std::size_t i = 0; i < n; ++i) {
            const T v = a[i];
            a[i] = (v > T(0)) ? T(1) : ((v < T(0)) ? T(-1) : (v != v ? v : T(0)));
        } break;
        case F_ASIN: for (std::size_t i = 0; i < n; ++i) a[i] = std::asin(a[i]); break;
        case F_ACOS: for (std::size_t i = 0; i < n; ++i) a[i] = std::acos(a[i]); break;
        case F_ATAN: for (std::size_t i = 0; i < n; ++i) a[i] = std::atan(a[i]); break;
        case F_SINH: for (std::size_t i = 0; i < n; ++i) a[i] = std::sinh(a[i]); break;
        case F_COSH: for (std::size_t i = 0; i < n; ++i) a[i] = std::cosh(a[i]); break;
        case F_TANH: for (std::size_t i = 0; i < n; ++i) a[i] = std::tanh(a[i]); break;
        case F_LOG2: for (std::size_t i = 0; i < n; ++i) a[i] = std::log2(a[i]); break;
        case F_EXPM1: for (std::size_t i = 0; i < n; ++i) a[i] = std::expm1(a[i]); break;
        case F_LOG1P: for (std::size_t i = 0; i < n; ++i) a[i] = std::log1p(a[i]); break;
        case F_DEG2RAD:
            for (std::size_t i = 0; i < n; ++i) a[i] = a[i] * T(3.14159265358979323846 / 180.0);
            break;
        case F_RAD2DEG:
            for (std::size_t i = 0; i < n; ++i) a[i] = a[i] * T(180.0 / 3.14159265358979323846);
            break;
        case F_ERF: for (std::size_t i = 0; i < n; ++i) a[i] = std::erf(a[i]); break;
        case F_ERFC: for (std::size_t i = 0; i < n; ++i) a[i] = std::erfc(a[i]); break;
        case F_FRAC: for (std::size_t i = 0; i < n; ++i) a[i] = a[i] - std::trunc(a[i]); break;
        default: break;
    }
}

//! The binary functions. All libm or NaN-propagating min/max, so one body
//! serves every tier.
template <typename T>
inline void apply_fun2(int f, T* __restrict a, const T* __restrict b, std::size_t n) {
    switch (f) {
        case F_POW2: for (std::size_t i = 0; i < n; ++i) a[i] = std::pow(a[i], b[i]); break;
        case F_MIN2: for (std::size_t i = 0; i < n; ++i) a[i] = numpy_min(a[i], b[i]); break;
        case F_ATAN2: for (std::size_t i = 0; i < n; ++i) a[i] = std::atan2(a[i], b[i]); break;
        case F_HYPOT: for (std::size_t i = 0; i < n; ++i) a[i] = std::hypot(a[i], b[i]); break;
        case F_ROOT:
            for (std::size_t i = 0; i < n; ++i) a[i] = std::pow(a[i], T(1) / b[i]);
            break;
        case F_LOGN:
            for (std::size_t i = 0; i < n; ++i) a[i] = std::log(a[i]) / std::log(b[i]);
            break;
        default: for (std::size_t i = 0; i < n; ++i) a[i] = numpy_max(a[i], b[i]);
    }
}

// The kernel bodies. `Lanes<T>::sqrt_block` and `Bytes::pack64` come from the
// enclosing tier namespace; ATTR is empty except for the AVX2 tier.
#define PTOLIB_SIMD_KERNELS(ATTR)                                                        \
    template <typename T, typename S>                                                     \
    ATTR inline void convert(const S* __restrict src, std::size_t n, T* __restrict dst) { \
        for (std::size_t i = 0; i < n; ++i) dst[i] = static_cast<T>(src[i]);              \
    }                                                                                     \
    /* One block of a column, in the working type. */                                     \
    template <typename T>                                                                 \
    ATTR inline void load(const ExprColumn& c, std::size_t base, std::size_t len, T* __restrict dst) { \
        switch (c.type) {                                                                 \
            case ExprScalarType::Float64:                                                 \
                convert(static_cast<const double*>(c.data) + base, len, dst); break;      \
            case ExprScalarType::Float32:                                                 \
                convert(static_cast<const float*>(c.data) + base, len, dst); break;       \
            case ExprScalarType::Int64:                                                   \
                convert(static_cast<const std::int64_t*>(c.data) + base, len, dst); break; \
            case ExprScalarType::Int32:                                                   \
                convert(static_cast<const std::int32_t*>(c.data) + base, len, dst); break; \
            case ExprScalarType::Int16:                                                   \
                convert(static_cast<const std::int16_t*>(c.data) + base, len, dst); break; \
            case ExprScalarType::Int8:                                                    \
                convert(static_cast<const std::int8_t*>(c.data) + base, len, dst); break; \
            case ExprScalarType::UInt64:                                                  \
                convert(static_cast<const std::uint64_t*>(c.data) + base, len, dst); break; \
            case ExprScalarType::UInt32:                                                  \
                convert(static_cast<const std::uint32_t*>(c.data) + base, len, dst); break; \
            case ExprScalarType::UInt16:                                                  \
                convert(static_cast<const std::uint16_t*>(c.data) + base, len, dst); break; \
            default:                                                                      \
                convert(static_cast<const std::uint8_t*>(c.data) + base, len, dst); break; \
        }                                                                                 \
    }                                                                                     \
    /* a op= b, over the block. Comparisons and the boolean operators never    */         \
    /* arrive here: they are answered on the byte stack.                       */         \
    template <typename T>                                                                 \
    ATTR inline void bin(int op, T* __restrict a, const T* __restrict b, std::size_t n) { \
        switch (op) {                                                                     \
            case B_ADD: for (std::size_t i = 0; i < n; ++i) a[i] += b[i]; break;          \
            case B_SUB: for (std::size_t i = 0; i < n; ++i) a[i] -= b[i]; break;          \
            case B_MUL: for (std::size_t i = 0; i < n; ++i) a[i] *= b[i]; break;          \
            case B_DIV: for (std::size_t i = 0; i < n; ++i) a[i] /= b[i]; break;          \
            default: for (std::size_t i = 0; i < n; ++i) a[i] = std::pow(a[i], b[i]);     \
        }                                                                                 \
    }                                                                                     \
    /* a op= v, against one broadcast value. A scalar parameter never becomes  */         \
    /* an array, which is where a column of a million rows used to be written  */         \
    /* out once per scalar operand.                                             */         \
    template <typename T>                                                                 \
    ATTR inline void bin_s(int op, T* __restrict a, T v, std::size_t n) {                 \
        if (op == B_SUB) { op = B_ADD; v = static_cast<T>(-v); }                          \
        if (op == B_DIV && reciprocal_is_exact(v)) { op = B_MUL; v = static_cast<T>(T(1) / v); } \
        switch (op) {                                                                     \
            case B_ADD: for (std::size_t i = 0; i < n; ++i) a[i] += v; break;             \
            case B_MUL: for (std::size_t i = 0; i < n; ++i) a[i] *= v; break;             \
            default: for (std::size_t i = 0; i < n; ++i) a[i] /= v;                       \
        }                                                                                 \
    }                                                                                     \
    /* dst = v op src, written straight to its destination slot. The obvious   */         \
    /* form -- compute in the right-hand slot, then copy it down to the left -- */         \
    /* costs an extra read and write of the whole block per operation.         */         \
    template <typename T>                                                                 \
    ATTR inline void bin_sv(int op, T* __restrict dst, const T* __restrict src, T v, std::size_t n) { \
        switch (op) {                                                                     \
            case B_ADD: for (std::size_t i = 0; i < n; ++i) dst[i] = src[i] + v; break;   \
            case B_SUB: for (std::size_t i = 0; i < n; ++i) dst[i] = v - src[i]; break;   \
            case B_MUL: for (std::size_t i = 0; i < n; ++i) dst[i] = src[i] * v; break;   \
            default: for (std::size_t i = 0; i < n; ++i) dst[i] = v / src[i];             \
        }                                                                                 \
    }                                                                                     \
    /* x ** c for the exponents that are one or two IEEE operations, and pow   */         \
    /* for the rest. A Newton-Raphson reciprocal was tried and measured no     */         \
    /* faster: the per-lane guards for zero and non-finite inputs cost as much */         \
    /* as the division.                                                         */         \
    template <typename T>                                                                 \
    ATTR inline void powc(double c, T* __restrict a, std::size_t n) {                     \
        if (c == -1.0) {                                                                  \
            for (std::size_t i = 0; i < n; ++i) a[i] = T(1) / a[i];                       \
        } else if (c == 0.5) {                                                            \
            Lanes<T>::sqrt_block(a, n);                                                   \
        } else if (c == -0.5) {                                                           \
            Lanes<T>::sqrt_block(a, n);                                                   \
            for (std::size_t i = 0; i < n; ++i) a[i] = T(1) / a[i];                       \
        } else if (c == 2.0) {                                                            \
            for (std::size_t i = 0; i < n; ++i) a[i] = a[i] * a[i];                       \
        } else if (c == 3.0) {                                                            \
            for (std::size_t i = 0; i < n; ++i) a[i] = a[i] * a[i] * a[i];                \
        } else if (c == -2.0) {                                                           \
            for (std::size_t i = 0; i < n; ++i) a[i] = T(1) / (a[i] * a[i]);              \
        } else if (c == 1.0) {                                                            \
            /* nothing to do */                                                           \
        } else {                                                                          \
            const T e = static_cast<T>(c);                                                \
            for (std::size_t i = 0; i < n; ++i) a[i] = std::pow(a[i], e);                 \
        }                                                                                 \
    }                                                                                     \
    /* The unary functions with a lane-wise form; everything else is libm.     */         \
    template <typename T>                                                                 \
    ATTR inline void fun(int f, T* __restrict a, std::size_t n) {                         \
        switch (f) {                                                                      \
            case F_NEG: for (std::size_t i = 0; i < n; ++i) a[i] = -a[i]; break;          \
            case F_ABS: for (std::size_t i = 0; i < n; ++i) a[i] = std::fabs(a[i]); break; \
            case F_SQRT: Lanes<T>::sqrt_block(a, n); break;                               \
            default: fun_libm(f, a, n);                                                   \
        }                                                                                 \
    }                                                                                     \
    /* Comparisons, branchless, writing one BYTE per row rather than a double. */         \
    /* Carrying a gate's result as an 8-byte value costs eight times the       */         \
    /* memory traffic of the byte numpy uses.                                  */         \
    template <typename T>                                                                 \
    ATTR inline void cmp(int op, const T* __restrict a, const T* __restrict b,            \
                         unsigned char* __restrict out, std::size_t n) {                  \
        switch (op) {                                                                     \
            case B_LT: for (std::size_t i = 0; i < n; ++i) out[i] = a[i] < b[i] ? 1 : 0; break; \
            case B_LE: for (std::size_t i = 0; i < n; ++i) out[i] = a[i] <= b[i] ? 1 : 0; break; \
            case B_GT: for (std::size_t i = 0; i < n; ++i) out[i] = a[i] > b[i] ? 1 : 0; break; \
            case B_GE: for (std::size_t i = 0; i < n; ++i) out[i] = a[i] >= b[i] ? 1 : 0; break; \
            case B_EQ: for (std::size_t i = 0; i < n; ++i) out[i] = a[i] == b[i] ? 1 : 0; break; \
            default: for (std::size_t i = 0; i < n; ++i) out[i] = a[i] != b[i] ? 1 : 0;   \
        }                                                                                 \
    }                                                                                     \
    ATTR inline void bcombine(int op, unsigned char* __restrict a,                        \
                              const unsigned char* __restrict b, std::size_t n) {         \
        if (op == B_AND) {                                                                \
            for (std::size_t i = 0; i < n; ++i) a[i] = static_cast<unsigned char>(a[i] & b[i]); \
        } else {                                                                          \
            for (std::size_t i = 0; i < n; ++i) a[i] = static_cast<unsigned char>(a[i] | b[i]); \
        }                                                                                 \
    }                                                                                     \
    ATTR inline void bnot(unsigned char* __restrict a, std::size_t n) {                   \
        for (std::size_t i = 0; i < n; ++i) a[i] = a[i] ? 0 : 1;                          \
    }                                                                                     \
    /* Pack a block of mask bytes into words, bit i of word i/64 from byte i.  */         \
    /* Bits past `len` are cleared.                                            */         \
    ATTR inline void pack(const unsigned char* __restrict src, std::size_t len,           \
                          std::uint64_t* __restrict w) {                                  \
        std::size_t k = 0;                                                                \
        for (; k + 64 <= len; k += 64) w[k >> 6] = Bytes::pack64(src + k);                \
        if (k < len) {                                                                    \
            unsigned char tail[64];                                                       \
            std::memset(tail, 0, sizeof(tail));                                           \
            std::memcpy(tail, src + k, len - k);                                          \
            w[k >> 6] = Bytes::pack64(tail);                                              \
        }                                                                                 \
    }

// -- the two intrinsics each tier supplies -----------------------------------
//
// sqrt_block: the square root over a block, lane-wise. pack64: 64 bytes to a
// word, bit k set where byte k is non-zero. Everything else is a plain loop.

struct ScalarBytes {
    static std::uint64_t pack64(const unsigned char* b) {
        std::uint64_t r = 0;
        for (int k = 0; k < 8; ++k) {
            std::uint64_t v;
            std::memcpy(&v, b + 8 * k, 8);
            r |= detail::pack8(v) << (8 * k);
        }
        return r;
    }
};
template <typename T> struct ScalarLanes {
    static void sqrt_block(T* __restrict a, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) a[i] = std::sqrt(a[i]);
    }
};

#if PTOLIB_SIMD_SSE2
struct Sse2Bytes {
    static std::uint64_t pack64(const unsigned char* b) {
        const __m128i zero = _mm_setzero_si128();
        std::uint64_t r = 0;
        for (int k = 0; k < 4; ++k) {
            const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + 16 * k));
            const unsigned z = static_cast<unsigned>(_mm_movemask_epi8(_mm_cmpeq_epi8(v, zero)));
            r |= static_cast<std::uint64_t>(~z & 0xFFFFu) << (16 * k);
        }
        return r;
    }
};
template <typename T> struct Sse2Lanes;
template <> struct Sse2Lanes<double> {
    static void sqrt_block(double* __restrict a, std::size_t n) {
        std::size_t i = 0;
        for (; i + 2 <= n; i += 2) _mm_storeu_pd(a + i, _mm_sqrt_pd(_mm_loadu_pd(a + i)));
        for (; i < n; ++i) a[i] = std::sqrt(a[i]);
    }
};
template <> struct Sse2Lanes<float> {
    static void sqrt_block(float* __restrict a, std::size_t n) {
        std::size_t i = 0;
        for (; i + 4 <= n; i += 4) _mm_storeu_ps(a + i, _mm_sqrt_ps(_mm_loadu_ps(a + i)));
        for (; i < n; ++i) a[i] = std::sqrt(a[i]);
    }
};
#endif

#if PTOLIB_SIMD_AVX2_DISPATCH || PTOLIB_SIMD_AVX2_BASELINE
struct Avx2Bytes {
    PTOLIB_TARGET_AVX2 static std::uint64_t pack64(const unsigned char* b) {
        const __m256i zero = _mm256_setzero_si256();
        const __m256i lo = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b));
        const __m256i hi = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + 32));
        const unsigned zl = static_cast<unsigned>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(lo, zero)));
        const unsigned zh = static_cast<unsigned>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(hi, zero)));
        return static_cast<std::uint64_t>(~zl) | (static_cast<std::uint64_t>(~zh) << 32);
    }
};
template <typename T> struct Avx2Lanes;
template <> struct Avx2Lanes<double> {
    PTOLIB_TARGET_AVX2 static void sqrt_block(double* __restrict a, std::size_t n) {
        std::size_t i = 0;
        for (; i + 4 <= n; i += 4) _mm256_storeu_pd(a + i, _mm256_sqrt_pd(_mm256_loadu_pd(a + i)));
        for (; i < n; ++i) a[i] = std::sqrt(a[i]);
    }
};
template <> struct Avx2Lanes<float> {
    PTOLIB_TARGET_AVX2 static void sqrt_block(float* __restrict a, std::size_t n) {
        std::size_t i = 0;
        for (; i + 8 <= n; i += 8) _mm256_storeu_ps(a + i, _mm256_sqrt_ps(_mm256_loadu_ps(a + i)));
        for (; i < n; ++i) a[i] = std::sqrt(a[i]);
    }
};
#endif

#if PTOLIB_SIMD_NEON
struct NeonBytes {
    // 64 bytes to 64 bits in four pairwise adds: each byte is masked to the
    // weight of its position within its eight, and three levels of `addp`
    // sum the eights into bytes of the result, in order. Measured at six
    // times the branch-free scalar form and at 1.6 times the horizontal-add
    // form it replaced.
    static std::uint64_t pack64(const unsigned char* b) {
        static const uint8_t kWeights[16] = {1, 2, 4, 8, 16, 32, 64, 128,
                                             1, 2, 4, 8, 16, 32, 64, 128};
        const uint8x16_t w = vld1q_u8(kWeights);
        const uint8x16_t v0 = vld1q_u8(b), v1 = vld1q_u8(b + 16);
        const uint8x16_t v2 = vld1q_u8(b + 32), v3 = vld1q_u8(b + 48);
        const uint8x16_t m0 = vandq_u8(vtstq_u8(v0, v0), w);
        const uint8x16_t m1 = vandq_u8(vtstq_u8(v1, v1), w);
        const uint8x16_t m2 = vandq_u8(vtstq_u8(v2, v2), w);
        const uint8x16_t m3 = vandq_u8(vtstq_u8(v3, v3), w);
        const uint8x16_t t = vpaddq_u8(vpaddq_u8(m0, m1), vpaddq_u8(m2, m3));
        const uint8x16_t u = vpaddq_u8(t, t);
        return vgetq_lane_u64(vreinterpretq_u64_u8(u), 0);
    }
};
template <typename T> struct NeonLanes;
template <> struct NeonLanes<double> {
    static void sqrt_block(double* __restrict a, std::size_t n) {
        std::size_t i = 0;
        for (; i + 2 <= n; i += 2) vst1q_f64(a + i, vsqrtq_f64(vld1q_f64(a + i)));
        for (; i < n; ++i) a[i] = std::sqrt(a[i]);
    }
};
template <> struct NeonLanes<float> {
    static void sqrt_block(float* __restrict a, std::size_t n) {
        std::size_t i = 0;
        for (; i + 4 <= n; i += 4) vst1q_f32(a + i, vsqrtq_f32(vld1q_f32(a + i)));
        for (; i < n; ++i) a[i] = std::sqrt(a[i]);
    }
};
#endif

// -- the base tier: what the whole header was compiled for --------------------
namespace tier_base {
#if PTOLIB_SIMD_AVX2_BASELINE
template <typename T> struct Lanes : Avx2Lanes<T> {};
typedef Avx2Bytes Bytes;
#elif PTOLIB_SIMD_SSE2
template <typename T> struct Lanes : Sse2Lanes<T> {};
typedef Sse2Bytes Bytes;
#elif PTOLIB_SIMD_NEON
template <typename T> struct Lanes : NeonLanes<T> {};
typedef NeonBytes Bytes;
#else
template <typename T> struct Lanes : ScalarLanes<T> {};
typedef ScalarBytes Bytes;
#endif
PTOLIB_SIMD_KERNELS()
}  // namespace tier_base

// -- AVX2, reached at run time on the CPUs that have it ----------------------
#if PTOLIB_SIMD_AVX2_DISPATCH
namespace tier_avx2 {
template <typename T> struct Lanes : Avx2Lanes<T> {};
typedef Avx2Bytes Bytes;
PTOLIB_SIMD_KERNELS(PTOLIB_TARGET_AVX2)
}  // namespace tier_avx2
#endif

#undef PTOLIB_SIMD_KERNELS

// -- the table the block loop calls through ----------------------------------
template <typename T>
struct KernelTable {
    void (*load)(const ExprColumn&, std::size_t, std::size_t, T*);
    void (*bin)(int, T*, const T*, std::size_t);
    void (*bin_s)(int, T*, T, std::size_t);
    void (*bin_sv)(int, T*, const T*, T, std::size_t);
    void (*powc)(double, T*, std::size_t);
    void (*fun)(int, T*, std::size_t);
    void (*cmp)(int, const T*, const T*, unsigned char*, std::size_t);
    void (*bcombine)(int, unsigned char*, const unsigned char*, std::size_t);
    void (*bnot)(unsigned char*, std::size_t);
    void (*pack)(const unsigned char*, std::size_t, std::uint64_t*);
};

#define PTOLIB_SIMD_TABLE(NS)                                                             \
    template <typename T> KernelTable<T> table_##NS() {                                   \
        KernelTable<T> k;                                                                 \
        k.load = &NS::load<T>;                                                            \
        k.bin = &NS::bin<T>; k.bin_s = &NS::bin_s<T>; k.bin_sv = &NS::bin_sv<T>;          \
        k.powc = &NS::powc<T>; k.fun = &NS::fun<T>; k.cmp = &NS::cmp<T>;                  \
        k.bcombine = &NS::bcombine; k.bnot = &NS::bnot; k.pack = &NS::pack;               \
        return k;                                                                         \
    }
PTOLIB_SIMD_TABLE(tier_base)
#if PTOLIB_SIMD_AVX2_DISPATCH
PTOLIB_SIMD_TABLE(tier_avx2)
#endif
#undef PTOLIB_SIMD_TABLE

enum SimdTierId { kTierBase = 0, kTierAvx2 = 1 };

inline const char* base_tier_name() {
#if PTOLIB_SIMD_AVX2_BASELINE
    return "avx2";
#elif PTOLIB_SIMD_SSE2
    return "sse2";
#elif PTOLIB_SIMD_NEON
    return "neon";
#else
    return "scalar";
#endif
}

//! Whether this build and this CPU can run a tier.
inline bool tier_available(int id) {
    if (id == kTierBase) return true;
#if PTOLIB_SIMD_AVX2_DISPATCH
    if (id == kTierAvx2) { static const bool ok = cpu_has_avx2(); return ok; }
#endif
    return false;
}

//! The tier in use: -1 until the first evaluation or the first set_simd_tier.
std::atomic<int> g_simd_tier(-1);

inline int current_tier() {
    int id = g_simd_tier.load(std::memory_order_relaxed);
    if (id < 0) {
        id = tier_available(kTierAvx2) ? kTierAvx2 : kTierBase;
        g_simd_tier.store(id, std::memory_order_relaxed);
    }
    return id;
}

template <typename T>
inline KernelTable<T> kernels_for(int id) {
#if PTOLIB_SIMD_AVX2_DISPATCH
    if (id == kTierAvx2) return table_tier_avx2<T>();
#endif
    (void) id;
    return table_tier_base<T>();
}

}  // namespace

const char* simd_tier() {
    return current_tier() == kTierAvx2 ? "avx2" : base_tier_name();
}

bool set_simd_tier(const char* name) {
    if (name == nullptr) return false;
    if (std::strcmp(name, base_tier_name()) == 0) {
        g_simd_tier.store(kTierBase, std::memory_order_relaxed);
        return true;
    }
    if (std::strcmp(name, "avx2") == 0 && tier_available(kTierAvx2)) {
        g_simd_tier.store(kTierAvx2, std::memory_order_relaxed);
        return true;
    }
    return false;
}

namespace {

// ---------------------------------------------------------------------------
// Reading a column into a block, converting as it goes
//
// The alternative -- widening every referenced column into a full-length double
// buffer before evaluating -- costs a pass over memory and n extra doubles per
// column. A block load has to copy anyway, so converting inside that copy is
// free.
// ---------------------------------------------------------------------------

template <typename T, typename S>
inline void convert_block(const S* src, std::size_t n, T* dst) {
    for (std::size_t i = 0; i < n; ++i) dst[i] = static_cast<T>(src[i]);
}

inline void convert_block(const double* src, std::size_t n, double* dst) {
    std::memcpy(dst, src, n * sizeof(double));
}

inline void convert_block(const float* src, std::size_t n, float* dst) {
    std::memcpy(dst, src, n * sizeof(float));
}

template <typename T>
inline void load_block(const ExprColumn& c, std::size_t base, std::size_t len, T* dst) {
    switch (c.type) {
        case ExprScalarType::Float64:
            convert_block(static_cast<const double*>(c.data) + base, len, dst); break;
        case ExprScalarType::Float32:
            convert_block(static_cast<const float*>(c.data) + base, len, dst); break;
        case ExprScalarType::Int64:
            convert_block(static_cast<const std::int64_t*>(c.data) + base, len, dst); break;
        case ExprScalarType::Int32:
            convert_block(static_cast<const std::int32_t*>(c.data) + base, len, dst); break;
        case ExprScalarType::Int16:
            convert_block(static_cast<const std::int16_t*>(c.data) + base, len, dst); break;
        case ExprScalarType::Int8:
            convert_block(static_cast<const std::int8_t*>(c.data) + base, len, dst); break;
        case ExprScalarType::UInt64:
            convert_block(static_cast<const std::uint64_t*>(c.data) + base, len, dst); break;
        case ExprScalarType::UInt32:
            convert_block(static_cast<const std::uint32_t*>(c.data) + base, len, dst); break;
        case ExprScalarType::UInt16:
            convert_block(static_cast<const std::uint16_t*>(c.data) + base, len, dst); break;
        default:
            convert_block(static_cast<const std::uint8_t*>(c.data) + base, len, dst); break;
    }
}

template <typename T>
inline T broadcast_value(const ExprColumn& c) {
    T v = T(0);
    load_block(c, 0, 1, &v);
    return v;
}

}  // namespace

// ---------------------------------------------------------------------------
// Compilation
// ---------------------------------------------------------------------------

std::string ExpressionEngine::normalise(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        const char next = (i + 1 < s.size()) ? s[i + 1] : '\0';
        if (c == '*' && next == '*') {
            out.push_back('^');
            ++i;
        } else if (c == '&') {
            out += " and ";
            if (next == '&') ++i;
        } else if (c == '|') {
            out += " or ";
            if (next == '|') ++i;
        } else if (c == '~') {
            out += " not ";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::vector<std::string> ExpressionEngine::free_variables(const std::string& s) {
    std::vector<std::string> names;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t j = i;
            while (j < s.size() &&
                   (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) {
                ++j;
            }
            const std::string name = s.substr(i, j - i);
            std::size_t k = j;
            while (k < s.size() && std::isspace(static_cast<unsigned char>(s[k]))) ++k;
            const bool is_call = (k < s.size() && s[k] == '(');
            if (!is_call && !is_reserved(name) &&
                std::find(names.begin(), names.end(), name) == names.end()) {
                names.push_back(name);
            }
            i = j;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            // Skip a number whole, so the 'e' of 1e-3 is never taken for a name.
            std::size_t j = i;
            while (j < s.size() &&
                   (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.')) {
                ++j;
            }
            if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) {
                std::size_t k = j + 1;
                if (k < s.size() && (s[k] == '+' || s[k] == '-')) ++k;
                if (k < s.size() && std::isdigit(static_cast<unsigned char>(s[k]))) {
                    j = k;
                    while (j < s.size() &&
                           std::isdigit(static_cast<unsigned char>(s[j]))) {
                        ++j;
                    }
                }
            }
            i = j;
        } else {
            ++i;
        }
    }
    return names;
}

bool ExpressionEngine::compile(const std::string& expression) {
    program_.clear();
    program_depth_ = 0;
    program_cache_size_ = 0;
    expression_ = expression;
    normalised_ = normalise(expression);
    variables_ = free_variables(normalised_);
    if (!compile_program(normalised_)) {
        program_.clear();
        program_depth_ = 0;
        program_cache_size_ = 0;
        return false;
    }
    return true;
}

bool ExpressionEngine::compile_program(const std::string& normalised) {
    program_.clear();
    program_cache_size_ = 0;
    std::vector<Token> tokens;
    if (!tokenize(normalised, &tokens)) return false;
    if (tokens.empty()) return false;

    std::vector<std::pair<bool, int> > stack;  // (is_function, id); '(' is (false,-1)
    auto emit = [this](int kind, double value, int index) {
        VecOp o; o.kind = kind; o.value = value; o.index = index;
        program_.push_back(o);
    };

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const Token& t = tokens[i];
        if (t.kind == 0) {
            emit(OP_CONST, t.number, 0);
        } else if (t.kind == 1) {
            const bool is_call = (i + 1 < tokens.size() && tokens[i + 1].kind == 3);
            if (is_call) {
                const int f = function_id(t.name);
                if (f < 0) return false;
                stack.push_back(std::make_pair(true, f));
            } else if (t.name == "pi") {
                emit(OP_CONST, 3.14159265358979323846, 0);
            } else if (t.name == "e") {
                emit(OP_CONST, 2.71828182845904523536, 0);
            } else {
                std::vector<std::string>::const_iterator it =
                    std::find(variables_.begin(), variables_.end(), t.name);
                if (it == variables_.end()) return false;
                emit(OP_VAR, 0.0, static_cast<int>(it - variables_.begin()));
            }
        } else if (t.kind == 2) {
            if (t.unary) {
                if (t.name == "not") stack.push_back(std::make_pair(true, F_NOT));
                else if (t.op == B_SUB) stack.push_back(std::make_pair(true, F_NEG));
                continue;
            }
            while (!stack.empty()) {
                const bool is_fun = stack.back().first;
                const int top = stack.back().second;
                int p;
                if (is_fun) {
                    // Only a pending prefix operator can be a bare function on
                    // the stack; a call always pushed its '(' straight after.
                    if (top != F_NEG && top != F_NOT) break;
                    p = unary_precedence(top);
                } else {
                    if (top < 0) break;  // '('
                    p = precedence(top);
                }
                const bool higher = p > precedence(t.op);
                const bool equal_left = p == precedence(t.op) && t.op != B_POW;
                if (!(higher || equal_left)) break;
                emit(is_fun ? OP_FUN : OP_BIN, 0.0, top);
                stack.pop_back();
            }
            stack.push_back(std::make_pair(false, t.op));
        } else if (t.kind == 3) {
            stack.push_back(std::make_pair(false, -1));
        } else if (t.kind == 4) {
            bool found = false;
            while (!stack.empty()) {
                if (!stack.back().first && stack.back().second == -1) {
                    stack.pop_back(); found = true; break;
                }
                emit(stack.back().first ? OP_FUN : OP_BIN, 0.0, stack.back().second);
                stack.pop_back();
            }
            if (!found) return false;
            if (!stack.empty() && stack.back().first && stack.back().second != F_NEG) {
                emit(OP_FUN, 0.0, stack.back().second);
                stack.pop_back();
            }
        } else if (t.kind == 5) {
            while (!stack.empty() &&
                   !(!stack.back().first && stack.back().second == -1)) {
                emit(stack.back().first ? OP_FUN : OP_BIN, 0.0, stack.back().second);
                stack.pop_back();
            }
        }
        // A pending unary minus applies once its operand is complete, but not
        // before a '^': -x^2 is -(x^2), as in Python.
        if (t.kind == 0 || t.kind == 1 || t.kind == 4) {
            while (!stack.empty() && stack.back().first &&
                   (stack.back().second == F_NEG || stack.back().second == F_NOT)) {
                // 'not' takes the whole comparison to its right, so it waits.
                if (stack.back().second == F_NOT) break;
                if (i + 1 < tokens.size() && tokens[i + 1].kind == 3) break;
                if (i + 1 < tokens.size() && tokens[i + 1].kind == 2 &&
                    tokens[i + 1].op == B_POW && !tokens[i + 1].unary) break;
                emit(OP_FUN, 0.0, F_NEG);
                stack.pop_back();
            }
        }
    }
    while (!stack.empty()) {
        if (!stack.back().first && stack.back().second == -1) return false;
        emit(stack.back().first ? OP_FUN : OP_BIN, 0.0, stack.back().second);
        stack.pop_back();
    }
    if (program_.empty()) return false;

    // Fold every constant subtree, and with it "push constant, then raise to
    // it" into one instruction, so the exponent is known when the block runs
    // rather than fetched per element from a buffer.
    //
    // The fold used to look only for a bare OP_CONST under a power, which
    // missed the shape real equations are made of: `x**(-1)` parses as a
    // *negated* constant -- OP_CONST 1 then OP_FUN F_NEG -- so the exponent
    // stayed a buffer and every element paid a std::pow() call, 9.7 ns a point.
    {
        std::vector<VecOp> folded;
        folded.reserve(program_.size());
        auto is_const_at = [&folded](std::size_t back) {
            return folded.size() >= back &&
                   folded[folded.size() - back].kind == OP_CONST;
        };
        for (std::size_t i = 0; i < program_.size(); ++i) {
            const VecOp& o = program_[i];
            // `not` is left alone: folding it to a plain 1.0 or 0.0 would lose
            // the fact that its slot is a mask, which is reconciled at the
            // stack's boundaries and not here. Comparisons, below, likewise.
            if (o.kind == OP_FUN && !binary_function(o.index) && o.index != F_NOT &&
                is_const_at(1)) {
                folded.back().value = apply_scalar_fun(o.index, folded.back().value);
                continue;
            }
            if (o.kind == OP_FUN && binary_function(o.index) && is_const_at(2) &&
                is_const_at(1)) {
                const double b = folded.back().value;
                folded.pop_back();
                folded.back().value = apply_binary_scalar(o.index, folded.back().value, b);
                continue;
            }
            if (o.kind == OP_POWC && is_const_at(1)) {
                folded.back().value = std::pow(folded.back().value, o.value);
                continue;
            }
            if (o.kind == OP_BIN && o.index <= B_POW && is_const_at(2) && is_const_at(1)) {
                const double b = folded.back().value;
                folded.pop_back();
                double& a = folded.back().value;
                switch (o.index) {
                    case B_ADD: a += b; break;
                    case B_SUB: a -= b; break;
                    case B_MUL: a *= b; break;
                    case B_DIV: a /= b; break;
                    default: a = std::pow(a, b); break;
                }
                continue;
            }
            if (o.kind == OP_BIN && o.index == B_POW && is_const_at(1)) {
                const double c = folded.back().value;
                folded.pop_back();
                VecOp p; p.kind = OP_POWC; p.value = c; p.index = 0;
                folded.push_back(p);
                continue;
            }
            folded.push_back(o);
        }
        program_.swap(folded);
    }

    if (!eliminate_common_subexpressions()) return false;

    // A well-formed program leaves exactly one value, and never needs a stack
    // deeper than the buffers reserved for it.
    int depth = 0, max_depth = 0;
    for (const VecOp& o : program_) {
        if (o.kind == OP_CONST || o.kind == OP_VAR || o.kind == OP_LOADC) ++depth;
        else if (o.kind == OP_POWC || o.kind == OP_SAVE) { /* in place */ }
        else if (o.kind == OP_BIN) depth -= 1;
        else if (binary_function(o.index)) depth -= 1;
        if (depth < 1) return false;
        max_depth = std::max(max_depth, depth);
    }
    if (depth != 1) return false;
    // The block stack is depth x 512 x sizeof(T); without a cap a deeply
    // nested expression asks for unbounded scratch.
    if (max_depth > kMaxDepth) return false;
    program_depth_ = max_depth;
    return true;
}

/*!
 * \brief Compute a repeated subtree once and reuse it, where that is cheaper.
 *
 * The RPN compiler emits a subtree wherever it appears, so an equation that
 * mentions `4*D*x/w_r**2` three times evaluates it three times.
 *
 * The pass hash-conses the program into a DAG: walking the RPN with a stack of
 * node ids, a node keyed by (opcode, left id, right id) that has been seen
 * before *is* the earlier node, because the operands are pure. A node reached
 * more than once is a candidate.
 *
 * Sharing is not free -- a cached slot costs one block copy to fill and one to
 * read back -- so a candidate is only taken when recomputing it costs more than
 * that. `x/1.2` does not qualify; `exp(-x/tau)` does. Cost-blind sharing was
 * measured to be a pessimisation, which is why this is per candidate.
 *
 * \return false only for a malformed program, which the caller treats as "this
 *         evaluator cannot represent the expression".
 */
bool ExpressionEngine::eliminate_common_subexpressions() {
    program_cache_size_ = 0;
    const std::size_t n_ops = program_.size();
    if (n_ops < 4) return true;

    struct CseKey { int kind; int index; double value; int a; int b; };
    std::vector<CseKey> keys;
    std::vector<int> counts, costs;
    std::vector<int> ids(n_ops, -1), child_a(n_ops, -1), child_b(n_ops, -1);

    // What one instruction costs, in passes over a block. A hardware divide, a
    // multiply and a memcpy are all "one"; exp/log/sin and a general
    // std::pow() are a libm call per element and are not close.
    auto op_cost = [](const VecOp& o) -> int {
        if (o.kind == OP_POWC) {
            const double c = o.value;
            if (c == -1.0 || c == 0.5 || c == 1.0 || c == 2.0) return 1;
            if (c == 3.0 || c == -2.0 || c == -0.5) return 2;
            return 8;
        }
        if (o.kind == OP_BIN) return (o.index == B_POW) ? 8 : 1;
        switch (o.index) {  // OP_FUN
            case F_EXP: case F_LOG: case F_LOG10:
            case F_SIN: case F_COS: case F_TAN: case F_POW2: return 8;
            default: return 1;
        }
    };

    std::vector<int> positions;
    positions.reserve(n_ops);
    for (std::size_t i = 0; i < n_ops; ++i) {
        const VecOp& o = program_[i];
        int arity;
        if (o.kind == OP_CONST || o.kind == OP_VAR) arity = 0;
        else if (o.kind == OP_POWC) arity = 1;
        else if (o.kind == OP_BIN) arity = 2;
        else arity = binary_function(o.index) ? 2 : 1;
        if (static_cast<int>(positions.size()) < arity) return false;
        int a = -1, b = -1;
        if (arity == 2) {
            b = positions.back(); positions.pop_back();
            a = positions.back(); positions.pop_back();
        } else if (arity == 1) {
            a = positions.back(); positions.pop_back();
        }
        child_a[i] = a;
        child_b[i] = b;

        CseKey k;
        k.kind = o.kind;
        k.index = o.index;
        k.value = o.value;
        k.a = (a >= 0) ? ids[static_cast<std::size_t>(a)] : -1;
        k.b = (b >= 0) ? ids[static_cast<std::size_t>(b)] : -1;
        int found = -1;
        for (std::size_t j = 0; j < keys.size(); ++j) {
            // The constant is compared bitwise, so -0.0 is not 0.0 and a NaN is
            // only itself: two subtrees are shared when they are the same text,
            // never when they merely compare equal.
            if (keys[j].kind == k.kind && keys[j].index == k.index &&
                keys[j].a == k.a && keys[j].b == k.b &&
                std::memcmp(&keys[j].value, &k.value, sizeof(double)) == 0) {
                found = static_cast<int>(j);
                break;
            }
        }
        if (found < 0) {
            found = static_cast<int>(keys.size());
            keys.push_back(k);
            counts.push_back(0);
            int c = 0;
            if (o.kind == OP_CONST) {
                c = 0;
            } else if (o.kind == OP_VAR) {
                c = 1;  // a column enters as a copy of the block
            } else {
                c = op_cost(o);
                if (k.a >= 0) c += costs[static_cast<std::size_t>(k.a)];
                if (k.b >= 0) c += costs[static_cast<std::size_t>(k.b)];
            }
            costs.push_back(c);
        }
        ids[i] = found;
        counts[static_cast<std::size_t>(found)] += 1;
        positions.push_back(static_cast<int>(i));
    }
    if (positions.size() != 1) return false;

    std::vector<std::pair<int, int> > ranked;  // (benefit, node id)
    for (std::size_t j = 0; j < keys.size(); ++j) {
        if (counts[j] < 2 || costs[j] < 3) continue;
        ranked.push_back(std::make_pair((counts[j] - 1) * (costs[j] - 1),
                                        static_cast<int>(j)));
    }
    if (ranked.empty()) return true;
    std::sort(ranked.begin(), ranked.end(),
              [](const std::pair<int, int>& l, const std::pair<int, int>& r) {
                  return l.first > r.first;
              });
    std::vector<int> slot_of(keys.size(), -1);
    int n_cache = 0;
    for (std::size_t j = 0; j < ranked.size() && n_cache < kMaxCache; ++j) {
        slot_of[static_cast<std::size_t>(ranked[j].second)] = n_cache++;
    }

    // Re-emit in the same left-to-right post-order, replacing every occurrence
    // of a shared node after the first with a load of its slot.
    std::vector<VecOp> out;
    out.reserve(n_ops + static_cast<std::size_t>(n_cache));
    std::vector<char> saved(keys.size(), 0);
    std::vector<std::pair<int, int> > work;  // (position, visited-children?)
    work.push_back(std::make_pair(static_cast<int>(n_ops) - 1, 0));
    while (!work.empty()) {
        const int pos = work.back().first;
        const int phase = work.back().second;
        work.pop_back();
        const std::size_t id =
            static_cast<std::size_t>(ids[static_cast<std::size_t>(pos)]);
        if (phase == 0) {
            if (slot_of[id] >= 0 && saved[id]) {
                VecOp l; l.kind = OP_LOADC; l.value = 0.0; l.index = slot_of[id];
                out.push_back(l);
                continue;
            }
            work.push_back(std::make_pair(pos, 1));
            if (child_b[static_cast<std::size_t>(pos)] >= 0) {
                work.push_back(std::make_pair(child_b[static_cast<std::size_t>(pos)], 0));
            }
            if (child_a[static_cast<std::size_t>(pos)] >= 0) {
                work.push_back(std::make_pair(child_a[static_cast<std::size_t>(pos)], 0));
            }
        } else {
            out.push_back(program_[static_cast<std::size_t>(pos)]);
            if (slot_of[id] >= 0 && !saved[id]) {
                VecOp s; s.kind = OP_SAVE; s.value = 0.0; s.index = slot_of[id];
                out.push_back(s);
                saved[id] = 1;
            }
        }
    }

    // A node whose every repeat sat inside a *larger* shared node is now
    // reached once, and its store is pure cost. Drop those.
    std::vector<char> loaded(static_cast<std::size_t>(n_cache), 0);
    for (const VecOp& o : out) {
        if (o.kind == OP_LOADC) loaded[static_cast<std::size_t>(o.index)] = 1;
    }
    std::vector<VecOp> kept;
    kept.reserve(out.size());
    for (const VecOp& o : out) {
        if (o.kind == OP_SAVE && !loaded[static_cast<std::size_t>(o.index)]) continue;
        kept.push_back(o);
    }
    program_.swap(kept);
    program_cache_size_ = n_cache;
    return true;
}

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

template <typename T>
void ExpressionEngine::run(const std::vector<ExprColumn>& columns,
                           std::size_t n_rows, double* out, std::uint64_t* words,
                           Blocks<T>& s) const {
    const std::size_t depth = static_cast<std::size_t>(program_depth_);
    if (s.stack.size() < depth * kBlock) s.stack.assign(depth * kBlock, T(0));
    if (s.bstack.size() < depth * kBlock) s.bstack.assign(depth * kBlock, 0);
    if (s.is_bool.size() < depth) s.is_bool.assign(depth, 0);
    if (s.is_scalar.size() < depth) {
        s.is_scalar.assign(depth, 0);
        s.scalar_value.assign(depth, T(0));
    }
    const std::size_t n_cache = static_cast<std::size_t>(program_cache_size_);
    if (n_cache > 0) {
        if (s.cache.size() < n_cache * kBlock) s.cache.assign(n_cache * kBlock, T(0));
        if (s.cache_bool.size() < n_cache * kBlock) s.cache_bool.assign(n_cache * kBlock, 0);
        if (s.cache_is_bool.size() < n_cache) {
            s.cache_is_bool.assign(n_cache, 0);
            s.cache_is_scalar.assign(n_cache, 0);
            s.cache_scalar.assign(n_cache, T(0));
        }
    }
    if (words != nullptr && s.pack_scratch.size() < kBlock) s.pack_scratch.assign(kBlock, 0);

    // The tier's kernels, picked once per evaluation: one indirect call per
    // block operation, and the CPU never sees an instruction it lacks.
    const KernelTable<T> k = kernels_for<T>(current_tier());

    T* stack = s.stack.data();
    unsigned char* bstack = s.bstack.data();
    char* is_bool = s.is_bool.data();
    char* is_scalar = s.is_scalar.data();
    T* scalar_value = s.scalar_value.data();

    for (std::size_t base = 0; base < n_rows; base += kBlock) {
        const std::size_t len = std::min(kBlock, n_rows - base);
        std::fill(s.is_bool.begin(), s.is_bool.end(), 0);
        int top = 0;

        // The mirror of booleanise(): a comparison's byte mask used as a
        // number. `(x > 2) * 3` is legal and means 0 or 3, so the mask is
        // widened back before any arithmetic reads the slot.
        auto numerify = [&](int slot) {
            const std::size_t sl = static_cast<std::size_t>(slot);
            if (!is_bool[sl]) return;
            const unsigned char* src = bstack + sl * kBlock;
            T* dst = stack + sl * kBlock;
            for (std::size_t i = 0; i < len; ++i) dst[i] = src[i] ? T(1) : T(0);
            is_bool[sl] = 0;
            is_scalar[sl] = 0;
        };
        auto materialise = [&](int slot) {
            numerify(slot);
            const std::size_t sl = static_cast<std::size_t>(slot);
            if (!is_scalar[sl]) return;
            T* dst = stack + sl * kBlock;
            const T v = scalar_value[sl];
            for (std::size_t i = 0; i < len; ++i) dst[i] = v;
            is_scalar[sl] = 0;
        };
        // `and`, `or` and `not` read the byte stack, so a slot holding numbers
        // is cast to bytes first -- `(x > 2) and y` is legal and its right
        // operand is a column of numbers. Nonzero is true, as in numpy.
        auto booleanise = [&](int slot) {
            const std::size_t sl = static_cast<std::size_t>(slot);
            if (is_bool[sl]) return;
            unsigned char* dst = bstack + sl * kBlock;
            if (is_scalar[sl]) {
                std::memset(dst, scalar_value[sl] != T(0) ? 1 : 0, len);
            } else {
                const T* src = stack + sl * kBlock;
                for (std::size_t i = 0; i < len; ++i) dst[i] = (src[i] != T(0)) ? 1 : 0;
            }
            is_bool[sl] = 1;
        };

        for (const VecOp& o : program_) {
            if (o.kind == OP_CONST) {
                // A push owns the slot outright: whatever it last held -- in
                // particular a comparison's byte mask -- is gone. Leaving the
                // boolean flag set makes `a>0 and b<1 or c>2` reuse slot 1 for
                // `c` while still believing it holds `b<1`.
                is_bool[top] = 0;
                is_scalar[top] = 1;
                scalar_value[top] = static_cast<T>(o.value);
                ++top;
            } else if (o.kind == OP_VAR) {
                const ExprColumn& c = columns[static_cast<std::size_t>(o.index)];
                is_bool[top] = 0;
                if (c.is_vector) {
                    k.load(c, base, len, stack + static_cast<std::size_t>(top) * kBlock);
                    is_scalar[top] = 0;
                } else {
                    is_scalar[top] = 1;
                    scalar_value[top] = broadcast_value<T>(c);
                }
                ++top;
            } else if (o.kind == OP_SAVE) {
                // A shared subtree, finished: copied aside *with* its type, so
                // a load restores the flags a push would have set.
                const std::size_t src = static_cast<std::size_t>(top - 1);
                const std::size_t c = static_cast<std::size_t>(o.index);
                if (is_bool[src]) {
                    std::memcpy(&s.cache_bool[c * kBlock], bstack + src * kBlock, len);
                    s.cache_is_bool[c] = 1;
                    s.cache_is_scalar[c] = 0;
                } else if (is_scalar[src]) {
                    s.cache_scalar[c] = scalar_value[src];
                    s.cache_is_bool[c] = 0;
                    s.cache_is_scalar[c] = 1;
                } else {
                    std::memcpy(&s.cache[c * kBlock], stack + src * kBlock, len * sizeof(T));
                    s.cache_is_bool[c] = 0;
                    s.cache_is_scalar[c] = 0;
                }
            } else if (o.kind == OP_LOADC) {
                const std::size_t dst = static_cast<std::size_t>(top);
                const std::size_t c = static_cast<std::size_t>(o.index);
                if (s.cache_is_bool[c]) {
                    std::memcpy(bstack + dst * kBlock, &s.cache_bool[c * kBlock], len);
                    is_bool[dst] = 1;
                    is_scalar[dst] = 0;
                } else if (s.cache_is_scalar[c]) {
                    is_bool[dst] = 0;
                    is_scalar[dst] = 1;
                    scalar_value[dst] = s.cache_scalar[c];
                } else {
                    std::memcpy(stack + dst * kBlock, &s.cache[c * kBlock], len * sizeof(T));
                    is_bool[dst] = 0;
                    is_scalar[dst] = 0;
                }
                ++top;
            } else if (o.kind == OP_POWC) {
                numerify(top - 1);
                if (is_scalar[top - 1]) {
                    T& v = scalar_value[top - 1];
                    v = static_cast<T>(std::pow(static_cast<double>(v), o.value));
                    continue;
                }
                k.powc(o.value, stack + static_cast<std::size_t>(top - 1) * kBlock, len);
            } else if (o.kind == OP_BIN && o.index >= B_LT && o.index <= B_NE) {
                // comparison: two numbers in, one byte mask out
                const int rhs = --top, lhs = top - 1;
                materialise(lhs);
                materialise(rhs);
                k.cmp(o.index, stack + static_cast<std::size_t>(lhs) * kBlock,
                      stack + static_cast<std::size_t>(rhs) * kBlock,
                      bstack + static_cast<std::size_t>(lhs) * kBlock, len);
                is_bool[lhs] = 1;
            } else if (o.kind == OP_BIN && (o.index == B_AND || o.index == B_OR)) {
                const int rhs = --top, lhs = top - 1;
                booleanise(lhs);
                booleanise(rhs);
                k.bcombine(o.index, bstack + static_cast<std::size_t>(lhs) * kBlock,
                           bstack + static_cast<std::size_t>(rhs) * kBlock, len);
                is_bool[lhs] = 1;
            } else if (o.kind == OP_FUN && o.index == F_NOT) {
                booleanise(top - 1);
                k.bnot(bstack + static_cast<std::size_t>(top - 1) * kBlock, len);
                is_bool[top - 1] = 1;
            } else if (o.kind == OP_BIN) {
                const int rhs = --top, lhs = top - 1;
                numerify(lhs);
                numerify(rhs);
                const bool rs = is_scalar[rhs] != 0;
                const bool ls = is_scalar[lhs] != 0;
                if (ls && rs) {
                    // Both still scalar: fold, and no array is touched at all.
                    T& a = scalar_value[lhs];
                    const T b = scalar_value[rhs];
                    switch (o.index) {
                        case B_ADD: a += b; break;
                        case B_SUB: a -= b; break;
                        case B_MUL: a *= b; break;
                        case B_DIV: a /= b; break;
                        default: a = std::pow(a, b);
                    }
                    continue;
                }
                if (rs && o.index == B_POW) {
                    // vector ** scalar takes the same reciprocal/sqrt/square
                    // kernels a folded constant exponent does, whether the
                    // exponent was written as a literal or arrived as a column.
                    k.powc(static_cast<double>(scalar_value[rhs]),
                           stack + static_cast<std::size_t>(lhs) * kBlock, len);
                    continue;
                }
                if (rs && o.index <= B_DIV) {
                    k.bin_s(o.index, stack + static_cast<std::size_t>(lhs) * kBlock,
                            scalar_value[rhs], len);
                    continue;
                }
                if (ls && o.index <= B_DIV) {
                    k.bin_sv(o.index, stack + static_cast<std::size_t>(lhs) * kBlock,
                             stack + static_cast<std::size_t>(rhs) * kBlock,
                             scalar_value[lhs], len);
                    is_scalar[lhs] = 0;
                    continue;
                }
                materialise(lhs);
                materialise(rhs);
                k.bin(o.index, stack + static_cast<std::size_t>(lhs) * kBlock,
                      stack + static_cast<std::size_t>(rhs) * kBlock, len);
            } else if (binary_function(o.index)) {
                const int rhs = --top, lhs = top - 1;
                materialise(lhs);
                materialise(rhs);
                apply_fun2(o.index, stack + static_cast<std::size_t>(lhs) * kBlock,
                           stack + static_cast<std::size_t>(rhs) * kBlock, len);
            } else {
                numerify(top - 1);
                if (is_scalar[top - 1]) {
                    T& v = scalar_value[top - 1];
                    v = static_cast<T>(apply_scalar_fun(o.index, static_cast<double>(v)));
                    continue;
                }
                k.fun(o.index, stack + static_cast<std::size_t>(top - 1) * kBlock, len);
            }
        }

        // The block's closing store, and the only place the two callers part.
        // Truthiness is numpy's cast to bool: anything that is not zero is
        // true, which keeps a negative value and a NaN true. A gate written as
        // a comparison never reaches that rule -- its slot is already a byte
        // mask and packs straight out.
        if (words != nullptr) {
            // kBlock is a multiple of 64, so a block always starts on a word.
            std::uint64_t* w = words + (base >> 6);
            if (is_bool[0]) {
                k.pack(bstack, len, w);
            } else if (is_scalar[0]) {
                const bool t = scalar_value[0] != T(0);
                std::size_t k = 0;
                for (; k + 64 <= len; k += 64) w[k >> 6] = t ? ~0ULL : 0ULL;
                if (k < len) w[k >> 6] = t ? ((1ULL << (len - k)) - 1ULL) : 0ULL;
            } else {
                unsigned char* tmp = s.pack_scratch.data();
                const T* v = stack;
                for (std::size_t i = 0; i < len; ++i) tmp[i] = (v[i] != T(0)) ? 1 : 0;
                k.pack(tmp, len, w);
            }
        } else if (is_bool[0]) {
            const unsigned char* m = bstack;
            for (std::size_t i = 0; i < len; ++i) out[base + i] = m[i] ? 1.0 : 0.0;
        } else if (is_scalar[0]) {
            const double v = static_cast<double>(scalar_value[0]);
            for (std::size_t i = 0; i < len; ++i) out[base + i] = v;
        } else {
            const T* v = stack;
            for (std::size_t i = 0; i < len; ++i) out[base + i] = static_cast<double>(v[i]);
        }
    }
}

void ExpressionEngine::dispatch(const std::vector<ExprColumn>& columns,
                                std::size_t n_rows, double* out,
                                std::uint64_t* words) const {
    if (program_.empty()) {
        throw std::domain_error(
            "ExpressionEngine: no compiled program; compile() must succeed first");
    }
    if (columns.size() != variables_.size()) {
        throw std::domain_error(
            "ExpressionEngine: one column is needed per variable, in the order "
            "variables() lists them");
    }
    for (const ExprColumn& c : columns) {
        if (c.data == nullptr) {
            throw std::domain_error("ExpressionEngine: a column has no data");
        }
    }
    if (n_rows == 0) return;
    // Float32 columns are evaluated in float32: nothing is widened, twice as
    // many lanes fit a SIMD register, and the answer is bit-for-bit what numpy
    // gives over the same columns. Any other mix runs in double, which is exact
    // for every integer type up to 2^53.
    bool all_f32 = !columns.empty();
    for (const ExprColumn& c : columns) {
        if (c.type != ExprScalarType::Float32) { all_f32 = false; break; }
    }
    if (all_f32) {
        run<float>(columns, n_rows, out, words, scratch32_);
    } else {
        run<double>(columns, n_rows, out, words, scratch64_);
    }
}

void ExpressionEngine::compute_mask(const std::vector<ExprColumn>& columns,
                                    std::size_t n_rows, std::uint64_t* words) const {
    dispatch(columns, n_rows, nullptr, words);
}

void ExpressionEngine::compute_values(const std::vector<ExprColumn>& columns,
                                      std::size_t n_rows, double* out) const {
    dispatch(columns, n_rows, out, nullptr);
}

}  // namespace pto


// ===========================================================================
// Codecs
// ===========================================================================
#if defined(PTOLIB_WITH_ZSTD)
#include <zstd.h>
#endif
#if defined(PTOLIB_WITH_BROTLI)
#include <brotli/decode.h>
#include <brotli/encode.h>
#endif
#if defined(PTOLIB_WITH_LZ4)
#include <lz4frame.h>
#endif

namespace pto {

namespace {

#if defined(PTOLIB_WITH_ZSTD)
bool zstd_compress(const unsigned char* in, std::size_t n, int level,
                   std::vector<unsigned char>& out) {
    const std::size_t bound = ZSTD_compressBound(n);
    if (ZSTD_isError(bound)) return false;
    out.resize(bound);
    const std::size_t got = ZSTD_compress(out.data(), out.size(), in, n,
                                          level < 0 ? ZSTD_CLEVEL_DEFAULT : level);
    if (ZSTD_isError(got)) { out.clear(); return false; }
    out.resize(got);
    return true;
}
bool zstd_decompress(const unsigned char* in, std::size_t n, std::size_t raw_size,
                     std::vector<unsigned char>& out) {
    if (raw_size == 0) {
        const unsigned long long known = ZSTD_getFrameContentSize(in, n);
        if (known != ZSTD_CONTENTSIZE_UNKNOWN && known != ZSTD_CONTENTSIZE_ERROR)
            raw_size = static_cast<std::size_t>(known);
    }
    if (raw_size != 0) {
        out.resize(raw_size);
        const std::size_t got = ZSTD_decompress(out.data(), out.size(), in, n);
        if (ZSTD_isError(got) || got != raw_size) { out.clear(); return false; }
        return true;
    }
    // Size unknown: stream it out, growing as it comes.
    ZSTD_DStream* ds = ZSTD_createDStream();
    if (ds == nullptr) return false;
    ZSTD_initDStream(ds);
    ZSTD_inBuffer src = {in, n, 0};
    out.clear();
    std::vector<unsigned char> chunk(ZSTD_DStreamOutSize());
    bool ok = true;
    while (src.pos < src.size) {
        ZSTD_outBuffer dst = {chunk.data(), chunk.size(), 0};
        const std::size_t r = ZSTD_decompressStream(ds, &dst, &src);
        if (ZSTD_isError(r)) { ok = false; break; }
        out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(dst.pos));
        if (r == 0) break;
    }
    ZSTD_freeDStream(ds);
    if (!ok) out.clear();
    return ok;
}
#endif

#if defined(PTOLIB_WITH_BROTLI)
bool brotli_compress(const unsigned char* in, std::size_t n, int level,
                     std::vector<unsigned char>& out) {
    // Quality 5 by default, not brotli's own 11: on numeric columns 11 is
    // twenty times slower than 5 for no more ratio, and a caller that wants
    // an archive setting asks for it.
    int quality = level < 0 ? 5 : level;
    if (quality > BROTLI_MAX_QUALITY) quality = BROTLI_MAX_QUALITY;
    if (quality < BROTLI_MIN_QUALITY) quality = BROTLI_MIN_QUALITY;
    std::size_t size = BrotliEncoderMaxCompressedSize(n);
    if (size == 0) size = n + 64;
    out.resize(size);
    if (!BrotliEncoderCompress(quality, BROTLI_DEFAULT_WINDOW, BROTLI_MODE_GENERIC, n, in,
                               &size, out.data())) {
        out.clear();
        return false;
    }
    out.resize(size);
    return true;
}
bool brotli_decompress(const unsigned char* in, std::size_t n, std::size_t raw_size,
                       std::vector<unsigned char>& out) {
    if (raw_size != 0) {
        out.resize(raw_size);
        std::size_t size = raw_size;
        if (BrotliDecoderDecompress(n, in, &size, out.data()) != BROTLI_DECODER_RESULT_SUCCESS ||
            size != raw_size) {
            out.clear();
            return false;
        }
        return true;
    }
    // Size unknown -- a stream from a writer that recorded none: stream it out.
    BrotliDecoderState* st = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
    if (st == nullptr) return false;
    std::size_t available_in = n;
    const std::uint8_t* next_in = in;
    out.clear();
    std::vector<unsigned char> chunk(1u << 16);
    bool ok = true;
    for (;;) {
        std::size_t available_out = chunk.size();
        std::uint8_t* next_out = chunk.data();
        const BrotliDecoderResult r = BrotliDecoderDecompressStream(
                st, &available_in, &next_in, &available_out, &next_out, nullptr);
        out.insert(out.end(), chunk.begin(),
                   chunk.begin() + static_cast<std::ptrdiff_t>(chunk.size() - available_out));
        if (r == BROTLI_DECODER_RESULT_SUCCESS) break;
        if (r == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) continue;
        ok = false;   // needs more input (truncated) or an error
        break;
    }
    BrotliDecoderDestroyInstance(st);
    if (!ok) out.clear();
    return ok;
}
#endif

#if defined(PTOLIB_WITH_LZ4)
// The lz4 FRAME format, not the raw block: a frame carries its content size
// and a checksum, so a stream from here reads with the `lz4` tool and one
// from the tool reads here.
bool lz4_compress(const unsigned char* in, std::size_t n, int level,
                  std::vector<unsigned char>& out) {
    LZ4F_preferences_t prefs;
    std::memset(&prefs, 0, sizeof(prefs));
    prefs.frameInfo.contentSize = n;
    prefs.frameInfo.blockSizeID = LZ4F_max4MB;
    prefs.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
    prefs.compressionLevel = level < 0 ? 0 : level;   // 0 fast; 3+ the HC coder, up to 12
    const std::size_t bound = LZ4F_compressFrameBound(n, &prefs);
    out.resize(bound);
    const std::size_t got = LZ4F_compressFrame(out.data(), bound, in, n, &prefs);
    if (LZ4F_isError(got)) { out.clear(); return false; }
    out.resize(got);
    return true;
}
bool lz4_decompress(const unsigned char* in, std::size_t n, std::size_t raw_size,
                    std::vector<unsigned char>& out) {
    LZ4F_dctx* d = nullptr;
    if (LZ4F_isError(LZ4F_createDecompressionContext(&d, LZ4F_VERSION))) return false;
    out.clear();
    if (raw_size == 0) {
        LZ4F_frameInfo_t info;
        std::size_t peek = n;
        const std::size_t r = LZ4F_getFrameInfo(d, &info, in, &peek);
        if (!LZ4F_isError(r) && info.contentSize != 0) raw_size = static_cast<std::size_t>(info.contentSize);
        LZ4F_resetDecompressionContext(d);
    }
    out.reserve(raw_size);
    std::vector<unsigned char> chunk(1u << 20);
    std::size_t consumed = 0;
    bool ok = true;
    for (;;) {
        std::size_t dst_size = chunk.size();
        std::size_t src_size = n - consumed;
        const std::size_t r = LZ4F_decompress(d, chunk.data(), &dst_size, in + consumed, &src_size, nullptr);
        if (LZ4F_isError(r)) { ok = false; break; }
        out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(dst_size));
        consumed += src_size;
        if (r == 0) break;                       // frame complete
        if (src_size == 0 && dst_size == 0) { ok = false; break; }   // truncated
    }
    LZ4F_freeDecompressionContext(d);
    if (ok && raw_size != 0 && out.size() != raw_size) ok = false;
    if (!ok) out.clear();
    return ok;
}
#endif

struct CodecRegistry {
    std::mutex mutex;
    std::map<std::string, Codec> codecs;
    CodecRegistry() {
#if defined(PTOLIB_WITH_ZSTD)
        { Codec c; c.name = "zstd"; c.compress = &zstd_compress; c.decompress = &zstd_decompress; codecs[c.name] = c; }
#endif
#if defined(PTOLIB_WITH_BROTLI)
        { Codec c; c.name = "brotli"; c.compress = &brotli_compress; c.decompress = &brotli_decompress; codecs[c.name] = c; }
#endif
#if defined(PTOLIB_WITH_LZ4)
        { Codec c; c.name = "lz4"; c.compress = &lz4_compress; c.decompress = &lz4_decompress; codecs[c.name] = c; }
#endif
    }
};

CodecRegistry& codec_registry() {
    static CodecRegistry* r = new CodecRegistry();   // never destroyed: codecs outlive static teardown
    return *r;
}

bool find_codec(const std::string& name, Codec* out) {
    CodecRegistry& r = codec_registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    std::map<std::string, Codec>::const_iterator it = r.codecs.find(name);
    if (it == r.codecs.end()) return false;
    *out = it->second;
    return true;
}

}  // namespace

void register_codec(const Codec& codec) {
    if (codec.name.empty() || !codec.compress || !codec.decompress) return;
    CodecRegistry& r = codec_registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    r.codecs[codec.name] = codec;
}

void unregister_codec(const std::string& name) {
    CodecRegistry& r = codec_registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    r.codecs.erase(name);
}

bool has_codec(const std::string& name) {
    Codec c;
    return find_codec(name, &c);
}

std::vector<std::string> codecs() {
    CodecRegistry& r = codec_registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    std::vector<std::string> names;
    for (std::map<std::string, Codec>::const_iterator it = r.codecs.begin(); it != r.codecs.end(); ++it)
        names.push_back(it->first);
    return names;
}

bool compress_bytes(const std::string& codec, const unsigned char* in, std::size_t n,
                    int level, std::vector<unsigned char>& out) {
    Codec c;
    if (!find_codec(codec, &c)) return false;
    try { return c.compress(in, n, level, out); } catch (...) { out.clear(); return false; }
}

bool decompress_bytes(const std::string& codec, const unsigned char* in, std::size_t n,
                      std::size_t raw_size, std::vector<unsigned char>& out) {
    Codec c;
    if (!find_codec(codec, &c)) return false;
    try { return c.decompress(in, n, raw_size, out); } catch (...) { out.clear(); return false; }
}

Encoding split_encoding(const std::string& encoding) {
    Encoding e;
    const std::string::size_type plus = encoding.find('+');
    if (plus == std::string::npos || plus + 1 >= encoding.size()) {
        e.inner = encoding;
        return e;
    }
    e.inner = encoding.substr(0, plus);
    const std::string rest = encoding.substr(plus + 1);
    const std::string::size_type next = rest.find('+');
    e.codec = rest.substr(0, next);
    if (next != std::string::npos && next + 1 < rest.size()) e.extra = rest.substr(next + 1);
    return e;
}

}  // namespace pto

// ===========================================================================
// .dstore implementation
// ===========================================================================
namespace pto {


namespace {

/// 2 added a per-column description; a version 1 file reads under 2 with an
/// empty one. 3 stores that description as msgpack rather than as JSON text,
/// in the same length-prefixed slot -- \ref metadata_to_msgpack says
/// why. A version 2 file still reads: the slot is there and holds text, so
/// only the decode differs.
/// Version 4 adds a codec, a transform and a raw size to every blob record.
/// A file that uses no codec is still written as version 3, so a reader that
/// predates 4 opens everything it could open before.
const std::uint32_t kVersion = 4;
const std::uint32_t kVersionRaw = 3;
const std::uint32_t kFlagLittleEndian = 1u << 0;

// The codec ids the format records. The names are what the registry knows
// the codecs by; the ids never change meaning.
const std::uint8_t kCodecNone = 0;
const char* const kCodecNames[] = {"", "zstd", "brotli", "lz4", "deflate"};
const std::uint8_t kCodecCount = 5;

std::uint8_t codec_id(const std::string& name) {
    for (std::uint8_t k = 1; k < kCodecCount; k++)
        if (name == kCodecNames[k]) return k;
    return kCodecNone;
}

// A transform byte: kind in the high nibble, element width in the low one.
const std::uint8_t kTransformDelta = 0x10;
const std::uint8_t kTransformShuffle = 0x20;

/// Delta-code `n` bytes of `width`-byte little-endian integers in place:
/// each becomes the difference to the one before, wrapping. Exact and
/// reversible whatever the values, and small where the column is sorted.
/// One loop per width, so the compiler sees a fixed element and vectorises
/// the prefix sum rather than calling memcpy with a runtime length per value.
template <typename U>
void delta_encode_t(unsigned char* b, std::size_t n) {
    const std::size_t count = n / sizeof(U);
    U prev = 0;
    for (std::size_t i = 0; i < count; i++) {
        U v;
        std::memcpy(&v, b + i * sizeof(U), sizeof(U));
        const U d = static_cast<U>(v - prev);
        prev = v;
        std::memcpy(b + i * sizeof(U), &d, sizeof(U));
    }
}
template <typename U>
void delta_decode_t(unsigned char* b, std::size_t n) {
    const std::size_t count = n / sizeof(U);
    U acc = 0;
    for (std::size_t i = 0; i < count; i++) {
        U d;
        std::memcpy(&d, b + i * sizeof(U), sizeof(U));
        acc = static_cast<U>(acc + d);
        std::memcpy(b + i * sizeof(U), &acc, sizeof(U));
    }
}
void delta_encode(unsigned char* b, std::size_t n, std::size_t width) {
    if (n < 2 * width) return;
    switch (width) {
        case 2: delta_encode_t<std::uint16_t>(b, n); break;
        case 4: delta_encode_t<std::uint32_t>(b, n); break;
        case 8: delta_encode_t<std::uint64_t>(b, n); break;
        default: break;
    }
}
void delta_decode(unsigned char* b, std::size_t n, std::size_t width) {
    if (n < 2 * width) return;
    switch (width) {
        case 2: delta_decode_t<std::uint16_t>(b, n); break;
        case 4: delta_decode_t<std::uint32_t>(b, n); break;
        case 8: delta_decode_t<std::uint64_t>(b, n); break;
        default: break;
    }
}
/// Byte shuffle: all first bytes, then all second bytes, ... so a float
/// column's sign-and-exponent bytes sit together and compress as one run.
template <std::size_t W>
void shuffle_t(const unsigned char* __restrict in, unsigned char* __restrict out, std::size_t count) {
    for (std::size_t k = 0; k < W; k++)
        for (std::size_t i = 0; i < count; i++) out[k * count + i] = in[i * W + k];
}
template <std::size_t W>
void unshuffle_t(const unsigned char* __restrict in, unsigned char* __restrict out, std::size_t count) {
    for (std::size_t k = 0; k < W; k++)
        for (std::size_t i = 0; i < count; i++) out[i * W + k] = in[k * count + i];
}
void shuffle(const unsigned char* in, unsigned char* out, std::size_t n, std::size_t width) {
    const std::size_t count = n / width;
    switch (width) {
        case 2: shuffle_t<2>(in, out, count); break;
        case 4: shuffle_t<4>(in, out, count); break;
        case 8: shuffle_t<8>(in, out, count); break;
        default: std::memcpy(out, in, count * width); break;
    }
    std::memcpy(out + count * width, in + count * width, n - count * width);
}
void unshuffle(const unsigned char* in, unsigned char* out, std::size_t n, std::size_t width) {
    const std::size_t count = n / width;
    switch (width) {
        case 2: unshuffle_t<2>(in, out, count); break;
        case 4: unshuffle_t<4>(in, out, count); break;
        case 8: unshuffle_t<8>(in, out, count); break;
        default: std::memcpy(out, in, count * width); break;
    }
    std::memcpy(out + count * width, in + count * width, n - count * width);
}
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
class StoreHandle {
public:
    // open_file, not fopen: the narrow CRT on Windows takes the active code
    // page, so a store -- or a .pto with one embedded in it -- under a path
    // outside that page could be written and then not read back.
    StoreHandle() = default;
    StoreHandle(const char* path, const char* mode, bool report = true)
            : f_(open_file(path, mode, report)) {}
    ~StoreHandle() { close(); }
    StoreHandle(const StoreHandle&) = delete;
    StoreHandle& operator=(const StoreHandle&) = delete;

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
    std::uint64_t bytes = 0;        ///< as stored
    std::uint8_t codec = 0;         ///< kCodecNone, or a codec id
    std::uint8_t transform = 0;     ///< 0, or a transform byte
    std::uint64_t raw_bytes = 0;    ///< after decoding; equals `bytes` when raw
};

// --- the directory, as bytes -------------------------------------------------

struct Writer {
    std::vector<unsigned char> b;
    std::uint32_t version = kVersion;

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
    void blob(const BlobRef& r) {
        u64(r.offset); u64(r.bytes);
        if (version >= 4) { u8(r.codec); u8(r.transform); u64(r.raw_bytes); }
    }
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
    BlobRef blob() {
        BlobRef r;
        r.offset = u64(); r.bytes = u64();
        if (version >= 4) { r.codec = u8(); r.transform = u8(); r.raw_bytes = u64(); }
        else r.raw_bytes = r.bytes;
        return r;
    }
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
        r.raw_bytes = n;
        bytes(data, n);
        return r;
    }
    /*!
     * A blob transformed and compressed, or raw when that is smaller or the
     * blob is below the threshold. `width` is the element width the transform
     * works in, 0 for none. Never fails into a wrong file: a codec that
     * declines leaves the blob raw, and the record says so.
     */
    BlobRef coded_blob(const void* data, std::size_t n, std::uint8_t codec, int level,
                       std::uint8_t transform, std::size_t width, std::size_t min_bytes) {
        if (codec == kCodecNone || n < min_bytes) return blob(data, n);
        const unsigned char* src = static_cast<const unsigned char*>(data);
        std::vector<unsigned char> staged;
        std::uint8_t applied = 0;
        if (transform != 0 && (width == 2 || width == 4 || width == 8) && n % width == 0) {
            staged.assign(src, src + n);
            if (transform == kTransformDelta) {
                delta_encode(staged.data(), n, width);
            } else {
                std::vector<unsigned char> tmp(n);
                shuffle(staged.data(), tmp.data(), n, width);
                staged.swap(tmp);
            }
            applied = static_cast<std::uint8_t>(transform | width);
            src = staged.data();
        }
        std::vector<unsigned char> packed;
        if (!compress_bytes(kCodecNames[codec], src, n, level, packed) || packed.size() >= n)
            return blob(data, n);
        pad_to_8();
        BlobRef r;
        r.offset = pos;
        r.bytes = packed.size();
        r.codec = codec;
        r.transform = applied;
        r.raw_bytes = n;
        bytes(packed.data(), packed.size());
        return r;
    }
};

/// What write_node compresses with, resolved once from the options.
struct CodecPlan {
    std::uint8_t codec = kCodecNone;
    int level = -1;
    bool transform = true;
    std::size_t min_bytes = 4096;
    /// The codec for one column: its own `codec` attribute wins.
    std::uint8_t for_column(const Column& c) const {
        const std::string own = c.attribute("codec");
        if (own.empty()) return codec;
        if (own == "none" || own == "raw") return kCodecNone;
        return codec_id(own);
    }
};

/// Which transform suits a column type: delta for the integers wide enough to
/// carry a difference, shuffle for the floats, nothing for bytes and codes.
std::uint8_t transform_for(ColumnType t, std::size_t* width) {
    switch (t) {
        case ColumnType::Float64: *width = 8; return kTransformShuffle;
        case ColumnType::Float32: *width = 4; return kTransformShuffle;
        case ColumnType::Int64: case ColumnType::UInt64: *width = 8; return kTransformDelta;
        case ColumnType::Int32: case ColumnType::UInt32: *width = 4; return kTransformDelta;
        case ColumnType::Int16: case ColumnType::UInt16: *width = 2; return kTransformDelta;
        default: *width = 0; return 0;
    }
}

/// Element width of a column type, or 0 for the two that are not a plain array.
std::size_t element_bytes(ColumnType t) {
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
        case ColumnType::String:  return 4;   // the codes
        case ColumnType::Bool:    return 0;   // bit-packed
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
    switch (static_cast<ColumnType>(raw)) {
        case ColumnType::Float64:
        case ColumnType::Float32:
        case ColumnType::Int64:
        case ColumnType::Int32:
        case ColumnType::Int16:
        case ColumnType::Int8:
        case ColumnType::UInt64:
        case ColumnType::UInt32:
        case ColumnType::UInt16:
        case ColumnType::UInt8:
        case ColumnType::Bool:
        case ColumnType::String:
            return true;
    }
    return false;
}

// The numeric value of a ColumnType is written into the file, so it is part of
// the format. Appending a new type is fine; renumbering the existing ones would
// silently change the dtype of every column in every file already written.
static_assert(static_cast<int>(ColumnType::Float64) == 0 &&
              static_cast<int>(ColumnType::Bool) == 10 &&
              static_cast<int>(ColumnType::String) == 11,
              "ColumnType values are part of the .dstore format; see kVersion");

void write_node(BlobStream& out, Writer& dir, const DataStore& store,
                const CodecPlan& plan) {
    dir.str(store.label());
    dir.u64(store.n_rows());

    // The selection travels as data. This format's contract is that a store
    // comes back as it went in, so unlike the HDF5 writer it does not export a
    // gated subset -- it saves the gate.
    const BitMask& rows = store.row_mask();
    dir.u64(rows.size());
    dir.blob(out.coded_blob(rows.words(), rows.nbytes(), plan.codec, plan.level, 0, 0, plan.min_bytes));

    dir.u32(static_cast<std::uint32_t>(store.n_columns()));
    for (int c = 0; c < store.n_columns(); c++) {
        const Column& column = store.column(c);
        dir.str(column.name());
        dir.u8(static_cast<std::uint8_t>(column.type()));
        dir.u64(column.size());
        dir.u8(column.has_mask() ? kColumnHasMask : 0);
        const std::uint8_t codec = plan.for_column(column);

        if (column.type() == ColumnType::Bool) {
            dir.blob(out.coded_blob(column.bits().words(), column.bits().nbytes(),
                                    codec, plan.level, 0, 0, plan.min_bytes));
        } else {
            const std::size_t width = element_bytes(column.type());
            const void* data = column.type() == ColumnType::String
                    ? static_cast<const void*>(column.codes().data())
                    : column.data_ptr();
            std::size_t twidth = 0;
            const std::uint8_t transform = plan.transform ? transform_for(column.type(), &twidth) : 0;
            dir.blob(out.coded_blob(data, data == nullptr ? 0 : column.size() * width,
                                    codec, plan.level, transform, twidth, plan.min_bytes));
        }

        const BitMask& mask = column.mask();
        dir.u64(mask.size());
        dir.blob(out.coded_blob(mask.words(), mask.nbytes(), codec, plan.level, 0, 0, plan.min_bytes));

        // The column's description, as msgpack. The name is written above as a
        // field of its own as well, even though it is an attribute of this: a
        // subset read decides whether to skip a column before it has any reason
        // to decode, and store_columns() promises the names without reading
        // data.
        const std::vector<unsigned char> meta =
            metadata_to_msgpack(column.metadata());
        dir.u32(static_cast<std::uint32_t>(meta.size()));
        if (!meta.empty()) dir.raw(meta.data(), meta.size());

        // The dictionary of a text column, as its own little block: a count and
        // then each string with its length. Written through the same blob path
        // so it is aligned and addressable like everything else.
        if (column.type() == ColumnType::String) {
            Writer d;
            d.u32(static_cast<std::uint32_t>(column.dictionary().size()));
            for (const std::string& s : column.dictionary()) d.str(s);
            dir.blob(out.coded_blob(d.b.data(), d.b.size(), codec, plan.level, 0, 0, plan.min_bytes));
        }
    }

    const std::vector<std::string> names = store.group_names();
    dir.u32(static_cast<std::uint32_t>(names.size()));
    for (const std::string& name : names) {
        dir.str(name);
        write_node(out, dir, store.group(name), plan);
    }
}

// --- reading -----------------------------------------------------------------

/// Every byte the store reader has moved. \see store_bytes_read.
std::uint64_t g_bytes_read = 0;

struct Blobs {
    std::FILE* f = nullptr;
    std::uint64_t file_bytes = 0;
    std::uint64_t base = 0;
    /// The store's bytes when it is in memory rather than in `f`.
    const unsigned char* mem = nullptr;

    /// `n` stored bytes from `off` into a blob, or throw. An offset past the
    /// end of the file is the shape a truncated or doctored file takes, and
    /// reading whatever is there instead would turn it into wrong numbers
    /// rather than an error.
    void read_stored(const BlobRef& r, std::uint64_t off, std::uint64_t n, void* into) const {
        if (n == 0) return;
        if (off + n > r.bytes || r.offset + r.bytes > file_bytes)
            throw std::runtime_error("store file: a column points past the end");
        if (mem != nullptr) {
            std::memcpy(into, mem + r.offset + off, static_cast<std::size_t>(n));
        } else if (fseek64(f, static_cast<std::int64_t>(base + r.offset + off), SEEK_SET) != 0 ||
                   std::fread(into, 1, static_cast<std::size_t>(n), f) != n) {
            throw std::runtime_error("store file: could not read a column");
        }
        g_bytes_read += n;
    }
    /*!
     * Bytes `[off, off + n)` of a blob's DECODED content. A raw blob is read
     * in place, and only that range; a compressed one is fetched and decoded
     * whole, because a window of a compressed stream has no meaning of its
     * own, and the range is copied out of the result.
     */
    void read_range(const BlobRef& r, std::uint64_t off, std::uint64_t n, void* into) const {
        if (n == 0) return;
        if (r.codec == kCodecNone && r.transform == 0) { read_stored(r, off, n, into); return; }
        if (off + n > r.raw_bytes)
            throw std::runtime_error("store file: a column is shorter than its record says");
        std::vector<unsigned char> stored(static_cast<std::size_t>(r.bytes));
        read_stored(r, 0, r.bytes, stored.data());
        std::vector<unsigned char> raw;
        if (r.codec != kCodecNone) {
            if (r.codec >= kCodecCount)
                throw std::runtime_error("store file: a column uses a codec this reader does not know");
            const std::string name = kCodecNames[r.codec];
            if (!has_codec(name))
                throw std::runtime_error("store file: a column is compressed with " + name +
                                         ", and no " + name + " codec is registered");
            if (!decompress_bytes(name, stored.data(), stored.size(),
                                  static_cast<std::size_t>(r.raw_bytes), raw) ||
                raw.size() != r.raw_bytes)
                throw std::runtime_error("store file: a " + name + " stream is corrupt");
        } else {
            raw.swap(stored);
        }
        if (r.transform != 0) {
            const std::size_t width = r.transform & 0x0F;
            const std::uint8_t kind = static_cast<std::uint8_t>(r.transform & 0xF0);
            if (kind == kTransformDelta) {
                delta_decode(raw.data(), raw.size(), width);
            } else if (kind == kTransformShuffle) {
                std::vector<unsigned char> tmp(raw.size());
                unshuffle(raw.data(), tmp.data(), raw.size(), width);
                raw.swap(tmp);
            } else {
                throw std::runtime_error("store file: a column uses a transform this reader does not know");
            }
        }
        std::memcpy(into, raw.data() + off, static_cast<std::size_t>(n));
    }
    /// A whole blob, decoded.
    std::vector<unsigned char> read(const BlobRef& r) const {
        std::vector<unsigned char> out(static_cast<std::size_t>(r.raw_bytes));
        read_range(r, 0, r.raw_bytes, out.data());
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
    const std::uint64_t have_words = r.raw_bytes / 8;
    if (first_word >= have_words) return out;
    const std::uint64_t stop = last_word < have_words ? last_word : have_words;

    std::vector<std::uint64_t> raw(static_cast<std::size_t>(stop - first_word));
    blobs.read_range(r, first_word * 8, (stop - first_word) * 8, raw.data());

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

void read_node(Reader& dir, const Blobs& blobs, DataStore& store,
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
                       ? metadata_from_msgpack(
                             reinterpret_cast<const unsigned char*>(stored.data()),
                             stored.size())
                       : stored;
        }
        if (!known_column_type(raw_type))
            throw std::runtime_error("store file: unknown column type for '" + name + "'");
        const ColumnType type = static_cast<ColumnType>(raw_type);
        BlobRef dict_blob;
        if (type == ColumnType::String) dict_blob = dir.blob();
        if (!want(name)) continue;

        // A column's own length, not the node's: they agree in a file this
        // library wrote, and clamping to both costs nothing if they ever do not.
        const std::uint64_t got = rows.take(n);
        const std::uint64_t at = rows.start() < n ? rows.start() : n;

        Column& column = store.column(store.add_column(name, type));
        if (!meta.empty()) column.set_metadata(meta);
        if (type == ColumnType::Bool) {
            std::vector<std::uint64_t> words = read_bits(blobs, data_blob, at, got);
            column.set_bits(words.data(), static_cast<std::size_t>(got));
        } else if (type == ColumnType::String) {
            // The whole dictionary, whatever the range: it is the labels, not
            // the rows, and is small by construction.
            const std::vector<unsigned char> raw = blobs.read(dict_blob);
            Reader d{raw.data(), raw.size(), 0};
            std::vector<std::string> dictionary(d.u32());
            for (std::string& s : dictionary) s = d.str();
            column.set_dictionary(dictionary);
            std::vector<int> codes(static_cast<std::size_t>(got));
            blobs.read_range(data_blob, at * 4, got * 4, codes.data());
            column.set_codes(codes.data(), static_cast<int>(got));
        } else {
            // Straight into the column's own buffer. resize_uninitialized does
            // not fill it first, so nothing is written twice.
            column.resize_uninitialized(static_cast<std::size_t>(got));
            const std::size_t w = element_bytes(type);
            blobs.read_range(data_blob, at * w, got * w, column.data_ptr());
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
    StoreHandle f;
    std::vector<unsigned char> directory;
    std::uint64_t file_bytes = 0;
    std::uint64_t base = 0;
    std::uint32_t format_version = kVersion;

    /// The store's bytes when it was handed over in memory; null for a file.
    const unsigned char* mem = nullptr;

    /// A store in memory: the same checks as for a file, over a buffer.
    OpenStore(const unsigned char* bytes, std::size_t n) : mem(bytes) {
        const std::string filename = "a store in memory";
        if (bytes == nullptr || n < kHeaderBytes ||
            std::memcmp(bytes, kStoreMagic, kMagicBytes) != 0)
            throw std::runtime_error(filename + " is not a store file");
        std::uint32_t version, flags, checksum;
        std::uint64_t dir_offset, dir_bytes, declared;
        std::memcpy(&version, bytes + 8, 4);
        std::memcpy(&flags, bytes + 12, 4);
        std::memcpy(&dir_offset, bytes + 16, 8);
        std::memcpy(&dir_bytes, bytes + 24, 8);
        std::memcpy(&declared, bytes + 32, 8);
        std::memcpy(&checksum, bytes + 40, 4);
        format_version = version;
        if (version > kVersion)
            throw std::runtime_error(filename + " was written by a newer store writer"
                                     " (store format version " +
                                     std::to_string(version) + ")");
        if (((flags & kFlagLittleEndian) != 0) != host_is_little_endian())
            throw std::runtime_error(filename + " was written on a machine of the"
                                     " opposite byte order, which is not supported");
        file_bytes = n;
        if (declared != file_bytes)
            throw std::runtime_error(filename + " is truncated or was appended to");
        if (dir_offset + dir_bytes > file_bytes)
            throw std::runtime_error(filename + " has no directory where it says");
        directory.assign(bytes + dir_offset, bytes + dir_offset + dir_bytes);
        if (fnv1a(directory.data(), directory.size()) != checksum)
            throw std::runtime_error(filename + ": the directory is corrupt");
    }

    OpenStore(const std::string& filename, std::uint64_t base_ = 0,
              std::uint64_t region = 0)
            : f(filename.c_str(), "rb"), base(base_) {
        if (!f.ok()) throw std::runtime_error("cannot open " + filename);
        if (base != 0 && fseek64(f.get(), static_cast<std::int64_t>(base), SEEK_SET) != 0)
            throw std::runtime_error(filename + " is shorter than the store in it");

        unsigned char head[kHeaderBytes];
        if (std::fread(head, 1, kHeaderBytes, f.get()) != kHeaderBytes ||
            std::memcmp(head, kStoreMagic, kMagicBytes) != 0)
            throw std::runtime_error(filename + " is not a store file");

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
            throw std::runtime_error(filename + " was written by a newer store writer"
                                     " (store format version " +
                                     std::to_string(version) + ")");
        // Byte order is recorded rather than converted. Swapping on the way in
        // would put back the conversion layer this format exists to avoid, and
        // no machine this runs on is big-endian.
        if (((flags & kFlagLittleEndian) != 0) != host_is_little_endian())
            throw std::runtime_error(filename + " was written on a machine of the"
                                     " opposite byte order, which is not supported");

        std::fseek(f.get(), 0, SEEK_END);
        const std::uint64_t whole = static_cast<std::uint64_t>(ftell64(f.get()));
        file_bytes = region != 0 ? region : (whole > base ? whole - base : 0);
        if (declared != file_bytes)
            throw std::runtime_error(filename + " is truncated or was appended to");
        if (dir_offset + dir_bytes > file_bytes)
            throw std::runtime_error(filename + " has no directory where it says");

        directory.resize(static_cast<std::size_t>(dir_bytes));
        if (fseek64(f.get(), static_cast<std::int64_t>(base + dir_offset), SEEK_SET) != 0 ||
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
        if (type == static_cast<std::uint8_t>(ColumnType::String)) dir.blob();
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

/*!
 * \brief Leave `dir` positioned at the start of `want`'s node record.
 *
 * The same positional walk as walk_paths and for the same reason -- the
 * directory has no index, so reaching a node means stepping over every field of
 * every node before it. That costs a scan of the directory and not one byte of
 * payload, which is the whole point: the directory is a few kilobytes and the
 * groups it skips are gigabytes.
 *
 * Returns false when the path is not in the file, leaving `dir` wherever it
 * got to -- a caller that gets false must not go on to read.
 */
void skip_node(Reader& dir);

bool seek_to_group(Reader& dir, const std::string& prefix,
                   const std::string& want) {
    dir.str();                                       // label
    dir.u64();                                       // n_rows
    dir.u64(); dir.blob();                           // row mask
    const std::uint32_t n_columns = dir.u32();
    for (std::uint32_t c = 0; c < n_columns; c++) {
        dir.str();                                   // name
        const std::uint8_t type = dir.u8();
        dir.u64(); dir.u8(); dir.blob(); dir.u64(); dir.blob();
        if (dir.version >= 2) dir.str();
        if (type == static_cast<std::uint8_t>(ColumnType::String)) dir.blob();
    }
    const std::uint32_t n_groups = dir.u32();
    for (std::uint32_t g = 0; g < n_groups; g++) {
        const std::string name = dir.str();
        const std::string path = prefix.empty() ? name : prefix + "/" + name;
        if (path == want) return true;
        // `want` is under this child when it starts with the child's path AND
        // the next character is the separator -- not merely when it starts with
        // the name, or `results2` would be entered looking for `results/x`.
        if (want.size() > path.size() && want.compare(0, path.size(), path) == 0 &&
            want[path.size()] == '/') {
            return seek_to_group(dir, path, want);
        }
        skip_node(dir);
    }
    return false;
}

/// Step over one node and everything under it. \see seek_to_group
void skip_node(Reader& dir) {
    seek_to_group(dir, std::string(), std::string());
}

/// A leading and a trailing separator are optional, matching the tree walker.
std::string normalise_group(const std::string& group) {
    std::size_t b = 0, e = group.size();
    while (b < e && group[b] == '/') b++;
    while (e > b && group[e - 1] == '/') e--;
    return group.substr(b, e - b);
}

}  // namespace

namespace {

/// Header, blobs, directory, then the header again with the real offsets.
/// Shared by the by-name and the into-an-open-file forms.
bool emit_store(BlobStream& out, const DataStore& store, const CodecPlan& plan) {
    unsigned char head[kHeaderBytes];
    std::memset(head, 0, kHeaderBytes);
    out.bytes(head, kHeaderBytes);

    // A file with no codec is written exactly as version 3 was, byte for byte,
    // so every reader that opened those keeps opening these.
    Writer dir;
    dir.version = plan.codec == kCodecNone ? kVersionRaw : kVersion;
    write_node(out, dir, store, plan);

    out.pad_to_8();
    const std::uint64_t dir_offset = out.pos;
    out.bytes(dir.b.data(), dir.b.size());
    const std::uint64_t file_bytes = out.pos;

    const std::uint32_t version = dir.version;
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

    if (fseek64(out.h, static_cast<std::int64_t>(out.base), SEEK_SET) != 0 ||
        std::fwrite(head, 1, kHeaderBytes, out.h) != kHeaderBytes)
        out.ok = false;
    if (out.ok && fseek64(out.h, static_cast<std::int64_t>(out.base + file_bytes),
                          SEEK_SET) != 0)
        out.ok = false;
    return out.ok;
}

}  // namespace

namespace {

/// The options as a plan, or false with the reason if the codec is not one
/// the format names or not one that is registered.
bool plan_for(const StoreOptions& options, CodecPlan* plan, std::string* why) {
    plan->codec = kCodecNone;
    plan->level = options.level;
    plan->transform = options.transform;
    plan->min_bytes = options.min_bytes;
    if (options.codec.empty()) return true;
    const std::uint8_t id = codec_id(options.codec);
    if (id == kCodecNone) {
        *why = "'" + options.codec + "' is not a codec the store format names";
        return false;
    }
    if (!has_codec(options.codec)) {
        *why = "no " + options.codec + " codec is registered";
        return false;
    }
    plan->codec = id;
    return true;
}

}  // namespace

std::uint64_t write_store_at(std::FILE* f, const DataStore& store,
                             const StoreOptions& options) {
    if (f == nullptr) return 0;
    CodecPlan plan;
    std::string why;
    if (!plan_for(options, &plan, &why)) {
        std::cerr << "store file: " << why << std::endl;
        return 0;
    }
    // A store embedded in a bigger container starts wherever that container is
    // already at, which on a large .pto is past the 2 GiB a 32-bit long holds.
    const std::int64_t here = ftell64(f);
    if (here < 0) return 0;
    BlobStream out(f, static_cast<std::uint64_t>(here));
    if (!emit_store(out, store, plan)) return 0;
    return out.pos;
}

std::uint64_t write_store_at(std::FILE* f, const DataStore& store) {
    return write_store_at(f, store, StoreOptions());
}

bool write_store(const std::string& filename, const DataStore& store) {
    return write_store(filename, store, StoreOptions());
}

bool write_store(const std::string& filename, const DataStore& store,
                 const StoreOptions& options) {
    CodecPlan plan;
    std::string why;
    if (!plan_for(options, &plan, &why)) {
        std::cerr << "store file: " << why << std::endl;
        return false;
    }
    // Beside the target, so the rename that follows stays on one filesystem and
    // is therefore atomic: a failure never leaves half a file where a good one
    // was.
    const std::string temp = filename + ".ptolib-tmp";

    StoreHandle owned(temp.c_str(), "wb");
    BlobStream out(owned.get(), 0);
    if (!out.ok) {
        std::cerr << "store file: could not create " << temp << std::endl;
        return false;
    }

    bool ok = emit_store(out, store, plan);
    if (!owned.close()) ok = false;

    if (ok && !detail::replace_file(temp, filename)) {
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
void read_open(DataStore& out, OpenStore& file, const std::string& filename,
               const ColumnFilter& want, const RowRange& rows,
               const std::string& group) {
    Reader dir{file.directory.data(), file.directory.size(), 0, file.format_version};
    Blobs blobs{file.f.get(), file.file_bytes, file.base, file.mem};
    out.release();

    const std::string path = normalise_group(group);
    if (!path.empty()) {
        // Seeking costs a scan of the directory and no payload at all, so the
        // groups stepped over are free however large they are.
        if (!seek_to_group(dir, std::string(), path))
            throw std::runtime_error("store file: no group '" + path + "' in " +
                                     filename);
    }
    read_node(dir, blobs, out, want, rows);
}

void read_region(DataStore& out, const std::string& filename,
                 std::uint64_t base, std::uint64_t bytes,
                 const ColumnFilter& want, const RowRange& rows,
                 const std::string& group = std::string()) {
    OpenStore file(filename, base, bytes);
    read_open(out, file, filename, want, rows, group);
}

}  // namespace

void read_store_into(DataStore& out, const unsigned char* bytes, std::size_t n) {
    OpenStore file(bytes, n);
    read_open(out, file, "a store in memory", ColumnFilter(), RowRange(), std::string());
}

void read_store_into(DataStore& out, const unsigned char* bytes, std::size_t n,
                     const std::vector<std::string>& columns,
                     std::uint64_t first_row, std::uint64_t n_rows,
                     const std::string& group) {
    ColumnFilter want;
    want.everything = columns.empty();
    want.wanted = &columns;
    RowRange rows;
    rows.everything = first_row == 0 && n_rows == 0;
    rows.first = first_row;
    rows.count = n_rows;
    OpenStore file(bytes, n);
    read_open(out, file, "a store in memory", want, rows, group);
}

std::vector<std::string> store_columns(const unsigned char* bytes, std::size_t n,
                                       const std::string& group) {
    std::vector<std::string> columns;
    try {
        OpenStore file(bytes, n);
        Reader dir{file.directory.data(), file.directory.size(), 0, file.format_version};
        std::vector<std::string> paths;
        walk_paths(dir, std::string(), paths, &columns, group, group.empty());
    } catch (const std::exception&) {
        return {};
    }
    return columns;
}

std::vector<std::string> store_groups(const unsigned char* bytes, std::size_t n) {
    std::vector<std::string> paths;
    try {
        OpenStore file(bytes, n);
        Reader dir{file.directory.data(), file.directory.size(), 0, file.format_version};
        walk_paths(dir, std::string(), paths, nullptr, std::string(), false);
    } catch (const std::exception&) {
        return {};
    }
    return paths;
}

std::uint32_t store_format_version(const std::string& filename) {
    StoreHandle f(filename.c_str(), "rb", false);
    if (!f.ok()) return 0;
    unsigned char head[kHeaderBytes];
    if (std::fread(head, 1, kHeaderBytes, f.get()) != kHeaderBytes ||
        std::memcmp(head, kStoreMagic, kMagicBytes) != 0)
        return 0;
    std::uint32_t version = 0;
    std::memcpy(&version, head + 8, 4);
    return version;
}

void read_store_into(DataStore& out, const std::string& filename) {
    read_region(out, filename, 0, 0, ColumnFilter(), RowRange());
}

void read_store_into(DataStore& out, const std::string& filename,
                     const std::vector<std::string>& columns) {
    ColumnFilter want;
    want.everything = false;
    want.wanted = &columns;
    read_region(out, filename, 0, 0, want, RowRange());
}

void read_store_into(DataStore& out, const std::string& filename,
                     std::uint64_t base, std::uint64_t bytes) {
    read_region(out, filename, base, bytes, ColumnFilter(), RowRange());
}

void read_store_into(DataStore& out, const std::string& filename,
                     std::uint64_t base, std::uint64_t bytes,
                     const std::vector<std::string>& columns) {
    ColumnFilter want;
    want.everything = false;
    want.wanted = &columns;
    read_region(out, filename, base, bytes, want, RowRange());
}

void read_store_into(DataStore& out, const std::string& filename,
                     std::uint64_t base, std::uint64_t bytes,
                     const std::vector<std::string>& columns,
                     std::uint64_t first_row, std::uint64_t n_rows,
                     const std::string& group) {
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
    read_region(out, filename, base, bytes, want, rows, group);
}

void read_store_into(DataStore& out, const std::string& filename,
                     const std::string& group) {
    read_region(out, filename, 0, 0, ColumnFilter(), RowRange(), group);
}

bool store_has(const std::string& filename, const std::string& group) {
    const std::string path = normalise_group(group);
    try {
        OpenStore file(filename, 0, 0);
        if (path.empty()) return true;              // the root is always there
        Reader dir{file.directory.data(), file.directory.size(), 0,
                   file.format_version};
        return seek_to_group(dir, std::string(), path);
    } catch (const std::exception&) {
        // Probing a file that is not one of ours is a normal thing to do, and
        // the same silence hdf5_table_has answers with.
        return false;
    }
}

std::uint64_t store_bytes_read() { return g_bytes_read; }

DataStore read_store(const std::string& filename) {
    DataStore out;
    read_store_into(out, filename);
    return out;
}

bool is_store_file(const std::string& filename) {
    // Quiet: this asks whether a path is a store, and "it is not" -- including
    // "there is nothing there" -- is the answer, not a failure to report.
    StoreHandle f(filename.c_str(), "rb", false);
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

}  // namespace pto

// ===========================================================================
// PTO implementation
// ===========================================================================
namespace pto {


namespace {

// --- object names as paths --------------------------------------------------
//
// An object name doubles as a relative path: File::disassemble puts the
// container back as a directory tree and the name carries the layout. That
// makes a name an instruction to write somewhere, so it has to be one that
// cannot point outside the directory it is given -- a container is an
// interchange format, and disassemble is what a recipient runs on a file
// somebody else wrote.
//
// Checked on the way in (emit_object) and again on the way out (disassemble).
// Both, because the two guard different things: the writer keeps this
// library from producing a container nobody can safely unpack, and the reader
// is what actually stands between a hostile file and the filesystem. Neither
// alone is enough.
//
// Rejected rather than sanitised. Rewriting "../x" to "x" would put the object
// somewhere the container did not ask for, silently, and a caller cannot
// predict where its data landed; refusing names the file and the object.

/*!
 * \brief Is `raw` usable as a relative path under a target directory?
 *
 * \param raw  the object name
 * \param why  set to the reason when the answer is false
 *
 * Both separators are considered whatever the host is: a container written on
 * one platform is disassembled on another, and `\` is an ordinary filename
 * character on POSIX but a separator on Windows. A name that traverses only on
 * Windows must still be refused on Linux, or the check is a no-op exactly where
 * the file crosses machines.
 */
bool name_is_a_safe_relative_path(const std::string& raw, std::string* why) {
    if (raw.empty()) { *why = "is empty"; return false; }

    std::string s = raw;
    for (std::size_t i = 0; i < s.size(); i++) if (s[i] == '\\') s[i] = '/';

    if (s[0] == '/') { *why = "is an absolute path"; return false; }
    // A drive-relative or drive-absolute name ("C:x", "C:\x"), and with the
    // backslashes already folded, a UNC prefix arrives as a leading "//".
    if (s.size() >= 2 && s[1] == ':' &&
        ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z'))) {
        *why = "names a drive"; return false;
    }

    // Walk the components keeping the depth below the target. "a/../b" is fine
    // -- it normalises to "b" and stays inside. What escapes is a ".." that
    // takes the depth negative, so that is what is refused.
    int depth = 0;
    std::size_t i = 0;
    while (i <= s.size()) {
        std::size_t j = s.find('/', i);
        if (j == std::string::npos) j = s.size();
        const std::string part = s.substr(i, j - i);
        if (part == "..") {
            if (--depth < 0) {
                *why = "walks out of the directory with '..'";
                return false;
            }
        } else if (!part.empty() && part != ".") {
            ++depth;
        }
        if (j == s.size()) break;
        i = j + 1;
    }
    if (depth == 0) { *why = "names no file once it is normalised"; return false; }
    return true;
}

/*!
 * \brief The writers' shared refusal: empty when `name` may be written.
 *
 * There is more than one place that lays down an object header -- `emit_object`
 * and `pto_add_store`, which does not go through it -- so the check and its
 * wording live here rather than at each, where they would drift.
 *
 * An empty name is allowed: `disassemble` falls back to the uid for one.
 */
std::string why_name_cannot_be_written(const std::string& name) {
    if (name.empty()) return std::string();
    std::string why;
    if (name_is_a_safe_relative_path(name, &why)) return std::string();
    return "the object name \"" + name + "\" " + why +
           "; a name is used as a relative path when the container is "
           "disassembled, so it has to stay under the target directory";
}

// --- element ids ------------------------------------------------------------
//
// Borrowed from Matroska wherever Matroska already means what PTO needs, with
// Matroska's own ids, so a generic EBML parser walks a .pto file and prints
// most of it with the right names. Only what is genuinely new gets a new id,
// and those live in 0x1E54xx, where Matroska assigns nothing.

const std::uint32_t kEBML            = 0x1A45DFA3;
const std::uint32_t kEBMLVersion     = 0x4286;
const std::uint32_t kEBMLReadVersion = 0x42F7;
const std::uint32_t kEBMLMaxIDLength = 0x42F2;
const std::uint32_t kEBMLMaxSizeLen  = 0x42F3;
const std::uint32_t kDocType         = 0x4282;
const std::uint32_t kDocTypeVersion  = 0x4287;
const std::uint32_t kDocTypeReadVer  = 0x4285;

const std::uint32_t kSegment      = 0x18538067;
const std::uint32_t kSeekHead     = 0x114D9B74;
const std::uint32_t kSeek         = 0x4DBB;
const std::uint32_t kSeekID       = 0x53AB;
const std::uint32_t kSeekPosition = 0x53AC;
const std::uint32_t kInfo         = 0x1549A966;
const std::uint32_t kSegmentUUID  = 0x73A4;
const std::uint32_t kTitle        = 0x7BA9;
const std::uint32_t kMuxingApp    = 0x4D80;
const std::uint32_t kWritingApp   = 0x5741;
const std::uint32_t kDateUTC      = 0x4461;
const std::uint32_t kAttachments  = 0x1941A469;
const std::uint32_t kAttachedFile = 0x61A7;
const std::uint32_t kFileDescr    = 0x467E;
const std::uint32_t kFileName     = 0x466E;
const std::uint32_t kFileMedia    = 0x4660;
const std::uint32_t kFileData     = 0x465C;
const std::uint32_t kFileUID      = 0x46AE;
/// FileUID is rewritten in place when an object moves, so it is always
/// written in eight octets: a narrower replacement would shorten the
/// element and shift every sibling after it.
const int kUidOctets = 8;
const std::uint32_t kTags         = 0x1254C367;
const std::uint32_t kTag          = 0x7373;
const std::uint32_t kTargets      = 0x63C0;
const std::uint32_t kTagAttachUID = 0x63C6;
const std::uint32_t kSimpleTag    = 0x67C8;
const std::uint32_t kTagName      = 0x45A3;
const std::uint32_t kTagString    = 0x4487;
const std::uint32_t kTagBinary    = 0x4485;
const std::uint32_t kVoid         = 0xEC;
const std::uint32_t kCRC32        = 0xBF;
const std::uint32_t kCues         = 0x1C53BB6B;
const std::uint32_t kCuePoint     = 0xBB;

const std::uint32_t kPtoKind        = 0x1E54F001;
const std::uint32_t kPtoEncoding    = 0x1E54F002;
const std::uint32_t kPtoRowCount    = 0x1E54F003;
const std::uint32_t kPtoRawSize     = 0x1E54F004;   ///< decoded size of a coded payload
const std::uint32_t kPtoGeneration  = 0x1E54F010;
const std::uint32_t kPtoSeekUID     = 0x1E54F011;
const std::uint32_t kPtoTagIndex    = 0x1E54F020;
const std::uint32_t kPtoTagSrcType  = 0x1E54F021;
const std::uint32_t kPtoTagUInt     = 0x1E54F022;
const std::uint32_t kPtoTagInt      = 0x1E54F023;
const std::uint32_t kPtoTagFloat    = 0x1E54F024;
const std::uint32_t kPtoTagDate     = 0x1E54F025;
const std::uint32_t kPtoTagUID      = 0x1E54F026;
const std::uint32_t kPtoTagUIDs     = 0x1E54F027;
const std::uint32_t kPtoTagFloats   = 0x1E54F028;
const std::uint32_t kPtoTagInts     = 0x1E54F029;
const std::uint32_t kPtoCueUID      = 0x1E54F030;
const std::uint32_t kPtoCueEvent    = 0x1E54F031;
const std::uint32_t kPtoCueOffset   = 0x1E54F032;
const std::uint32_t kPtoCueTime     = 0x1E54F033;
const std::uint32_t kPtoAnnotations = 0x1E54F100;
const std::uint32_t kPtoAnnotation  = 0x1E54F101;
const std::uint32_t kPtoAnnTarget   = 0x1E54F102;
const std::uint32_t kPtoAnnFirstRow = 0x1E54F103;
const std::uint32_t kPtoAnnLastRow  = 0x1E54F104;
const std::uint32_t kPtoAnnText     = 0x1E54F105;
const std::uint32_t kPtoAnnAuthor   = 0x1E54F106;
const std::uint32_t kPtoAnnDate     = 0x1E54F107;
const std::uint32_t kPtoBanner      = 0x1E54F040;

/// Every size that may have to grow later is written this wide from the start.
/// RFC 8794 permits an over-wide Data Size expressly so it can be overwritten,
/// and that permission is what makes in-place update possible at all.
const int kWideSize = 8;

/// Payload bytes each SeekHead is given, so it can be rewritten where it lies.
/// The commit protocol depends on that: a SeekHead that had to move could not
/// be the fallback for the move.
const std::uint64_t kSeekHeadReserve = 8192;

/// A Void needs one octet of id and one of size, so a gap of exactly one octet
/// can hold nothing at all and must never be left.
const std::uint64_t kMinVoid = 2;

/*!
 * \brief Every payload starts on a multiple of this.
 *
 * EBML guarantees no alignment -- an element header is a variable number of
 * octets, so `FileData` would otherwise begin wherever the name and the encoding
 * strings happened to leave it. That is fine for bytes and wrong for everything
 * a payload actually is here: a PTU record stream is `uint32`, a `.dstore`
 * column is `double`, and a reader that wants to map the file and point at one
 * without copying needs the first byte on a boundary.
 *
 * Eight, not four, and it costs nothing to say eight: it satisfies the 32-bit
 * case as well, and it is the alignment `.dstore` already uses for its own
 * blobs -- those offsets are relative to the store, so the store has to start
 * 8-aligned for them to be 8-aligned in the file.
 *
 * A writer pads with `Void`, records nothing about having done so, and a reader
 * must still check rather than assume: the specification makes this a SHOULD,
 * and an alignment a conformant writer may omit is not one a reader may rely on.
 */
const std::uint64_t kPayloadAlign = 8;

/// Bytes of padding that put `at` on a \ref kPayloadAlign boundary, never
/// leaving a gap of one octet -- which is the one size a Void cannot be.
std::uint64_t align_pad(std::uint64_t at) {
    std::uint64_t pad = (kPayloadAlign - (at % kPayloadAlign)) % kPayloadAlign;
    if (pad != 0 && pad < kMinVoid) pad += kPayloadAlign;
    return pad;
}

// --- EBML primitives --------------------------------------------------------

int id_octets(std::uint32_t id) {
    if (id <= 0xFF) return 1;
    if (id <= 0xFFFF) return 2;
    if (id <= 0xFFFFFF) return 3;
    return 4;
}

/// Octets needed to hold `v` as a Data Size. All-ones is reserved for "unknown",
/// so a value that would encode as all-ones needs one octet more.
int size_octets(std::uint64_t v) {
    for (int n = 1; n <= 8; n++)
        if (v < ((1ULL << (7 * n)) - 1)) return n;
    return 8;
}

/// Octets a value occupies as an EBML integer: the fewest that hold it.
int uint_octets(std::uint64_t v) {
    int n = 0;
    while (v != 0) { n++; v >>= 8; }
    return n == 0 ? 1 : n;
}

/*!
 * \brief Bytes of element header in front of an object's payload.
 *
 * `head_bytes` is the AttachedFile's own children -- uid, kind, encoding, name
 * -- whose length varies with the strings. Everything else here is a fixed
 * width, which is what makes the payload's landing position computable before a
 * byte is written, and therefore what makes \ref align_pad possible.
 */
std::uint64_t header_before_payload(std::size_t head_bytes) {
    return id_octets(kAttachments) + kWideSize +
           id_octets(kAttachedFile) + kWideSize +
           head_bytes + id_octets(kFileData) + kWideSize;
}

std::uint32_t crc32_ebml(const unsigned char* p, std::size_t n) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}

/// Builds an element tree in memory. Everything except a payload goes through
/// here; payloads are streamed straight to the file and never buffered.
struct Buf {
    std::vector<unsigned char> b;

    void raw(const void* p, std::size_t n) {
        const unsigned char* c = static_cast<const unsigned char*>(p);
        b.insert(b.end(), c, c + n);
    }
    void put_id(std::uint32_t id) {
        const int n = id_octets(id);
        for (int i = n - 1; i >= 0; i--) b.push_back((id >> (8 * i)) & 0xFF);
    }
    void put_size(std::uint64_t v, int octets = 0) {
        const int n = octets ? octets : size_octets(v);
        const std::uint64_t enc = v | (1ULL << (7 * n));
        for (int i = n - 1; i >= 0; i--) b.push_back((enc >> (8 * i)) & 0xFF);
    }
    void be(std::uint64_t v, int n) {
        for (int i = n - 1; i >= 0; i--) b.push_back((v >> (8 * i)) & 0xFF);
    }

    void uint_elem(std::uint32_t id, std::uint64_t v) {
        const int n = uint_octets(v);
        put_id(id); put_size(n); be(v, n);
    }

    /*!
     * An unsigned integer written in a fixed number of octets, so the element
     * has the same length whatever the value is.
     *
     * Needed wherever an element is **rewritten in place**. `uint_elem` packs
     * to the smallest width that holds the value, which is right for a value
     * written once; it is wrong for one that is written again later, because a
     * smaller replacement leaves the tail of the old element behind and every
     * following child is then read from the wrong offset. That is a silent
     * corruption, not a failure: the next parse simply finds an empty
     * PtoEncoding.
     */
    void uint_elem_fixed(std::uint32_t id, std::uint64_t v, int n) {
        put_id(id); put_size(n); be(v, n);
    }
    void int_elem(std::uint32_t id, long long v) {
        int n = 1;
        while (n < 8) {
            const long long lo = -(1LL << (8 * n - 1)), hi = (1LL << (8 * n - 1)) - 1;
            if (v >= lo && v <= hi) break;
            n++;
        }
        put_id(id); put_size(n); be(static_cast<std::uint64_t>(v), n);
    }
    void float_elem(std::uint32_t id, double v) {
        std::uint64_t bits;
        std::memcpy(&bits, &v, 8);
        put_id(id); put_size(8); be(bits, 8);
    }
    void text_elem(std::uint32_t id, const std::string& s) {
        put_id(id); put_size(s.size()); raw(s.data(), s.size());
    }
    void bytes_elem(std::uint32_t id, const unsigned char* p, std::size_t n) {
        put_id(id); put_size(n); raw(p, n);
    }
    void master(std::uint32_t id, const Buf& child, int size_octets_ = 0) {
        put_id(id); put_size(child.b.size(), size_octets_);
        raw(child.b.data(), child.b.size());
    }
};

/// Walks what Buf wrote. Refuses to run off the end: a truncated element is a
/// damaged file, not licence to read whatever follows.
struct Cursor {
    const unsigned char* p = nullptr;
    std::size_t n = 0, i = 0;
    /// Where the element last returned begins, relative to `p`. What lets a
    /// caller locate its Data Size without assuming a width.
    std::size_t hdr = 0;

    bool done() const { return i >= n; }

    bool element(std::uint32_t* id, const unsigned char** data, std::uint64_t* size) {
        if (i >= n) return false;
        hdr = i;
        const unsigned char first = p[i];
        if (first == 0) return false;
        int len = 1;
        while (len <= 4 && !(first & (0x80 >> (len - 1)))) len++;
        if (len > 4 || i + len > n) return false;
        std::uint32_t v = 0;
        for (int k = 0; k < len; k++) v = (v << 8) | p[i + k];
        i += len;

        if (i >= n) return false;
        const unsigned char sf = p[i];
        if (sf == 0) return false;
        int slen = 1;
        while (slen <= 8 && !(sf & (0x80 >> (slen - 1)))) slen++;
        if (slen > 8 || i + slen > n) return false;
        std::uint64_t sz = sf & (0xFF >> slen);
        for (int k = 1; k < slen; k++) sz = (sz << 8) | p[i + k];
        i += slen;
        if (sz == ((1ULL << (7 * slen)) - 1)) return false;   // unknown size
        if (i + sz > n) return false;

        *id = v; *data = p + i; *size = sz;
        i += static_cast<std::size_t>(sz);
        return true;
    }
};

std::uint64_t get_uint(const unsigned char* p, std::uint64_t n) {
    std::uint64_t v = 0;
    for (std::uint64_t i = 0; i < n && i < 8; i++) v = (v << 8) | p[i];
    return v;
}

long long get_int(const unsigned char* p, std::uint64_t n) {
    if (n == 0) return 0;
    long long v = (p[0] & 0x80) ? -1 : 0;
    for (std::uint64_t i = 0; i < n && i < 8; i++)
        v = static_cast<long long>((static_cast<std::uint64_t>(v) << 8) | p[i]);
    return v;
}

double get_float(const unsigned char* p, std::uint64_t n) {
    if (n == 8) {
        const std::uint64_t bits = get_uint(p, 8);
        double d;
        std::memcpy(&d, &bits, 8);
        return d;
    }
    if (n == 4) {
        std::uint32_t bits = static_cast<std::uint32_t>(get_uint(p, 4));
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
    return 0.0;
}

std::string get_text(const unsigned char* p, std::uint64_t n) {
    return std::string(reinterpret_cast<const char*>(p), static_cast<std::size_t>(n));
}

// --- the file ---------------------------------------------------------------

#ifdef _WIN32
/*!
 * \brief Where the writer lock lives: one byte far past any real end of file.
 *
 * A Windows byte-range lock is mandatory -- it stops other processes *reading*
 * the locked range, not just writing it -- so locking byte 0 would shut out the
 * readers this lock exists to leave alone. A byte nothing will ever hold data
 * at makes it a pure flag. POSIX has no equivalent problem: `flock` takes no
 * range and readers never ask for the lock.
 */
const std::uint32_t kLockOffsetLo = 0;
const std::uint32_t kLockOffsetHi = 0x7FFFFFFF;
#endif

}  // namespace   (the anonymous one; reopened just below)

// FileHandle lives in `detail`, not in the anonymous namespace, because
// File::Impl keeps one as a member. A member whose type has internal
// linkage gives the enclosing class a different type in every translation
// unit -- GCC says so as -Wsubobject-linkage -- which is exactly the kind
// of quiet one-definition-rule breach a header must not carry, however
// few translation units compile the implementation today.
namespace detail {
class FileHandle {
public:
    /// What \ref open_exclusive did about the lock.
    enum LockResult {
        kLockTaken,       ///< we hold it
        kLockBusy,        ///< someone else holds it; nothing was opened
        kLockUnsupported  ///< the filesystem has no locks; the file is open anyway
    };

    FileHandle() = default;
    ~FileHandle() { close(); }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    bool open(const std::string& path, const char* mode) {
        close();
        // open_file, not fopen: a container may live under a path the active
        // Windows code page cannot name, and reading one must not depend on
        // what the machine is set to.
        f_ = open_file(path, mode, false);
        return f_ != nullptr;
    }

    /*!
     * \brief Open for writing, holding an exclusive advisory lock.
     *
     * The lock is taken on the descriptor before the file is truncated, so a
     * `create` against a container someone else is writing fails without having
     * destroyed it -- which is why this cannot be `fopen("w+b")` plus a lock.
     *
     * \param create make the file if it is missing, and truncate it. False
     *        opens an existing file and fails if there is none.
     * \param why set on every return; see \ref LockResult.
     */
    bool open_exclusive(const std::string& path, bool create, LockResult* why) {
        close();
        *why = kLockTaken;
#ifdef _WIN32
        const int flags = _O_RDWR | _O_BINARY | (create ? (_O_CREAT) : 0);
        int fd = -1;
        if (::_wsopen_s(&fd, utf8_to_wide_path(path).c_str(), flags, _SH_DENYNO,
                        _S_IREAD | _S_IWRITE) != 0 || fd < 0)
            return false;
        HANDLE h = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
        if (h != INVALID_HANDLE_VALUE) {
            OVERLAPPED ov;
            std::memset(&ov, 0, sizeof(ov));
            ov.Offset = kLockOffsetLo;
            ov.OffsetHigh = kLockOffsetHi;
            if (::LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                             0, 1, 0, &ov)) {
                locked_ = true;
            } else {
                const DWORD e = ::GetLastError();
                if (e == ERROR_LOCK_VIOLATION || e == ERROR_SHARING_VIOLATION) {
                    ::_close(fd);
                    *why = kLockBusy;
                    return false;
                }
                *why = kLockUnsupported;
            }
        } else {
            *why = kLockUnsupported;
        }
        if (create && ::_chsize_s(fd, 0) != 0) { ::_close(fd); return false; }
        f_ = ::_fdopen(fd, "r+b");
#else
        const int flags = O_RDWR | (create ? O_CREAT : 0);
        int fd = ::open(path.c_str(), flags, 0666);
        if (fd < 0) return false;
        int rc = 0;
        do { rc = ::flock(fd, LOCK_EX | LOCK_NB); } while (rc != 0 && errno == EINTR);
        if (rc == 0) {
            locked_ = true;
        } else if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EACCES) {
            ::close(fd);
            *why = kLockBusy;
            return false;
        } else {
            // Some network mounts have no flock at all. Refusing to open there
            // would trade a rare race for a filesystem the library cannot use.
            *why = kLockUnsupported;
        }
        if (create && ::ftruncate(fd, 0) != 0) { ::close(fd); return false; }
        f_ = ::fdopen(fd, "r+b");
#endif
        if (f_ == nullptr) {
            unlock_fd(fd);
#ifdef _WIN32
            ::_close(fd);
#else
            ::close(fd);
#endif
            locked_ = false;
            return false;
        }
        return true;
    }

    bool ok() const { return f_ != nullptr; }
    std::FILE* get() const { return f_; }
    void close() {
        if (f_) {
            if (locked_) {
#ifdef _WIN32
                unlock_fd(::_fileno(f_));
#else
                unlock_fd(::fileno(f_));
#endif
                locked_ = false;
            }
            std::fclose(f_);
            f_ = nullptr;
        }
    }

    // 64-bit throughout: `long` is 32 bits on Windows, and every object in a
    // container past 2 GiB is reached through exactly these two.
    bool seek(std::uint64_t off) {
        return fseek64(f_, static_cast<std::int64_t>(off), SEEK_SET) == 0;
    }
    std::uint64_t tell() { return static_cast<std::uint64_t>(ftell64(f_)); }
    std::uint64_t length() {
        const std::uint64_t here = tell();
        std::fseek(f_, 0, SEEK_END);
        const std::uint64_t n = tell();
        seek(here);
        return n;
    }
    bool write(const void* p, std::size_t n) {
        return n == 0 || std::fwrite(p, 1, n, f_) == n;
    }
    bool read(void* p, std::size_t n) {
        return n == 0 || std::fread(p, 1, n, f_) == n;
    }
    bool at(std::uint64_t off, const void* p, std::size_t n) {
        return seek(off) && write(p, n);
    }
    void flush() { std::fflush(f_); }

private:
    static void unlock_fd(int fd) {
        if (fd < 0) return;
#ifdef _WIN32
        HANDLE h = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
        if (h == INVALID_HANDLE_VALUE) return;
        OVERLAPPED ov;
        std::memset(&ov, 0, sizeof(ov));
        ov.Offset = kLockOffsetLo;
        ov.OffsetHigh = kLockOffsetHi;
        ::UnlockFileEx(h, 0, 1, 0, &ov);
#else
        ::flock(fd, LOCK_UN);
#endif
    }

    std::FILE* f_ = nullptr;
    bool locked_ = false;
};
}  // namespace detail

namespace {
using detail::FileHandle;

/*!
 * \brief A fresh object identity: 53 random bits in a 64-bit element, never zero.
 *
 * \par The storage is a uint64 and stays one
 * Nothing is narrowed on disk. RFC 8794 makes an unsigned integer element a
 * whole ``uint64``, libebml's ``EbmlUInteger`` stores one, and Matroska
 * constrains its UIDs with ``range: not 0`` and nothing else. A ``FileUID``
 * here is written in eight octets whatever its value -- it has to be, because
 * it is the one element rewritten in place when an object relocates, and a
 * narrower replacement would shift every sibling after it. Any conformant
 * reader gets a uint64 and must treat it as one.
 *
 * \par The value is bounded, and why that is now belt-and-braces
 * What is bounded is the number this writer *chooses*, to 2\f$^{53}\f$ - 1.
 *
 * It was load-bearing when it was introduced: a `Number` in JavaScript and a
 * `numeric` in R are both IEEE doubles, so a wider uid came back from either
 * binding as a *different number* -- one naming no object, with
 * `f.read(f.add_file(...), 0, 4)` failing on "no object with that uid".
 *
 * Both bindings have since been fixed at the binding, which is where the fix
 * belonged: JavaScript routes 64-bit scalars through `BigInt` (see
 * ext/js/jsarrays.i -- the arrays always did), and R carries a uid as a
 * character string, the only thing base R holds exactly without `bit64` (see
 * the SWIGR typemaps in ext/python/Pto.i). A container from another writer that
 * uses the whole range is therefore read correctly everywhere.
 *
 * The bound stays because it costs nothing observable -- 9x10\f$^{15}\f$
 * identities, with uniqueness inside a file guaranteed by \ref unused_uid
 * rather than left to chance -- and because a uid that fits a double is one
 * fewer thing to get wrong in the next binding.
 *
 */
std::uint64_t random_uid() {
    static std::mt19937_64 rng(std::random_device{}());
    std::uint64_t v = 0;
    while (v == 0) v = rng() >> 11;      // 53 bits; see above
    return v;
}

/// A uid no object in `slots` already has. \see Impl::fresh_uid.
template <class Slots>
std::uint64_t unused_uid(const Slots& slots) {
    for (;;) {
        const std::uint64_t v = random_uid();
        bool taken = false;
        for (std::size_t i = 0; i < slots.size(); i++)
            if (slots[i].meta.uid == v) { taken = true; break; }
        if (!taken) return v;
    }
}

}  // namespace

// --- the model ---------------------------------------------------------------

struct File::Impl {
    FileHandle f;
    std::string path;
    std::uint64_t ebml_at = 0;       ///< where the EBML header starts (a bundle has a stub before it)
    std::uint64_t doctype_version = 0;
    int head_count = 0;              ///< SeekHeads found: 2 for this writer, 1 for an append-only one
    std::string err;
    bool writable = false;
    bool dirty = false;

    std::uint64_t seg_data = 0;        ///< first byte of Segment's data
    std::uint64_t seg_size_at = 0;     ///< where Segment's size VINT lives
    std::uint64_t seg_bytes = 0;       ///< Segment's data length
    std::uint64_t head_at[2] = {0, 0}; ///< the two SeekHead element offsets
    std::uint64_t head_total = 0;      ///< bytes each SeekHead element occupies
    /// The payload each index slot was reserved with. This writer reserves
    /// kSeekHeadReserve; another writer may have reserved more or less, and
    /// an index rewritten in place has to fill exactly the slot it found.
    std::uint64_t head_reserve[2] = {kSeekHeadReserve, kSeekHeadReserve};
    int live = 0;                      ///< which SeekHead is authoritative
    std::uint64_t generation = 0;

    // Metadata, held whole because it is kilobytes.
    std::vector<unsigned char> uuid;
    std::string title, muxing_app, writing_app;
    std::string banner;              ///< the PtoBanner text, carried across compact()
    long long created = 0;
    std::vector<PtoTag> tags;
    std::vector<PtoAnnotation> notes;

    /// One cue, plus the object it indexes. Flat rather than a map per uid:
    /// there are thousands of these at most and they are written as a flat list.
    struct CueEntry { std::uint64_t uid; PtoCue cue; };
    std::vector<CueEntry> cuepoints;

    struct Slot {
        PtoObject meta;
        std::uint64_t elem_at = 0;      ///< the Attachments element header
        std::uint64_t elem_bytes = 0;   ///< how much of the file it occupies
        std::uint64_t seg_size_at = 0;  ///< Attachments' size VINT
        std::uint64_t att_size_at = 0;  ///< AttachedFile's size VINT
        std::uint64_t data_size_at = 0; ///< FileData's size VINT
        std::uint64_t rows_at = 0;      ///< the row count's 8-octet payload, or 0
        std::uint64_t raw_size_at = 0;  ///< the raw size's 8-octet payload, or 0
        std::uint64_t slack_at = 0;     ///< a Void right after, or 0
        std::uint64_t slack_bytes = 0;
        /// True when other objects live in the same Attachments element, so
        /// this one cannot be updated or removed in place.
        bool shared = false;
    };
    std::vector<Slot> slots;

    // Where the relocatable metadata elements currently sit, so they can be
    // rewritten in place when they still fit.
    std::uint64_t info_at = 0, info_bytes = 0;
    std::uint64_t tags_at = 0, tags_bytes = 0;
    std::uint64_t notes_at = 0, notes_bytes = 0;
    std::uint64_t cues_at = 0, cues_bytes = 0;

    std::vector<std::pair<std::uint64_t, std::uint64_t> > freelist;

    /// Whether a payload is padded onto a \ref kPayloadAlign boundary. Always,
    /// except while \ref File::compact is writing a `tight` copy.
    bool align_payloads = true;

    bool fail(const std::string& why) { err = why; return false; }

    /// Fail out of \ref File::open or \ref File::create. Closing the file
    /// is the point: it drops the writer lock, so a container rejected halfway
    /// through parsing does not stay locked for the life of the process.
    bool fail_open(const std::string& why) { close_failed(); return fail(why); }
    /// The same, for a step that has already set \ref err.
    bool fail_open() { close_failed(); return false; }

    void close_failed() {
        f.close();
        writable = false;
        dirty = false;
    }

    Slot* find(std::uint64_t uid) {
        for (Slot& s : slots) if (s.meta.uid == uid) return &s;
        return nullptr;
    }
    const Slot* find(std::uint64_t uid) const {
        for (const Slot& s : slots) if (s.meta.uid == uid) return &s;
        return nullptr;
    }

    // -- free space ------------------------------------------------------------

    void release(std::uint64_t off, std::uint64_t bytes) {
        if (bytes < kMinVoid) return;
        // The Void goes in the file as well as in the list: an element left
        // lying there would be found again by the next open, and if it still
        // carried its uid it would be found TWICE.
        write_void(off, bytes);
        freelist.push_back(std::make_pair(off, bytes));
        coalesce();
    }

    void coalesce() {
        std::sort(freelist.begin(), freelist.end());
        std::vector<std::pair<std::uint64_t, std::uint64_t> > out;
        for (std::size_t i = 0; i < freelist.size(); i++) {
            if (!out.empty() && out.back().first + out.back().second == freelist[i].first)
                out.back().second += freelist[i].second;
            else
                out.push_back(freelist[i]);
        }
        freelist.swap(out);
    }

    /*!
     * \brief A run of `bytes` for a whole element.
     *
     * Reuses a hole where one fits -- exactly, or with room left for a Void --
     * and grows the Segment otherwise.
     *
     * Carving the front of a hole leaves a remainder that MUST be given its own
     * Void header before this returns. The hole is one Void covering the whole
     * span, and the caller is about to write its element over that header; the
     * remainder would then be the tail of the old element's bytes with nothing
     * saying so, and the next reader walking the Segment parses whatever
     * happens to be there. It reads as "an element could not be read" at an
     * offset in the middle of a healthy file, which is the *reader* reporting
     * damage that the writer did.
     */
    std::uint64_t allocate(std::uint64_t bytes) {
        for (std::size_t i = 0; i < freelist.size(); i++) {
            const std::uint64_t have = freelist[i].second;
            if (have != bytes && have < bytes + kMinVoid) continue;
            const std::uint64_t at = freelist[i].first;
            if (have == bytes) freelist.erase(freelist.begin() + i);
            else {
                freelist[i].first += bytes;
                freelist[i].second -= bytes;
                write_void(freelist[i].first, freelist[i].second);
            }
            return at;
        }
        const std::uint64_t at = seg_data + seg_bytes;
        seg_bytes += bytes;
        return at;
    }

    /*!
     * \brief \see allocate, for an element whose payload must land on a boundary.
     *
     * The padding cannot be decided before the address is, and the address
     * cannot be chosen without knowing the padding -- a hole big enough for the
     * element may not be big enough once its own alignment is paid for. So the
     * two are worked out together, per candidate, and exactly `*pad + bytes` is
     * claimed. Over-allocating a boundary's worth and giving the remainder back
     * as slack was the first attempt; it left every object trailing up to
     * sixteen bytes of Void that nothing ever wanted.
     *
     * \param align false packs it tight and leaves the payload wherever the
     *        header ends. \see File::compact.
     */
    std::uint64_t allocate_aligned(std::uint64_t bytes, std::uint64_t before_payload,
                                   bool align, std::uint64_t* pad) {
        *pad = 0;
        if (!align) return allocate(bytes);
        for (std::size_t i = 0; i < freelist.size(); i++) {
            const std::uint64_t at = freelist[i].first;
            const std::uint64_t p = align_pad(at + before_payload);
            const std::uint64_t need = p + bytes;
            const std::uint64_t have = freelist[i].second;
            if (have != need && have < need + kMinVoid) continue;
            if (have == need) freelist.erase(freelist.begin() + i);
            else {
                // \see allocate -- the remainder needs its own Void header, for
                // the same reason and with the same failure mode if it does not
                // get one.
                freelist[i].first += need;
                freelist[i].second -= need;
                write_void(freelist[i].first, freelist[i].second);
            }
            *pad = p;
            return at;
        }
        const std::uint64_t at = seg_data + seg_bytes;
        *pad = align_pad(at + before_payload);
        seg_bytes += *pad + bytes;
        return at;
    }

    /// Payload octets of a Void that must occupy exactly `total` bytes. A wider
    /// size VINT means a shorter payload, and for some totals only one width
    /// works out at all.
    static std::uint64_t void_payload_for(std::uint64_t total) {
        for (int octets = 1; octets <= 8; octets++) {
            if (total < 1 + static_cast<std::uint64_t>(octets)) continue;
            const std::uint64_t payload = total - 1 - octets;
            if (size_octets(payload) <= octets) return payload;
        }
        return 0;
    }

    bool write_void(std::uint64_t at, std::uint64_t total) {
        if (total == 0) return true;
        if (total < kMinVoid) return fail("a gap of one octet cannot hold a Void");
        Buf v;
        v.put_id(kVoid);
        // The size VINT has to be chosen so id + size + payload lands exactly on
        // `total`: a wider VINT means a shorter payload, and for some totals only
        // one width works out.
        std::uint64_t payload = 0;
        int octets = 1;
        for (octets = 1; octets <= 8; octets++) {
            if (total < 1 + static_cast<std::uint64_t>(octets)) continue;
            payload = total - 1 - octets;
            if (size_octets(payload) <= octets) break;
        }
        if (octets > 8) return fail("cannot express a Void of that size");
        v.put_size(payload, octets);
        std::vector<unsigned char> zeros(static_cast<std::size_t>(payload), 0);
        return f.at(at, v.b.data(), v.b.size()) && f.write(zeros.data(), zeros.size());
    }

    // -- serialising the metadata ------------------------------------------------

    Buf build_info() const {
        Buf c;
        if (!uuid.empty()) c.bytes_elem(kSegmentUUID, uuid.data(), uuid.size());
        if (!title.empty()) c.text_elem(kTitle, title);
        if (!muxing_app.empty()) c.text_elem(kMuxingApp, muxing_app);
        if (!writing_app.empty()) c.text_elem(kWritingApp, writing_app);
        if (created != 0) { c.put_id(kDateUTC); c.put_size(8);
                            c.be(static_cast<std::uint64_t>(created), 8); }
        Buf e;
        e.master(kInfo, c);
        return e;
    }

    static void tag_value(Buf& s, const PtoTag& t) {
        switch (t.type) {
            case PtoType::Empty: break;
            case PtoType::UInt: s.uint_elem(kPtoTagUInt, t.u); break;
            case PtoType::Int: s.int_elem(kPtoTagInt, t.i); break;
            case PtoType::Float: s.float_elem(kPtoTagFloat, t.d); break;
            case PtoType::Date: s.put_id(kPtoTagDate); s.put_size(8);
                                s.be(static_cast<std::uint64_t>(t.i), 8); break;
            case PtoType::Text: s.text_elem(kTagString, t.text); break;
            case PtoType::Bytes: s.bytes_elem(kTagBinary, t.bytes.data(), t.bytes.size());
                                 break;
            case PtoType::UID: s.uint_elem(kPtoTagUID, t.u); break;
            case PtoType::UIDs: {
                Buf a;
                for (std::size_t i = 0; i < t.uids.size(); i++) a.be(t.uids[i], 8);
                s.bytes_elem(kPtoTagUIDs, a.b.data(), a.b.size());
                break;
            }
            case PtoType::Floats: {
                Buf a;
                for (std::size_t i = 0; i < t.floats.size(); i++) {
                    std::uint64_t bits;
                    std::memcpy(&bits, &t.floats[i], 8);
                    a.be(bits, 8);
                }
                s.bytes_elem(kPtoTagFloats, a.b.data(), a.b.size());
                break;
            }
            case PtoType::Ints: {
                Buf a;
                for (std::size_t i = 0; i < t.ints.size(); i++)
                    a.be(static_cast<std::uint64_t>(t.ints[i]), 8);
                s.bytes_elem(kPtoTagInts, a.b.data(), a.b.size());
                break;
            }
        }
    }

    Buf build_tags() const {
        Buf all;
        for (std::size_t k = 0; k < tags.size(); k++) {
            const PtoTag& t = tags[k];
            Buf targets;
            if (t.target != 0) targets.uint_elem(kTagAttachUID, t.target);
            Buf simple;
            simple.text_elem(kTagName, t.name);
            tag_value(simple, t);
            if (t.index != -1) simple.int_elem(kPtoTagIndex, t.index);
            if (t.source_type != 0) simple.uint_elem(kPtoTagSrcType, t.source_type);
            Buf one;
            one.master(kTargets, targets);
            one.master(kSimpleTag, simple);
            all.master(kTag, one);
        }
        Buf e;
        e.master(kTags, all);
        return e;
    }

    Buf build_notes() const {
        Buf all;
        for (std::size_t k = 0; k < notes.size(); k++) {
            const PtoAnnotation& a = notes[k];
            Buf one;
            if (a.target != 0) one.uint_elem(kPtoAnnTarget, a.target);
            if (a.last_row != 0) {
                one.uint_elem(kPtoAnnFirstRow, a.first_row);
                one.uint_elem(kPtoAnnLastRow, a.last_row);
            }
            if (!a.text.empty()) one.text_elem(kPtoAnnText, a.text);
            if (!a.author.empty()) one.text_elem(kPtoAnnAuthor, a.author);
            if (a.when != 0) { one.put_id(kPtoAnnDate); one.put_size(8);
                               one.be(static_cast<std::uint64_t>(a.when), 8); }
            all.master(kPtoAnnotation, one);
        }
        Buf e;
        e.master(kPtoAnnotations, all);
        return e;
    }

    /*!
     * \brief The cue table, as Matroska's `Cues` holding Matroska's `CuePoint`s.
     *
     * The element ID is Matroska's because the job is Matroska's: an index that
     * says where in a stream something is. What sits inside a CuePoint is ours,
     * since Matroska's cues address a timecode in a track and these address an
     * event ordinal in a payload.
     */
    Buf build_cuepoints() const {
        Buf all;
        for (std::size_t k = 0; k < cuepoints.size(); k++) {
            const CueEntry& c = cuepoints[k];
            Buf one;
            one.uint_elem(kPtoCueUID, c.uid);
            one.uint_elem(kPtoCueEvent, c.cue.event);
            one.uint_elem(kPtoCueOffset, c.cue.offset);
            if (c.cue.time != 0) one.uint_elem(kPtoCueTime, c.cue.time);
            all.master(kCuePoint, one);
        }
        Buf e;
        e.master(kCues, all);
        return e;
    }

    /// Forget an object's cues. Anything that moves or rewrites a payload has
    /// to: a cue into bytes that changed is worse than no cue at all.
    void drop_cues(std::uint64_t uid) {
        std::vector<CueEntry> keep;
        for (std::size_t k = 0; k < cuepoints.size(); k++)
            if (cuepoints[k].uid != uid) keep.push_back(cuepoints[k]);
        if (keep.size() != cuepoints.size()) { cuepoints.swap(keep); dirty = true; }
    }

    /*!
     * \brief Lay down one object: the header, then `n` payload bytes from `emit`.
     *
     * The one place an object is written. `emit` is handed the file positioned
     * at the payload and writes exactly `n` bytes -- from a caller's buffer for
     * \ref File::add, from another file for \ref File::add_file. The
     * header is written first precisely so the payload can be streamed after
     * it, and a gigabyte never goes through a buffer either way.
     *
     * \return the new object's uid, or 0.
     */
    std::uint64_t emit_object(const std::string& kind, const std::string& encoding,
                              const std::string& name, std::uint64_t n,
                              std::uint64_t reserve,
                              const std::function<bool(FileHandle&)>& emit,
                              const std::string& media_type = std::string(),
                              std::uint64_t rows = 0, std::uint64_t raw_size = 0) {
        err.clear();
        if (!writable) { fail("opened read-only"); return 0; }
        // A name is a relative path to disassemble, so refuse one that could
        // not be unpacked safely rather than writing a container whose only
        // honest reader is one that rejects it.
        {
            const std::string bad = why_name_cannot_be_written(name);
            if (!bad.empty()) { fail(bad); return 0; }
        }

        Slot s;
        s.meta.uid = unused_uid(slots);
        s.meta.kind = kind;
        s.meta.encoding = encoding;
        s.meta.name = name;
        s.meta.media_type = media_type;
        s.meta.size = n;
        s.meta.rows = rows;
        s.meta.raw_size = raw_size;

        Buf head;
        head.uint_elem_fixed(kFileUID, s.meta.uid, kUidOctets);
        head.text_elem(kPtoKind, kind);
        head.text_elem(kPtoEncoding, encoding);
        if (!name.empty()) head.text_elem(kFileName, name);
        if (!media_type.empty()) head.text_elem(kFileMedia, media_type);
        // Fixed width, so it can be rewritten in place when the payload is
        // replaced; before the row count, which stays last.
        std::size_t raw_size_back = 0;
        if (raw_size != 0) {
            head.uint_elem_fixed(kPtoRawSize, raw_size, 8);
            raw_size_back = head.b.size() - 8;
        }
        // Fixed width and last in the header, so the payload position is
        // head-relative and the count can be rewritten in place later.
        if (rows != 0) head.uint_elem_fixed(kPtoRowCount, rows, 8);

        const std::uint64_t att_payload =
                head.b.size() + id_octets(kFileData) + kWideSize + n;
        const std::uint64_t att_total = id_octets(kAttachedFile) + kWideSize + att_payload;
        const std::uint64_t elem_total = id_octets(kAttachments) + kWideSize + att_total;

        if (reserve != 0 && reserve < kMinVoid) reserve = kMinVoid;

        const std::uint64_t before_payload = header_before_payload(head.b.size());
        std::uint64_t pad = 0;
        const std::uint64_t at_raw =
                allocate_aligned(elem_total + reserve, before_payload, align_payloads, &pad);
        const std::uint64_t at = at_raw + pad;
        if (pad != 0 && !write_void(at_raw, pad)) return 0;

        Buf prefix;
        prefix.put_id(kAttachments);
        prefix.put_size(att_total, kWideSize);
        prefix.put_id(kAttachedFile);
        prefix.put_size(att_payload, kWideSize);
        prefix.raw(head.b.data(), head.b.size());
        prefix.put_id(kFileData);
        prefix.put_size(n, kWideSize);
        // The arithmetic above and the bytes just built must agree, or the
        // payload lands somewhere other than where it was aligned to.
        if (prefix.b.size() != before_payload) {
            fail("internal: the object header is not the size it was computed to be");
            return 0;
        }
        s.elem_at = at;
        s.elem_bytes = elem_total;
        s.seg_size_at = at + id_octets(kAttachments);
        s.att_size_at = s.seg_size_at + kWideSize + id_octets(kAttachedFile);
        s.data_size_at = at + prefix.b.size() - kWideSize;
        s.meta.offset = at + prefix.b.size();
        if (rows != 0) s.rows_at = s.att_size_at + kWideSize + head.b.size() - 8;
        if (raw_size != 0) s.raw_size_at = s.att_size_at + kWideSize + raw_size_back;

        if (!f.at(at, prefix.b.data(), prefix.b.size()) || !emit(f)) {
            fail(err.empty() ? "write failed" : err);
            return 0;
        }
        if (reserve != 0 && !write_void(at + elem_total, reserve)) return 0;
        s.slack_at = reserve ? at + elem_total : 0;
        s.slack_bytes = reserve;
        s.meta.capacity = n + reserve;

        slots.push_back(s);
        dirty = true;
        return s.meta.uid;
    }

    /*!
     * \brief \ref emit_object with the payload coming from a file on disk.
     *
     * Behind both \ref File::add_file and \ref File::attach, which differ
     * only in who decided what the object is.
     */
    std::uint64_t add_path(const std::string& kind, const std::string& encoding,
                           const std::string& name, const std::string& media_type,
                           const std::string& path, std::uint64_t reserve) {
        err.clear();

        std::error_code ec;
        const std::uintmax_t n =
                detail::fs::file_size(detail::fs::u8path(path), ec);
        if (ec) { fail("cannot size " + path + ": " + ec.message()); return 0; }

        FileHandle in;
        if (!in.open(path, "rb")) { fail("cannot open " + path); return 0; }

        // The only difference from add: where the bytes come from. In blocks, so
        // embedding a four-gigabyte instrument file costs a megabyte of memory.
        return emit_object(kind, encoding, name, n, reserve, [&](FileHandle& out) {
            std::vector<unsigned char> chunk(1u << 20);
            std::uint64_t left = n;
            while (left > 0) {
                const std::size_t take =
                        static_cast<std::size_t>(left < chunk.size() ? left : chunk.size());
                if (!in.read(chunk.data(), take)) return fail("could not read " + path);
                if (!out.write(chunk.data(), take)) return fail("write failed");
                left -= take;
            }
            return true;
        }, media_type);
    }

    /// Rewrite one of the three metadata elements, in place if it still fits.
    bool place(const Buf& e, std::uint64_t* at, std::uint64_t* bytes) {
        const std::uint64_t need = e.b.size();
        if (*at != 0 && (need == *bytes || need + kMinVoid <= *bytes)) {
            if (!f.at(*at, e.b.data(), need)) return fail("write failed");
            if (need < *bytes && !write_void(*at + need, *bytes - need)) return false;
            *bytes = need;
            return true;
        }
        if (*at != 0) release(*at, *bytes);
        const std::uint64_t to = allocate(need);
        if (!f.at(to, e.b.data(), need)) return fail("write failed");
        *at = to;
        *bytes = need;
        return true;
    }

    // -- the index ---------------------------------------------------------------

    Buf build_seekhead(std::uint64_t gen) const {
        Buf seeks;
        struct { std::uint32_t id; std::uint64_t at; } top[4] = {
            {kInfo, info_at}, {kTags, tags_at}, {kPtoAnnotations, notes_at},
            {kCues, cues_at}};
        for (int i = 0; i < 4; i++) {
            if (top[i].at == 0) continue;
            Buf s;
            Buf idb;
            idb.put_id(top[i].id);
            s.bytes_elem(kSeekID, idb.b.data(), idb.b.size());
            s.uint_elem(kSeekPosition, top[i].at - seg_data);
            seeks.master(kSeek, s);
        }
        for (std::size_t i = 0; i < slots.size(); i++) {
            Buf s;
            Buf idb;
            idb.put_id(kAttachedFile);
            s.bytes_elem(kSeekID, idb.b.data(), idb.b.size());
            s.uint_elem(kPtoSeekUID, slots[i].meta.uid);
            s.uint_elem(kSeekPosition, slots[i].elem_at - seg_data);
            seeks.master(kSeek, s);
        }

        Buf body;
        body.uint_elem(kPtoGeneration, gen);
        body.raw(seeks.b.data(), seeks.b.size());
        return body;
    }

    /*!
     * \brief Write one index where it lies, padded out to its reserved size.
     *
     * The CRC-32 covers every other child of the SeekHead, INCLUDING the Void
     * that pads it -- which is what RFC 8794 says ("all sibling data except
     * itself") and therefore what a generic EBML tool will check. Covering only
     * the useful part would have been self-consistent and wrong.
     */
    bool write_seekhead(int which, std::uint64_t gen) {
        const std::size_t crc_bytes = 1 + 1 + 4;      // id, size, the checksum
        const std::uint64_t reserve = head_reserve[which];
        Buf body = build_seekhead(gen);
        if (crc_bytes + body.b.size() + kMinVoid > reserve &&
            crc_bytes + body.b.size() != reserve)
            return fail("the index outgrew its reserved space; compact the file");

        const std::uint64_t pad = reserve - crc_bytes - body.b.size();
        if (pad != 0) {
            Buf v;
            const std::uint64_t payload = void_payload_for(pad);
            v.put_id(kVoid);
            v.put_size(payload, static_cast<int>(pad - 1 - payload));
            v.b.resize(v.b.size() + static_cast<std::size_t>(payload), 0);
            body.raw(v.b.data(), v.b.size());
        }

        Buf e;
        e.put_id(kSeekHead);
        e.put_size(reserve, kWideSize);
        const std::uint32_t crc = crc32_ebml(body.b.data(), body.b.size());
        const unsigned char le[4] = {static_cast<unsigned char>(crc & 0xFF),
                                     static_cast<unsigned char>((crc >> 8) & 0xFF),
                                     static_cast<unsigned char>((crc >> 16) & 0xFF),
                                     static_cast<unsigned char>((crc >> 24) & 0xFF)};
        e.bytes_elem(kCRC32, le, 4);
        e.raw(body.b.data(), body.b.size());
        return f.at(head_at[which], e.b.data(), e.b.size()) ? true : fail("write failed");
    }

    bool patch_segment_size() {
        Buf s;
        s.put_size(seg_bytes, kWideSize);
        return f.at(seg_size_at, s.b.data(), s.b.size());
    }
};

// --- construction ------------------------------------------------------------

File::File() : p_(new Impl) {}
File::~File() { delete p_; }

bool File::is_open() const { return p_->f.ok(); }

File::Mode File::mode() const {
    if (!p_->f.ok()) return Mode::ReadOnly;
    return p_->writable ? Mode::ReadWrite : Mode::ReadOnly;
}
const std::string& File::filename() const { return p_->path; }
const std::string& File::error() const { return p_->err; }
/// Releases the writer lock. Clearing `writable` with it keeps a later `update`
/// from taking the write path on a file that is no longer there.
void File::close() {
    p_->f.close();
    p_->writable = false;
    p_->dirty = false;
}

bool File::create(const std::string& filename, const std::string& title,
                  const std::string& banner) {
    Impl& m = *p_;
    m.err.clear();
    FileHandle::LockResult lock = FileHandle::kLockTaken;
    if (!m.f.open_exclusive(filename, true, &lock)) {
        if (lock == FileHandle::kLockBusy)
            return m.fail(filename + " is open for writing elsewhere");
        return m.fail("cannot create " + filename + ": " + std::strerror(errno));
    }
    m.path = filename;
    m.writable = true;
    m.slots.clear();
    m.tags.clear();
    m.notes.clear();
    m.freelist.clear();
    m.title = title;
    m.muxing_app = "ptolib " PTOLIB_VERSION_STRING;
    m.writing_app.clear();
    m.created = 0;
    m.generation = 0;
    m.live = 1;                       // so the first commit writes slot 0

    m.uuid.resize(16);
    {
        static std::mt19937_64 rng(std::random_device{}());
        for (int i = 0; i < 2; i++) {
            const std::uint64_t v = rng();
            std::memcpy(&m.uuid[i * 8], &v, 8);
        }
    }

    Buf head;
    {
        Buf c;
        c.uint_elem(kEBMLVersion, 1);
        c.uint_elem(kEBMLReadVersion, 1);
        c.uint_elem(kEBMLMaxIDLength, 4);
        c.uint_elem(kEBMLMaxSizeLen, 8);
        c.text_elem(kDocType, "pto");
        // 2 since cues exist; the READ version stays 1, because a cue is an
        // element a 1.0 reader skips by size and is none the worse for missing.
        c.uint_elem(kDocTypeVersion, 2);
        m.doctype_version = 2;
        c.uint_elem(kDocTypeReadVer, 1);
        head.master(kEBML, c);
    }
    if (!m.f.at(0, head.b.data(), head.b.size())) return m.fail_open("write failed");

    Buf seg;
    seg.put_id(kSegment);
    seg.put_size(0, kWideSize);
    m.seg_size_at = head.b.size() + id_octets(kSegment);
    if (!m.f.write(seg.b.data(), seg.b.size())) return m.fail_open("write failed");
    m.seg_data = m.seg_size_at + kWideSize;
    m.seg_bytes = 0;

    m.banner = banner;
    if (!banner.empty()) {
        Buf b;
        b.text_elem(kPtoBanner, banner);
        if (!m.f.write(b.b.data(), b.b.size())) return m.fail_open("write failed");
        m.seg_bytes += b.b.size();
    }

    // The two indexes come first, each with room to be rewritten where it lies.
    const std::uint64_t total = id_octets(kSeekHead) + kWideSize + kSeekHeadReserve;
    m.head_total = total;
    for (int i = 0; i < 2; i++) {
        m.head_at[i] = m.seg_data + m.seg_bytes;
        m.seg_bytes += total;
        Buf e;
        e.put_id(kSeekHead);
        e.put_size(kSeekHeadReserve, kWideSize);
        if (!m.f.at(m.head_at[i], e.b.data(), e.b.size())) return m.fail_open("write failed");
        if (!m.write_void(m.head_at[i] + e.b.size(), kSeekHeadReserve)) return m.fail_open();
    }
    // An empty but valid index, rather than a full commit: committing here
    // would place an Info element before the caller has set anything, and the
    // next commit would then have to move it and leave a hole behind. A file
    // that was just created should not already need compacting.
    if (!m.write_seekhead(0, 1)) return m.fail_open();
    m.live = 0;
    m.generation = 1;
    if (!m.patch_segment_size()) return m.fail_open("write failed");
    m.f.flush();
    m.dirty = true;
    return true;
}

// --- reading a file back ------------------------------------------------------

namespace {

/// Read a whole element's payload, given where its header starts.
bool read_element(FileHandle& f, std::uint64_t at, std::uint32_t* id,
                  std::vector<unsigned char>* payload, std::uint64_t* total,
                  std::uint64_t* size_at) {
    unsigned char head[16];
    if (!f.seek(at) || !f.read(head, 1)) return false;
    int len = 1;
    while (len <= 4 && !(head[0] & (0x80 >> (len - 1)))) len++;
    if (len > 4) return false;
    if (len > 1 && !f.read(head + 1, len - 1)) return false;
    std::uint32_t v = 0;
    for (int k = 0; k < len; k++) v = (v << 8) | head[k];

    unsigned char sz[8];
    if (!f.read(sz, 1)) return false;
    int slen = 1;
    while (slen <= 8 && !(sz[0] & (0x80 >> (slen - 1)))) slen++;
    if (slen > 8) return false;
    if (slen > 1 && !f.read(sz + 1, slen - 1)) return false;
    std::uint64_t n = sz[0] & (0xFF >> slen);
    for (int k = 1; k < slen; k++) n = (n << 8) | sz[k];
    if (n == ((1ULL << (7 * slen)) - 1)) return false;

    // A Data Size is read out of the file, so it is whatever the file says --
    // including, in a corrupted or hostile one, a number far larger than the
    // file itself. Everything sized from it has to be bounded by what the file
    // can actually contain before a byte of it is believed: allocating first
    // and discovering the truncation on the read is how a one-byte corruption
    // becomes an out-of-memory kill rather than "this file is damaged".
    //
    // This is the rule libebml carries SafeReadIOCallback for, applied at the
    // only place PTO sizes an allocation from untrusted input.
    const std::uint64_t end = f.length();
    if (n > end || at > end - n || at + len + slen + n > end) return false;

    *id = v;
    *total = len + slen + n;
    if (size_at) *size_at = at + len;
    if (payload != nullptr) {
        payload->resize(static_cast<std::size_t>(n));
        if (!f.read(payload->data(), payload->size())) return false;
    }
    return true;
}


/*!
 * \brief Where the EBML header begins: 0, or the offset of an executable stub's
 *        end in a bundle. Returns false when no EBML header is in the first
 *        8 MiB.
 *
 * Looks for the four magic octets and validates each hit as an element with a
 * small header -- never tries to decode an element at every offset, because
 * garbage decodes to enormous Data Sizes and an attempt per byte over a
 * multi-megabyte non-container took minutes.
 */
bool find_ebml_header(FileHandle& f, std::uint64_t* at) {
    std::uint32_t id = 0;
    std::uint64_t total = 0;
    const std::uint64_t len = f.length();
    auto ok_at = [&](std::uint64_t off) -> bool {
        // total is header + payload; a real EBML header is a few dozen octets.
        return read_element(f, off, &id, nullptr, &total, nullptr) && id == kEBML && total <= 4096;
    };
    if (ok_at(0)) { *at = 0; return true; }
    const std::uint64_t max_bytes = len > 8388608 ? 8388608 : len;
    if (max_bytes < 8) return false;
    std::vector<unsigned char> head(static_cast<std::size_t>(max_bytes));
    if (!f.seek(0) || !f.read(head.data(), head.size())) return false;
    static const unsigned char magic[4] = {0x1A, 0x45, 0xDF, 0xA3};
    for (std::size_t off = 1; off + 4 <= head.size();) {
        const void* hit = std::memchr(head.data() + off, 0x1A, head.size() - off - 3);
        if (hit == nullptr) break;
        off = static_cast<std::size_t>(static_cast<const unsigned char*>(hit) - head.data());
        if (std::memcmp(head.data() + off, magic, 4) == 0 && ok_at(off)) { *at = off; return true; }
        off++;
    }
    return false;
}

}  // namespace

bool File::open(const std::string& filename, bool writable) {
    Impl& m = *p_;
    m.err.clear();
    if (writable) {
        // A writer takes the container; a reader never does. A viewer open
        // while an analysis writes is the normal case, and the format already
        // has the reader seeing the pre-commit state.
        FileHandle::LockResult lock = FileHandle::kLockTaken;
        if (!m.f.open_exclusive(filename, false, &lock)) {
            if (lock == FileHandle::kLockBusy)
                return m.fail(filename + " is open for writing elsewhere");
            // Which open failed, and why the OS says it did: the two branches
            // used the same words, so a failure here could not be told from a
            // failure to read and every diagnosis started by guessing.
            return m.fail("cannot open " + filename + " for writing: " +
                          std::strerror(errno));
        }
    } else if (!m.f.open(filename, "rb")) {
        return m.fail("cannot open " + filename + " for reading: " +
                      std::strerror(errno));
    }
    m.path = filename;
    m.writable = writable;
    m.slots.clear();
    m.tags.clear();
    m.notes.clear();
    m.freelist.clear();
    m.cuepoints.clear();
    m.info_at = m.tags_at = m.notes_at = m.cues_at = 0;
    m.info_bytes = m.tags_bytes = m.notes_bytes = m.cues_bytes = 0;

    // EBML header, and the DocType that says this is ours. A bundle carries an
    // executable stub in front of it, so the header may start later than 0.
    std::uint64_t ebml_offset = 0;
    std::uint32_t id = 0;
    std::vector<unsigned char> payload;
    std::uint64_t total = 0;
    const bool found_ebml = find_ebml_header(m.f, &ebml_offset) &&
                            read_element(m.f, ebml_offset, &id, &payload, &total, nullptr);

    if (!found_ebml)
        return m.fail_open(filename + " is not an EBML file");
    {
        Cursor c{payload.data(), payload.size(), 0};
        std::uint32_t cid;
        const unsigned char* d;
        std::uint64_t n;
        std::string doctype;
        std::uint64_t read_version = 1;
        std::uint64_t max_id = 4;
        std::uint64_t max_size = 8;
        while (c.element(&cid, &d, &n)) {
            if (cid == kDocType) doctype = get_text(d, n);
            else if (cid == kDocTypeVersion) m.doctype_version = get_uint(d, n);
            else if (cid == kDocTypeReadVer) read_version = get_uint(d, n);
            else if (cid == kEBMLMaxIDLength) max_id = get_uint(d, n);
            else if (cid == kEBMLMaxSizeLen) max_size = get_uint(d, n);
        }
        if (doctype != "pto") return m.fail_open(filename + " is not a PTO file");
        if (read_version > 1)
            return m.fail_open(filename + " needs a newer PTO reader (DocTypeReadVersion "
                          + std::to_string(read_version) + ")");
        if (max_id > 4 || max_size > 8)
            return m.fail_open(filename + " declares EBMLMaxIDLength " +
                          std::to_string(max_id) + " / EBMLMaxSizeLength " +
                          std::to_string(max_size) + ", wider than this reader parses");
    }

    m.ebml_at = ebml_offset;
    std::uint64_t seg_at = ebml_offset + total;
    if (!read_element(m.f, seg_at, &id, nullptr, &total, &m.seg_size_at) ||
        id != kSegment)
        return m.fail_open(filename + " has no Segment");
    m.seg_data = m.seg_size_at + kWideSize;
    m.seg_bytes = total - (m.seg_data - seg_at);

    // Walk the Segment's children. Sizes are in the file, so this needs a seek
    // per element and reads nothing but the small ones.
    struct Child { std::uint64_t at, total; std::uint32_t id; };
    std::vector<Child> children;
    for (std::uint64_t at = m.seg_data; at < m.seg_data + m.seg_bytes; ) {
        std::uint32_t cid;
        std::uint64_t ctotal;
        if (!read_element(m.f, at, &cid, nullptr, &ctotal, nullptr) || ctotal == 0)
            return m.fail_open(filename + " is damaged: an element at " +
                          std::to_string(at) + " could not be read");
        Child c; c.at = at; c.total = ctotal; c.id = cid;
        children.push_back(c);
        at += ctotal;
    }

    // The two indexes, and which of them is the truth.
    int found = 0;
    std::uint64_t best_gen = 0;
    int best = -1;
    std::vector<std::uint64_t> live_uids;
    bool have_live = false;
    for (std::size_t i = 0; i < children.size() && found < 2; i++) {
        if (children[i].id != kSeekHead) continue;
        m.head_at[found] = children[i].at;
        m.head_total = children[i].total;
        std::uint32_t hid;
        std::uint64_t htotal;
        std::vector<unsigned char> body;
        if (read_element(m.f, children[i].at, &hid, &body, &htotal, nullptr)) {
            m.head_reserve[found] = body.size();
            Cursor c{body.data(), body.size(), 0};
            std::uint32_t cid;
            const unsigned char* d;
            std::uint64_t n;
            std::uint32_t stored = 0;
            bool has_crc = false;
            std::size_t after_crc = 0;
            std::uint64_t gen = 0;
            std::vector<std::uint64_t> uids;
            while (c.element(&cid, &d, &n)) {
                if (cid == kCRC32 && n == 4) {
                    stored = static_cast<std::uint32_t>(d[0]) |
                             (static_cast<std::uint32_t>(d[1]) << 8) |
                             (static_cast<std::uint32_t>(d[2]) << 16) |
                             (static_cast<std::uint32_t>(d[3]) << 24);
                    has_crc = true;
                    after_crc = c.i;
                } else if (cid == kPtoGeneration) {
                    gen = get_uint(d, n);
                } else if (cid == kSeek) {
                    Cursor s{d, static_cast<std::size_t>(n), 0};
                    std::uint32_t sid;
                    const unsigned char* sd;
                    std::uint64_t sn;
                    std::uint64_t uid = 0;
                    while (s.element(&sid, &sd, &sn))
                        if (sid == kPtoSeekUID) uid = get_uint(sd, sn);
                    if (uid != 0) uids.push_back(uid);
                }
            }
            // Everything after the CRC element, padding included: that is what
            // "all sibling data except itself" means, and what a generic EBML
            // tool will compute.
            const std::size_t body_end = body.size();
            if (has_crc && body_end >= after_crc) {
                const std::uint32_t got = crc32_ebml(body.data() + after_crc,
                                                     body_end - after_crc);
                if (got == stored && (best < 0 || gen > best_gen)) {
                    best = found;
                    best_gen = gen;
                    live_uids = uids;
                    have_live = true;
                }
            }
        }
        found++;
    }
    if (found == 0) return m.fail_open(filename + " has no index");
    // One SeekHead and no generation is an append-only writer's file (an
    // earlier writer, or any plain EBML muxer). Everything in it is live and it reads like any
    // other -- but committing needs the second, spare index that create() lays
    // down, and this file has nowhere to put one. compact() is the way to a
    // file that can be edited; it works from a read-only handle and keeps uids.
    if (found < 2 && writable)
        return m.fail_open(filename + " has a single index (written by an append-only writer); "
                           "open it read-only, or compact() it to a new file to edit it");
    m.head_count = found;
    m.live = best < 0 ? 0 : best;
    m.generation = best_gen;

    // Everything the live index does not vouch for is space to reuse: a
    // half-finished write from a session that died leaves valid elements that
    // were never committed.
    for (std::size_t i = 0; i < children.size(); i++) {
        const Child& c = children[i];
        if (c.id == kSeekHead) continue;
        if (c.id == kVoid) { m.freelist.push_back(std::make_pair(c.at, c.total)); continue; }

        std::uint32_t cid;
        std::vector<unsigned char> body;
        std::uint64_t ctotal, size_at;
        if (c.id == kAttachments) {
            if (!read_element(m.f, c.at, &cid, &body, &ctotal, &size_at)) continue;
            // Where the Attachments PAYLOAD starts -- the element header is
            // however many octets the id and the size took, which is not a
            // constant for a file somebody else wrote.
            const std::uint64_t atts_payload_at = c.at + c.total - body.size();
            Cursor outer{body.data(), body.size(), 0};
            std::uint32_t aid;
            const unsigned char* ad;
            std::uint64_t an;
            // This writer puts one AttachedFile in each Attachments element, so
            // the element is the unit that is moved and freed. An append-only
            // writer puts every object in one Attachments; those read like any
            // other, but nothing can be done to one in place without touching
            // its neighbours, so they are marked `shared` and update() and
            // remove() refuse them.
            std::vector<Impl::Slot> found_here;
            while (outer.element(&aid, &ad, &an)) {
                if (aid != kAttachedFile) continue;   // Void padding, or something newer
                Impl::Slot s;
                s.elem_at = c.at;
                s.elem_bytes = c.total;
                s.seg_size_at = size_at;
                const std::uint64_t att_payload_at = atts_payload_at + (ad - body.data());
                s.att_size_at = atts_payload_at + outer.hdr + id_octets(kAttachedFile);
                Cursor in{ad, static_cast<std::size_t>(an), 0};
                std::uint32_t fid;
                const unsigned char* fd;
                std::uint64_t fn;
                while (in.element(&fid, &fd, &fn)) {
                    switch (fid) {
                        case kFileUID: s.meta.uid = get_uint(fd, fn); break;
                        case kPtoKind: s.meta.kind = get_text(fd, fn); break;
                        case kPtoEncoding: s.meta.encoding = get_text(fd, fn); break;
                        case kFileName: s.meta.name = get_text(fd, fn); break;
                        case kFileMedia: s.meta.media_type = get_text(fd, fn); break;
                        case kFileDescr: s.meta.description = get_text(fd, fn); break;
                        case kPtoRowCount:
                            s.meta.rows = get_uint(fd, fn);
                            // Patchable in place only at the full width this writer
                            // uses; a packed count from an older file stays as-is.
                            if (fn == 8) s.rows_at = att_payload_at + (fd - ad);
                            break;
                        case kPtoRawSize:
                            s.meta.raw_size = get_uint(fd, fn);
                            if (fn == 8) s.raw_size_at = att_payload_at + (fd - ad);
                            break;
                        case kFileData:
                            s.meta.offset = att_payload_at + (fd - ad);
                            s.meta.size = fn;
                            s.data_size_at = att_payload_at + in.hdr + id_octets(kFileData);
                            break;
                        default: break;
                    }
                }
                const bool committed =
                        !have_live ||
                        std::find(live_uids.begin(), live_uids.end(), s.meta.uid) !=
                                live_uids.end();
                if (s.meta.uid == 0 || !committed) continue;
                found_here.push_back(s);
            }
            if (found_here.empty()) {
                // Nothing live in it: a half-finished write from a session that
                // died, or an object that was removed. Space to reuse.
                m.freelist.push_back(std::make_pair(c.at, c.total));
                continue;
            }
            if (found_here.size() > 1)
                for (Impl::Slot& s : found_here) s.shared = true;
            for (const Impl::Slot& s : found_here) m.slots.push_back(s);
            continue;
        }

        if (!read_element(m.f, c.at, &cid, &body, &ctotal, nullptr)) continue;
        Cursor in{body.data(), body.size(), 0};
        std::uint32_t eid;
        const unsigned char* ed;
        std::uint64_t en;
        if (c.id == kPtoBanner) {
            m.banner = get_text(body.data(), body.size());
        } else if (c.id == kInfo) {
            m.info_at = c.at; m.info_bytes = c.total;
            while (in.element(&eid, &ed, &en)) {
                if (eid == kSegmentUUID) m.uuid.assign(ed, ed + en);
                else if (eid == kTitle) m.title = get_text(ed, en);
                else if (eid == kMuxingApp) m.muxing_app = get_text(ed, en);
                else if (eid == kWritingApp) m.writing_app = get_text(ed, en);
                else if (eid == kDateUTC) m.created = get_int(ed, en);
            }
        } else if (c.id == kTags) {
            m.tags_at = c.at; m.tags_bytes = c.total;
            while (in.element(&eid, &ed, &en)) {
                if (eid != kTag) continue;
                PtoTag t;
                Cursor tc{ed, static_cast<std::size_t>(en), 0};
                std::uint32_t xid;
                const unsigned char* xd;
                std::uint64_t xn;
                while (tc.element(&xid, &xd, &xn)) {
                    if (xid == kTargets) {
                        Cursor g{xd, static_cast<std::size_t>(xn), 0};
                        std::uint32_t gid;
                        const unsigned char* gd;
                        std::uint64_t gn;
                        while (g.element(&gid, &gd, &gn))
                            if (gid == kTagAttachUID) t.target = get_uint(gd, gn);
                    } else if (xid == kSimpleTag) {
                        Cursor g{xd, static_cast<std::size_t>(xn), 0};
                        std::uint32_t gid;
                        const unsigned char* gd;
                        std::uint64_t gn;
                        while (g.element(&gid, &gd, &gn)) {
                            switch (gid) {
                                case kTagName: t.name = get_text(gd, gn); break;
                                case kPtoTagIndex: t.index = static_cast<int>(get_int(gd, gn)); break;
                                case kPtoTagSrcType: t.source_type =
                                        static_cast<std::uint32_t>(get_uint(gd, gn)); break;
                                case kPtoTagUInt: t.type = PtoType::UInt; t.u = get_uint(gd, gn); break;
                                case kPtoTagInt: t.type = PtoType::Int; t.i = get_int(gd, gn); break;
                                case kPtoTagFloat: t.type = PtoType::Float; t.d = get_float(gd, gn); break;
                                case kPtoTagDate: t.type = PtoType::Date; t.i = get_int(gd, gn); break;
                                case kTagString: t.type = PtoType::Text; t.text = get_text(gd, gn); break;
                                case kTagBinary: t.type = PtoType::Bytes;
                                                 t.bytes.assign(gd, gd + gn); break;
                                case kPtoTagUID: t.type = PtoType::UID; t.u = get_uint(gd, gn); break;
                                case kPtoTagUIDs: t.type = PtoType::UIDs;
                                    for (std::uint64_t k = 0; k + 8 <= gn; k += 8)
                                        t.uids.push_back(get_uint(gd + k, 8));
                                    break;
                                case kPtoTagFloats: t.type = PtoType::Floats;
                                    for (std::uint64_t k = 0; k + 8 <= gn; k += 8)
                                        t.floats.push_back(get_float(gd + k, 8));
                                    break;
                                case kPtoTagInts: t.type = PtoType::Ints;
                                    for (std::uint64_t k = 0; k + 8 <= gn; k += 8)
                                        t.ints.push_back(get_int(gd + k, 8));
                                    break;
                                default: break;
                            }
                        }
                    }
                }
                m.tags.push_back(t);
            }
        } else if (c.id == kPtoAnnotations) {
            m.notes_at = c.at; m.notes_bytes = c.total;
            while (in.element(&eid, &ed, &en)) {
                if (eid != kPtoAnnotation) continue;
                PtoAnnotation a;
                Cursor ac{ed, static_cast<std::size_t>(en), 0};
                std::uint32_t xid;
                const unsigned char* xd;
                std::uint64_t xn;
                while (ac.element(&xid, &xd, &xn)) {
                    switch (xid) {
                        case kPtoAnnTarget: a.target = get_uint(xd, xn); break;
                        case kPtoAnnFirstRow: a.first_row = get_uint(xd, xn); break;
                        case kPtoAnnLastRow: a.last_row = get_uint(xd, xn); break;
                        case kPtoAnnText: a.text = get_text(xd, xn); break;
                        case kPtoAnnAuthor: a.author = get_text(xd, xn); break;
                        case kPtoAnnDate: a.when = get_int(xd, xn); break;
                        default: break;
                    }
                }
                m.notes.push_back(a);
            }
        } else if (c.id == kCues) {
            m.cues_at = c.at; m.cues_bytes = c.total;
            while (in.element(&eid, &ed, &en)) {
                if (eid != kCuePoint) continue;
                Impl::CueEntry e;
                e.uid = 0;
                Cursor cc{ed, static_cast<std::size_t>(en), 0};
                std::uint32_t xid;
                const unsigned char* xd;
                std::uint64_t xn;
                while (cc.element(&xid, &xd, &xn)) {
                    switch (xid) {
                        case kPtoCueUID: e.uid = get_uint(xd, xn); break;
                        case kPtoCueEvent: e.cue.event = get_uint(xd, xn); break;
                        case kPtoCueOffset: e.cue.offset = get_uint(xd, xn); break;
                        case kPtoCueTime: e.cue.time = get_uint(xd, xn); break;
                        default: break;
                    }
                }
                if (e.uid != 0) m.cuepoints.push_back(e);
            }
        } else {
            // Something a newer writer put here. Left where it is, untouched.
        }
    }

    // Anything past the end of the Segment is an abandoned write -- a session
    // that added an object and died before the commit that would have claimed
    // the bytes. They are reclaimed here rather than left to grow the file
    // forever, which needs the space to become a Void that the next walk can
    // step over.
    const std::uint64_t end = m.seg_data + m.seg_bytes;
    const std::uint64_t on_disk = m.f.length();
    if (writable && on_disk > end && on_disk - end >= kMinVoid) {
        const std::uint64_t stray = on_disk - end;
        if (m.write_void(end, stray)) {
            m.seg_bytes += stray;
            m.freelist.push_back(std::make_pair(end, stray));
        }
    }

    // A Void immediately after an object is that object's room to grow.
    for (std::size_t i = 0; i < m.slots.size(); i++) {
        Impl::Slot& s = m.slots[i];
        const std::uint64_t after = s.elem_at + s.elem_bytes;
        for (std::size_t k = 0; k < m.freelist.size(); k++) {
            if (m.freelist[k].first != after) continue;
            s.slack_at = after;
            s.slack_bytes = m.freelist[k].second;
            m.freelist.erase(m.freelist.begin() + k);
            break;
        }
        s.meta.capacity = s.meta.size + s.slack_bytes;
    }
    m.coalesce();
    m.dirty = false;
    return true;
}

// --- objects -------------------------------------------------------------------

int File::n_objects() const { return static_cast<int>(p_->slots.size()); }

std::vector<PtoObject> File::objects() const {
    std::vector<PtoObject> out;
    out.reserve(p_->slots.size());
    for (std::size_t i = 0; i < p_->slots.size(); i++) out.push_back(p_->slots[i].meta);
    return out;
}

bool File::has(std::uint64_t uid) const { return p_->find(uid) != nullptr; }

PtoObject File::object(std::uint64_t uid) const {
    const Impl::Slot* s = p_->find(uid);
    if (s == nullptr) throw std::invalid_argument("no object with that uid");
    return s->meta;
}

std::uint64_t File::find(const std::string& name) const {
    // Backwards: slots are in write order, and the newest match is the answer.
    // Reading forwards -- which this did -- returns the stalest object with the
    // name, and does it to whoever reached for the most obvious call.
    for (std::size_t i = p_->slots.size(); i-- > 0; )
        if (p_->slots[i].meta.name == name) return p_->slots[i].meta.uid;
    return 0;
}

std::vector<std::uint64_t> File::find_all(const std::string& name) const {
    std::vector<std::uint64_t> out;
    for (std::size_t i = 0; i < p_->slots.size(); i++)
        if (p_->slots[i].meta.name == name) out.push_back(p_->slots[i].meta.uid);
    return out;
}

std::uint64_t File::add(const std::string& kind, const std::string& encoding,
                           const std::string& name, const unsigned char* data,
                           std::size_t n, std::uint64_t reserve) {
    return p_->emit_object(kind, encoding, name, n, reserve,
                           [data, n](FileHandle& f) { return f.write(data, n); });
}


std::uint64_t File::add_file(const std::string& kind, const std::string& encoding,
                                const std::string& name, const std::string& path,
                                std::uint64_t reserve) {
    return p_->add_path(kind, encoding, name, std::string(), path, reserve);
}

std::uint64_t File::attach(const std::string& path, const std::string& name,
                              const std::string& kind, const std::string& encoding,
                              const std::string& media_type) {
    const PtoFileType t = classify(path);
    std::string label = name;
    if (label.empty()) {
        // The filename, not the path it was found at: a container is not a copy
        // of somebody's directory layout. pto_bundle_files overrides this when
        // it walks a directory, where the layout is the point.
        label = detail::fs::u8path(path).filename().string();
    }
    return p_->add_path(kind.empty() ? t.kind : kind,
                        encoding.empty() ? t.encoding : encoding, label,
                        media_type.empty() ? t.media_type : media_type, path, 0);
}


bool File::update(std::uint64_t uid, const unsigned char* data, std::size_t n) {
    Impl& m = *p_;
    m.err.clear();
    if (!m.writable) return m.fail("opened read-only");
    Impl::Slot* s = m.find(uid);
    if (s == nullptr) return m.fail("no object with that uid");
    if (s->shared)
        return m.fail("object shares its Attachments element with others; compact() to a new file first");
    // Whatever the old cues pointed at is about to stop being there.
    m.drop_cues(uid);

    // Patching a size where it lies only works if it was written wide enough to
    // hold a bigger number. A file from a writer that packed its sizes tightly
    // takes the relocating path rather than a corrupted one.
    const bool patchable =
            (s->meta.offset - s->data_size_at) == static_cast<std::uint64_t>(kWideSize);
    const std::uint64_t room = patchable ? s->meta.size + s->slack_bytes : 0;
    const std::uint64_t left = room >= n ? room - n : 0;
    if (n <= room && (left == 0 || left >= kMinVoid)) {
        const std::uint64_t grew = n > s->meta.size ? n - s->meta.size : 0;
        const std::uint64_t shrank = s->meta.size > n ? s->meta.size - n : 0;
        Buf sz;
        sz.put_size(n, kWideSize);
        Buf att;
        att.put_size(s->meta.size ? 0 : 0, kWideSize);   // placeholder, replaced below
        const std::uint64_t att_payload =
                (s->meta.offset - (s->att_size_at + kWideSize)) + n;
        const std::uint64_t att_total = id_octets(kAttachedFile) + kWideSize + att_payload;
        Buf a, b;
        a.put_size(att_payload, kWideSize);
        b.put_size(att_total, kWideSize);

        if (!m.f.at(s->data_size_at, sz.b.data(), sz.b.size()) ||
            !m.f.at(s->att_size_at, a.b.data(), a.b.size()) ||
            !m.f.at(s->seg_size_at, b.b.data(), b.b.size()) ||
            !m.f.at(s->meta.offset, data, n))
            return m.fail("write failed");

        s->elem_bytes = id_octets(kAttachments) + kWideSize + att_total;
        s->meta.size = n;
        s->slack_at = left ? s->elem_at + s->elem_bytes : 0;
        s->slack_bytes = left;
        if (left && !m.write_void(s->slack_at, left)) return false;
        (void)grew; (void)shrank;
        m.dirty = true;
        return true;
    }

    // Too big for the hole it is in: write it somewhere else and let the old
    // space go. The uid survives, the offset does not.
    const PtoObject old = s->meta;
    const std::uint64_t old_at = s->elem_at;
    const std::uint64_t old_bytes = s->elem_bytes + s->slack_bytes;
    const std::uint64_t keep_uid = old.uid;

    for (std::size_t i = 0; i < m.slots.size(); i++)
        if (m.slots[i].meta.uid == uid) { m.slots.erase(m.slots.begin() + i); break; }
    m.release(old_at, old_bytes);

    const std::uint64_t made = m.emit_object(
            old.kind, old.encoding, old.name, n, n / 8 + 64,
            [data, n](FileHandle& f) { return f.write(data, n); },
            old.media_type, old.rows, old.raw_size);
    if (made == 0) return false;
    Impl::Slot* fresh = m.find(made);
    fresh->meta.uid = keep_uid;
    fresh->meta.description = old.description;
    // The uid is inside the element, so it has to be rewritten there too.
    Buf u;
    u.uint_elem_fixed(kFileUID, keep_uid, kUidOctets);
    const std::uint64_t uid_at = fresh->att_size_at + kWideSize;
    if (!m.f.at(uid_at, u.b.data(), u.b.size())) return m.fail("write failed");
    m.dirty = true;
    return true;
}

bool File::remove(std::uint64_t uid) {
    Impl& m = *p_;
    m.err.clear();
    if (!m.writable) return m.fail("opened read-only");
    for (std::size_t i = 0; i < m.slots.size(); i++) {
        if (m.slots[i].meta.uid != uid) continue;
        if (m.slots[i].shared)
            return m.fail("object shares its Attachments element with others; compact() to a new file first");
        m.release(m.slots[i].elem_at, m.slots[i].elem_bytes + m.slots[i].slack_bytes);
        m.slots.erase(m.slots.begin() + i);
        m.drop_cues(uid);
        m.dirty = true;
        return true;
    }
    return m.fail("no object with that uid");
}

std::vector<unsigned char> File::read_stored(std::uint64_t uid) const {
    const Impl::Slot* s = p_->find(uid);
    if (s == nullptr) throw std::runtime_error("no object with that uid");
    std::vector<unsigned char> out(static_cast<std::size_t>(s->meta.size));
    if (!out.empty()) {
        p_->f.flush();
        if (!p_->f.seek(s->meta.offset) || !p_->f.read(out.data(), out.size()))
            throw std::runtime_error("could not read the payload of " +
                                     std::to_string(uid));
    }
    return out;
}

std::vector<unsigned char> File::read(std::uint64_t uid) const {
    const Impl::Slot* s = p_->find(uid);
    if (s == nullptr) throw std::runtime_error("no object with that uid");
    const Encoding enc = split_encoding(s->meta.encoding);
    std::vector<unsigned char> stored = read_stored(uid);
    if (enc.codec.empty() || stored.empty()) return stored;
    if (!has_codec(enc.codec))
        throw std::runtime_error("object " + std::to_string(uid) + " is encoded '" +
                                 s->meta.encoding + "' and no " + enc.codec +
                                 " codec is registered");
    std::vector<unsigned char> out;
    if (!decompress_bytes(enc.codec, stored.data(), stored.size(),
                          static_cast<std::size_t>(s->meta.raw_size), out))
        throw std::runtime_error("the " + enc.codec + " stream of object " +
                                 std::to_string(uid) + " is corrupt");
    return out;
}

std::uint64_t File::add_coded(const std::string& kind, const std::string& inner_encoding,
                              const std::string& codec, const std::string& name,
                              const unsigned char* data, std::size_t n,
                              int level, std::uint64_t reserve) {
    Impl& m = *p_;
    m.err.clear();
    if (codec.empty() || codec.find('+') != std::string::npos) { m.fail("a codec name is needed"); return 0; }
    if (!has_codec(codec)) { m.fail("no " + codec + " codec is registered"); return 0; }
    std::vector<unsigned char> packed;
    if (!compress_bytes(codec, data, n, level, packed)) { m.fail(codec + " could not compress the payload"); return 0; }
    const std::string encoding = inner_encoding + "+" + codec;
    const unsigned char* pd = packed.data();
    const std::size_t pn = packed.size();
    return m.emit_object(kind, encoding, name, pn, reserve,
                         [pd, pn](FileHandle& f) { return f.write(pd, pn); },
                         std::string(), 0, n);
}

bool File::update_coded(std::uint64_t uid, const unsigned char* data, std::size_t n,
                        int level) {
    Impl& m = *p_;
    m.err.clear();
    Impl::Slot* s = m.find(uid);
    if (s == nullptr) return m.fail("no object with that uid");
    const Encoding enc = split_encoding(s->meta.encoding);
    if (enc.codec.empty()) return m.fail("object " + std::to_string(uid) + " is not coded; use update()");
    if (!has_codec(enc.codec)) return m.fail("no " + enc.codec + " codec is registered");
    std::vector<unsigned char> packed;
    if (!compress_bytes(enc.codec, data, n, level, packed))
        return m.fail(enc.codec + " could not compress the payload");
    // Set before the call so a relocating update writes it into the fresh
    // header, and patched after so an in-place update corrects the old one.
    s->meta.raw_size = n;
    if (!update(uid, packed.data(), packed.size())) return false;
    s = m.find(uid);
    if (s == nullptr) return m.fail("object lost during update");
    s->meta.raw_size = n;
    if (s->raw_size_at != 0) {
        Buf r;
        r.be(n, 8);
        if (!m.f.at(s->raw_size_at, r.b.data(), r.b.size())) return m.fail("write failed");
    }
    return true;
}

std::size_t File::read_at(std::uint64_t uid, std::uint64_t at,
                             void* into, std::size_t n) const {
    Impl& m = *p_;
    const Impl::Slot* s = m.find(uid);
    if (s == nullptr || at >= s->meta.size || n == 0) return 0;
    // Short at the end rather than an error, because that is what a read of a
    // file does and a payload is a file that happens to live inside another.
    const std::uint64_t left = s->meta.size - at;
    const std::size_t take = n < left ? n : static_cast<std::size_t>(left);
    m.f.flush();
    if (!m.f.seek(s->meta.offset + at) || !m.f.read(into, take)) return 0;
    return take;
}


bool File::stream(std::uint64_t uid,
                     const std::function<bool(const void*, std::size_t)>& sink) const {
    Impl& m = *p_;
    m.err.clear();
    const Impl::Slot* s = m.find(uid);
    if (s == nullptr) return m.fail("no object with that uid");
    // A coded payload is decoded whole and then handed over in blocks: the
    // codecs are whole-buffer, and a consumer of `stream` wants the bytes the
    // object was added with, not the stream they travel as.
    if (!split_encoding(s->meta.encoding).codec.empty() && s->meta.size != 0) {
        std::vector<unsigned char> whole;
        try { whole = read(uid); } catch (const std::exception& e) { return m.fail(e.what()); }
        const std::size_t block = 1u << 20;
        for (std::size_t at = 0; at < whole.size(); at += block) {
            const std::size_t take = whole.size() - at < block ? whole.size() - at : block;
            if (!sink(whole.data() + at, take)) return false;
        }
        return true;
    }
    return stream_stored(uid, sink);
}

bool File::stream_stored(std::uint64_t uid,
                            const std::function<bool(const void*, std::size_t)>& sink) const {
    Impl& m = *p_;
    m.err.clear();
    const Impl::Slot* s = m.find(uid);
    if (s == nullptr) return m.fail("no object with that uid");

    if (s->meta.size == 0) {
        std::string sc = external_payload_path(uid);
        if (!sc.empty() && detail::fs::exists(sc)) {
            FileHandle in;
            if (!in.open(sc, "rb")) return m.fail("cannot open sidecar " + sc);
            std::vector<unsigned char> chunk(1u << 20);
            std::uintmax_t left = detail::fs::file_size(sc);
            while (left > 0) {
                const std::size_t take = static_cast<std::size_t>(left < chunk.size() ? left : chunk.size());
                if (!in.read(chunk.data(), take)) return m.fail("could not read sidecar " + sc);
                if (!sink(chunk.data(), take)) return false;
                left -= take;
            }
            return true;
        }
    }

    m.f.flush();
    if (!m.f.seek(s->meta.offset)) return m.fail("cannot reach the payload");

    // In blocks, so an eight-gigabyte stream costs a megabyte of memory
    // whatever the sink does with it.
    std::vector<unsigned char> chunk(1u << 20);
    std::uint64_t left = s->meta.size;
    while (left > 0) {
        const std::size_t take =
                static_cast<std::size_t>(left < chunk.size() ? left : chunk.size());
        if (!m.f.read(chunk.data(), take))
            return m.fail("could not read the payload of " + std::to_string(uid));
        if (!sink(chunk.data(), take)) return false;
        left -= take;
    }
    return true;
}

std::vector<unsigned char> File::read(std::uint64_t uid, std::uint64_t at,
                                         std::size_t n) const {
    if (p_->find(uid) == nullptr) throw std::runtime_error("no object with that uid");
    std::vector<unsigned char> out(n);
    out.resize(read_at(uid, at, out.empty() ? nullptr : out.data(), n));
    return out;
}

bool File::extract(std::uint64_t uid, const std::string& filename) const {
    Impl& m = *p_;
    m.err.clear();
    if (m.find(uid) == nullptr) return m.fail("no object with that uid");

    const Impl::Slot* s = m.find(uid);
    if (s != nullptr && s->meta.size == 0) {
        std::string sc = external_payload_path(uid);
        if (!sc.empty() && detail::fs::exists(sc)) {
            std::error_code ec;
            detail::fs::path src(sc);
            detail::fs::path dst(filename);
            if (detail::fs::equivalent(src, dst, ec)) return true;
            detail::fs::copy_file(src, dst, detail::fs::copy_options::overwrite_existing, ec);
            if (ec) return m.fail("cannot copy sidecar " + sc + " to " + filename + ": " + ec.message());
            return true;
        }
    }

    FileHandle out;
    if (!out.open(filename, "wb")) return m.fail("cannot create " + filename);
    return stream(uid, [&](const void* block, std::size_t n) {
        if (out.write(block, n)) return true;
        return m.fail("could not copy the payload of " + std::to_string(uid));
    });
}

std::vector<std::string> File::disassemble(
        const std::string& directory,
        const std::function<void(const std::string&)>& on_written) const {
    std::vector<std::string> written;
    std::vector<std::string> used;
    p_->err.clear();
    const std::string sep = directory.empty() ? "" : "/";

    // Every name is checked BEFORE anything is written. The writer refuses
    // such a name, so reaching one here means the container came from
    // somewhere else -- which is the case that matters, since a .pto is passed
    // between people and this is what a recipient runs on one.
    //
    // The pre-pass is the point rather than an optimisation: checking inside
    // the loop would refuse the hostile object correctly and still leave the
    // objects before it on disk, so a caller who saw the failure would find a
    // directory that is neither empty nor complete.
    for (std::size_t i = 0; i < p_->slots.size(); i++) {
        const PtoObject& o = p_->slots[i].meta;
        if (o.name.empty()) continue;   // disassemble falls back to the uid
        std::string why;
        if (!name_is_a_safe_relative_path(o.name, &why)) {
            p_->fail("object " + std::to_string(o.uid) + " is named \"" + o.name +
                     "\", which " + why + "; it would be written outside " +
                     directory + ", so nothing was disassembled");
            return std::vector<std::string>();
        }
    }

    for (std::size_t i = 0; i < p_->slots.size(); i++) {
        const PtoObject& o = p_->slots[i].meta;
        std::string name = o.name;
        if (name.empty()) name = std::to_string(o.uid);

        // A name is a label, and two objects may share one. The first keeps it.
        if (std::find(used.begin(), used.end(), name) != used.end())
            name = std::to_string(o.uid) + "-" + name;
        used.push_back(name);
        const std::string path = directory + sep + name;

        std::error_code ec;
        detail::fs::path p(path);
        auto parent = p.parent_path();
        if (!parent.empty()) {
            detail::fs::create_directories(parent, ec);
        }

        if (!extract(o.uid, path)) return std::vector<std::string>();
        written.push_back(path);
        if (on_written) on_written(path);
    }
    return written;
}

// --- metadata ---------------------------------------------------------------------

std::string File::title() const { return p_->title; }
void File::set_title(const std::string& s) { p_->title = s; p_->dirty = true; }
std::string File::writing_app() const { return p_->writing_app; }
std::string File::banner() const { return p_->banner; }
void File::set_writing_app(const std::string& s) {
    p_->writing_app = s; p_->dirty = true;
}
std::vector<unsigned char> File::uuid() const { return p_->uuid; }
std::uint64_t File::generation() const { return p_->generation; }
std::uint64_t File::doctype_version() const { return p_->doctype_version; }

std::vector<PtoTag> File::tags() const { return p_->tags; }

std::vector<PtoTag> File::tags_for(std::uint64_t uid) const {
    std::vector<PtoTag> out;
    for (std::size_t i = 0; i < p_->tags.size(); i++)
        if (p_->tags[i].target == uid) out.push_back(p_->tags[i]);
    return out;
}

namespace {
bool tag_equal(const PtoTag& a, const PtoTag& b) {
    return a.name == b.name && a.type == b.type && a.target == b.target &&
           a.index == b.index && a.source_type == b.source_type &&
           a.u == b.u && a.i == b.i && a.d == b.d && a.text == b.text &&
           a.bytes == b.bytes && a.floats == b.floats && a.ints == b.ints &&
           a.uids == b.uids;
}
} // anonymous namespace

void File::add_tag(const PtoTag& tag) {
    for (const PtoTag& t : p_->tags)
        if (tag_equal(t, tag)) return;
    p_->tags.push_back(tag);
    p_->dirty = true;
}
void File::set_tag(const PtoTag& tag) {
    std::vector<PtoTag>& ts = p_->tags;
    ts.erase(std::remove_if(ts.begin(), ts.end(), [&](const PtoTag& t) {
                 return t.target == tag.target && t.name == tag.name &&
                        t.index == tag.index;
             }),
             ts.end());
    ts.push_back(tag);
    p_->dirty = true;
}
void File::set_tags(const std::vector<PtoTag>& tags) {
    p_->tags = tags; p_->dirty = true;
}
void File::clear_tags() { p_->tags.clear(); p_->dirty = true; }
void File::clear_tags(std::uint64_t target, const std::string& name) {
    std::vector<PtoTag>& ts = p_->tags;
    const std::size_t before = ts.size();
    ts.erase(std::remove_if(ts.begin(), ts.end(), [&](const PtoTag& t) {
                 return t.target == target && t.name == name;
             }),
             ts.end());
    if (ts.size() != before) p_->dirty = true;
}

std::vector<PtoAnnotation> File::annotations() const { return p_->notes; }
void File::add_annotation(const PtoAnnotation& note) {
    p_->notes.push_back(note); p_->dirty = true;
}
void File::clear_annotations() { p_->notes.clear(); p_->dirty = true; }

std::vector<PtoCue> File::cues(std::uint64_t uid) const {
    std::vector<PtoCue> out;
    for (std::size_t i = 0; i < p_->cuepoints.size(); i++)
        if (p_->cuepoints[i].uid == uid) out.push_back(p_->cuepoints[i].cue);
    std::sort(out.begin(), out.end(),
              [](const PtoCue& a, const PtoCue& b) { return a.event < b.event; });
    return out;
}

void File::clear_cues(std::uint64_t uid) { p_->drop_cues(uid); }

void File::set_cues(std::uint64_t uid, const std::vector<PtoCue>& cues) {
    Impl& m = *p_;
    m.drop_cues(uid);
    for (const PtoCue& c : cues) m.cuepoints.push_back(Impl::CueEntry{uid, c});
    m.dirty = true;
}

void File::flush() const { const_cast<FileHandle&>(p_->f).flush(); }

void File::set_error(const std::string& why) { p_->err = why; }

std::uint64_t File::ebml_offset() const { return p_->ebml_at; }

PtoFileType File::classify(const std::string& path) const { return classify_by_extension(path); }

std::string File::external_payload_path(std::uint64_t) const { return std::string(); }

namespace {

/// Decode one element header at `at`, bounded by `limit`. Nothing is
/// allocated from what the file says, so a hostile size costs nothing.
bool read_header(FileHandle& f, std::uint64_t at, std::uint64_t limit, Element* e) {
    if (at + 2 > limit) return false;
    unsigned char head[12];
    std::size_t n = static_cast<std::size_t>(limit - at);
    if (n > sizeof(head)) n = sizeof(head);
    if (!f.seek(at) || !f.read(head, n)) return false;
    int len = 1;
    if (head[0] == 0) return false;
    while (len <= 4 && !(head[0] & (0x80 >> (len - 1)))) len++;
    if (len > 4 || static_cast<std::size_t>(len) >= n) return false;
    std::uint32_t id = 0;
    for (int k = 0; k < len; k++) id = (id << 8) | head[k];
    const unsigned char sf = head[len];
    if (sf == 0) return false;
    int slen = 1;
    while (slen <= 8 && !(sf & (0x80 >> (slen - 1)))) slen++;
    if (slen > 8 || static_cast<std::size_t>(len + slen) > n) return false;
    std::uint64_t sz = sf & (0xFF >> slen);
    for (int k = 1; k < slen; k++) sz = (sz << 8) | head[len + k];
    if (sz == ((1ULL << (7 * slen)) - 1)) return false;   // unknown size: never written by PTO
    e->id = id;
    e->name = element_name(id);
    e->offset = at;
    e->data_offset = at + len + slen;
    e->size = sz;
    e->total = len + slen + sz;
    return true;
}

bool is_master(std::uint32_t id) {
    switch (id) {
        case kEBML: case kSegment: case kSeekHead: case kSeek: case kInfo:
        case kAttachments: case kAttachedFile: case kTags: case kTag: case kTargets:
        case kSimpleTag: case kCues: case kCuePoint: case kPtoAnnotations: case kPtoAnnotation:
            return true;
        default:
            return false;
    }
}

/// Walk `[at, limit)` as a sequence of elements at `depth`, descending into
/// masters. Returns false if something did not decode; `problems` says what.
void walk(FileHandle& f, std::uint64_t at, std::uint64_t limit, int depth,
          std::vector<Element>* out, std::vector<Problem>* problems) {
    while (at < limit) {
        Element e;
        if (!read_header(f, at, limit, &e)) {
            if (problems) problems->push_back(Problem{at, "element header does not decode", true});
            return;
        }
        if (e.offset + e.total > limit) {
            if (problems)
                problems->push_back(Problem{at, e.name + std::string(e.name.empty() ? "" : " ") +
                                                    "element reaches past its parent", true});
            return;
        }
        e.depth = depth;
        if (out) out->push_back(e);
        if (problems && e.name.empty() && depth > 0)
            problems->push_back(Problem{at, "unknown element id 0x" + [&] {
                                            char b[16]; std::snprintf(b, sizeof b, "%X", e.id); return std::string(b); }(), false});
        if (is_master(e.id)) walk(f, e.data_offset, e.data_offset + e.size, depth + 1, out, problems);
        at = e.offset + e.total;
    }
    if (problems && at != limit)
        problems->push_back(Problem{at, "children do not fill their parent exactly", true});
}

}  // namespace

const char* element_name(std::uint32_t id) {
    switch (id) {
        case kEBML: return "EBML";
        case kEBMLVersion: return "EBMLVersion";
        case kEBMLReadVersion: return "EBMLReadVersion";
        case kEBMLMaxIDLength: return "EBMLMaxIDLength";
        case kEBMLMaxSizeLen: return "EBMLMaxSizeLength";
        case kDocType: return "DocType";
        case kDocTypeVersion: return "DocTypeVersion";
        case kDocTypeReadVer: return "DocTypeReadVersion";
        case kSegment: return "Segment";
        case kSeekHead: return "SeekHead";
        case kSeek: return "Seek";
        case kSeekID: return "SeekID";
        case kSeekPosition: return "SeekPosition";
        case kInfo: return "Info";
        case kSegmentUUID: return "SegmentUUID";
        case kTitle: return "Title";
        case kMuxingApp: return "MuxingApp";
        case kWritingApp: return "WritingApp";
        case kDateUTC: return "DateUTC";
        case kAttachments: return "Attachments";
        case kAttachedFile: return "AttachedFile";
        case kFileDescr: return "FileDescription";
        case kFileName: return "FileName";
        case kFileMedia: return "FileMediaType";
        case kFileData: return "FileData";
        case kFileUID: return "FileUID";
        case kTags: return "Tags";
        case kTag: return "Tag";
        case kTargets: return "Targets";
        case kTagAttachUID: return "TagAttachmentUID";
        case kSimpleTag: return "SimpleTag";
        case kTagName: return "TagName";
        case kTagString: return "TagString";
        case kTagBinary: return "TagBinary";
        case kVoid: return "Void";
        case kCRC32: return "CRC-32";
        case kCues: return "Cues";
        case kCuePoint: return "CuePoint";
        case kPtoKind: return "PtoKind";
        case kPtoEncoding: return "PtoEncoding";
        case kPtoRowCount: return "PtoRowCount";
        case kPtoRawSize: return "PtoRawSize";
        case kPtoGeneration: return "PtoGeneration";
        case kPtoSeekUID: return "PtoSeekUID";
        case kPtoTagIndex: return "PtoTagIndex";
        case kPtoTagSrcType: return "PtoTagSourceType";
        case kPtoTagUInt: return "PtoTagUInt";
        case kPtoTagInt: return "PtoTagInt";
        case kPtoTagFloat: return "PtoTagFloat";
        case kPtoTagDate: return "PtoTagDate";
        case kPtoTagUID: return "PtoTagUID";
        case kPtoTagUIDs: return "PtoTagUIDs";
        case kPtoTagFloats: return "PtoTagFloats";
        case kPtoTagInts: return "PtoTagInts";
        case kPtoCueUID: return "PtoCueUID";
        case kPtoCueEvent: return "PtoCueEvent";
        case kPtoCueOffset: return "PtoCueOffset";
        case kPtoCueTime: return "PtoCueTime";
        case kPtoBanner: return "PtoBanner";
        case kPtoAnnotations: return "PtoAnnotations";
        case kPtoAnnotation: return "PtoAnnotation";
        case kPtoAnnTarget: return "PtoAnnotationTarget";
        case kPtoAnnFirstRow: return "PtoAnnotationFirstRow";
        case kPtoAnnLastRow: return "PtoAnnotationLastRow";
        case kPtoAnnText: return "PtoAnnotationText";
        case kPtoAnnAuthor: return "PtoAnnotationAuthor";
        case kPtoAnnDate: return "PtoAnnotationDate";
        default: return "";
    }
}

std::vector<Element> File::elements() const {
    std::vector<Element> out;
    if (!is_open()) return out;
    FileHandle& f = const_cast<FileHandle&>(p_->f);
    walk(f, p_->ebml_at, f.length(), 0, &out, nullptr);
    return out;
}

std::vector<Problem> File::verify(bool strict_alignment) const {
    std::vector<Problem> out;
    if (!is_open()) {
        out.push_back(Problem{0, "no file is open", true});
        return out;
    }
    const Impl& m = *p_;
    FileHandle& f = const_cast<FileHandle&>(m.f);
    const std::uint64_t end = f.length();
    std::vector<Element> els;
    walk(f, m.ebml_at, end, 0, &els, &out);

    // The top level: an EBML header, then one Segment that reaches the end.
    std::size_t n_top = 0;
    bool saw_segment = false;
    for (const Element& e : els) {
        if (e.depth != 0) continue;
        n_top++;
        if (n_top == 1 && e.id != kEBML)
            out.push_back(Problem{e.offset, "the first element is not an EBML header", true});
        if (e.id == kSegment) {
            saw_segment = true;
            if (e.offset + e.total != end)
                out.push_back(Problem{e.offset + e.total, std::to_string(end - (e.offset + e.total)) +
                                                              " bytes after the Segment", true});
        }
    }
    if (!saw_segment) out.push_back(Problem{m.ebml_at, "no Segment", true});

    // Payload alignment, and every object's FileUID being the eight octets that
    // let it be rewritten in place.
    for (const Element& e : els) {
        if (e.id == kFileData && e.size != 0 && (e.data_offset % kPayloadAlign) != 0)
            out.push_back(Problem{e.data_offset, "payload is not 8-byte aligned", strict_alignment});
        if (e.id == kFileUID && e.size != kUidOctets)
            out.push_back(Problem{e.offset, "FileUID is " + std::to_string(e.size) +
                                                " octets, not 8 -- cannot be rewritten in place", false});
    }

    // Each index: does its CRC-32 hold, and does every uid it names exist?
    std::vector<std::uint64_t> uids;
    for (const Impl::Slot& sl : m.slots) uids.push_back(sl.meta.uid);
    std::sort(uids.begin(), uids.end());
    for (std::size_t i = 1; i < uids.size(); i++)
        if (uids[i] == uids[i - 1])
            out.push_back(Problem{0, "uid " + std::to_string(uids[i]) + " is used by two objects", true});
    int verified = 0, heads = 0;
    int carry_crc = 0;   // indexes that carry a CRC-32 at all
    for (const Element& e : els) {
        if (e.id != kSeekHead) continue;
        heads++;
        std::vector<unsigned char> body(static_cast<std::size_t>(e.size));
        if (!f.seek(e.data_offset) || !f.read(body.data(), body.size())) continue;
        Cursor c{body.data(), body.size(), 0};
        std::uint32_t cid;
        const unsigned char* d;
        std::uint64_t n;
        bool has_crc = false, ok = false;
        std::size_t after = 0;
        std::uint32_t stored = 0;
        while (c.element(&cid, &d, &n)) {
            if (cid == kCRC32 && n == 4) {
                stored = static_cast<std::uint32_t>(d[0]) | (static_cast<std::uint32_t>(d[1]) << 8) |
                         (static_cast<std::uint32_t>(d[2]) << 16) | (static_cast<std::uint32_t>(d[3]) << 24);
                has_crc = true;
                carry_crc++;
                after = c.i;
            } else if (cid == kSeek) {
                Cursor sc{d, static_cast<std::size_t>(n), 0};
                std::uint32_t sid;
                const unsigned char* sd;
                std::uint64_t sn;
                std::uint64_t uid = 0;
                std::uint64_t pos = 0;
                bool has_pos = false;
                while (sc.element(&sid, &sd, &sn)) {
                    if (sid == kPtoSeekUID) uid = get_uint(sd, sn);
                    if (sid == kSeekPosition) { pos = get_uint(sd, sn); has_pos = true; }
                }
                if (has_pos) {
                    const std::uint64_t target = m.seg_data + pos;
                    bool lands = false;
                    for (const Element& t : els) if (t.offset == target) { lands = true; break; }
                    if (!lands)
                        out.push_back(Problem{e.offset, "a SeekPosition points between elements", true});
                }
                if (uid != 0 && !std::binary_search(uids.begin(), uids.end(), uid) &&
                    m.head_count >= 2 && m.live == (heads - 1))
                    out.push_back(Problem{e.offset, "the live index names uid " + std::to_string(uid) +
                                                        " which is not in the file", true});
            }
        }
        if (has_crc) {
            ok = crc32_ebml(body.data() + after, body.size() - after) == stored;
            if (ok) verified++;
        }
    }
    if (heads == 0) out.push_back(Problem{0, "no SeekHead", true});
    // An index that carries a CRC-32 makes a claim, and at least one such
    // claim must hold. Indexes with no CRC at all are the append-only profile
    // -- a hint of where the directory objects are, every object live -- and
    // a file written that way is conformant, whether it has one or two.
    if (carry_crc > 0 && verified == 0)
        out.push_back(Problem{0, "an index carries a CRC-32 and none verifies", true});
    return out;
}

std::vector<PtoExtent> File::free_extents() const {
    std::vector<PtoExtent> out;
    out.reserve(p_->freelist.size());
    for (std::size_t i = 0; i < p_->freelist.size(); i++) {
        PtoExtent e;
        e.offset = p_->freelist[i].first;
        e.bytes = p_->freelist[i].second;
        out.push_back(e);
    }
    return out;
}

// --- commit -------------------------------------------------------------------------

bool File::commit() {
    Impl& m = *p_;
    m.err.clear();
    if (!m.writable) return m.fail("opened read-only");

    Buf info = m.build_info();
    if (!m.place(info, &m.info_at, &m.info_bytes)) return false;
    Buf tags = m.build_tags();
    if (!m.tags.empty() || m.tags_at != 0)
        if (!m.place(tags, &m.tags_at, &m.tags_bytes)) return false;
    Buf notes = m.build_notes();
    if (!m.notes.empty() || m.notes_at != 0)
        if (!m.place(notes, &m.notes_at, &m.notes_bytes)) return false;
    Buf cues = m.build_cuepoints();
    if (!m.cuepoints.empty() || m.cues_at != 0)
        if (!m.place(cues, &m.cues_at, &m.cues_bytes)) return false;

    if (!m.patch_segment_size()) return m.fail("write failed");
    m.f.flush();

    // The index that is not live, so a crash here falls back to the one that is.
    const int spare = 1 - m.live;
    if (!m.write_seekhead(spare, m.generation + 1)) return false;
    m.f.flush();

    m.live = spare;
    m.generation += 1;
    m.dirty = false;
    return true;
}

bool File::compact(const std::string& to, bool tight, double reserve) {
    Impl& m = *p_;
    m.err.clear();
    if (reserve < 0.0) return m.fail("a negative reserve is not a fraction");
    File out;
    if (!out.create(to, m.title, m.banner)) return m.fail(out.error());
    out.set_writing_app(m.writing_app);
    out.p_->align_payloads = !tight;
    for (std::size_t i = 0; i < m.slots.size(); i++) {
        const PtoObject& o = m.slots[i].meta;
        const std::uint64_t room =
                static_cast<std::uint64_t>(static_cast<double>(o.size) * reserve);
        // Streamed, not read: this is the one operation that touches every
        // payload in the file, so materialising them would make compacting an
        // eight-gigabyte container need eight gigabytes of memory -- for the
        // job whose entire purpose is to make the file smaller.
        bool copied = true;
        const std::uint64_t made = out.p_->emit_object(
                o.kind, o.encoding, o.name, o.size, room,
                [&](FileHandle& dst) {
                    copied = stream_stored(o.uid, [&](const void* block, std::size_t n) {
                        return dst.write(block, n);
                    });
                    return copied;
                },
                o.media_type, o.rows, o.raw_size);
        // A failed copy already left its reason in this file's error, since
        // `stream` reads from here; a failed write left it in the new one's.
        if (made == 0) return copied ? m.fail(out.error()) : false;
        // Keep the identity: everything that refers to this object refers to it
        // by uid, and compaction is not supposed to be observable.
        Impl::Slot* s = out.p_->find(made);
        s->meta.uid = o.uid;
        Buf u;
        u.uint_elem_fixed(kFileUID, o.uid, kUidOctets);
        if (!out.p_->f.at(s->att_size_at + kWideSize, u.b.data(), u.b.size()))
            return m.fail("write failed");
    }
    out.set_tags(m.tags);
    for (std::size_t i = 0; i < m.notes.size(); i++) out.add_annotation(m.notes[i]);
    // Cues survive: they address a byte offset INTO a payload, and compaction
    // moves payloads without changing one of them.
    out.p_->cuepoints = m.cuepoints;
    return out.commit() ? true : m.fail(out.error());
}

// --- probing --------------------------------------------------------------------

// --- tables ---------------------------------------------------------------------

std::uint64_t pto_add_store(File& file, const std::string& kind,
                            const std::string& name, const DataStore& store,
                            std::uint64_t reserve) {
    return pto_add_store(file, kind, name, store, reserve, StoreOptions());
}

std::uint64_t pto_add_store(File& file, const std::string& kind,
                            const std::string& name, const DataStore& store,
                            std::uint64_t reserve, const StoreOptions& options) {
    File::Impl& m = *file.p_;
    m.err.clear();
    if (!m.writable) { m.fail("opened read-only"); return 0; }
    // This lays down its own header rather than going through emit_object, so
    // it needs the name gate of its own. See why_name_cannot_be_written.
    {
        const std::string bad = why_name_cannot_be_written(name);
        if (!bad.empty()) { m.fail(bad); return 0; }
    }

    File::Impl::Slot s;
    s.meta.uid = unused_uid(m.slots);
    s.meta.kind = kind;
    s.meta.encoding = "dstore";
    s.meta.media_type = "application/x-dstore";
    s.meta.name = name;
    s.meta.rows = store.n_rows();

    Buf head;
    head.uint_elem_fixed(kFileUID, s.meta.uid, kUidOctets);
    head.text_elem(kPtoKind, kind);
    head.text_elem(kPtoEncoding, "dstore");
    if (!name.empty()) head.text_elem(kFileName, name);
    head.text_elem(kFileMedia, "application/x-dstore");
    // Always present and always 8 octets, even for an empty table: an update
    // that finds a different number of rows patches this in place, and a
    // packed width would not have the byte a grown count needs.
    head.uint_elem_fixed(kPtoRowCount, s.meta.rows, 8);

    // The size is not known until the store has been written, so this always
    // appends -- there is no hole to look for one that fits. The three sizes
    // are written wide and patched afterwards, which is what RFC 8794 permits
    // an over-wide Data Size for.
    //
    // The payload is aligned the same way \ref File::emit_object aligns one,
    // and it matters more here than anywhere: a .dstore's own blob offsets are
    // 8-aligned RELATIVE TO THE STORE, so they are only 8-aligned in the file
    // if the store itself begins on a boundary.
    const std::uint64_t before_payload = header_before_payload(head.b.size());
    const std::uint64_t at_raw = m.seg_data + m.seg_bytes;
    const std::uint64_t pad =
            m.align_payloads ? align_pad(at_raw + before_payload) : 0;
    if (pad != 0) {
        if (!m.write_void(at_raw, pad)) return 0;
        m.seg_bytes += pad;
    }
    const std::uint64_t at = at_raw + pad;

    Buf prefix;
    prefix.put_id(kAttachments);
    prefix.put_size(0, kWideSize);
    prefix.put_id(kAttachedFile);
    prefix.put_size(0, kWideSize);
    prefix.raw(head.b.data(), head.b.size());
    prefix.put_id(kFileData);
    prefix.put_size(0, kWideSize);
    if (prefix.b.size() != before_payload) {
        m.fail("internal: the object header is not the size it was computed to be");
        return 0;
    }

    if (!m.f.at(at, prefix.b.data(), prefix.b.size())) { m.fail("write failed"); return 0; }

    const std::uint64_t n = write_store_at(m.f.get(), store, options);
    if (n == 0) { m.fail("could not write the store into the container"); return 0; }

    const std::uint64_t att_payload = head.b.size() + id_octets(kFileData) + kWideSize + n;
    const std::uint64_t att_total = id_octets(kAttachedFile) + kWideSize + att_payload;
    const std::uint64_t elem_total = id_octets(kAttachments) + kWideSize + att_total;

    s.elem_at = at;
    s.elem_bytes = elem_total;
    s.seg_size_at = at + id_octets(kAttachments);
    s.att_size_at = s.seg_size_at + kWideSize + id_octets(kAttachedFile);
    s.data_size_at = at + prefix.b.size() - kWideSize;
    s.rows_at = s.att_size_at + kWideSize + head.b.size() - 8;
    s.meta.offset = at + prefix.b.size();
    s.meta.size = n;

    Buf a, b, c;
    a.put_size(att_total, kWideSize);
    b.put_size(att_payload, kWideSize);
    c.put_size(n, kWideSize);
    if (!m.f.at(s.seg_size_at, a.b.data(), a.b.size()) ||
        !m.f.at(s.att_size_at, b.b.data(), b.b.size()) ||
        !m.f.at(s.data_size_at, c.b.data(), c.b.size())) {
        m.fail("write failed");
        return 0;
    }

    if (reserve != 0 && reserve < kMinVoid) reserve = kMinVoid;
    m.seg_bytes += elem_total + reserve;
    if (reserve != 0 && !m.write_void(at + elem_total, reserve)) return 0;
    s.slack_at = reserve ? at + elem_total : 0;
    s.slack_bytes = reserve;
    s.meta.capacity = n + reserve;

    m.slots.push_back(s);
    m.dirty = true;
    return s.meta.uid;
}

bool pto_update_store(File& file, std::uint64_t uid, const DataStore& store) {
    return pto_update_store(file, uid, store, StoreOptions());
}

bool pto_update_store(File& file, std::uint64_t uid, const DataStore& store,
                      const StoreOptions& options) {
    // Unlike pto_add_store this buffers, because File::update has to know
    // the length before it can decide whether the new payload fits where the
    // old one was -- and fitting is the whole point of an update. A table being
    // recomputed is megabytes; a photon stream that is not should be added once
    // and left alone.
    std::FILE* tmp = std::tmpfile();
    if (tmp == nullptr) return file.p_->fail("no temporary file to serialise into");
    const std::uint64_t n = write_store_at(tmp, store, options);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(n));
    bool ok = n != 0 && std::fseek(tmp, 0, SEEK_SET) == 0 &&
              (bytes.empty() || std::fread(bytes.data(), 1, bytes.size(), tmp) == bytes.size());
    std::fclose(tmp);
    if (!ok) return file.p_->fail("could not serialise the store");

    // The row count lives in the object header, which update() does not touch.
    // Set it before the call so a relocating update writes it into the fresh
    // header, and patch it after so an in-place update corrects the old one.
    File::Impl& m = *file.p_;
    const std::uint64_t rows = store.n_rows();
    if (File::Impl::Slot* s = m.find(uid)) s->meta.rows = rows;
    if (!file.update(uid, bytes.data(), bytes.size())) return false;
    if (File::Impl::Slot* s = m.find(uid)) {
        s->meta.rows = rows;
        if (s->rows_at != 0) {
            Buf r;
            r.be(rows, 8);
            if (!m.f.at(s->rows_at, r.b.data(), r.b.size()))
                return m.fail("write failed");
        }
    }
    return true;
}

void pto_mark_sidecar(File& file, std::uint64_t uid, std::uint64_t primary) {
    PtoTag t;
    t.name = kPtoSidecarTag;
    t.type = PtoType::UID;
    t.target = uid;
    t.u = primary;
    file.add_tag(t);
}

PtoObject pto_store_region(const File& file, std::uint64_t uid) {
    const File::Impl& m = *file.p_;
    const File::Impl::Slot* s = m.find(uid);
    if (s == nullptr) throw std::runtime_error("no object with that uid");
    if (split_encoding(s->meta.encoding).inner != "dstore")
        throw std::runtime_error("object " + std::to_string(uid) + " is encoded as '" +
                                 s->meta.encoding + "', not 'dstore'");
    // Not incidental: a store added in this session may still be in the stdio
    // buffer, and the store reader opens the path again rather than sharing
    // this handle.
    const_cast<FileHandle&>(m.f).flush();
    return s->meta;
}

namespace {
/// Whether the store object is coded as a whole (`dstore+zstd`), in which case
/// it is read through memory, decoded first.
bool store_is_coded(const PtoObject& o) { return !split_encoding(o.encoding).codec.empty(); }
}  // namespace

void pto_read_store(const File& file, std::uint64_t uid, DataStore& out) {
    const PtoObject o = pto_store_region(file, uid);
    if (store_is_coded(o)) {
        const std::vector<unsigned char> bytes = file.read(uid);
        read_store_into(out, bytes.data(), bytes.size());
        return;
    }
    read_store_into(out, file.filename(), o.offset, o.size);
}

void pto_read_store(const File& file, std::uint64_t uid, DataStore& out,
                    const std::vector<std::string>& columns) {
    const PtoObject o = pto_store_region(file, uid);
    if (store_is_coded(o)) {
        const std::vector<unsigned char> bytes = file.read(uid);
        read_store_into(out, bytes.data(), bytes.size(), columns);
        return;
    }
    read_store_into(out, file.filename(), o.offset, o.size, columns);
}

void pto_read_store(const File& file, std::uint64_t uid, DataStore& out,
                    const std::vector<std::string>& columns,
                    std::uint64_t first_row, std::uint64_t n_rows) {
    const PtoObject o = pto_store_region(file, uid);
    if (store_is_coded(o)) {
        const std::vector<unsigned char> bytes = file.read(uid);
        read_store_into(out, bytes.data(), bytes.size(), columns, first_row, n_rows);
        return;
    }
    read_store_into(out, file.filename(), o.offset, o.size, columns, first_row, n_rows);
}

std::vector<std::string> pto_store_columns(const File& file, std::uint64_t uid,
                                           const std::string& group) {
    try {
        const PtoObject o = pto_store_region(file, uid);
        if (store_is_coded(o)) {
            const std::vector<unsigned char> bytes = file.read(uid);
            return store_columns(bytes.data(), bytes.size(), group);
        }
        return store_columns(file.filename(), o.offset, o.size, group);
    } catch (const std::exception&) {
        return std::vector<std::string>();
    }
}

std::vector<std::string> pto_store_groups(const File& file, std::uint64_t uid) {
    try {
        const PtoObject o = pto_store_region(file, uid);
        if (store_is_coded(o)) {
            const std::vector<unsigned char> bytes = file.read(uid);
            return store_groups(bytes.data(), bytes.size());
        }
        return store_groups(file.filename(), o.offset, o.size);
    } catch (const std::exception&) {
        return std::vector<std::string>();
    }
}

// --- probing --------------------------------------------------------------------

bool is_pto_file(const std::string& filename) {
    FileHandle f;
    if (!f.open(filename, "rb")) return false;
    std::uint64_t at = 0;
    if (!find_ebml_header(f, &at)) return false;
    std::uint32_t id = 0;
    std::vector<unsigned char> payload;
    std::uint64_t total = 0;
    if (!read_element(f, at, &id, &payload, &total, nullptr) || id != kEBML) return false;
    Cursor c{payload.data(), payload.size(), 0};
    std::uint32_t cid;
    const unsigned char* d;
    std::uint64_t n;
    while (c.element(&cid, &d, &n))
        if (cid == kDocType) return get_text(d, n) == "pto";
    return false;
}
// --- bundling files ------------------------------------------------------------

namespace {

/// The extension, lowercased and without the dot. Empty when there is none.
std::string extension_of(const std::string& path) {
    std::string e = detail::fs::u8path(path).extension().string();
    if (!e.empty() && e[0] == '.') e.erase(0, 1);
    return detail::lowered(e);
}

/*!
 * \brief What a name alone says an object is.
 *
 * No photon format is in here. Those are recognised by their contents, which is
 * the only thing that tells four ".spc" flavours apart -- and which is why a
 * file that merely *ends* in ".raw" does not become a ConfoCor3 stream.
 */
struct ByExtension {
    const char* ext;
    const char* kind;
    const char* encoding;
    const char* media_type;
};
const ByExtension kByExtension[] = {
    {"csv",    "table",      "csv",    "text/csv"},
    {"tsv",    "table",      "tsv",    "text/tab-separated-values"},
    {"dstore", "table",      "dstore", "application/x-dstore"},
    {"npy",    "table",      "npy",    ""},
    {"png",    "image",      "png",    "image/png"},
    {"jpg",    "image",      "jpeg",   "image/jpeg"},
    {"jpeg",   "image",      "jpeg",   "image/jpeg"},
    {"tif",    "image",      "tiff",   "image/tiff"},
    {"tiff",   "image",      "tiff",   "image/tiff"},
    {"svg",    "image",      "svg",    "image/svg+xml"},
    {"pdf",    "attachment", "pdf",    "application/pdf"},
    {"txt",    "attachment", "text",   "text/plain"},
    {"log",    "attachment", "text",   "text/plain"},
    {"rst",    "attachment", "text",   "text/plain"},
    {"md",     "attachment", "text",   "text/markdown"},
    // Half a Becker & Hickl header, and the reason kPtoSidecarTag exists.
    {"set",    "attachment", "set",    "text/plain"},
    {"json",   "attachment", "json",   "application/json"},
    {"yaml",   "attachment", "yaml",   "application/yaml"},
    {"yml",    "attachment", "yaml",   "application/yaml"},
    {"xml",    "attachment", "xml",    "application/xml"},
    {"html",   "attachment", "html",   "text/html"},
    {"py",     "attachment", "python", "text/x-python"},
    {"ipynb",  "attachment", "json",   "application/json"},
    {"h5",     "attachment", "hdf5",   "application/x-hdf5"},
    {"hdf5",   "attachment", "hdf5",   "application/x-hdf5"},
    {"zip",    "attachment", "zip",    "application/zip"},
    {"gz",     "attachment", "gz",     "application/gzip"},
    {"pto",    "attachment", "pto",    "application/x-pto"},
};

}  // namespace

PtoFileType classify_by_extension(const std::string& path) {
    PtoFileType t;
    const std::string ext = extension_of(path);

    for (std::size_t i = 0; i < sizeof(kByExtension) / sizeof(kByExtension[0]); i++) {
        if (ext != kByExtension[i].ext) continue;
        t.kind = kByExtension[i].kind;
        t.encoding = kByExtension[i].encoding;
        t.media_type = kByExtension[i].media_type;
        return t;
    }

    // Carried, named, and left alone. An unrecognised encoding is an object a
    // reader skips, never a file it rejects.
    t.kind = "attachment";
    t.encoding = "raw";
    return t;
}

std::vector<PtoObject> pto_bundle_files(File& file,
                                        const std::vector<std::string>& paths,
                                        bool link_sidecars) {
    std::vector<PtoObject> made;

    // Path on disk, and what the object will be called. The two differ under a
    // directory, where the name carries the layout so disassemble can put it back.
    std::vector<std::pair<std::string, std::string>> work;
    for (std::size_t i = 0; i < paths.size(); i++) {
        std::error_code ec;
        const detail::fs::path here = detail::fs::u8path(paths[i]);
        if (!detail::fs::is_directory(here, ec)) {
            work.push_back(std::make_pair(paths[i], here.filename().string()));
            continue;
        }
        std::vector<detail::fs::path> found;
        detail::fs::list_regular_files(here, found, ec);
        // A walk that stops early would bundle some of the directory and say it
        // bundled the directory, which is the one outcome nobody could detect.
        if (ec) {
            throw std::runtime_error("cannot walk " + here.string() + ": " + ec.message());
        }
        // Sorted, so the same directory bundles to the same object order twice
        // running -- a container nobody can reproduce is a container nobody can
        // compare.
        std::sort(found.begin(), found.end());
        for (std::size_t k = 0; k < found.size(); k++) {
            std::error_code rel_ec;
            std::string rel =
                    detail::fs::relative(found[k], here, rel_ec).generic_string();
            if (rel_ec || rel.empty()) rel = found[k].filename().string();
            work.push_back(std::make_pair(found[k].string(), rel));
        }
    }

    for (std::size_t i = 0; i < work.size(); i++) {
        // A directory holding the container being written holds it while it is
        // being written, and a file cannot carry itself.
        std::error_code ec;
        if (detail::fs::equivalent(detail::fs::u8path(work[i].first),
                                        detail::fs::u8path(file.filename()), ec)) {
            continue;
        }
        const std::uint64_t uid = file.attach(work[i].first, work[i].second);
        if (uid == 0) return made;
        made.push_back(file.object(uid));
    }

    if (link_sidecars) {
        for (std::size_t i = 0; i < made.size(); i++) {
            const detail::fs::path sn = detail::fs::u8path(made[i].name);
            if (detail::lowered(sn.extension().string()) != ".set") continue;
            for (std::size_t k = 0; k < made.size(); k++) {
                if (made[k].uid == made[i].uid) continue;
                const detail::fs::path pn = detail::fs::u8path(made[k].name);
                if (detail::lowered(pn.extension().string()) != ".spc") continue;
                if (pn.parent_path() != sn.parent_path() || pn.stem() != sn.stem()) continue;
                pto_mark_sidecar(file, made[i].uid, made[k].uid);
                break;
            }
        }
    }
    return made;
}
}  // namespace pto

#undef fseek64
#undef ftell64
#undef open_file
#undef utf8_to_wide_path
#endif  // PTOLIB_IMPLEMENTATION
