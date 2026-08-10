// SPDX-License-Identifier: BSD-3-Clause
//
// tttr pto ls|info|tree|tags|cat|extract FILE [OBJECT] [DIR]
//
// The PTO container explorer. Everything here opens the file read-only and
// costs one PtoFile::open -- the table of contents of a .pto lives in the
// index, which is a few kilobytes no matter how big the payloads are.

#include "tttr_cli.h"
#include "cli_progress.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

#include "cxxopts.hpp"

#include "io_pto.h"

namespace tttr = tttrlib;

namespace {

using tttr::io::PtoFile;
using tttr::io::PtoObject;
using tttr::io::PtoTag;

std::string human_bytes(std::uint64_t n) {
    const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double v = (double) n;
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    std::ostringstream oss;
    if (u == 0) {
        oss << n << " B";
    } else {
        oss << std::fixed << std::setprecision(1) << v << " " << units[u];
    }
    return oss.str();
}

void print_object(const PtoObject& o) {
    std::cout << "uid=" << o.uid
              << "  kind=" << o.kind
              << "  encoding=" << o.encoding
              << "  size=" << human_bytes(o.size);
    if (o.rows > 0) std::cout << "  rows=" << o.rows;
    if (!o.name.empty()) std::cout << "  name=" << o.name;
    if (!o.media_type.empty()) std::cout << "  mime=" << o.media_type;
    std::cout << "\n";
}

int usage(const std::string& msg, const std::string& help) {
    std::cerr << "error: " << msg << "\n" << help << std::endl;
    return 1;
}

const char* const kHelp =
        "tttr pto - explore a PTO container\n\n"
        "  tttr pto ls FILE          list objects\n"
        "  tttr pto info FILE        file metadata, tags, annotations\n"
        "  tttr pto tree FILE        objects with their tags and notes\n"
        "  tttr pto tags FILE        every tag, one per line\n"
        "  tttr pto cat FILE NAME    print an object's payload\n"
        "  tttr pto extract FILE [OBJECT] DIR   extract one or all objects\n"
        "  tttr pto pack -o OUT.pto PATH...     bundle files into a new container\n"
        "  tttr pto add FILE PATH...            bundle files into an existing one\n";

/*!
 * \brief `pack` and `add`: the write side, a directory of files in one call.
 *
 * Its own parse, because everything else here takes at most three positionals
 * and this takes a list of them.
 */
int bundle(int argc, char** argv, bool create) {
    const std::string verb = create ? "pack" : "add";
    cxxopts::Options opts("tttr pto " + verb,
                          create ? "bundle files into a new PTO container"
                                 : "bundle files into an existing PTO container");
    opts.add_options()
        ("o,output", "container to write (pack only)", cxxopts::value<std::string>())
        ("title", "the container's title (pack only)", cxxopts::value<std::string>())
        ("no-sidecars", "bundle a .set as a plain object, unlinked from its .spc")
        ("inputs", "files and directories to bundle",
         cxxopts::value<std::vector<std::string>>())
        ("h,help", "print usage");
    opts.parse_positional({"inputs"});

    cxxopts::ParseResult r;
    try {
        r = opts.parse(argc, argv);
    } catch (const cxxopts::exceptions::exception& e) {
        return usage(e.what(), opts.help());
    }
    if (r.count("help")) {
        std::cout << opts.help() << std::endl;
        return 0;
    }

    std::vector<std::string> inputs =
            r.count("inputs") ? r["inputs"].as<std::vector<std::string>>()
                              : std::vector<std::string>();
    // `add` takes the container first and the files after it; `pack` names the
    // container with -o, because every positional is an input.
    std::string container;
    if (create) {
        if (!r.count("output")) return usage("pack needs -o OUT.pto", opts.help());
        container = r["output"].as<std::string>();
    } else {
        if (inputs.empty()) return usage("add needs a container file", opts.help());
        container = inputs.front();
        inputs.erase(inputs.begin());
    }
    if (inputs.empty()) return usage("nothing to bundle", opts.help());

    PtoFile file;
    const bool opened = create ? file.create(container, r.count("title")
                                                               ? r["title"].as<std::string>()
                                                               : std::string())
                               : file.open(container, true);
    if (!opened) {
        std::cerr << "error: cannot open " << container << ": " << file.error() << std::endl;
        return 1;
    }
    // MuxingApp is already "tttrlib", the library that laid the bytes down.
    // WritingApp is who asked for them, which for a container built here is
    // this command -- and is the only record of that afterwards.
    if (create) file.set_writing_app("tttr pto pack");

    const std::vector<PtoObject> made =
            tttr::io::pto_bundle_files(file, inputs, r.count("no-sidecars") == 0);
    // Bundling stops at the first path it cannot read, and says so there. What
    // was written before that is still in the file, and still uncommitted.
    if (!file.error().empty()) {
        std::cerr << "error: " << file.error() << std::endl;
        return 1;
    }
    if (!file.commit()) {
        std::cerr << "error: cannot commit " << container << ": " << file.error() << std::endl;
        return 1;
    }
    for (const auto& o : made) print_object(o);
    std::cout << made.size() << " object" << (made.size() == 1 ? "" : "s") << " in "
              << container << std::endl;
    return 0;
}

}  // namespace

