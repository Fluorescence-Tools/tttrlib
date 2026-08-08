// SPDX-License-Identifier: BSD-3-Clause
//
// Shared string helpers for the tttr CLI (see tttr_cli.h).

#include "tttr_cli.h"

#include <algorithm>
#include <filesystem>
#include <sstream>

namespace tttrlib {
namespace cli {

namespace fs = std::filesystem;

std::vector<std::string> tttr_split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::vector<signed char> parse_channels(const std::string& s) {
    std::vector<signed char> v;
    for (auto& tok : tttr_split(s, ',')) {
        v.push_back(static_cast<signed char>(std::stoi(tok)));
    }
    return v;
}

std::vector<std::vector<int>> parse_channel_groups(const std::string& s) {
    std::vector<std::vector<int>> groups;
    for (auto& g : tttr_split(s, ':')) {
        std::vector<int> grp;
        for (auto& tok : tttr_split(g, ',')) {
            grp.push_back(std::stoi(tok));
        }
        if (!grp.empty()) groups.push_back(std::move(grp));
    }
    return groups;
}

std::string strip_ext(const std::string& fn) {
    auto p = fs::path(fn);
    return (p.parent_path() / p.stem()).string();
}

std::string file_stem(const std::string& fn) {
    return fs::path(fn).stem().string();
}

}  // namespace cli
}  // namespace tttrlib