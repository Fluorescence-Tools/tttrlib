// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_CLI_H
#define TTTRLIB_CLI_H

// tttr - the compiled runner for time-tagged time-resolved data.
//
// One executable, many jobs: format conversion, FCS correlation, CLSM image
// export, burst search, and the PTO container (explorer subcommands plus a
// terminal UI). Everything a Python script once did from bin/tttrlib is here,
// compiled, plus the container work the scripts never had.
//
// Each cmd_*.cpp owns one subcommand and parses its own options with cxxopts,
// so a subcommand's help is its own page and the dispatch table in cli_main.cpp
// stays a switch with no shared state.

#include <string>
#include <vector>

namespace tttrlib {
namespace cli {

// -- small string helpers shared by several subcommands ---------------------

/// Split on a single delimiter, dropping empty fields.
std::vector<std::string> tttr_split(const std::string& s, char delim);

/// "0,1,2" -> signed char vector (routing channel list).
std::vector<signed char> parse_channels(const std::string& s);

/// "0,3:1,2" -> groups of channel numbers. Groups by ':', channels by ','.
std::vector<std::vector<int>> parse_channel_groups(const std::string& s);

/// Path without its extension, parent kept (matches os.path.splitext).
std::string strip_ext(const std::string& fn);

/// A file's stem (basename without extension), for derived file names.
std::string file_stem(const std::string& fn);

// ---------------------------------------------------------------------------
// subcommands - each returns a process exit code
// ---------------------------------------------------------------------------

/// tttr convert INPUT OUTPUT [-c CONTAINER] [-r RECORD]
int cmd_convert(int argc, char** argv);

/// tttr correlate FILE... --ch1 CH1 [--ch2 CH2] [options]
int cmd_correlate(int argc, char** argv);

/// tttr image export FILE [--channels GROUPS]
int cmd_image(int argc, char** argv);

/// tttr formats
int cmd_formats(int argc, char** argv);

/// tttr pto ls|info|tree|tags|cat|extract FILE ...
int cmd_pto(int argc, char** argv);

/// tttr tui FILE.pto
int cmd_tui(int argc, char** argv);

/// tttr sm|burst FILE [--config JSON] [flags] [--output JSON]
int cmd_sm(int argc, char** argv);

/// tttr detectors FILE [--add [--name NAME]]
int cmd_detectors(int argc, char** argv);

/// First-argument subcommand dispatch. Returns the process exit code.
int run(int argc, char** argv);

}  // namespace cli
}  // namespace tttrlib

#endif  // TTTRLIB_CLI_H