// SPDX-License-Identifier: BSD-3-Clause
//
// tttr image export FILE [--channels GROUPS]
//
// Export CLSM intensity images as TIFF. Channels make groups with ':' and
// channels within a group with ',' -- "0,3:1,2" writes two TIFFs, one per
// group. Default group: all used routing channels combined.

#include "tttr_cli.h"
#include "cli_progress.h"
#include "detector_setup.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>

#include "cxxopts.hpp"

#include "TTTR.h"
#include "CLSMImage.h"
#include "TiffArrayIO.h"

namespace tttr = tttrlib;

namespace {

std::string channels_joined(const std::vector<int>& chs) {
    std::ostringstream oss;
    for (size_t i = 0; i < chs.size(); ++i) {
        if (i) oss << ',';
        oss << chs[i];
    }
    return oss.str();
}

}  // namespace

int tttrlib::cli::cmd_image(int argc, char** argv) {
    // tttr image export FILE
    // The first argument after "image" is the image subcommand.
    if (argc < 2) {
        std::cerr << "error: image needs a subcommand: export\n" << std::endl;
        return 1;
    }
    std::string sub = argv[1];
    if (sub == "-h" || sub == "--help") {
        std::cout <<
            "tttr image - image processing\n"
            "\n"
            "subcommands:\n"
            "  export    export intensity images as TIFF\n"
            "\n"
            "run 'tttr image export --help' for options\n";
        return 0;
    }
    if (sub != "export") {
        std::cerr << "error: unknown image subcommand '" << sub
                  << "' (only 'export' exists)" << std::endl;
        return 1;
    }
    argc -= 1;  // drop "export": argv[0] becomes the program name for cxxopts
    argv += 1;

    cxxopts::Options opts("tttr image export",
                          "Export intensity image(s) from a CLSM TTTR file");
    opts.add_options()
        ("input", "input CLSM TTTR file", cxxopts::value<std::string>())
        ("channels", "channel groups, e.g. \"0,3:1,2\"", cxxopts::value<std::string>())
        ("setup", "chiSurf detector_setups.json; its detectors select the image groups",
         cxxopts::value<std::string>())
        ("setup-name", "setup in --setup (default: file's last_used/first)",
         cxxopts::value<std::string>())
        ("detector", "single detector group to export; without it each detector is a group",
         cxxopts::value<std::string>())
        ("progress", "write JSONL progress events for a client"
         " (\"-\" = stderr, \"stdout\" = stdout)",
         cxxopts::value<std::string>())
        ("h,help", "print usage");
    opts.parse_positional({"input"});

    try {
        auto r = opts.parse(argc, argv);
        if (r.count("help")) {
            std::cout << opts.help() << std::endl;
            return 0;
        }
        if (!r.count("input")) {
            std::cerr << "error: image export needs an input file\n" << std::endl;
            std::cerr << opts.help() << std::endl;
            return 1;
        }
        std::string input = r["input"].as<std::string>();
        std::string channels_str = r.count("channels") ? r["channels"].as<std::string>() : "";
        std::string setup_path = r.count("setup") ? r["setup"].as<std::string>() : "";
        std::string setup_name = r.count("setup-name") ? r["setup-name"].as<std::string>() : "";
        std::string detector_name = r.count("detector") ? r["detector"].as<std::string>() : "";

        auto data = std::make_shared<TTTR>(input.c_str());

        std::string channels;
        std::vector<std::vector<int>> groups;
        if (!channels_str.empty()) {
            channels = channels_str;
            groups = tttr::cli::parse_channel_groups(channels);
        } else if (!setup_path.empty()) {
            tttr::cli::DetectorSetups setups;
            std::string err;
            if (!tttr::cli::load_detector_setups(setup_path, &setups, &err)) {
                std::cerr << "error: " << err << std::endl;
                return 1;
            }
            const tttr::cli::DetectorSetup* s = setups.default_setup();
            if (!setup_name.empty()) s = setups.find(setup_name);
            if (!s) {
                std::cerr << "error: no detector setup '" << setup_name
                          << "' in " << setup_path << std::endl;
                return 1;
            }
            if (!detector_name.empty()) {
                const tttr::cli::DetectorDef* d = s->find_detector(detector_name);
                if (!d) {
                    std::cerr << "error: no detector '" << detector_name
                              << "' in setup '" << s->name << "'" << std::endl;
                    return 1;
                }
                groups.push_back(d->channels);
            } else {
                for (auto& d : s->detectors) groups.push_back(d.channels);
            }
            if (groups.empty()) {
                std::cerr << "error: setup '" << s->name << "' has no detectors"
                          << std::endl;
                return 1;
            }
            for (size_t i = 0; i < groups.size(); ++i) {
                if (i) channels += ':';
                channels += channels_joined(groups[i]);
            }
            std::cout << "Detector setup: " << setup_path;
            if (!setup_name.empty()) std::cout << "  (" << setup_name << ")";
            if (!detector_name.empty())
                std::cout << "  detector " << detector_name;
            std::cout << std::endl;
        } else {
            signed char* urc = nullptr; int nurc = 0;
            data->get_used_routing_channels(&urc, &nurc);
            std::vector<int> sorted(urc, urc + nurc);
            std::sort(sorted.begin(), sorted.end());
            if (urc) free(urc);
            channels = channels_joined(sorted);
            groups = tttr::cli::parse_channel_groups(channels);
        }

        std::cout << "Input:  " << input << std::endl;
        std::cout << "Export: " << channels << std::endl;

        CLSMImage clsm(data, CLSMSettings(), nullptr, false);
        tttrlib::cli::Progress progress;
        progress.set_job("image");
        progress.open(r.count("progress") ? r["progress"].as<std::string>() : "");
        progress.set_total(groups.size());
        progress.begin();
        for (auto& chs : groups) {
            std::string fn_chs = channels_joined(chs);
            std::string fn_out = tttr::cli::strip_ext(input) + "_ch(" + fn_chs + ").tif";
            std::cout << "Output: " << fn_out << std::endl;

            clsm.fill(data, chs, true);
            progress.tick();
            unsigned short* img = nullptr; int d1 = 0, d2 = 0, d3 = 0;
            clsm.get_intensity(&img, &d1, &d2, &d3);
            if (img && d1 > 0 && d2 > 0 && d3 > 0) {
                tttr::write_tiff<unsigned short>(fn_out, img, d1, d2, d3);
            } else {
                std::cerr << "warning: empty image for channels " << fn_chs << std::endl;
            }
            if (img) free(img);
        }
        progress.finish();
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