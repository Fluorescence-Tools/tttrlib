// SPDX-License-Identifier: BSD-3-Clause
//
// Persisting a decoded H2MM state assignment.
//
// A decoder (Viterbi, the marginal γ draw, or FFBS) produces one state per
// photon.  That assignment has to outlive the process, and there are two ways
// to keep it:
//
//   Path A — rewrite each photon's routing channel so the (stream, state) pair
//            it belongs to has its own id, and write one PTU holding every
//            photon.  Self-describing: per-state decays, FCS and burst
//            analyses become ordinary channel selections, with no new plumbing
//            anywhere downstream.  Bounded by the container's channel field.
//
//   Path B — leave the file untouched and carry the assignment beside it in a
//            msgpack sidecar.  No id budget, nothing altered, but only
//            consumers that accept a mask see the states.
//
// Both are built on the same per-photon state array, and both must agree
// photon for photon — that equivalence is the contract the tests hold them to.

#include "H2MM.h"
#include "TTTRMask.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>

namespace tttrlib {

// ---------------------------------------------------------------------------
// Channel map (Path A)
// ---------------------------------------------------------------------------

std::string H2mmChannelMap::to_json() const {
    nlohmann::json j;
    j["n_streams"] = n_streams;
    j["n_states"] = n_states;
    j["max_channel"] = max_channel;
    j["channels"] = channels;
    j["used_channels"] = used_channels;
    j["compressed_channels"] = compressed_channels;
    return j.dump();
}

H2mmChannelMap H2mmChannelMap::from_json(const std::string& payload) {
    nlohmann::json j = nlohmann::json::parse(payload);
    H2mmChannelMap m;
    m.n_streams = j.value("n_streams", 0);
    m.n_states = j.value("n_states", 0);
    m.max_channel = j.value("max_channel", H2MM_PTU_MAX_CHANNEL);
    m.channels = j.value("channels", std::vector<int>{});
    m.used_channels = j.value("used_channels", std::vector<int>{});
    m.compressed_channels = j.value("compressed_channels", std::vector<int>{});
    return m;
}

H2mmChannelMap H2MM::build_channel_map(
    std::shared_ptr<TTTR> src, int n_states, int max_channel
) const {
    if (!src) throw std::invalid_argument("build_channel_map: null TTTR");
    if (n_states < 1) throw std::invalid_argument("build_channel_map: n_states < 1");
    if (n_streams_ < 1)
        throw std::invalid_argument("build_channel_map: no streams (load bursts first)");

    src->find_used_routing_channels();
    std::set<int> used;
    {
        signed char* chans = nullptr; int n_chans = 0;
        src->get_used_routing_channels(&chans, &n_chans);
        for (int i = 0; i < n_chans; ++i) used.insert(static_cast<int>(chans[i]));
        free(chans);
    }

    // Compress the source ids first.  A file's channels are usually sparse
    // (1, 12, 30 for three detectors is ordinary) and those gaps are dead weight
    // in a field a few bits wide: leaving them alone would keep a photon on id
    // 30 and need 5 bits to store a file with 7 distinct channels.  So the used
    // ids collapse to 0..k-1 in ascending order and the states are allocated
    // immediately after -- everything the split writes ends up in one dense run
    // starting at 0.
    const size_t n_used = used.size();
    const size_t need = static_cast<size_t>(n_streams_) * n_states;
    const size_t total = n_used + need;
    if (total > static_cast<size_t>(max_channel) + 1) {
        // Loud, with the numbers, and pointing at the path that has no budget:
        // silently truncating ids would misattribute photons to a state that
        // looks perfectly plausible downstream.
        std::string msg =
            "H2MM::build_channel_map: " + std::to_string(n_used) +
            " source channels + " + std::to_string(n_streams_) + " streams x " +
            std::to_string(n_states) + " states needs " + std::to_string(total) +
            " routing-channel ids, but only " + std::to_string(max_channel + 1) +
            " (0.." + std::to_string(max_channel) + ") are available. The target "
            "container's record field is the limit -- PTU HydraHarp T2/T3 store 6 "
            "bits, so ids above " + std::to_string(H2MM_PTU_MAX_CHANNEL) +
            " cannot be written at all, and narrower containers (PicoHarp / "
            "SPC-130: 4 bits, SPC-600/256: 3 bits) truncate silently. "
            "Use the state sidecar (H2MM::state_sidecar), which has no id budget.";
        throw std::runtime_error(msg);
    }

    H2mmChannelMap m;
    m.n_streams = n_streams_;
    m.n_states = n_states;
    m.max_channel = max_channel;
    m.used_channels.assign(used.begin(), used.end());   // std::set -> ascending
    m.compressed_channels.resize(n_used);
    for (size_t i = 0; i < n_used; ++i)
        m.compressed_channels[i] = static_cast<int>(i);
    m.channels.resize(need);
    int next = static_cast<int>(n_used);
    for (int s = 0; s < n_streams_; ++s)          // stream major
        for (int st = 0; st < n_states; ++st)     // state minor
            m.channels[static_cast<size_t>(s) * n_states + st] = next++;
    return m;
}

// ---------------------------------------------------------------------------
// Spreading a CSR-ordered decode over the source photon range
// ---------------------------------------------------------------------------

void H2MM::photon_states(
    const long long* path, int n_path,
    unsigned char** states_out, int* n_states_out
) const {
    if (photon_index_.empty())
        throw std::runtime_error(
            "H2MM::photon_states: no source photon index -- load the bursts with "
            "set_bursts_from_tttr / set_bursts_from_filter, not set_bursts");
    if (static_cast<long long>(n_path) != get_n_photons())
        throw std::invalid_argument(
            "H2MM::photon_states: path length " + std::to_string(n_path) +
            " != photon count " + std::to_string(get_n_photons()));

    const size_t n_src = static_cast<size_t>(n_source_photons_);
    auto* out = static_cast<unsigned char*>(malloc(std::max<size_t>(n_src, 1)));
    if (!out) throw std::bad_alloc();
    std::fill(out, out + n_src, H2MM_UNASSIGNED);
    for (int i = 0; i < n_path; ++i) {
        const long long st = path[i];
        if (st < 0 || st >= H2MM_UNASSIGNED) continue;
        out[photon_index_[i]] = static_cast<unsigned char>(st);
    }
    *states_out = out;
    *n_states_out = static_cast<int>(n_src);
}

void H2MM::photon_stream_index(unsigned char** streams_out, int* n_streams_out) const {
    if (photon_index_.empty())
        throw std::runtime_error(
            "H2MM::photon_stream_index: no source photon index -- load the bursts "
            "with set_bursts_from_tttr / set_bursts_from_filter, not set_bursts");
    const size_t n_src = static_cast<size_t>(n_source_photons_);
    auto* out = static_cast<unsigned char*>(malloc(std::max<size_t>(n_src, 1)));
    if (!out) throw std::bad_alloc();
    std::fill(out, out + n_src, H2MM_UNASSIGNED);
    for (size_t i = 0; i < photon_index_.size(); ++i)
        out[photon_index_[i]] = static_cast<unsigned char>(streams_[i]);
    *streams_out = out;
    *n_streams_out = static_cast<int>(n_src);
}

// ---------------------------------------------------------------------------
// Path A — state-encoded routing channels
// ---------------------------------------------------------------------------

std::shared_ptr<TTTR> H2MM::split_routing_channels(
    std::shared_ptr<TTTR> src,
    const long long* path, int n_path,
    const H2mmChannelMap& map
) const {
    if (!src) throw std::invalid_argument("split_routing_channels: null TTTR");
    if (photon_index_.empty())
        throw std::runtime_error(
            "H2MM::split_routing_channels: no source photon index -- load the "
            "bursts with set_bursts_from_tttr / set_bursts_from_filter");
    if (static_cast<long long>(src->size()) != n_source_photons_)
        throw std::invalid_argument(
            "H2MM::split_routing_channels: TTTR has " + std::to_string(src->size()) +
            " photons but the bursts were loaded from a file with " +
            std::to_string(n_source_photons_));
    if (static_cast<long long>(n_path) != get_n_photons())
        throw std::invalid_argument(
            "H2MM::split_routing_channels: path length " + std::to_string(n_path) +
            " != photon count " + std::to_string(get_n_photons()));
    if (map.n_streams != n_streams_)
        throw std::invalid_argument(
            "H2MM::split_routing_channels: channel map has " +
            std::to_string(map.n_streams) + " streams, engine has " +
            std::to_string(n_streams_));

    // Compression table over the whole signed-char domain, so the per-photon
    // remap below is a lookup rather than a search.
    std::vector<int> compress(256, -1);
    for (size_t i = 0; i < map.used_channels.size(); ++i) {
        if (i >= map.compressed_channels.size()) break;
        const auto slot = static_cast<unsigned char>(
            static_cast<signed char>(map.used_channels[i]));
        compress[slot] = map.compressed_channels[i];
    }

    auto out = std::make_shared<TTTR>(*src);
    const int64_t n_src = static_cast<int64_t>(src->size());

    // Every photon moves: assigned ones onto their (stream, state) id, the rest
    // onto the compressed form of the channel they were already on.  Doing the
    // unassigned photons first means the assigned ones simply overwrite.
    for (int64_t i = 0; i < n_src; ++i) {
        const signed char orig = src->get_routing_channel_at(static_cast<size_t>(i));
        const int ch = compress[static_cast<unsigned char>(orig)];
        if (ch < 0)
            throw std::runtime_error(
                "H2MM::split_routing_channels: photon " + std::to_string(i) +
                " is on routing channel " + std::to_string(static_cast<int>(orig)) +
                ", which the channel map does not know -- the map was built from a "
                "different file");
        out->set_routing_channel_at(static_cast<size_t>(i),
                                    static_cast<signed char>(ch));
    }
    for (int i = 0; i < n_path; ++i) {
        const long long st = path[i];
        if (st < 0 || st >= map.n_states) continue;
        const int ch = map.channel_for(streams_[i], static_cast<int>(st));
        if (ch < 0) continue;
        out->set_routing_channel_at(static_cast<size_t>(photon_index_[i]),
                                    static_cast<signed char>(ch));
    }
    out->find_used_routing_channels();
    return out;
}

// ---------------------------------------------------------------------------
// Path B — the msgpack state sidecar
// ---------------------------------------------------------------------------

H2mmStateSidecar H2MM::state_sidecar(
    const long long* path, int n_path,
    const H2mmModel& model,
    const std::string& decoder,
    long long seed,
    const H2mmChannelMap* map
) const {
    H2mmStateSidecar sc;
    unsigned char* st = nullptr; int n_st = 0;
    unsigned char* sr = nullptr; int n_sr = 0;
    photon_states(path, n_path, &st, &n_st);
    sc.states.assign(st, st + n_st);
    free(st);
    photon_stream_index(&sr, &n_sr);
    sc.streams.assign(sr, sr + n_sr);
    free(sr);

    sc.n_states = model.n_states();
    sc.n_streams = n_streams_;
    sc.decoder = decoder;
    sc.seed = seed;
    sc.model = model;
    if (map) { sc.channel_map = *map; sc.has_channel_map = true; }
    return sc;
}

long long H2mmStateSidecar::count_state(int state) const {
    if (state < 0 || state >= 255) return 0;
    const auto want = static_cast<uint8_t>(state);
    long long c = 0;
    for (uint8_t v : states) if (v == want) ++c;
    return c;
}

std::shared_ptr<TTTRMask> H2mmStateSidecar::mask_for_state(int state) const {
    auto m = std::make_shared<TTTRMask>();
    const size_t n = states.size();
    // TTTRMask bits mark *excluded* events, so everything outside the state is
    // masked and get_indices(true) hands back exactly the state's photons.
    std::vector<unsigned char> bytes(n, 1);
    if (state >= 0 && state < 255) {
        const auto want = static_cast<uint8_t>(state);
        for (size_t i = 0; i < n; ++i) if (states[i] == want) bytes[i] = 0;
    }
    m->set_mask(bytes.data(), static_cast<int>(n));
    return m;
}

namespace {

/// Pack a byte vector as a msgpack `bin` field rather than an array of
/// integers: one entry per photon means the difference between ~1 byte and
/// ~3 characters each.
nlohmann::json to_binary(const std::vector<uint8_t>& v) {
    return nlohmann::json::binary(std::vector<uint8_t>(v.begin(), v.end()));
}

std::vector<uint8_t> from_binary(const nlohmann::json& j) {
    if (j.is_binary()) {
        const auto& b = j.get_binary();
        return std::vector<uint8_t>(b.begin(), b.end());
    }
    // Tolerate a plain array so a hand-written or JSON-transcoded payload loads.
    return j.get<std::vector<uint8_t>>();
}

}  // namespace

void H2mmStateSidecar::write(const std::string& filename) const {
    nlohmann::json j;
    j["format"] = "tttrlib.h2mm.states";
    j["version"] = 1;
    j["n_states"] = n_states;
    j["n_streams"] = n_streams;
    j["decoder"] = decoder;
    j["seed"] = seed;
    j["unassigned"] = static_cast<int>(H2MM_UNASSIGNED);
    j["n_photons"] = static_cast<long long>(states.size());
    j["states"] = to_binary(states);
    j["streams"] = to_binary(streams);
    j["model"] = {
        {"prior", model.prior},
        {"trans", model.trans},
        {"obs", model.obs},
        {"loglik", model.loglik},
        {"n_iter", model.n_iter},
        {"n_phot", model.n_phot},
        {"converged", model.converged},
    };
    if (has_channel_map) {
        j["channel_map"] = {
            {"n_streams", channel_map.n_streams},
            {"n_states", channel_map.n_states},
            {"max_channel", channel_map.max_channel},
            {"channels", channel_map.channels},
            {"used_channels", channel_map.used_channels},
            {"compressed_channels", channel_map.compressed_channels},
        };
    }
    const std::vector<uint8_t> buf = nlohmann::json::to_msgpack(j);
    std::ofstream fp(filename, std::ios::binary);
    if (!fp) throw std::runtime_error(
        "H2mmStateSidecar::write: cannot open " + filename);
    fp.write(reinterpret_cast<const char*>(buf.data()),
             static_cast<std::streamsize>(buf.size()));
    if (!fp) throw std::runtime_error(
        "H2mmStateSidecar::write: write failed for " + filename);
}

H2mmStateSidecar H2mmStateSidecar::read(const std::string& filename) {
    std::ifstream fp(filename, std::ios::binary);
    if (!fp) throw std::runtime_error(
        "H2mmStateSidecar::read: cannot open " + filename);
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(fp)),
                             std::istreambuf_iterator<char>());
    nlohmann::json j = nlohmann::json::from_msgpack(buf);
    if (j.value("format", std::string()) != "tttrlib.h2mm.states")
        throw std::runtime_error(
            "H2mmStateSidecar::read: " + filename + " is not an H2MM state sidecar");

    H2mmStateSidecar sc;
    sc.n_states = j.value("n_states", 0);
    sc.n_streams = j.value("n_streams", 0);
    sc.decoder = j.value("decoder", std::string());
    sc.seed = j.value("seed", 0LL);
    sc.states = from_binary(j.at("states"));
    sc.streams = from_binary(j.at("streams"));
    if (j.contains("model")) {
        const auto& m = j["model"];
        sc.model.prior = m.value("prior", std::vector<double>{});
        sc.model.trans = m.value("trans", std::vector<double>{});
        sc.model.obs = m.value("obs", std::vector<double>{});
        sc.model.loglik = m.value("loglik", 0.0);
        sc.model.n_iter = m.value("n_iter", 0);
        sc.model.n_phot = m.value("n_phot", 0LL);
        sc.model.converged = m.value("converged", false);
    }
    if (j.contains("channel_map")) {
        const auto& c = j["channel_map"];
        sc.channel_map.n_streams = c.value("n_streams", 0);
        sc.channel_map.n_states = c.value("n_states", 0);
        sc.channel_map.max_channel = c.value("max_channel", H2MM_PTU_MAX_CHANNEL);
        sc.channel_map.channels = c.value("channels", std::vector<int>{});
        sc.channel_map.used_channels = c.value("used_channels", std::vector<int>{});
        sc.channel_map.compressed_channels =
            c.value("compressed_channels", std::vector<int>{});
        sc.has_channel_map = true;
    }
    return sc;
}

}  // namespace tttrlib
