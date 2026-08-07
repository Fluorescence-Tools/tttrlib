// SPDX-License-Identifier: BSD-3-Clause
#include "io_bh_set.h"

#include <fstream>
#include <sstream>

namespace tttrlib {
namespace io {

namespace {

/*!
 * \brief Strip leading and trailing whitespace and control bytes.
 *
 * Control bytes, not just whitespace: the CR of a CRLF line is one, and the
 * identification block wraps its values in 0x04 -- `ID : \x04SPC Setup Script
 * File\x04` -- which is a field delimiter and not part of the value.
 */
std::string trim(const std::string& s) {
    auto junk = [](unsigned char c) { return c <= ' ' || c == 0x7F; };
    std::size_t a = 0, b = s.size();
    while (a < b && junk((unsigned char) s[a])) a++;
    while (b > a && junk((unsigned char) s[b - 1])) b--;
    return s.substr(a, b - a);
}

/*!
 * \brief The section this line opens, or empty.
 *
 * A section marker is `*NAME`, and it is not always at the start of its line: a
 * .set opens with a binary preamble that runs straight into `*IDENTIFICATION`
 * with no newline between them, so anchoring on column zero loses the whole
 * identification block. Anchor on the marker instead, and require what follows
 * it to be a name -- which is what keeps `#WI #1 *NO *0 [0,0]` out.
 */
std::string section_marker(const std::string& line) {
    const std::size_t star = line.rfind('*');
    if (star == std::string::npos) return {};
    const std::string name = line.substr(star + 1);
    // Three characters, not one. Hand a .spc to this parser and its records
    // contain a "*K" about every few kilobytes; three uppercase letters in a
    // row after a star is rare enough, and every real marker is longer.
    if (name.size() < 3) return {};
    for (char c : name)
        if (!((c >= 'A' && c <= 'Z') || c == '_')) return {};
    return name;
}

/*!
 * \brief Whether \p s could be a parameter name.
 *
 * The gate that keeps a binary file from parsing as a sparse .set. Anything
 * with a colon in it produces a `key : value` line, and a megabyte of records
 * has thousands -- so a name has to look like one: printable, short, and made
 * of the characters Becker & Hickl actually use.
 */
bool looks_like_a_name(const std::string& s) {
    if (s.empty() || s.size() > 40) return false;
    for (char c : s) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == ' ' ||
                        c == '-' || c == '.';
        if (!ok) return false;
    }
    return true;
}

/*!
 * \brief `#SP [SP_IMG_X,I,512]` -> name, type, value.
 *
 * Split on the first TWO commas and take everything after the second as the
 * value. Neither a name nor a type letter can contain a comma, and a value
 * can: `#SP [SP_SCF_FN,S,C:\BHdata\a,b.cfg]` is a legal line, and splitting on
 * the LAST comma instead turns its value into "b.cfg" and its type into
 * "S,C:\BHdata\a".
 *
 * \note io_bh.cpp's header-tag parser does split on the last comma. It reads
 *       five numeric keys and nothing else, so the difference cannot reach it;
 *       this parser reads every line, so it can.
 */
bool split_bracket(const std::string& body,
                   std::string* name, std::string* type, std::string* value) {
    const std::size_t first = body.find(',');
    if (first == std::string::npos) return false;
    const std::size_t second = body.find(',', first + 1);
    if (second == std::string::npos) return false;
    *name = body.substr(0, first);
    *type = body.substr(first + 1, second - first - 1);
    *value = body.substr(second + 1);
    return true;
}

}  // namespace

std::vector<BhSetParameter> parse_set(const std::string& content) {
    std::vector<BhSetParameter> out;

    // The blocks are small and the file is not; reserving the usual size of one
    // saves a handful of reallocations on every read.
    out.reserve(256);

    std::string star_section;   // the enclosing *NAME, e.g. "SETUP"
    std::string block;          // the enclosing NAME_BEGIN:, e.g. "SYS_PARA"

    std::istringstream text(content);
    std::string raw_line;
    while (std::getline(text, raw_line)) {
        const std::string line = trim(raw_line);
        if (line.empty()) continue;

        // Everything from here on is a binary blob of window geometry and
        // colours. A line parser run over it invents parameters.
        if (line.rfind("BIN_PARA_BEGIN", 0) == 0) break;

        if (line[0] != '#') {
            const std::string name = section_marker(line);
            if (!name.empty()) {
                star_section = (name == "END") ? std::string() : name;
                block.clear();
                continue;
            }
        }

        // "SYS_PARA_BEGIN:" / "SYS_PARA_END:" bracket a block inside *SETUP.
        if (line.size() > 7 && line.compare(line.size() - 7, 7, "_BEGIN:") == 0) {
            block = line.substr(0, line.size() - 7);
            continue;
        }
        if (line.size() > 5 && line.compare(line.size() - 5, 5, "_END:") == 0) {
            block.clear();
            continue;
        }

        const std::string section = block.empty() ? star_section : block;
        // A .set opens with a binary preamble, and running the line parser over
        // it would invent parameters out of whatever bytes happen to contain a
        // colon. Nothing real appears before the first `*SECTION`.
        if (section.empty()) continue;

        if (line[0] == '#') {
            // "#SP [KEY,TYPE,VALUE]", or an indexed line the trace and window
            // blocks use: "#TR #0 [...]", "#WI #1 *NO *3 [0,0]".
            const std::size_t space = line.find(' ');
            if (space == std::string::npos) continue;
            const std::string group = line.substr(1, space - 1);
            if (!looks_like_a_name(group) || group.size() > 4) continue;
            const std::string rest = trim(line.substr(space + 1));
            const std::size_t open = rest.find('[');
            if (open == std::string::npos) continue;
            const std::size_t close = rest.rfind(']');
            if (close == std::string::npos || close <= open) continue;

            BhSetParameter p;
            p.section = section;
            p.group = group;
            if (open == 0) {
                const std::string body = rest.substr(1, close - 1);
                if (!split_bracket(body, &p.name, &p.type, &p.value)) continue;
            } else {
                // Indexed rather than named. Whatever stands before the bracket
                // identifies the row; the bracket and everything after it is
                // the value, because the trace lines use ']' as a separator
                // INSIDE the value as well as to close it.
                p.name = trim(rest.substr(0, open));
                p.value = rest.substr(open);
            }
            out.push_back(p);
            continue;
        }

        // "  ID        : SPC Setup Script File" -- the identification block.
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos && colon > 0) {
            BhSetParameter p;
            p.section = section;
            p.name = trim(line.substr(0, colon));
            if (!looks_like_a_name(p.name)) continue;
            p.value = trim(line.substr(colon + 1));
            if (!p.name.empty()) out.push_back(p);
        }
    }
    return out;
}

std::vector<BhSetParameter> read_set_file(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary);
    if (!f.is_open()) return {};
    std::stringstream whole;
    whole << f.rdbuf();
    return parse_set(whole.str());
}

}  // namespace io
}  // namespace tttrlib
