// SPDX-License-Identifier: BSD-3-Clause
#include "TTTRMask.h"
#include "info.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

#ifdef _OPENMP
#include <omp.h>
#endif

using tttrlib::bitops::ctz64;
using tttrlib::bitops::popcount64;
using tttrlib::bitops::tail_mask;
using tttrlib::bitops::word_count;

void TTTRMask::set_tttr(TTTR* tttr){
    resize_bits(tttr->size());
}

TTTRMask::TTTRMask(TTTR* tttr){
    set_tttr(tttr);
}

void TTTRMask::select_channels(
        TTTR* tttr,
        signed char *routing_channels, int n_routing_channels,
        bool mask
) {
    set_tttr(tttr);

    // Build lookup table for O(1) channel checking
    // Routing channels are typically in range [-128, 127] or [0, 255]
    constexpr int LOOKUP_SIZE = 256;
    bool channel_lookup[LOOKUP_SIZE] = {false};

    for (int i = 0; i < n_routing_channels; i++) {
        // Handle signed char by offsetting to [0, 255]
        unsigned char ch_idx = static_cast<unsigned char>(routing_channels[i]);
        channel_lookup[ch_idx] = true;
    }

    int n = static_cast<int>(tttr->size());
    signed char* channels = tttr->routing_channels;
    int n_words = static_cast<int>(word_count(static_cast<size_t>(n)));
    uint64_t* words = masked_words.data();

    // Use OpenMP for large datasets. Parallelize over whole 64-bit words so
    // each thread owns its read-modify-write exclusively (no bit-level races).
    bool use_openmp = tttrlib::cpu_features::get_openmp_enabled();
    (void) use_openmp;

#ifdef _OPENMP
    #pragma omp parallel for schedule(static) if(use_openmp && n > 100000)
#endif
    for (int wi = 0; wi < n_words; wi++) {
        uint64_t bits = words[wi];
        const int base = wi * 64;
        const int lim = std::min(64, n - base);
        for (int b = 0; b < lim; b++) {
            unsigned char ch_idx = static_cast<unsigned char>(channels[base + b]);
            if (channel_lookup[ch_idx]) {
                uint64_t m = 1ull << b;
                bits = mask ? (bits | m) : (bits & ~m);
            }
        }
        words[wi] = bits;
    }
}

void TTTRMask::select_microtime_ranges(
        TTTR* tttr,
        std::vector<std::pair<int, int>> micro_time_ranges
) {
    set_tttr(tttr);

    if (micro_time_ranges.empty()) {
        return;  // No ranges to filter
    }

    int n = static_cast<int>(tttr->size());
    unsigned short* micro_times = tttr->micro_times;

    // Build bitmap for O(1) lookup (micro times are 16-bit: 0-65535)
    constexpr int MICROTIME_MAX = 65536;
    bool micro_time_valid[MICROTIME_MAX];
    std::memset(micro_time_valid, 0, MICROTIME_MAX);

    // Mark valid ranges in bitmap using memset for contiguous ranges
    for (const auto& r : micro_time_ranges) {
        int start = std::max(0, r.first + 1);  // Exclusive lower bound
        int end = std::min(MICROTIME_MAX - 1, r.second - 1);  // Exclusive upper bound
        if (end >= start) {
            std::memset(&micro_time_valid[start], 1, end - start + 1);
        }
    }

    int n_words = static_cast<int>(word_count(static_cast<size_t>(n)));
    uint64_t* words = masked_words.data();
    bool use_openmp = tttrlib::cpu_features::get_openmp_enabled();
    (void) use_openmp;

    // Word-per-thread: no bit-level write races
#ifdef _OPENMP
    #pragma omp parallel for schedule(static) if(use_openmp && n > 100000)
#endif
    for (int wi = 0; wi < n_words; wi++) {
        uint64_t bits = words[wi];
        const int base = wi * 64;
        const int lim = std::min(64, n - base);
        for (int b = 0; b < lim; b++) {
            if (!micro_time_valid[micro_times[base + b]]) {
                bits |= 1ull << b;
            }
        }
        words[wi] = bits;
    }
}

std::vector<int> TTTRMask::get_indices(bool selected) {
    // Count first so the result is allocated exactly once
    size_t n_set = 0;
    const size_t nw = masked_words.size();
    for (size_t wi = 0; wi < nw; wi++) {
        n_set += static_cast<size_t>(popcount64(masked_words[wi]));
    }
    size_t n_out = selected ? (masked_size - n_set) : n_set;

    std::vector<int> idxs;
    idxs.reserve(n_out);

    // Word-skip scan: iterate only set bits (complement for selected)
    for (size_t wi = 0; wi < nw; wi++) {
        uint64_t x = selected ? ~masked_words[wi] : masked_words[wi];
        if (wi == nw - 1) {
            x &= tail_mask(masked_size);
        }
        while (x) {
            int b = ctz64(x);
            x &= x - 1;
            idxs.emplace_back(static_cast<int>((wi << 6) + b));
        }
    }
    return idxs;
}


std::vector<int> TTTRMask::get_selected_ranges() {
    // Preserves the original element-wise semantics exactly: a range starts at
    // the next unmasked (selected) index and stops at the following unmasked
    // index (or size() if none).
    const size_t n = masked_size;
    const size_t nw = masked_words.size();

    // Find the first index >= pos whose mask bit is 0; returns n if none.
    auto find_next_selected = [&](size_t pos) -> size_t {
        while (pos < n) {
            size_t wi = pos >> 6;
            uint64_t x = ~masked_words[wi];        // 1 = selected
            x &= (~0ull) << (pos & 63);            // clear bits below pos
            if (wi == nw - 1) {
                x &= tail_mask(n);                 // pad bits are not indices
            }
            if (x) {
                return (wi << 6) + static_cast<size_t>(ctz64(x));
            }
            pos = (wi + 1) << 6;
        }
        return n;
    };

    std::vector<int> rng;
    size_t start = find_next_selected(0);
    while (start < n) {
        size_t stop = find_next_selected(start + 1);
        rng.emplace_back(static_cast<int>(start));
        rng.emplace_back(static_cast<int>(stop));
        start = stop;
    }
    return rng;
}

