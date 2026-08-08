// SPDX-License-Identifier: BSD-3-Clause
//
// tttr sm FILE [--config CONFIG.json] [flags] [--output out.json]
//
// Single-molecule / burst processing from the command line. A TTTR file in,
// a burst table out: the burst search parameters can come from a JSON config
// file, from inline flags, or both (flags win). The result is a JSON document
// with the search settings, one entry per burst, and the per-burst integrals;
// --csv also writes a delimited burst table.
//
// The JSON config matches the shape the burst-search registry advertises, so a
// settings block that drives tttrlib's own Python objects drives this tool too:
//
//   {
//     "method": "sliding_window",
//     "min_photons": 20,
//     "rate_window": 10,
//     "time_separation": 0.0005,
//     "channels": [0, 1],
//     "output": "bursts.json"
//   }

#include "tttr_cli.h"
#include "cli_progress.h"
#include "detector_setup.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>

#include "cxxopts.hpp"
#include "nlohmann/json.hpp"

#include "TTTR.h"
#include "TTTRHeader.h"

namespace tttr = tttrlib;
using nlohmann::json;

namespace {

json load_config(const std::string& path, std::string* err) {
    std::ifstream ifs(path);
    if (!ifs) {
        *err = "cannot read config " + path;
        return json::object();
    }
    try {
        return json::parse(ifs);
    } catch (const json::parse_error& e) {
        *err = std::string("config parse error: ") + e.what();
        return json::object();
    }
}

}  // namespace

