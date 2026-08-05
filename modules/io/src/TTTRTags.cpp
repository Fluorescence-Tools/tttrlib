// SPDX-License-Identifier: BSD-3-Clause
#include "TTTRTags.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

#include "Verbose.h"
#include "string_encoding.h"

namespace tttrlib {
namespace io {

void add_tag(
        nlohmann::json &json_data,
        const std::string &name,
        std::any value,
        unsigned int type,
        int idx
) {
    using namespace std;
    nlohmann::json tag;
    tag["name"] = ::tttrlib::string_encoding::iso_8859_1_to_utf8(name); // there are sometimes conversion issues
    tag["type"] = type;
    tag["idx"] = idx;
    if (type == tyEmpty8) {
        tag["value"] = nullptr;
    } else if (type == tyBool8) {
        tag["value"] = any_cast<bool>(value);
    } else if ((type == tyInt8) || (type == tyBitSet64) || (type == tyColor8)) {
        tag["value"] = any_cast<int>(value);
    } else if ((type == tyFloat8) || (type == tyTDateTime)) {
        tag["value"] = any_cast<double>(value);
    } else if (type == tyFloat8Array) {
        tag["value"] = any_cast<std::vector<double>>(value);
    }
    else if (type == tyAnsiString) {
         auto str = any_cast<char*>(value);
         auto str2 = std::string(str);
         auto str3 = ::tttrlib::string_encoding::iso_8859_1_to_utf8(str2);
         tag["value"] = str3;
    }
    else if (type == tyWideString) {
        auto str = any_cast<wchar_t *>(value);
        auto str2 = std::wstring(str);
        tag["value"] = str2;
    }
    else if (type == tyBinaryBlob) {
        tag["value"] = any_cast<std::vector<int32_t>>(value);
    } else {
        tag["value"] = std::to_string(any_cast<int>(value));
    }
    int tag_idx = find_tag(json_data, name, idx);
    if (tag_idx < 0) {
        json_data["tags"].emplace_back(tag);
    } else {
        json_data["tags"][tag_idx] = tag;
    }
if (is_verbose()) {
    std::clog << "ADD_TAG: " << tag << std::endl;
}
}

nlohmann::json get_tag(
        const nlohmann::json &json_data,
        const std::string &name,
        int idx
){
    for (const auto& it : json_data["tags"].items()) {
        if(it.value()["name"] == name){
            if((idx < 0) || (idx == it.value()["idx"])){
if (is_verbose()) {
                std::clog << "-- GET_TAG:" << name << ":" << it.value() << std::endl;
}
                return it.value();
            }
        }
    }
if (is_verbose()) {
    std::cerr << "ERROR: TTTR-TAG " << name << ":" << idx << " not found." << std::endl;
}
    nlohmann::json re = {
            {"value", -1.0},
            {"idx", -1},
            {"name", "NONE"}
    };
    return re;
}

int find_tag(
        nlohmann::json &json_data,
        const std::string &name,
        int idx
) {
    int tag_idx = -1;
    int curr_idx = 0;
    for (auto &it : json_data["tags"].items()) {
        if ((it.value()["name"] == name) && (it.value()["idx"] == idx)) {
            tag_idx = curr_idx;
            break;
        }
        curr_idx++;
    }
if (is_verbose()) {
    std::clog << "FIND_TAG: " << name << ":" << idx << ":" << tag_idx  << std::endl;
}
    return tag_idx;
}

}  // namespace io
}  // namespace tttrlib
