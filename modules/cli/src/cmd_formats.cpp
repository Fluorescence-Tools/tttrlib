// SPDX-License-Identifier: BSD-3-Clause
//
// tttr formats - list the TTTR containers the library can read and write.

#include "tttr_cli.h"

#include "cxxopts.hpp"

#include "TTTR.h"

namespace tttr = tttrlib;

int tttrlib::cli::cmd_formats(int argc, char** argv) {
    cxxopts::Options opts("tttr formats", "List supported TTTR container formats");
    opts.add_options()
        ("h,help", "print usage");

    try {
        opts.parse(argc, argv);
    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }
    auto names = TTTR::get_supported_container_names();
    std::cout << "Supported TTTR containers:\n";
    for (auto& n : names) {
        std::cout << "  " << n << "\n";
    }
    return 0;
}