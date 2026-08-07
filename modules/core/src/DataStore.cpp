// SPDX-License-Identifier: BSD-3-Clause
#include "DataStore.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace tttrlib {
namespace data {


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
                           const std::string& value) {
    nlohmann::json j = nlohmann::json::object();
    if (!blob.empty()) {
        nlohmann::json parsed = nlohmann::json::parse(blob, nullptr, false);
        if (!parsed.is_discarded() && parsed.is_object()) j = std::move(parsed);
    }
    if (value.empty()) j.erase(key);
    else j[key] = value;
    return j.empty() ? std::string() : j.dump();
}

}  // namespace

void Column::set_metadata(const std::string& json) {
    if (json.empty()) {
        metadata_.clear();
        units_.clear();
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
