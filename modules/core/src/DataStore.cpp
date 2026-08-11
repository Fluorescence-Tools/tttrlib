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
