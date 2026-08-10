// SPDX-License-Identifier: BSD-3-Clause
//
// tttr dispatch: the first argument names the subcommand, everything else is
// handed to it. `image` is the one nested case (image export). No shared state
// between subcommands exists by design.

#include "tttr_cli.h"

#include <iostream>

namespace tttr = tttrlib;

namespace {

void print_usage() {
    std::cout <<
        "tttr - runner for time-tagged time-resolved data\n"
        "\n"
        "usage:\n"
        "  tttr <subcommand> [options]\n"
        "\n"
        "subcommands:\n"
        "  convert     convert a TTTR file into another container\n"
        "  correlate   fluorescence correlation spectroscopy (FCS)\n"
        "  image       image processing (export)\n"
        "  formats     list supported TTTR containers\n"
        "  sm          single-molecule / burst processing (alias: burst)\n"
        "  sim         photon-event simulator (alias: simulate)\n"
        "  detectors   inspect or author detector_setups.json (chiSurf)\n"
        "  pto         the PTO container: pack, add, ls, info, tree, tags, cat, extract\n"
        "  tui         terminal UI over a PTO container (or a TTTR file)\n"
        "\n"
        "run 'tttr <subcommand> --help' for a subcommand's options\n";
}

}  // namespace

int tttrlib::cli::run(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    std::string sub = argv[1];
    if (sub == "-h" || sub == "--help" || sub == "help") {
        print_usage();
        return 0;
    }
    argc -= 1;
    argv += 1;

    if (sub == "convert") return cmd_convert(argc, argv);
    if (sub == "correlate") return cmd_correlate(argc, argv);
    if (sub == "image") return cmd_image(argc, argv);
    if (sub == "formats") return cmd_formats(argc, argv);
    if (sub == "pto") return cmd_pto(argc, argv);
    if (sub == "tui") return cmd_tui(argc, argv);
    if (sub == "sm" || sub == "burst") return cmd_sm(argc, argv);
    if (sub == "sim" || sub == "simulate") return cmd_sim(argc, argv);
    if (sub == "detectors") return cmd_detectors(argc, argv);

    std::cerr << "error: unknown subcommand '" << sub << "'\n" << std::endl;
    print_usage();
    return 1;
}