int tttrlib::cli::cmd_pto(int argc, char** argv) {
    if (argc < 2) {
        std::cout << kHelp;
        return argc == 2 ? 0 : 1;
    }
    std::string sub = argv[1];
    if (sub == "-h" || sub == "--help") {
        std::cout << kHelp;
        return 0;
    }
    argc -= 1;  // drop the subcommand: argv[0] becomes program name for cxxopts
    argv += 1;

    if (sub == "pack" || sub == "add") {
        try {
            return bundle(argc, argv, sub == "pack");
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << std::endl;
            return 1;
        }
    }

    cxxopts::Options opts("tttr pto " + sub, "PTO container explorer");
    opts.add_options()
        ("file", "container file", cxxopts::value<std::string>())
        ("object", "object name or uid", cxxopts::value<std::string>())
        ("dir", "output directory", cxxopts::value<std::string>())
        ("progress", "write JSONL progress events for a client"
         " (\"-\" = stderr, \"stdout\" = stdout)",
         cxxopts::value<std::string>())
        ("h,help", "print usage");
    opts.parse_positional({"file", "object", "dir"});

    cxxopts::ParseResult r;
    try {
        r = opts.parse(argc, argv);
    } catch (const cxxopts::exceptions::exception& e) {
        return usage(e.what(), opts.help());
    }
    if (r.count("help")) {
        std::cout << opts.help() << std::endl;
        return 0;
    }
    if (!r.count("file")) {
        return usage("a container file is required", opts.help());
    }

    std::vector<std::string> files;
    if (r.count("file")) {
        files.push_back(r["file"].as<std::string>());
    }

    std::string want = r.count("object") ? r["object"].as<std::string>() : "";
    std::string dir = r.count("dir") ? r["dir"].as<std::string>() : "";

    try {
        if (sub == "ls" || sub == "tree") {
            for (auto& f : files) {
                PtoFile file;
                if (!file.open(f, false)) {
                    std::cerr << "error: cannot open " << f << ": " << file.error() << std::endl;
                    return 1;
                }
                std::cout << f << "\n";
                for (auto& o : file.objects()) {
                    print_object(o);
                    if (sub == "tree") {
                        for (auto& t : file.tags_for(o.uid)) {
                            std::cout << "    tag " << t.name;
                            switch (t.type) {
                                case tttr::io::PtoType::UInt:
                                case tttr::io::PtoType::UID:
                                    std::cout << " = " << t.u;
                                    break;
                                case tttr::io::PtoType::Int:
                                    std::cout << " = " << t.i;
                                    break;
                                case tttr::io::PtoType::Float:
                                    std::cout << " = " << t.d;
                                    break;
                                case tttr::io::PtoType::Date:
                                    std::cout << " = date(" << t.i << ")";
                                    break;
                                case tttr::io::PtoType::Text:
                                    std::cout << " = " << t.text;
                                    break;
                                case tttr::io::PtoType::Bytes:
                                    std::cout << " = <" << t.bytes.size() << " bytes>";
                                    break;
                                case tttr::io::PtoType::UIDs:
                                    std::cout << " = [";
                                    for (size_t k = 0; k < t.uids.size(); ++k)
                                        std::cout << (k ? "," : "") << t.uids[k];
                                    std::cout << "]";
                                    break;
                                default:
                                    break;
                            }
                            if (!t.text.empty() && t.type != tttr::io::PtoType::Text)
                                std::cout << " text=" << t.text;
                            std::cout << "\n";
                        }
                    }
                }
            }
            return 0;
        }

        if (sub == "info") {
            for (auto& f : files) {
                PtoFile file;
                if (!file.open(f, false)) {
                    std::cerr << "error: cannot open " << f << ": " << file.error() << std::endl;
                    return 1;
                }
                std::cout << "file:       " << f << "\n";
                std::cout << "title:      " << file.title() << "\n";
                std::cout << "writing app: " << file.writing_app() << "\n";
                std::cout << "generation: " << file.generation() << "\n";
                auto uuid = file.uuid();
                std::cout << "uuid:       ";
                for (auto b : uuid) std::cout << std::hex << std::setw(2) << std::setfill('0')
                                              << (int) b;
                std::cout << std::dec << "\n";
                std::cout << "objects:    " << file.n_objects() << "\n";
                std::cout << "\nfile tags:\n";
                for (auto& t : file.tags_for(0)) {
                    std::cout << "  " << t.name << "\n";
                }
                auto ann = file.annotations();
                if (!ann.empty()) {
                    std::cout << "\nannotations:\n";
                    for (auto& a : ann) {
                        std::cout << "  [" << a.target << "] " << a.text << "\n";
                    }
                }
            }
            return 0;
        }

        if (sub == "tags") {
            for (auto& f : files) {
                PtoFile file;
                if (!file.open(f, false)) {
                    std::cerr << "error: cannot open " << f << ": " << file.error() << std::endl;
                    return 1;
                }
                for (auto& t : file.tags()) {
                    std::cout << "target=" << t.target << "  " << t.name;
                    switch (t.type) {
                        case tttr::io::PtoType::UInt:
                        case tttr::io::PtoType::UID:
                            std::cout << "=" << t.u;
                            break;
                        case tttr::io::PtoType::Int:
                            std::cout << "=" << t.i;
                            break;
                        case tttr::io::PtoType::Float:
                            std::cout << "=" << t.d;
                            break;
                        case tttr::io::PtoType::Text:
                            std::cout << "=" << t.text;
                            break;
                        default:
                            break;
                    }
                    std::cout << "\n";
                }
            }
            return 0;
        }

        if (sub == "cat") {
            if (want.empty()) {
                return usage("cat needs an object name or uid", opts.help());
            }
            for (auto& f : files) {
                PtoFile file;
                if (!file.open(f, false)) {
                    std::cerr << "error: cannot open " << f << ": " << file.error() << std::endl;
                    return 1;
                }
                std::uint64_t uid = 0;
                try {
                    uid = (std::uint64_t) std::stoull(want);
                } catch (...) {
                    uid = file.find(want);
                }
                if (!uid || !file.has(uid)) {
                    std::cerr << "error: no object '" << want << "' in " << f << std::endl;
                    return 1;
                }
                auto obj = file.object(uid);
                auto bytes = file.read(uid);
                std::cout.write((const char*) bytes.data(), (std::streamsize) bytes.size());
                if (!bytes.empty() && bytes.back() != '\n') std::cout << "\n";
                std::cerr << "[object " << uid << " " << obj.kind << "/" << obj.encoding
                          << "  " << bytes.size() << " bytes]" << std::endl;
            }
            return 0;
        }

        if (sub == "extract" || sub == "extract-all") {
            tttrlib::cli::Progress progress;
            progress.set_job("pto extract");
            progress.open(r.count("progress") ? r["progress"].as<std::string>() : "");
            for (auto& f : files) {
                PtoFile file;
                if (!file.open(f, false)) {
                    std::cerr << "error: cannot open " << f << ": " << file.error() << std::endl;
                    return 1;
                }
                // extract FILE [OBJECT] [DIR]. The positional mapping below
                // cannot tell "OBJECT" from "DIR", so resolve the ambiguity:
                // an OBJECT is a uid or a name the file knows, anything else
                // is the output directory.
                bool single = false;
                std::string obj = want;
                if (!want.empty() && sub != "extract-all") {
                    std::uint64_t uid = 0;
                    try {
                        uid = (std::uint64_t) std::stoull(want);
                    } catch (...) {
                        uid = file.find(want);
                    }
                    single = uid != 0 && file.has(uid);
                }
                if (!dir.empty()) {
                    single = true;  // FILE OBJECT DIR always means one object
                } else if (!want.empty() && !single) {
                    dir = want;     // FILE DIR: extract everything down there
                    obj.clear();
                }
                if (single && sub != "extract-all") {
                    // one object out
                    std::uint64_t uid = 0;
                    try {
                        uid = (std::uint64_t) std::stoull(obj);
                    } catch (...) {
                        uid = file.find(obj);
                    }
                    if (!uid || !file.has(uid)) {
                        std::cerr << "error: no object '" << obj << "' in " << f << std::endl;
                        return 1;
                    }
                    progress.set_total(1);
                    progress.begin();
                    std::string out = dir.empty() ? obj : dir;
                    std::error_code ec;
                    auto parent = std::filesystem::u8path(out).parent_path();
                    if (!parent.empty()) {
                        std::filesystem::create_directories(parent, ec);
                    }
                    if (file.extract(uid, out)) {
                        progress.tick();
                        progress.finish();
                        std::cout << "extracted " << uid << " -> " << out << std::endl;
                    } else {
                        std::cerr << "error: extraction failed: " << file.error() << std::endl;
                        return 1;
                    }
                } else {
                    // disassemble everything, replicating PtoFile::disassemble's
                    // naming so the progress count matches the object list.
                    std::vector<std::string> used;
                    std::vector<PtoObject> objs = file.objects();
                    const std::string sep = dir.empty() ? "" : "/";
                    progress.set_total(objs.size());
                    progress.begin();
                    for (auto& o : objs) {
                        std::string name = o.name;
                        if (name.empty()) name = std::to_string(o.uid);
                        if (std::find(used.begin(), used.end(), name) != used.end())
                            name = std::to_string(o.uid) + "-" + name;
                        used.push_back(name);
                        std::string path = dir + sep + name;
                        std::error_code ec;
                        auto parent = std::filesystem::u8path(path).parent_path();
                        if (!parent.empty()) {
                            std::filesystem::create_directories(parent, ec);
                        }
                        if (!file.extract(o.uid, path)) {
                            std::cerr << "error: extraction failed for uid " << o.uid
                                      << ": " << file.error() << std::endl;
                            return 1;
                        }
                        progress.tick();
                        std::cout << path << "\n";
                    }
                    progress.finish();
                    if (dir.empty()) {
                        std::cerr << "warning: no directory given, wrote beside " << f << "\n";
                    }
                }
            }
            return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }
    return usage("unknown pto subcommand '" + sub + "'", opts.help());
}