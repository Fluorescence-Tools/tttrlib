// SPDX-License-Identifier: BSD-3-Clause
#include "DataStore.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace tttrlib {
namespace data {

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

}  // namespace data
}  // namespace tttrlib

// ---------------------------------------------------------------------------
// Expression selection
//
// The general gate, where select_range and the geometric selectors are fixed
// shapes.
//
// The evaluator is ExpressionEngine, and it is the only one: the query is
// compiled once into a block-vectorised program and run over the store's own
// memory, answering with packed bits rather than a float per row. Before
// 2026-08-31 this was ExprTk evaluating a tree per element into a float array,
// which a second pass then packed -- eight bytes of traffic and a whole extra
// pass to say one bit.
//
// ExprTk was then kept as a fallback for what the engine refused, and that
// fallback was retired on 2026-09-02 (T-20260831-13): its multi-argument
// functions evaluated at element 0 and broadcast the scalar, so a gate like
// `hypot(g, r) > 5` kept the wrong rows with nothing to show for it. The two
// reasons a query used to fall through are now handled here instead -- a Bool
// or String column is widened into a per-program double buffer the engine can
// read, and syntax the engine does not implement is *refused*, loudly, at
// compile time. A wrong-and-silent answer is the one behaviour this gate must
// never have.
// ---------------------------------------------------------------------------

#include "ExpressionEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <vector>

namespace {

//! Widening an Int64/UInt64 column to double is exact only to 2^53. Beyond
//! that an `==` would match the wrong rows, so refuse rather than answer
//! wrongly.
void check_exactly_representable(const tttrlib::data::Column& c,
                                 const std::string& name, std::size_t n) {
    const double limit = 9007199254740992.0;  // 2^53
    if (c.type() == tttrlib::data::ColumnType::Int64) {
        const std::int64_t* v = c.i64_ptr();
        for (std::size_t i = 0; i < n; ++i) {
            if (std::fabs(static_cast<double>(v[i])) > limit) {
                throw std::invalid_argument(
                    "select_expression: column '" + name +
                    "' holds Int64 values beyond 2^53, which double-precision "
                    "evaluation cannot represent exactly");
            }
        }
    } else if (c.type() == tttrlib::data::ColumnType::UInt64) {
        const std::uint64_t* v = c.u64_ptr();
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
bool engine_readable(tttrlib::data::ColumnType t) {
    return t != tttrlib::data::ColumnType::Bool &&
           t != tttrlib::data::ColumnType::String;
}

tttrlib::data::ExprScalarType expr_type_of(tttrlib::data::ColumnType t) {
    using tttrlib::data::ColumnType;
    using tttrlib::data::ExprScalarType;
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

void widen_column(const tttrlib::data::Column& c, std::size_t n, double* dst) {
    using tttrlib::data::ColumnType;
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

namespace tttrlib {
namespace data {

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

}  // namespace data
}  // namespace tttrlib
