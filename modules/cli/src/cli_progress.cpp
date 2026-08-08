// SPDX-License-Identifier: BSD-3-Clause
//
// Progress reporting for the tttr CLI: the rendering side of the library's
// general ProgressTicker.
//
// Two audiences, two channels:
//
//   * terminal - a redrawn-in-place bar on stderr when stderr is a tty, so
//     piped/scripted runs stay clean;
//   * client    - a JSONL event stream via --progress FILE ("-" = stderr,
//     "stdout" = stdout). A GUI or watcher can poll the file / stream and
//     drive a progress widget from any of the events:
//
//       {"event":"begin","job":"correlate","phase":"window","done":0,"total":40,...}
//       {"event":"progress","job":"correlate","phase":"window","done":17,"total":40,...}
//       {"event":"finish","job":"correlate","phase":"window","done":40,"total":40,...}
//
// Every long-running subcommand (convert, correlate, image, sm, pto extract)
// reports the same three events; the fraction field is 0..1. The JSON stream
// is the only thing a client needs to parse - one JSON object per line.

#include "cli_progress.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <fstream>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace tttrlib {
namespace cli {

namespace {

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool stderr_is_tty() {
#ifdef _WIN32
    return false;
#else
    return ::isatty(2) == 1;
#endif
}

}  // namespace

Progress::Progress() = default;
Progress::~Progress() = default;

void Progress::install() {
    // One sink, decided now -- the open() call is the only place the channel
    // policy is fixed, so capture the resolved streams by value.
    std::ostream* json = json_;
    std::ostream* console = console_;
    bool show_bar = console_ != nullptr;
    long long started_ms = now_ms();
    ticker_.set_sink([json, console, show_bar, started_ms](const tttrlib::ProgressEvent& ev) {
        (void) show_bar;
        if (json) {
            std::ostringstream oss;
            const char* event =
                    ev.kind[0] == 'b' ? "begin" :
                    ev.kind[0] == 'f' ? "finish" : "progress";
            oss << "{\"event\":\"" << event
                << "\",\"job\":\"" << ev.job
                << "\",\"phase\":\"" << ev.phase
                << "\",\"done\":" << ev.done
                << ",\"total\":" << ev.total
                << ",\"fraction\":" << std::fixed << std::setprecision(4) << ev.fraction
                << ",\"seconds\":" << std::fixed << std::setprecision(3) << ev.seconds
                << "}\n";
            *json << oss.str();
            json->flush();
        }
        if (console && ev.kind[0] == 'f') {
            // clear the bar line and drop a newline so the shell prompt is
            // not glued to the last redraw
            *console << "\r\033[K\n";
            console->flush();
        } else if (console && ev.kind[0] != 'b') {
            std::ostringstream o;
            o << "\r[" << std::setw(3) << (int) (ev.fraction * 100.0) << "%] "
              << ev.job;
            if (ev.phase && *ev.phase) o << " " << ev.phase;
            o << "  " << ev.done << "/" << ev.total << "\033[K";
            *console << o.str();
            console->flush();
        } else if (console && ev.kind[0] == 'b') {
            *console << "\r[" << std::setw(3) << 0 << "%] " << ev.job
                     << (ev.phase && *ev.phase ? " " + std::string(ev.phase) : "")
                     << "\033[K";
            console->flush();
        }
    });
}

void Progress::open(const std::string& path) {
    json_ = nullptr;
    console_ = nullptr;
    if (path.empty()) {
        // default: human bar only, on tty stderr
        console_ = stderr_is_tty() ? &std::cerr : nullptr;
    } else if (path == "none") {
        // explicitly silent
    } else if (path == "-" || path == "stderr") {
        json_ = &std::cerr;
    } else if (path == "stdout") {
        json_ = &std::cout;
    } else {
        file_.open(path.c_str(), std::ios::out | std::ios::trunc);
        if (!file_) {
            std::cerr << "tttr: cannot open progress file " << path
                      << ", progress disabled" << std::endl;
        } else {
            json_ = &file_;
        }
    }
    if (json_ && json_ != &std::cerr && json_ != &std::cout) {
        std::cerr << "tttr: progress file -> " << path << std::endl;
    }
    install();
}

const char* progress_option_help() {
    return "  --progress json  write JSONL progress events for a client"
           " (\"-\" = stderr, \"stdout\" = stdout)";
}

}  // namespace cli
}  // namespace tttrlib