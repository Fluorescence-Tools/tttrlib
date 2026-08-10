// SPDX-License-Identifier: BSD-3-Clause
//
// tttr convert INPUT OUTPUT [-c CONTAINER] [-r RECORD]
//
// Convert one TTTR file into another container. The output container is taken
// from the output extension, or forced with -c; the record type can be
// overridden with -r (ids in TTTRHeaderTypes.h).

#include "tttr_cli.h"
#include "cli_progress.h"

#include <iostream>

#include "cxxopts.hpp"

#include "TTTR.h"
#include "TTTRHeader.h"

namespace tttr = tttrlib;

int tttrlib::cli::cmd_convert(int argc, char** argv) {
    cxxopts::Options opt("tttr convert", "Convert between TTTR file formats");
    opt.add_options()
        ("input", "input TTTR file, or - for stdin", cxxopts::value<std::string>())
        ("output", "output TTTR file", cxxopts::value<std::string>())
        ("c,container", "output container by name (default: from extension)",
         cxxopts::value<std::string>())
        ("r,record", "output record type id (see TTTRHeaderTypes.h)",
         cxxopts::value<int>()->default_value("-1"))
        ("progress", "write JSONL progress events for a client"
         " (\"-\" = stderr, \"stdout\" = stdout)",
         cxxopts::value<std::string>())
        ("h,help", "print usage");
    opt.parse_positional({"input", "output"});

    try {
        auto r = opt.parse(argc, argv);
        if (r.count("help")) {
            std::cout << opt.help() << std::endl;
            return 0;
        }
        if (!r.count("input") || !r.count("output")) {
            std::cerr << "error: convert needs INPUT and OUTPUT\n" << std::endl;
            std::cerr << opt.help() << std::endl;
            return 1;
        }
        std::string input = r["input"].as<std::string>();
        std::string output = r["output"].as<std::string>();
        std::string container = r.count("container") ? r["container"].as<std::string>() : "";
        int record = r["record"].as<int>();

        tttrlib::cli::Progress progress;
        progress.set_job("convert");
        progress.open(r.count("progress") ? r["progress"].as<std::string>() : "");
        progress.set_total(2);  // read, write
        progress.begin();

        progress.set_phase("read");
        progress.tick();

        InputPath in_path;                       // `-` is stdin; see InputPath
        {
            std::string err;
            if (!in_path.resolve(input, &err)) {
                std::cerr << "error: " << err << std::endl;
                return 1;
            }
        }
        TTTR data(in_path.path().c_str());
        if (record >= 0) {
            data.get_header()->set_tttr_record_type(record);
        }
        std::cout << "Input:  " << input << " ("
                  << data.get_n_valid_events() << " events)" << std::endl;
        std::cout << "Output: " << output;
        bool ok;
        progress.set_phase("write");
        progress.tick();
        if (!container.empty()) {
            std::cout << " [container: " << container << "]" << std::endl;
            ok = data.write(output, container.c_str());
        } else {
            std::cout << " [container from extension]" << std::endl;
            ok = data.write(output);
        }
        if (!ok) {
            progress.finish();
            std::cerr << "error: conversion failed" << std::endl;
            return 1;
        }
        progress.finish();
        return 0;
    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "error: " << e.what() << "\n" << std::endl;
        std::cerr << opt.help() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }
}