// SPDX-License-Identifier: BSD-3-Clause
//
// tttr correlate FILE... --ch1 CH1 [--ch2 CH2] [--n-casc N] [--n-bins N]
//     [-c N] [-f] [-j]
//
// Multi-tau FCS correlation over time windows, written as .cor files with the
// same four columns the old bin/tttrlib script produced: time-ms, mean,
// sureness, stderr.

#include "tttr_cli.h"
#include "cli_progress.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>

#include "cxxopts.hpp"

#include "TTTR.h"
#include "TTTRHeader.h"
#include "Correlator.h"

namespace tttr = tttrlib;

namespace {

struct CorrResult {
    std::vector<double> x_axis;               // [n_pts], taken from the first window
    std::vector<std::vector<double>> curves;  // [n_win][n_pts] normalized
};

// Correlate one channel pair over the time windows. Windows where either
// channel selection is empty are skipped: Correlator::dt() reads the stream
// endpoints and crashes on an empty side.
CorrResult correlate_pair(
        TTTR& data,
        const std::vector<std::pair<int, int>>& start_stop,
        const std::vector<signed char>& ch1,
        const std::vector<signed char>& ch2,
        int n_casc, int n_bins, bool fine,
        tttrlib::cli::Progress* progress = nullptr
) {
    Correlator correlator(nullptr, "wahl", n_bins, n_casc, fine);
    CorrResult r;
    for (auto& [start, stop] : start_stop) {
        if (progress) progress->tick();
        if (stop <= start) continue;
        std::vector<int> sel(stop - start);
        std::iota(sel.begin(), sel.end(), start);
        auto slice = data.get_tttr_by_selection(sel.data(), (int) sel.size());
        auto tttr_ch1 = slice->get_tttr_by_channel(
                const_cast<signed char*>(ch1.data()), (int) ch1.size());
        auto tttr_ch2 = slice->get_tttr_by_channel(
                const_cast<signed char*>(ch2.data()), (int) ch2.size());
        if (!tttr_ch1 || tttr_ch1->get_n_valid_events() == 0 ||
            !tttr_ch2 || tttr_ch2->get_n_valid_events() == 0) {
            continue;
        }
        correlator.set_tttr(tttr_ch1, tttr_ch2, fine);

        double* x = nullptr; int nx = 0;
        double* c = nullptr; int nc = 0;
        correlator.get_x_axis(&x, &nx);
        correlator.get_corr_normalized(&c, &nc);
        int n = std::min(nx, nc);
        if (r.x_axis.empty() && n > 0) {
            r.x_axis.assign(x, x + n);
        }
        if (n > 0) {
            r.curves.emplace_back(c, c + n);
        }
        if (x) free(x);
        if (c) free(c);
    }
    return r;
}

void save_correlation(
        const CorrResult& res,
        double duration_sec,
        double avg_cr_khz,
        int n_chunks,
        const std::string& filename,
        const std::string& label
) {
    if (res.curves.empty()) return;
    int n_pts = (int) res.x_axis.size();
    int n_win = (int) res.curves.size();

    std::vector<double> avg(n_pts, 0.0), std_(n_pts, 0.0);
    for (int w = 0; w < n_win; ++w) {
        for (int i = 0; i < n_pts; ++i) avg[i] += res.curves[w][i];
    }
    for (int i = 0; i < n_pts; ++i) avg[i] /= n_win;
    for (int w = 0; w < n_win; ++w) {
        for (int i = 0; i < n_pts; ++i) {
            double d = res.curves[w][i] - avg[i];
            std_[i] += d * d;
        }
    }
    for (int i = 0; i < n_pts; ++i) std_[i] = std::sqrt(std_[i] / n_win);

    std::string out = tttrlib::cli::strip_ext(filename) + label + ".cor";
    std::ofstream ofs(out);
    if (!ofs) {
        std::cerr << "warning: cannot write " << out << std::endl;
        return;
    }
    ofs << std::setprecision(10);
    for (int i = 0; i < n_pts; ++i) {
        double time_axis = res.x_axis[i] * 1000.0;  // ms
        double suren0 = (i == 0) ? duration_sec : 0.0;
        double suren1 = (i == 1) ? avg_cr_khz : 0.0;
        ofs << time_axis << '\t'
            << avg[i] << '\t'
            << suren0 << '\t'
            << (std_[i] / std::sqrt((double) n_chunks)) << '\n';
    }
    std::cout << "  wrote " << out << std::endl;
}

}  // namespace

