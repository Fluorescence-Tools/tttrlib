// SPDX-License-Identifier: BSD-3-Clause
//
// Shared string helpers for the tttr CLI (see tttr_cli.h).

#include "tttr_cli.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <process.h>
#else
#include <unistd.h>
#endif
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

InputPath::~InputPath() {
    if (spooled_.empty()) return;
    std::error_code ec;                 // a temp we cannot remove is not worth
    fs::remove(spooled_, ec);           // failing a finished command over
}

bool InputPath::resolve(const std::string& spec, std::string* err) {
    if (spec != "-") {
        path_ = spec;
        display_ = spec;
        return true;
    }
    display_ = "stdin";

    // Spool stdin. Binary mode matters on Windows, where the default text mode
    // turns 0x1A into end-of-file and mangles CRLF -- in a photon record stream
    // both are ordinary bytes.
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec);
    if (ec) {
        if (err) *err = "cannot find a temporary directory: " + ec.message();
        return false;
    }
    // The extension is not decoration: the reader infers the container from it
    // when the caller did not say, and a pipe carries no name. `.ptu` would be
    // a guess; the readers sniff magic bytes first and fall back to the
    // extension, so a neutral one keeps the sniff in charge.
    fs::path tmp = dir / ("tttr-stdin-" + std::to_string(
#ifdef _WIN32
        static_cast<unsigned long long>(_getpid())
#else
        static_cast<unsigned long long>(::getpid())
#endif
        ) + ".tttr");

    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out) {
            if (err) *err = "cannot write the stdin spool file " + tmp.string();
            return false;
        }
        out << std::cin.rdbuf();
        if (!out) {
            if (err) *err = "failed writing the stdin spool file " + tmp.string();
            return false;
        }
    }
    if (fs::file_size(tmp, ec) == 0 || ec) {
        fs::remove(tmp, ec);
        if (err) *err = "nothing arrived on stdin";
        return false;
    }
    spooled_ = tmp.string();
    path_ = spooled_;
    return true;
}

}  // namespace cli
}  // namespace tttrlib