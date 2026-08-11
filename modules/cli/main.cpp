// SPDX-License-Identifier: BSD-3-Clause
//
// The tttr executable's entry point. Everything else lives in the module: this
// file only calls into tttrlib::cli::run, which is where the dispatch lives.

#include "tttr_cli.h"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    // Some subcommands catch their own failures and some do not, and the
    // library throws for an input it cannot identify. Without this, such a
    // path leaves main by exception and the process aborts through
    // std::terminate -- a `libc++abi: terminating` dump where the user should
    // see what was wrong with their file.
    try {
        return tttrlib::cli::run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "tttr: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "tttr: unknown error" << std::endl;
        return 1;
    }
}