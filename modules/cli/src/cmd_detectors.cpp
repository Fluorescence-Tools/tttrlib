// SPDX-License-Identifier: BSD-3-Clause
//
// tttr detectors FILE [--add [--name NAME]]
//
// Inspect and author chiSurf-compatible detector_setups.json files. Without
// --add the setups are listed; with --add a prompt-driven wizard appends (or
// replaces) one setup and rewrites the file. No detector knowledge is compiled
// in - the file is the definition, shared verbatim with chiSurf.

#include "tttr_cli.h"
#include "detector_setup.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "cxxopts.hpp"

namespace tttr = tttrlib;
using tttr::cli::DetectorDef;
using tttr::cli::DetectorSetup;
using tttr::cli::DetectorSetups;

namespace {

std::string ask(const char* prompt) {
    std::cout << prompt << " ";
    std::cout.flush();
    std::string line;
    if (!std::getline(std::cin, line)) return "";
    return line;
}

/// Comma/space separated decimal ints. Returns -1 for "empty", -2 for garbage.
int parse_channel_list(const std::string& s, std::vector<int>* out) {
    out->clear();
    std::string cur;
    for (char c : s) {
        if (c == ',' || c == ' ') {
            if (!cur.empty()) {
                if (!std::isdigit((unsigned char) cur[0])) return -2;
                out->push_back(std::stoi(cur));
                cur.clear();
            }
            continue;
        }
        cur += c;
    }
    if (!cur.empty()) {
        if (!std::isdigit((unsigned char) cur[0])) return -2;
        out->push_back(std::stoi(cur));
    }
    return out->empty() ? -1 : 0;
}

/// "0-1023,2048-4095" -> gates. Throws on garbage.
void parse_gates(const std::string& s, std::vector<std::pair<int, int>>* out) {
    out->clear();
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            if (cur.empty()) continue;
            size_t dash = cur.find('-');
            out->emplace_back(std::stoi(cur.substr(0, dash)),
                              std::stoi(cur.substr(dash + 1)));
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (!cur.empty()) {
        size_t dash = cur.find('-');
        out->emplace_back(std::stoi(cur.substr(0, dash)),
                          std::stoi(cur.substr(dash + 1)));
    }
}

}  // namespace

int tttrlib::cli::cmd_detectors(int argc, char** argv) {
    cxxopts::Options opts(
            "tttr detectors",
            "Inspect or author chiSurf-compatible detector_setups.json");
    opts.add_options()
        ("file", "detector_setups.json file", cxxopts::value<std::string>())
        ("add", "interactive wizard: append/replace one setup",
         cxxopts::value<bool>()->default_value("false"))
        ("name", "setup name for --add (defaults to a prompt)",
         cxxopts::value<std::string>())
        ("h,help", "print usage");
    opts.parse_positional({"file"});

    try {
        auto r = opts.parse(argc, argv);
        if (r.count("help")) {
            std::cout << opts.help() << std::endl;
            return 0;
        }
        if (!r.count("file")) {
            std::cerr << "error: detectors needs a detector_setups.json file"
                      << "\n" << std::endl;
            std::cerr << opts.help() << std::endl;
            return 1;
        }
        std::string path = r["file"].as<std::string>();
        bool add = r["add"].as<bool>();

        DetectorSetups setups;
        std::string err;
        bool loaded = false;
        {
            std::ifstream probe(path);
            loaded = probe.good();
        }
        if (loaded) loaded = load_detector_setups(path, &setups, &err);

        if (!add) {
            if (!loaded) {
                std::cerr << "error: " << (err.empty()
                        ? "cannot read " + path : err) << std::endl;
                return 1;
            }
            for (auto& s : setups.setups) {
                bool sel = (s.name == setups.last_used);
                std::cout << (sel ? "* " : "  ") << s.name << "\n";
                for (auto& d : s.detectors) {
                    std::cout << "    " << d.name << "  chs [";
                    for (size_t i = 0; i < d.channels.size(); ++i) {
                        if (i) std::cout << ", ";
                        std::cout << d.channels[i];
                    }
                    std::cout << "]";
                    for (auto& g : d.micro_time_ranges)
                        std::cout << "  gate " << g.first << "-" << g.second;
                    std::cout << "\n";
                }
                for (const auto& win : s.windows)
                    std::cout << "    window " << win.name << " "
                              << win.lo << "-" << win.hi << "\n";
            }
            if (setups.setups.empty())
                std::cout << "(no setups here)" << std::endl;
            return 0;
        }

        // ---- wizard ----
        std::string name = r.count("name") ? r["name"].as<std::string>()
                                           : ask("setup name:");
        if (name.empty()) {
            std::cerr << "error: setup name required" << std::endl;
            return 1;
        }
        DetectorSetup s;
        s.name = name;
        for (;;) {
            std::string dn = ask("detector name  (blank quits)");
            if (dn.empty()) break;
            std::vector<int> chs;
            int rc = parse_channel_list(
                    ask("  routing channels, e.g. 0,1"), &chs);
            if (rc < 0) {
                std::cerr << "  skip: need at least one integer channel"
                          << std::endl;
                continue;
            }
            DetectorDef d;
            d.name = dn;
            d.channels = chs;
            try {
                parse_gates(ask(
                        "  micro-time gates lo-hi, blank = none"), &d.micro_time_ranges);
            } catch (const std::exception&) {
                std::cerr << "  skip: bad gates (want lo-hi,lo-hi)"
                          << std::endl;
                continue;
            }
            s.detectors.push_back(std::move(d));
        }
        if (s.detectors.empty()) {
            std::cerr << "error: a setup needs at least one detector"
                      << std::endl;
            return 1;
        }
        for (;;) {
            std::string wn = ask("window name  (blank = no windows)");
            if (wn.empty()) break;
            std::string wr = ask("  range, e.g. 0,1023");
            try {
                std::vector<std::pair<int, int>> g;
                parse_gates(wr, &g);
                if (g.empty()) {
                    std::cerr << "  skip: bad range" << std::endl;
                    continue;
                }
                s.set_window(wn, g.front().first, g.front().second);
            } catch (const std::exception&) {
                std::cerr << "  skip: bad range" << std::endl;
                continue;
            }
        }

        // replace a same-named setup, keep the rest, select as last_used
        auto& ss = setups.setups;
        ss.erase(std::remove_if(ss.begin(), ss.end(),
                                [&](const DetectorSetup& x) {
                                    return x.name == s.name;
                                }),
                 ss.end());
        ss.push_back(std::move(s));
        setups.last_used = name;
        if (!save_detector_setups(path, setups, &err)) {
            std::cerr << "error: " << err << std::endl;
            return 1;
        }
        std::cout << "Wrote " << setups.setups.size() << " setup(s), last used: "
                  << name << " -> " << path << std::endl;
        return 0;
    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "error: " << e.what() << "\n" << std::endl;
        std::cerr << opts.help() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }
}