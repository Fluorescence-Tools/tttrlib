// SPDX-License-Identifier: BSD-3-Clause
#include "io_fl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <set>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "FileIO.h"

namespace tttrlib {
namespace io {

namespace {

constexpr std::size_t kMagicSize   = 4;
constexpr std::size_t kLengthSize  = 4;
constexpr std::size_t kStt1Record  = 17;   // u8 + f64 + f64
constexpr std::size_t kItt1Record  = 9;    // u8 + f64

std::size_t record_size(FlimLabsFlavour flavour) {
    return flavour == FLIMLABS_STT1 ? kStt1Record
         : flavour == FLIMLABS_ITT1 ? kItt1Record
         : 0;
}

/*!
 * Read a little-endian ``float64`` out of a byte buffer.
 *
 * Byte by byte into a ``uint64_t`` and then bit-cast, rather than pointing a
 * ``double*`` at the buffer: the field sits at offset 1 of a 17-byte record, so
 * it is unaligned on every platform, and the format is little-endian on all of
 * them whatever the machine is.
 */
double read_le_double(const unsigned char* p) {
    uint64_t bits = 0;
    for (int i = 7; i >= 0; --i) bits = (bits << 8) | p[i];
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

uint32_t read_le_u32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/// The envelope: magic, header length, header. Returns FLIMLABS_NOT unless the
/// file is one of the two time taggers *and* its record area divides evenly.
struct Envelope {
    FlimLabsFlavour flavour = FLIMLABS_NOT;
    std::string json;
    long long records_begin = 0;
    std::size_t n_records = 0;
};

Envelope read_envelope(const std::string& filename, bool want_header) {
    Envelope e;
    FILE* f = open_file(filename, "rb");
    if (f == nullptr) return e;

    unsigned char head[kMagicSize + kLengthSize];
    if (std::fread(head, 1, sizeof(head), f) != sizeof(head)) { std::fclose(f); return e; }

    FlimLabsFlavour flavour = FLIMLABS_NOT;
    if (std::memcmp(head, "STT1", kMagicSize) == 0)      flavour = FLIMLABS_STT1;
    else if (std::memcmp(head, "ITT1", kMagicSize) == 0) flavour = FLIMLABS_ITT1;
    if (flavour == FLIMLABS_NOT) { std::fclose(f); return e; }

    const uint32_t header_length = read_le_u32(head + kMagicSize);

    if (fseek64(f, 0, SEEK_END) != 0) { std::fclose(f); return e; }
    const long long size = ftell64(f);
    const long long begin = static_cast<long long>(sizeof(head)) + header_length;
    if (size < begin) { std::fclose(f); return e; }

    // Four magic bytes are a weak claim on an extension as generic as ".bin".
    // A file whose record area is not a whole number of records is not one of
    // these, whatever it starts with.
    const long long payload = size - begin;
    const std::size_t rec = record_size(flavour);
    if (rec == 0 || payload % static_cast<long long>(rec) != 0) { std::fclose(f); return e; }

    e.flavour = flavour;
    e.records_begin = begin;
    e.n_records = static_cast<std::size_t>(payload / static_cast<long long>(rec));

    if (want_header && header_length > 0) {
        if (fseek64(f, static_cast<long long>(sizeof(head)), SEEK_SET) == 0) {
            std::string json(header_length, '\0');
            if (std::fread(&json[0], 1, header_length, f) == header_length) {
                e.json = std::move(json);
            }
        }
    }
    std::fclose(f);
    return e;
}

}  // namespace

FlimLabsFlavour flimlabs_flavour(const std::string& filename) {
    return read_envelope(filename, false).flavour;
}

FlimLabsData read_flimlabs(const std::string& filename) {
    const Envelope env = read_envelope(filename, true);
    if (env.flavour == FLIMLABS_NOT) {
        throw std::runtime_error(
            filename + " is not a FLIM LABS time tagger file (expected magic STT1 or ITT1 "
                       "and a whole number of records)");
    }

    FlimLabsData out;
    out.flavour = env.flavour;
    out.metadata_json = env.json;

    // --- header -----------------------------------------------------------
    std::set<int> declared;
    if (!env.json.empty()) {
        nlohmann::json j = nlohmann::json::parse(env.json, nullptr, false);
        if (!j.is_discarded() && j.is_object()) {
            if (j.contains("channels") && j["channels"].is_array()) {
                for (const auto& c : j["channels"]) {
                    if (c.is_number_integer()) {
                        out.channels.push_back(c.get<int>());
                        declared.insert(c.get<int>());
                    }
                }
            }
            if (j.contains("laser_period_ns") && j["laser_period_ns"].is_number()) {
                out.laser_period_ns = j["laser_period_ns"].get<double>();
            }
        }
    }

    // A detector index of 70, 76 or 80 would be written into the same byte as a
    // frame, line or pixel marker, and nothing in the record distinguishes
    // them. No FLIM LABS hardware has that many channels, so this has never
    // happened -- but silently tagging a detector's photons as scanner markers
    // is not a failure anyone would notice downstream.
    for (int c : {static_cast<int>(FlimLabsData::MARKER_FRAME),
                  static_cast<int>(FlimLabsData::MARKER_LINE),
                  static_cast<int>(FlimLabsData::MARKER_PIXEL)}) {
        if (declared.count(c)) {
            throw std::runtime_error(
                filename + ": the header declares detector channel " + std::to_string(c) +
                ", which is also the code for a frame, line or pixel marker. The two "
                "cannot be told apart in this format.");
        }
    }

    // --- quantisation -----------------------------------------------------
    // See fl_quantisation in io_fl.h. Decided before reading, because it is
    // what every record is converted through.
    double macro_tick_ns;
    if (env.flavour == FLIMLABS_STT1) {
        if (!(out.laser_period_ns > 0.0)) {
            throw std::runtime_error(
                filename + ": the STT1 header does not state laser_period_ns, so its "
                "floating-point times cannot be expressed as integer ticks. Choosing a "
                "tick here would rescale every time in the file without saying so.");
        }
        macro_tick_ns = out.laser_period_ns;
        out.n_micro_time_channels = 256;    // the hardware's own binning
        out.micro_time_resolution_s = out.laser_period_ns * 1e-9 / 256.0;
    } else {
        macro_tick_ns = 1e-3;               // one picosecond
        out.n_micro_time_channels = 1;
        out.micro_time_resolution_s = 0.0;
    }
    out.macro_time_resolution_s = macro_tick_ns * 1e-9;

    // --- records ----------------------------------------------------------
    const std::size_t rec = record_size(env.flavour);
    const std::size_t n = env.n_records;

    std::vector<double> macro_ns(n, 0.0);
    std::vector<double> micro_ns(n, 0.0);
    std::vector<uint8_t> event(n, 0);

    {
        FILE* f = open_file(filename, "rb");
        if (f == nullptr) throw std::runtime_error("cannot open " + filename);
        if (fseek64(f, env.records_begin, SEEK_SET) != 0) {
            std::fclose(f);
            throw std::runtime_error("cannot seek to the records of " + filename);
        }
        // Blocks rather than one record at a time: 17 bytes per fread is three
        // million system calls on a modest file.
        constexpr std::size_t kRecordsPerBlock = 65536;
        std::vector<unsigned char> block(kRecordsPerBlock * rec);
        std::size_t i = 0;
        while (i < n) {
            const std::size_t want = std::min(kRecordsPerBlock, n - i);
            const std::size_t got = std::fread(block.data(), rec, want, f);
            if (got == 0) break;
            for (std::size_t k = 0; k < got; ++k) {
                const unsigned char* p = block.data() + k * rec;
                event[i + k] = p[0];
                if (env.flavour == FLIMLABS_STT1) {
                    micro_ns[i + k] = read_le_double(p + 1);
                    macro_ns[i + k] = read_le_double(p + 9);
                } else {
                    macro_ns[i + k] = read_le_double(p + 1);
                }
            }
            i += got;
        }
        std::fclose(f);
        if (i != n) {
            macro_ns.resize(i);
            micro_ns.resize(i);
            event.resize(i);
        }
    }

    // --- order ------------------------------------------------------------
    // The records are interleaved per channel, so the file is not in time
    // order; every reader the vendor ships sorts it. Stable, so events sharing
    // a macro time keep the order the instrument wrote them in -- which is what
    // decides whether a photon falls before or after the pixel marker it sits
    // on.
    const std::size_t m = event.size();
    std::vector<uint32_t> order(m);
    std::iota(order.begin(), order.end(), 0u);
    std::stable_sort(order.begin(), order.end(),
                     [&](uint32_t a, uint32_t b) { return macro_ns[a] < macro_ns[b]; });

    // --- convert ----------------------------------------------------------
    out.macro_times.resize(m);
    out.micro_times.resize(m);
    out.routing_channels.resize(m);
    out.event_types.resize(m);

    const double micro_bin_ns = env.flavour == FLIMLABS_STT1
                                    ? out.laser_period_ns / 256.0
                                    : 0.0;
    const uint16_t max_micro = static_cast<uint16_t>(out.n_micro_time_channels - 1);

    for (std::size_t k = 0; k < m; ++k) {
        const uint32_t src = order[k];
        const int code = event[src];

        // Floor, not round. If the file's macro times are already pulse counts
        // the two agree; if they are absolute arrival times, only the floor
        // leaves the remainder for the micro time to carry, and rounding would
        // put a late photon on the next pulse.
        //
        // The nudge below is what keeps an exact multiple from falling to the
        // pulse beneath it. A count of k pulses reaches the file as the nearest
        // double to k*period, and dividing that back can land just under k --
        // by up to an ulp, which grows with k. So the tolerance grows with it
        // too: a fixed epsilon stops working after a few million pulses, which
        // at 40 MHz is a tenth of a second of acquisition.
        const double pulses = macro_ns[src] < 0.0 ? 0.0 : macro_ns[src] / macro_tick_ns;
        double index = std::floor(pulses);
        const double tolerance = std::max(1e-9, pulses * 1e-12);
        if (index + 1.0 - pulses <= tolerance) index += 1.0;
        out.macro_times[k] = static_cast<uint64_t>(index < 0.0 ? 0.0 : index);
        out.macro_time_residual_ns = std::max(
            out.macro_time_residual_ns, std::fabs(macro_ns[src] - index * macro_tick_ns));

        if (code == FlimLabsData::MARKER_FRAME || code == FlimLabsData::MARKER_LINE ||
            code == FlimLabsData::MARKER_PIXEL) {
            out.routing_channels[k] = static_cast<int8_t>(code);
            out.event_types[k] = 1;
            out.micro_times[k] = 0;
            continue;
        }

        if (code > 127) {
            throw std::runtime_error(
                filename + ": detector channel " + std::to_string(code) +
                " does not fit a routing channel, which is a signed byte");
        }
        if (!declared.empty() && !declared.count(code)) out.n_undeclared_channel += 1;

        out.routing_channels[k] = static_cast<int8_t>(code);
        out.event_types[k] = 0;
        if (micro_bin_ns > 0.0) {
            const double bins = std::floor(micro_ns[src] / micro_bin_ns);
            // Saturate rather than wrap: a micro time past the end of the laser
            // period means the period in the header is wrong, and a photon
            // aliased back to zero would hide that in the middle of the decay.
            const double clamped = bins < 0.0 ? 0.0
                                 : bins > static_cast<double>(max_micro)
                                       ? static_cast<double>(max_micro)
                                       : bins;
            out.micro_times[k] = static_cast<uint16_t>(clamped);
        } else {
            out.micro_times[k] = 0;
        }
    }

    return out;
}

}  // namespace io
}  // namespace tttrlib