void TTTRMask::select_count_rate(TTTR* tttr, double time_window, int n_ph_max, bool invert){
    if(tttr == nullptr) return;
    set_tttr(tttr);

    double macro_time_calibration = tttr->get_header()->get_macro_time_resolution();
    auto tw = (unsigned long) (time_window / macro_time_calibration);

    int i = 0;
    while (i < tttr->size() - 1){
        int n_ph = 0; int r = i;
        unsigned long long t_i = tttr->get_macro_time_at(i);
        while((tttr->get_macro_time_at(r) - t_i < tw) && (r < tttr->size() - 1)){
            r++; n_ph++;
        }
        set_bit(static_cast<size_t>(i), invert ? (n_ph >= n_ph_max) : (n_ph < n_ph_max));
        i = r;
    }
}

std::string TTTRMask::to_json() const {
    nlohmann::json j;
    j["size"] = static_cast<int>(masked_size);
    std::vector<int> mask_data;
    mask_data.reserve(masked_size);
    for (size_t i = 0; i < masked_size; i++) {
        mask_data.push_back(get_bit(i) ? 1 : 0);
    }
    j["mask"] = mask_data;
    return j.dump();
}

void TTTRMask::from_json(const std::string& payload) {
    nlohmann::json j = nlohmann::json::parse(payload);
    int size = j["size"];
    std::vector<int> mask_data = j["mask"];
    resize_bits(static_cast<size_t>(size < 0 ? 0 : size));
    for (int i = 0; i < size && i < static_cast<int>(mask_data.size()); ++i) {
        set_bit(static_cast<size_t>(i), mask_data[i] != 0);
    }
}

namespace {

/// Bit-packed words -> msgpack buffer.
std::vector<uint8_t> mask_to_msgpack(
    const std::vector<uint64_t>& masked_words, size_t masked_size
) {
    // The bits are already packed 64 to a word; hand msgpack those bytes as a
    // `bin` field so they stay bytes instead of becoming one integer per event.
    std::vector<uint8_t> words(masked_words.size() * sizeof(uint64_t));
    for (size_t w = 0; w < masked_words.size(); ++w) {
        uint64_t v = masked_words[w];      // little-endian, explicitly, so a
        for (int b = 0; b < 8; ++b) {      // payload is portable across hosts
            words[w * 8 + b] = static_cast<uint8_t>(v & 0xFF);
            v >>= 8;
        }
    }
    nlohmann::json j;
    j["format"] = "tttrlib.mask";
    j["version"] = 1;
    j["size"] = static_cast<long long>(masked_size);
    j["words"] = nlohmann::json::binary(words);
    return nlohmann::json::to_msgpack(j);
}

}  // namespace

void TTTRMask::to_msgpack(unsigned char** msgpack_out, int* n_msgpack_out) const {
    const std::vector<uint8_t> buf = mask_to_msgpack(masked_words, masked_size);
    get_array<unsigned char>(buf.size(), const_cast<unsigned char*>(buf.data()),
                             msgpack_out, n_msgpack_out);
}

void TTTRMask::from_msgpack(unsigned char* input, int n_input) {
    std::vector<uint8_t> payload(input, input + std::max(n_input, 0));
    nlohmann::json j = nlohmann::json::from_msgpack(payload);
    if (j.value("format", std::string()) != "tttrlib.mask")
        throw std::runtime_error("TTTRMask::from_msgpack: not a tttrlib mask payload");
    const long long size = j.value("size", 0LL);
    resize_bits(static_cast<size_t>(size < 0 ? 0 : size), true);
    std::vector<uint8_t> words;
    const auto& jw = j.at("words");
    if (jw.is_binary()) {
        const auto& b = jw.get_binary();
        words.assign(b.begin(), b.end());
    } else {
        words = jw.get<std::vector<uint8_t>>();
    }
    const size_t nw = std::min(masked_words.size(), words.size() / sizeof(uint64_t));
    for (size_t w = 0; w < nw; ++w) {
        uint64_t v = 0;
        for (int b = 7; b >= 0; --b) v = (v << 8) | words[w * 8 + b];
        masked_words[w] = v;
    }
    if (!masked_words.empty())
        masked_words.back() &= tail_mask(masked_size);
}

void TTTRMask::write_msgpack(const std::string& filename) const {
    const std::vector<uint8_t> buf = mask_to_msgpack(masked_words, masked_size);
    std::ofstream fp(filename, std::ios::binary);
    if (!fp) throw std::runtime_error("TTTRMask::write_msgpack: cannot open " + filename);
    fp.write(reinterpret_cast<const char*>(buf.data()),
             static_cast<std::streamsize>(buf.size()));
    if (!fp) throw std::runtime_error("TTTRMask::write_msgpack: write failed for " + filename);
}

void TTTRMask::read_msgpack(const std::string& filename) {
    std::ifstream fp(filename, std::ios::binary);
    if (!fp) throw std::runtime_error("TTTRMask::read_msgpack: cannot open " + filename);
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(fp)),
                                   std::istreambuf_iterator<char>());
    from_msgpack(buf.data(), static_cast<int>(buf.size()));
}
