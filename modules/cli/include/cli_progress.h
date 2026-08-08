// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_CLI_PROGRESS_H
#define TTTRLIB_CLI_PROGRESS_H

// Progress reporting for the tttr CLI: a renderer over the library's general
// ProgressTicker (see util/include/ProgressTicker.h). The ticker counts units
// and emits ProgressEvents; this class only decides where the events are
// drawn:
//
//   * a human at a terminal - a redrawn-in-place bar on stderr, only when
//     stderr is a tty, so piped/scripted runs stay clean;
//   * a client (GUI, watcher, test harness) - a stream of JSON objects, one
//     per line (JSONL), via --progress FILE. Every command that can take a
//     while reports the same event types so a client can drive a widget from
//     any of them:
//
//       {"event":"begin","job":"correlate","total":40}
//       {"event":"progress","job":"correlate","done":17,"total":40,
//        "fraction":0.425,"seconds":1.3}
//       {"event":"finish","job":"correlate","done":40,"total":40,"seconds":4.2}
//
// Commands report per unit of work they can count (window, object, group,
// phase); correlate reports per time window and per channel pair, image per
// channel group, pto extract per object, convert per read/write phase. The
// JSON stream is append-only, one object per line, no other bytes.

#include "ProgressTicker.h"

#include <cstddef>
#include <fstream>
#include <iosfwd>
#include <string>

namespace tttrlib {
namespace cli {

/// Progress event sink: mechanical bar to a tty, machine events to a JSON
/// stream. The counting and timing live in tttrlib::ProgressTicker.
class Progress {
public:
    Progress();
    ~Progress();
    Progress(const Progress&) = delete;
    Progress& operator=(const Progress&) = delete;

    /// Pick the channel. "" uses the human bar (stderr, tty only);
    /// "none" silences; "-" or "stderr" writes JSONL to stderr; "stdout" to
    /// stdout; anything else is a file path that gets truncated.
    void open(const std::string& path);

    /// Name of the job (subcommand) shown in events and the bar.
    void set_job(const std::string& job) { ticker_.set_job(job); }

    /// Human-readable sub-phase, shown in the bar and in events.
    void set_phase(const std::string& phase) { ticker_.set_phase(phase); }

    /// Total units of this job. A total of zero means "unknown yet";
    /// the bar shows an unbounded count until update(total) exceeds it.
    void set_total(size_t total) { ticker_.set_total(total); }

    /// Mark the whole job started. Emits the begin event.
    void begin() { ticker_.begin(); }

    /// Advance the done counter by one.
    void tick() { ticker_.tick(); }

    /// Set the done counter to an absolute value.
    void update(size_t done) { ticker_.update(done); }

    /// Finish: emit the finish event and clear the bar line.
    void finish() { ticker_.finish(); }

    bool has_json() const { return json_ != nullptr; }

  private:
    void install();

    ProgressTicker ticker_;                    /// general library ticker
    std::ostream* json_{nullptr};              /// json channel (never owned)
    std::ostream* console_{nullptr};           /// bar channel (stderr or nullptr)
    std::ofstream file_;                       /// owned file sink, if any
};

/// Short option text shared by every subcommand help page.
const char* progress_option_help();

}  // namespace cli
}  // namespace tttrlib

#endif  // TTTRLIB_CLI_PROGRESS_H