// SPDX-License-Identifier: BSD-3-Clause
//
// tttr sim CONFIG.json [-o OUT.ptu] [--progress ...]
//
// Monte Carlo TTTR photon stream simulation subcommand.

#include "tttr_cli.h"
#include "cli_progress.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cxxopts.hpp"
#include "nlohmann/json.hpp"

#include "SimEngine.h"
#include "SimMicrotimeEncoder.h"
#include "SimRandom.h"
#include "TTTR.h"

namespace tttr = tttrlib;
using nlohmann::json;

namespace {

// Encode the run and read it back as a TTTR.
//
// The micro-time axis comes from the *engine's own settings*, not from constants
// here: a config that simulates a 3.8 ns decay on an 0.008 ns/channel axis and
// is then encoded at 0.004069 ns/channel reads back as a 1.9 ns decay, and
// nothing about the file says so. Same reason the macro-time clock is derived
// from the laser period the run actually used. (The Python `to_tttr` was fixed
// this way first; this is the same fix on the compiled path.)
std::shared_ptr<TTTR> sim_to_tttr(tttrlib::SimEngine* sim, double dt, int n_channels,
                                  const std::vector<unsigned short>& routing) {
    if (!sim) return nullptr;
    const auto& s = sim->settings();
    tttrlib::SimMicrotimeEncoder enc;
    enc.n_channels = n_channels;
    enc.tw = dt;
    enc.n_microtime_channels = s.n_microtime_channels;
    enc.microtime_resolution = s.microtime_resolution;
    enc.laser_period = s.laser_period;
    enc.pulsed_exc = s.laser_period > 0.0 ? 1 : 0;
    enc.reverse_tac = true;
    // Simulation channel i -> hardware routing channel. Identity unless the
    // caller says otherwise: which routing channel a detector sits on is
    // instrument knowledge, so it comes in from outside rather than being
    // compiled in.
    std::vector<unsigned short> ch_conv(n_channels);
    for (int i = 0; i < n_channels; ++i)
        ch_conv[i] = i < (int) routing.size() ? routing[i]
                                              : static_cast<unsigned short>(i);
    enc.ch_conversion = ch_conv;
    enc.macro_time_clock = static_cast<int>(std::round(enc.laser_period * 10.0));

    tttrlib::SimRandom rng(1);
    auto rec = sim->encode(enc, rng);
    auto file_bytes = enc.file_bytes(rec.bytes);

    std::string tmp_file = (std::filesystem::temp_directory_path() / "sim_tmp.spc").string();
    {
        std::ofstream ofs(tmp_file, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(file_bytes.data()), file_bytes.size());
    }

    auto data = std::make_shared<TTTR>(tmp_file.c_str());
    std::remove(tmp_file.c_str());
    return data;
}

}  // namespace

int tttrlib::cli::cmd_sim(int argc, char** argv) {
    cxxopts::Options opts("tttr sim", "Monte Carlo photon simulation from JSON config");
    opts.add_options()
        ("config", "input simulation JSON config", cxxopts::value<std::string>())
        ("o,output", "output TTTR file (default: sim.ptu or - for stdout)", cxxopts::value<std::string>()->default_value("sim.ptu"))
        ("dt", "time step dt for photon extraction (default: 0.01)", cxxopts::value<double>()->default_value("0.01"))
        ("channels", "number of channels (default: 2)", cxxopts::value<int>()->default_value("2"))
        ("routing-channels", "routing channel per simulation channel, e.g. \"0,8,1,9\""
         " (default: identity). Which channel a detector sits on is instrument"
         " knowledge, so it is said here rather than compiled in.",
         cxxopts::value<std::string>())
        ("progress", "write JSONL progress events (\"-\" = stderr, \"stdout\" = stdout)", cxxopts::value<std::string>())
        ("h,help", "print usage");
    opts.parse_positional({"config"});

    try {
        auto r = opts.parse(argc, argv);
        if (r.count("help") || !r.count("config")) {
            std::cout << opts.help() << std::endl;
            return r.count("help") ? 0 : 1;
        }

        std::string config_path = r["config"].as<std::string>();
        std::string output_path = r["output"].as<std::string>();
        double dt = r["dt"].as<double>();
        int n_channels = r["channels"].as<int>();

        std::vector<unsigned short> routing;
        if (r.count("routing-channels")) {
            for (const std::string& t :
                 tttr_split(r["routing-channels"].as<std::string>(), ','))
                routing.push_back(static_cast<unsigned short>(std::stoi(t)));
            if ((int) routing.size() != n_channels) {
                std::cerr << "error: --routing-channels lists " << routing.size()
                          << " channels, --channels says " << n_channels << std::endl;
                return 1;
            }
        }

        std::ifstream ifs(config_path);
        if (!ifs) {
            std::cerr << "error: cannot read config " << config_path << std::endl;
            return 1;
        }

        tttrlib::cli::Progress progress;
        progress.set_job("sim");
        progress.open(r.count("progress") ? r["progress"].as<std::string>() : "");
        progress.set_total(3); // read, run, write
        progress.begin();

        progress.set_phase("read config");
        std::string json_str((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        progress.tick();

        progress.set_phase("simulation run");
        auto sim = tttrlib::SimEngine::from_json(json_str);
        if (!sim) {
            std::cerr << "error: failed to create SimEngine from JSON" << std::endl;
            return 1;
        }
        sim->run();
        progress.tick();

        progress.set_phase("extract & write");
        auto tttr_data = sim_to_tttr(sim, dt, n_channels, routing);
        if (!tttr_data) {
            std::cerr << "error: failed to extract TTTR from simulation" << std::endl;
            return 1;
        }

        tttr_data->write(output_path.c_str());
        std::cout << "Simulated " << tttr_data->get_n_valid_events() << " events -> " << output_path << std::endl;
        progress.tick();
        progress.finish();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }
}
