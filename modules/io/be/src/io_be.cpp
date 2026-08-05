// SPDX-License-Identifier: BSD-3-Clause
#include "io_be.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <vector>

#include "FileIO.h"

namespace tttrlib {
namespace io {

namespace {

constexpr uint16_t kFiller     = 0x7FFF;
constexpr int      kIdDummy    = 123;
constexpr int      kIdLaser    = 124;
constexpr int      kIdStepA    = 125;
constexpr int      kIdStepB    = 126;
constexpr int      kIdStepC    = 127;
// The coarse counter is unwrapped at 65536, not at the 2^21 the three 7-bit
// step bytes could hold. That is the vendor's rule -- ttp.py builds
// cumulativeStep as `step + cumsum((diff(step) < 0) * 65536)`, with
// force_16bit_step defaulting to True -- and it is not a detail: unwrapping at
// 2^21 stretches this file's 55-second acquisition to 1776 seconds, while
// treating only near-full-range drops as wraps loses the wraps entirely.
// Any decrease is a wrap, and a wrap is 65536.
constexpr uint64_t kStepWrap   = 65536;

inline bool word_valid(uint16_t w) { return (w >> 15) & 1u; }
inline int  word_id(uint16_t w)    { return (w >> 8) & 0x7F; }
inline int  word_data(uint16_t w)  { return w & 0xFF; }

/*!
 * Read the whole file as little-endian uint16.
 *
 * Read as bytes and assembled explicitly rather than fread into a uint16 array:
 * the format is defined as little-endian and tttrlib runs on big-endian
 * machines too, where the lazy version silently swaps every field.
 */
std::vector<uint16_t> read_words(const std::string& filename) {
    FILE* f = open_file(filename, "rb");
    if (f == nullptr) throw std::runtime_error("cannot open " + filename);
    if (fseek64(f, 0, SEEK_END) != 0) { std::fclose(f); throw std::runtime_error("cannot seek " + filename); }
    const long long size = ftell64(f);
    std::rewind(f);
    if (size < 0) { std::fclose(f); throw std::runtime_error("cannot size " + filename); }

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    const std::size_t got = bytes.empty() ? 0 : std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    bytes.resize(got);

    std::vector<uint16_t> words(bytes.size() / 2);
    for (std::size_t i = 0; i < words.size(); ++i) {
        words[i] = static_cast<uint16_t>(bytes[2 * i] | (bytes[2 * i + 1] << 8));
    }
    return words;
}

/// The coarse counter, unwrapped. Kept in one place because both the scan and
/// the decode need exactly the same wrap arithmetic to agree.
class StepCounter {
public:
    void update(int a, int b, int c) {
        const uint32_t step = (uint32_t(c & 0x7F) << 14) |
                              (uint32_t(b & 0x7F) << 7)  |
                               uint32_t(a & 0x7F);
        if (have_previous_ && step < previous_) wraps_ += 1;
        previous_ = step;
        have_previous_ = true;
        cumulative_ = wraps_ * kStepWrap + step;
    }
    uint64_t cumulative() const { return cumulative_; }
    bool started() const { return have_previous_; }

private:
    uint32_t previous_ = 0;
    uint64_t wraps_ = 0;
    uint64_t cumulative_ = 0;
    bool have_previous_ = false;
};

}  // namespace

TtrStats scan_ttr(const std::string& filename, const TtrParams& params) {
    const std::vector<uint16_t> words = read_words(filename);
    TtrStats s;
    s.n_words = words.size();

    StepCounter step;
    uint64_t first_step = 0, last_step = 0;
    bool have_first = false;
    int a = 0, b = 0, c = 0;
    bool laser_valid_this_record = false;
    bool last_scan = false, last_line = false;

    for (const uint16_t w : words) {
        if (params.drop_filler && w == kFiller) { s.n_filler += 1; continue; }
        const int id = word_id(w);
        const bool valid = word_valid(w);

        if (id < params.n_channels) {
            if (valid) { s.n_photons += 1; s.max_channel_id = std::max(s.max_channel_id, id); }
        } else if (id == kIdLaser) {
            if (valid) laser_valid_this_record = true;
        } else if (id == kIdStepA) {
            a = word_data(w);
            if (a & 0x80) s.n_pixel += 1;      // pixel_enable is level, counted directly
        } else if (id == kIdStepB) {
            b = word_data(w);
        } else if (id == kIdStepC) {
            c = word_data(w);
            // Frame and line come from RISING edges, not from the bit being set:
            // the enable stays high for as long as the scanner asserts it, so
            // counting set bits counts clock ticks rather than lines.
            const bool scan = (b & 0x80) != 0;   // ID 126 -> new frame
            const bool line = (c & 0x80) != 0;   // ID 127 -> new line
            if (scan && !last_scan) s.n_frame += 1;
            if (line && !last_line) s.n_line += 1;
            last_scan = scan;
            last_line = line;
            s.n_records += 1;
            step.update(a, b, c);
            if (!have_first) { first_step = step.cumulative(); have_first = true; }
            last_step = step.cumulative();
            if (laser_valid_this_record) s.n_laser_valid += 1;
            laser_valid_this_record = false;
        }
        // id == kIdDummy and anything else is ignored
    }
    s.step_span = have_first ? last_step - first_step : 0;
    return s;
}

TtrData read_ttr(const std::string& filename, const TtrParams& params) {
    const std::vector<uint16_t> words = read_words(filename);
    TtrData out;
    // One event per valid detector word, plus scanner markers. Guessing high
    // costs a little memory; guessing low costs repeated reallocation of arrays
    // that reach tens of millions of entries.
    const std::size_t guess = words.size() / 4;
    out.macro_times.reserve(guess);
    out.micro_times.reserve(guess);
    out.routing_channels.reserve(guess);
    out.event_types.reserve(guess);

    StepCounter step;
    int a = 0, b = 0, c = 0;
    int laser_code = 0;
    bool have_laser = false;
    bool last_scan = false, last_line = false;

    // Detector words arrive before the step bytes that time them, so a record's
    // photons are buffered until its coarse time is known.
    std::vector<std::pair<int, int>> pending;   // (channel, tdc code)

    auto emit = [&](uint64_t macro, uint16_t micro, int8_t channel, int8_t type) {
        out.macro_times.push_back(macro);
        out.micro_times.push_back(micro);
        out.routing_channels.push_back(channel);
        out.event_types.push_back(type);
    };

    for (const uint16_t w : words) {
        if (params.drop_filler && w == kFiller) continue;
        const int id = word_id(w);
        const bool valid = word_valid(w);

        if (id < params.n_channels) {
            // The FPGA emits detector words in pairs with only one valid; the
            // other is zero-filled. Taking both doubles the photon count and
            // invents arrivals at TDC code 0.
            if (valid) pending.emplace_back(id, word_data(w));
        } else if (id == kIdLaser) {
            if (valid) { laser_code = word_data(w); have_laser = true; }
        } else if (id == kIdStepA) {
            a = word_data(w);
        } else if (id == kIdStepB) {
            b = word_data(w);
        } else if (id == kIdStepC) {
            c = word_data(w);
            step.update(a, b, c);
            const uint64_t macro = step.cumulative();

            for (const auto& p : pending) {
                // Micro time is the channel code measured against the laser
                // reference. Without a laser word there is no reference, so the
                // photon keeps its raw code rather than a difference computed
                // against a stale one.
                int micro = have_laser ? (p.second - laser_code) : p.second;
                if (micro < 0) micro += 256;          // codes wrap within a period
                if (params.tdc_ps_per_code > 0.0) {
                    micro = static_cast<int>(micro * params.tdc_ps_per_code);
                }
                emit(macro, static_cast<uint16_t>(micro), static_cast<int8_t>(p.first), 0);
            }
            pending.clear();
            have_laser = false;

            // Scanner clocks, after the photons of the record they close.
            //
            // ID 126 is scan_enable and starts a FRAME; ID 127 is line_enable
            // and starts a LINE. The names invite the opposite reading, so this
            // follows the vendor decoder (ttpCython.pyx::analysisForImg), where
            // a scan_enable edge resets line and pixel and increments frame.
            //
            // Edges, not levels: the enable stays asserted while the scanner
            // holds it, so emitting on every set bit yields one marker per clock
            // tick instead of one per line.
            const bool scan = (b & 0x80) != 0;
            const bool line = (c & 0x80) != 0;
            if (a & 0x80)             emit(macro, 0, TtrData::MARKER_PIXEL, 1);
            if (line && !last_line)   emit(macro, 0, TtrData::MARKER_LINE,  1);
            if (scan && !last_scan)   emit(macro, 0, TtrData::MARKER_FRAME, 1);
            last_scan = scan;
            last_line = line;
        }
    }
    return out;
}

}  // namespace io
}  // namespace tttrlib
