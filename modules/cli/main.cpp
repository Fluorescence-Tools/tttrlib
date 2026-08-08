// SPDX-License-Identifier: BSD-3-Clause
//
// The tttr executable's entry point. Everything else lives in the module: this
// file only calls into tttrlib::cli::run, which is where the dispatch lives.

#include "tttr_cli.h"

int main(int argc, char** argv) {
    return tttrlib::cli::run(argc, argv);
}