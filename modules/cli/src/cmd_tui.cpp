// SPDX-License-Identifier: BSD-3-Clause
//
// tttr tui FILE.pto - a terminal UI over the container's table of contents.
//
// Everything here runs on the same read-only PtoFile handle as `tttr pto ls`,
// so a container of any size opens instantly; only the payload you point at is
// read. Keys: up/down to move, enter to toggle an object's tags, 'e' to extract
// the selection to disk, 'E' to extract everything, 'q' to quit.

#include "tttr_cli.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#    include <io.h>
#    define isatty _isatty
#else
#    include <unistd.h>
#endif

#include "cxxopts.hpp"
#include "ptolib/pto_tui.hpp"

#include "io_pto.h"

namespace tttr = tttrlib;

namespace {

using tttr::io::PtoFile;
using tttr::io::PtoObject;

std::string tag_value(const tttr::io::PtoTag& t) {
    switch (t.type) {
        case tttr::io::PtoType::UInt:
        case tttr::io::PtoType::UID:
            return std::to_string(t.u);
        case tttr::io::PtoType::Int:
            return std::to_string(t.i);
        case tttr::io::PtoType::Date:
            return "date";
        case tttr::io::PtoType::Float:
            return std::to_string(t.d);
        case tttr::io::PtoType::Text:
            return t.text;
        case tttr::io::PtoType::Bytes:
            return "<" + std::to_string(t.bytes.size()) + " bytes>";
        case tttr::io::PtoType::UIDs: {
            std::string s;
            for (size_t k = 0; k < t.uids.size(); ++k)
                s += (k ? "," : "") + std::to_string(t.uids[k]);
            return "[" + s + "]";
        }
        default:
            return "";
    }
}

}  // namespace

int tttrlib::cli::cmd_tui(int argc, char** argv) {
    cxxopts::Options opts("tttr tui", "Terminal UI over a PTO container");
    opts.add_options()
        ("file", "container file", cxxopts::value<std::string>())
        ("h,help", "print usage");
    opts.parse_positional({"file"});

    cxxopts::ParseResult r;
    try {
        r = opts.parse(argc, argv);
    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }
    if (r.count("help") || !r.count("file")) {
        std::cout << opts.help() << std::endl;
        return r.count("help") ? 0 : 1;
    }
    std::string filename = r["file"].as<std::string>();

    // Raw mode must never be entered when stdin is not a terminal: cpptui and
    // this little app both assume one, and the escape sequence (1049h) that
    // starts the alternate screen would corrupt a piped transcript.
    if (!isatty(0)) {
        std::cerr << "error: tui needs an interactive terminal" << std::endl;
        return 1;
    }

    try {
        auto file = std::make_shared<PtoFile>();
        if (!file->open(filename, false)) {
            std::cerr << "error: cannot open " << filename << ": "
                      << file->error() << std::endl;
            return 1;
        }

        std::vector<PtoObject> objs = file->objects();
        int sel = 0;
        bool show_tags = false;
        std::string status = "reading";
        std::string note;

        pto_tui::App app;
        app.title = "tttr tui  |  " + filename;

        app.run(
            [&]() {
                int w = 0, h = 0;
                pto_tui::Terminal::get_size(w, h);
                std::cout << "\033[2J\033[1;1H";
                std::cout << "\033[1m" << file->title() << "\033[0m"
                          << "  (" << objs.size() << " objects, generation "
                          << file->generation() << ")\n";
                std::cout << "up/down select, enter toggle tags, e extract, "
                             "E extract all, q quit\n";
                int body = h - 3;
                int first = std::max(0, sel - body + 3);
                for (int i = first; i < (int) objs.size() && i < first + body; ++i) {
                    auto& o = objs[i];
                    bool sel_line = (i == sel);
                    if (sel_line) std::cout << "\033[7m";
                    std::cout << (sel_line ? "> " : "  ")
                              << "uid=" << o.uid
                              << "  " << o.kind
                              << "/" << o.encoding
                              << "  " << o.size << "B";
                    if (!o.name.empty()) std::cout << "  " << o.name;
                    if (sel_line) std::cout << "\033[0m";
                    std::cout << "\n";
                    if (show_tags && i == sel) {
                        for (auto& t : file->tags_for(o.uid)) {
                            std::cout << "    tag " << t.name << " = " << tag_value(t) << "\n";
                        }
                    }
                }
                if (!note.empty()) {
                    std::cout << "\033[33m" << note << "\033[0m\n";
                } else {
                    std::cout << "\n" << status << "\n";
                }
            },
            [&](pto_tui::Key k) {
                switch (k) {
                    case pto_tui::Key::Up:
                    case pto_tui::Key::CtrlUp:
                        sel = std::max(0, sel - 1);
                        break;
                    case pto_tui::Key::Down:
                    case pto_tui::Key::CtrlDown:
                        sel = std::min((int) objs.size() - 1, sel + 1);
                        break;
                    case pto_tui::Key::Enter:
                        show_tags = !show_tags;
                        break;
                    case pto_tui::Key::Extract: {
                        if (sel >= 0 && sel < (int) objs.size()) {
                            auto& o = objs[sel];
                            std::string out = o.name.empty()
                                ? "obj_" + std::to_string(o.uid)
                                : o.name;
                            if (file->extract(o.uid, out)) {
                                note = "extracted -> " + out;
                            } else {
                                note = "extract failed: " + file->error();
                            }
                        }
                        break;
                    }
                    case pto_tui::Key::ExtractAll: {
                        std::string dir = "extracted";
                        try {
                            auto paths = file->disassemble(dir);
                            note = "extracted " + std::to_string(paths.size()) +
                                   " objects -> " + dir + "/";
                        } catch (const std::exception& e) {
                            note = std::string("extract all failed: ") + e.what();
                        }
                        break;
                    }
                    case pto_tui::Key::Quit:
                    case pto_tui::Key::Escape:
                        app.running = false;
                        break;
                    default:
                        break;
                }
                return true;
            }
        );
        std::cout << "\033[0m";
        if (!objs.empty() && sel < (int) objs.size()) {
            std::cout << "opened " << filename << " (" << objs.size()
                      << " objects)" << std::endl;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }
}