int tttrlib::cli::cmd_sm(int argc, char** argv) {
    cxxopts::Options opts(
            "tttr sm",
            "Single-molecule / burst processing from a TTTR file");
    opts.add_options()
        ("file", "input TTTR file", cxxopts::value<std::string>())
        ("config", "JSON config with search parameters and output paths",
         cxxopts::value<std::string>())
        ("method", "search method: sliding_window, cusum_sprt, maxtree",
         cxxopts::value<std::string>())
        ("min-photons", "minimum photons in a burst (L)", cxxopts::value<int>())
        ("rate-window", "photons used to compute the local rate (m)", cxxopts::value<int>())
        ("time-separation", "max time separation of m photons, seconds (T)",
         cxxopts::value<double>())
        ("channels", "restrict to routing channels, e.g. \"0,1\"",
         cxxopts::value<std::string>())
        ("setup", "chiSurf detector_setups.json; its channels select the detector(s)",
         cxxopts::value<std::string>())
        ("setup-name", "setup in --setup (default: file's last_used/first)",
         cxxopts::value<std::string>())
        ("detector", "detector in the setup; without it the whole setup is used",
         cxxopts::value<std::string>())
        ("output", "output JSON (default: <stem>.burst.json)", cxxopts::value<std::string>())
        ("csv", "also write a delimited burst table there", cxxopts::value<std::string>())
        ("progress", "write JSONL progress events for a client"
         " (\"-\" = stderr, \"stdout\" = stdout)",
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
            std::cerr << "error: sm needs an input file\n" << std::endl;
            std::cerr << opts.help() << std::endl;
            return 1;
        }
        std::string file = r["file"].as<std::string>();

        // defaults
        std::string method = "sliding_window";
        int L = 20;
        int m = 10;
        double T = 5e-4;
        std::string channels;
        std::string setup_path;
        std::string setup_name;
        std::string detector_name;
        std::string output;
        std::string csv;

        if (r.count("config")) {
            std::string err;
            json cfg = load_config(r["config"].as<std::string>(), &err);
            if (!err.empty()) {
                std::cerr << "error: " << err << std::endl;
                return 1;
            }
            if (cfg.contains("method")) method = cfg["method"].get<std::string>();
            if (cfg.contains("min_photons")) L = cfg["min_photons"].get<int>();
            if (cfg.contains("rate_window")) m = cfg["rate_window"].get<int>();
            if (cfg.contains("time_separation")) T = cfg["time_separation"].get<double>();
            if (cfg.contains("channels")) {
                for (auto& c : cfg["channels"]) {
                    if (!channels.empty()) channels += ",";
                    channels += std::to_string(c.get<int>());
                }
            }
            if (cfg.contains("detector_setup")) setup_path = cfg["detector_setup"].get<std::string>();
            if (cfg.contains("setup_name")) setup_name = cfg["setup_name"].get<std::string>();
            if (cfg.contains("detector")) detector_name = cfg["detector"].get<std::string>();
            if (cfg.contains("output")) output = cfg["output"].get<std::string>();
            if (cfg.contains("csv")) csv = cfg["csv"].get<std::string>();
        }

        // inline flags override the config
        if (r.count("method")) method = r["method"].as<std::string>();
        if (r.count("min-photons")) L = r["min-photons"].as<int>();
        if (r.count("rate-window")) m = r["rate-window"].as<int>();
        if (r.count("time-separation")) T = r["time-separation"].as<double>();
        if (r.count("channels")) channels = r["channels"].as<std::string>();
        if (r.count("setup")) setup_path = r["setup"].as<std::string>();
        if (r.count("setup-name")) setup_name = r["setup-name"].as<std::string>();
        if (r.count("detector")) detector_name = r["detector"].as<std::string>();
        if (r.count("output")) output = r["output"].as<std::string>();
        if (r.count("csv")) csv = r["csv"].as<std::string>();

        if (output.empty()) output = strip_ext(file) + ".burst.json";

        tttrlib::cli::Progress progress;
        progress.set_job("sm");
        progress.open(r.count("progress") ? r["progress"].as<std::string>() : "");
        progress.set_total(3);  // load, search, write
        progress.begin();

        std::cout << "Loading: " << file << std::endl;
        std::shared_ptr<TTTR> data = std::make_shared<TTTR>(file.c_str());
        progress.tick();  // load

        std::vector<signed char> used;
        if (!channels.empty()) {
            used = parse_channels(channels);
            if (used.empty()) {
                std::cerr << "error: invalid channel list" << std::endl;
                return 1;
            }
            std::cout << "Channels: ";
            for (auto c : used) std::cout << " " << (int) c;
            std::cout << std::endl;
        } else if (!setup_path.empty()) {
            std::string err;
            std::vector<int> ch = resolve_setup_channels(
                    setup_path, setup_name, detector_name, &err);
            if (!err.empty() || ch.empty()) {
                std::cerr << "error: "
                          << (err.empty() ? "no channels in detector setup" : err)
                          << std::endl;
                return 1;
            }
            used.assign(ch.begin(), ch.end());
            std::cout << "Detector setup: " << setup_path;
            if (!setup_name.empty()) std::cout << "  (" << setup_name << ")";
            if (!detector_name.empty())
                std::cout << "  detector " << detector_name;
            std::cout << std::endl;
            std::cout << "Channels: ";
            for (auto c : used) std::cout << " " << (int) c;
            std::cout << std::endl;
        }
        if (!used.empty()) {
            data = data->get_tttr_by_channel(
                    const_cast<signed char*>(used.data()), (int) used.size());
        }

        double mt_res = data->get_header()->get_macro_time_resolution();
        std::cout << "Burst search: " << method << " L=" << L
                  << " m=" << m << " T=" << T << "s" << std::endl;

        progress.set_phase("burst search");
        std::vector<long long> sel =
                data->burst_search(L, m, T, method, 0.05, 0.05);
        progress.tick();  // search

        // macro times for per-burst durations
        unsigned long long* mt = nullptr; int nmt = 0;
        data->get_macro_times(&mt, &nmt);
        auto mt_at = [&](long long idx) -> double {
            if (!mt || idx < 0 || idx >= nmt) return 0.0;
            return (double) mt[idx] * mt_res;
        };

        json out;
        out["file"] = file;
        out["n_events"] = data->get_n_valid_events();
        out["macro_time_resolution_s"] = mt_res;
        out["parameters"] = {
            {"method", method},
            {"min_photons", L},
            {"rate_window", m},
            {"time_separation", T},
            {"channels", channels.empty() ? json(nullptr) : json(channels)},
            {"detector_setup", setup_path.empty() ? json(nullptr) : json(setup_path)},
            {"detector", detector_name.empty() ? json(nullptr) : json(detector_name)},
        };

        json bursts = json::array();
        for (size_t i = 0; i + 1 < sel.size(); i += 2) {
            long long start = sel[i];
            long long stop = sel[i + 1];
            long long n = stop - start;
            double duration = mt_at(stop - 1) - mt_at(start);
            if (duration < 0) duration = 0.0;
            double rate = (duration > 0) ? (double) n / duration : 0.0;
            bursts.push_back({
                {"start", start},
                {"stop", stop},
                {"n_photons", n},
                {"duration_s", duration},
                {"rate_khz", rate / 1e3},
            });
        }
        out["n_bursts"] = (long long) bursts.size();
        out["bursts"] = bursts;

        if (mt) free(mt);

        progress.set_phase("write");
        {
            std::ofstream ofs(output);
            if (!ofs) {
                std::cerr << "error: cannot write " << output << std::endl;
                return 1;
            }
            ofs << out.dump(2) << std::endl;
        }
        progress.tick();  // write
        progress.finish();
        std::cout << "Wrote " << output << " (" << bursts.size()
                  << " bursts)" << std::endl;

        if (!csv.empty()) {
            std::ofstream ofs(csv);
            if (!ofs) {
                std::cerr << "warning: cannot write " << csv << std::endl;
            } else {
                ofs << "start\tstop\tn_photons\tduration_s\trate_khz\n";
                for (auto& b : bursts) {
                    ofs << b["start"].get<long long>() << '\t'
                        << b["stop"].get<long long>() << '\t'
                        << b["n_photons"].get<long long>() << '\t'
                        << b["duration_s"].get<double>() << '\t'
                        << b["rate_khz"].get<double>() << '\n';
                }
                std::cout << "Wrote " << csv << std::endl;
            }
        }
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