int tttrlib::cli::cmd_correlate(int argc, char** argv) {
    cxxopts::Options opts(
            "tttr correlate",
            "Compute fluorescence correlation (FCS) over time windows");
    opts.add_options()
        ("input", "input TTTR file(s)", cxxopts::value<std::vector<std::string>>())
        ("ch1", "routing channels for correlation channel 1, e.g. \"0,1\"",
         cxxopts::value<std::string>())
        ("ch2", "routing channels for correlation channel 2 (default: ch1)",
         cxxopts::value<std::string>())
        ("n-casc", "multi-tau: number of cascades", cxxopts::value<int>()->default_value("25"))
        ("n-bins", "multi-tau: bins per cascade", cxxopts::value<int>()->default_value("9"))
        ("c,n-chunks", "number of time windows to split into", cxxopts::value<int>()->default_value("5"))
        ("f,fine", "fine correlation (use micro time)", cxxopts::value<bool>()->default_value("false"))
        ("j,join", "lexically join multiple input files into one stream", cxxopts::value<bool>()->default_value("false"))
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
            std::cerr << "error: correlate needs at least one input file\n"
                      << std::endl;
            std::cerr << opts.help() << std::endl;
            return 1;
        }
        auto inputs = r["input"].as<std::vector<std::string>>();
        std::string ch1_str = r["ch1"].as<std::string>();
        std::string ch2_str = r.count("ch2") ? r["ch2"].as<std::string>() : "";
        int n_casc = r["n-casc"].as<int>();
        int n_bins = r["n-bins"].as<int>();
        int n_chunks = r["n-chunks"].as<int>();
        bool fine = r["fine"].as<bool>();
        bool join = r["join"].as<bool>();

        tttrlib::cli::Progress progress;
        progress.set_job("correlate");
        progress.open(r.count("progress") ? r["progress"].as<std::string>() : "");

        auto ch1 = tttr::cli::parse_channels(ch1_str);
        auto ch2 = ch2_str.empty() ? ch1 : tttr::cli::parse_channels(ch2_str);
        if (ch1.empty() || ch2.empty()) {
            std::cerr << "error: invalid channel list" << std::endl;
            return 1;
        }
        std::cout << "Correlation channel 1:";
        for (auto c : ch1) std::cout << " " << (int) c;
        std::cout << "\nCorrelation channel 2:";
        for (auto c : ch2) std::cout << " " << (int) c;
        std::cout << std::endl;

        std::vector<std::pair<std::string, std::shared_ptr<TTTR>>> datas;

        // Keeps a spooled stdin alive for as long as the TTTR reads it.
        std::vector<std::shared_ptr<InputPath>> held;
        for (auto& fn : inputs) {
            std::cout << "Loading: " << fn << std::endl;
            // One of the inputs may be `-`; a second would consume an
            // already-exhausted stdin, so the first claims it.
            auto slot = std::make_shared<InputPath>();
            std::string err;
            if (!slot->resolve(fn, &err)) {
                std::cerr << "error: " << err << std::endl;
                return 1;
            }
            held.push_back(slot);
            datas.emplace_back(fn, std::make_shared<TTTR>(slot->path().c_str()));
        }
        if (datas.empty()) {
            std::cerr << "error: no input files" << std::endl;
            return 1;
        }

        if (join && datas.size() > 1) {
            std::cout << "Joining data..." << std::endl;
            auto joined = datas[0].second;
            for (size_t i = 1; i < datas.size(); ++i) {
                TTTR sum = *joined + datas[i].second.get();
                joined = std::make_shared<TTTR>(std::move(sum));
            }
            datas.clear();
            datas.emplace_back(inputs.front(), joined);
        }

        std::string ch2_label = ch2_str.empty() ? ch1_str : ch2_str;
        auto mk_label = [](const std::string& a, const std::string& b) {
            return "(" + a + "-" + b + ")";
        };

        for (auto& [fn, data] : datas) {
            auto* header = data->get_header();
            double mt_cal = header->get_macro_time_resolution();

            unsigned long long* mt = nullptr; int nmt = 0;
            data->get_macro_times(&mt, &nmt);
            double duration_sec = 0.0;
            if (nmt > 0) {
                duration_sec = (double) (mt[nmt - 1] - (nmt > 1 ? mt[0] : 0)) * mt_cal;
            }
            if (mt) free(mt);
            double window_length = duration_sec / n_chunks;
            std::cout << "-- Macro time calibration [s]: " << mt_cal << std::endl;
            std::cout << "-- Duration [s]: " << duration_sec << std::endl;
            std::cout << "-- Time window length [s]: " << window_length << std::endl;

            int* tw = nullptr; int ntw = 0;
            data->get_ranges_by_time_window(&tw, &ntw, window_length, -1, -1, -1, mt_cal, false);
            std::vector<std::pair<int, int>> start_stop;
            for (int i = 0; i + 1 < ntw; i += 2) {
                start_stop.emplace_back(tw[i], tw[i + 1]);
            }
            if (tw) free(tw);

            int* sel1 = nullptr; int nsel1 = 0;
            data->get_selection_by_channel(&sel1, &nsel1, ch1.data(), (int) ch1.size());
            long n_ph1 = nsel1;
            if (sel1) free(sel1);
            int* sel2 = nullptr; int nsel2 = 0;
            data->get_selection_by_channel(&sel2, &nsel2, ch2.data(), (int) ch2.size());
            long n_ph2 = nsel2;
            if (sel2) free(sel2);

            double cr1 = (duration_sec > 0) ? n_ph1 / duration_sec / 1000.0 : 0.0;
            double cr2 = (duration_sec > 0) ? n_ph2 / duration_sec / 1000.0 : 0.0;
            double avg_cr = (cr1 + cr2) / 2.0;

            std::cout << "-- Correlation(Ch1,Ch2)" << std::endl;
            progress.set_total((size_t) start_stop.size() * 4);
            progress.begin();
            progress.set_phase("(Ch1,Ch2)");
            auto r12 = correlate_pair(*data, start_stop, ch1, ch2, n_casc, n_bins, fine, &progress);
            std::cout << "-- Correlation(Ch2,Ch1)" << std::endl;
            progress.set_phase("(Ch2,Ch1)");
            auto r21 = correlate_pair(*data, start_stop, ch2, ch1, n_casc, n_bins, fine, &progress);
            std::cout << "-- Correlation(Ch1,Ch1)" << std::endl;
            progress.set_phase("(Ch1,Ch1)");
            auto r11 = correlate_pair(*data, start_stop, ch1, ch1, n_casc, n_bins, fine, &progress);
            std::cout << "-- Correlation(Ch2,Ch2)" << std::endl;
            progress.set_phase("(Ch2,Ch2)");
            auto r22 = correlate_pair(*data, start_stop, ch2, ch2, n_casc, n_bins, fine, &progress);

            progress.finish();

            save_correlation(r12, duration_sec, avg_cr, n_chunks, fn, mk_label(ch1_str, ch2_label));
            save_correlation(r21, duration_sec, avg_cr, n_chunks, fn, mk_label(ch2_label, ch1_str));
            save_correlation(r11, duration_sec, avg_cr, n_chunks, fn, mk_label(ch1_str, ch1_str));
            save_correlation(r22, duration_sec, avg_cr, n_chunks, fn, mk_label(ch2_label, ch2_label));
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