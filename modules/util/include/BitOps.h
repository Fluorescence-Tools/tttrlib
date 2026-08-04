// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_BITOPS_H
#define TTTRLIB_BITOPS_H

/// Internal helpers for packed 64-bit-word bitsets (std-only, no third-party
/// code). Not exposed through SWIG.

#include <cstdint>
#include <cstddef>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace tttrlib {
namespace bitops {

/// Index of the lowest set bit; undefined for x == 0.
inline int ctz64(uint64_t x) {
#if defined(_MSC_VER)
    unsigned long i;
    _BitScanForward64(&i, x);
    return static_cast<int>(i);
#else
    return __builtin_ctzll(x);
#endif
}

inline int popcount64(uint64_t x) {
#if defined(_MSC_VER)
    return static_cast<int>(__popcnt64(x));
#else
    return __builtin_popcountll(x);
#endif
}

inline size_t word_count(size_t n_bits) {
    return (n_bits + 63) >> 6;
}

/// Mask covering the valid bits of the last word of an n_bits bitset
/// (all-ones when n_bits is a multiple of 64 or zero).
inline uint64_t tail_mask(size_t n_bits) {
    unsigned r = static_cast<unsigned>(n_bits & 63);
    return r ? ((1ull << r) - 1) : ~0ull;
}

inline bool get_bit(const std::vector<uint64_t>& words, size_t i) {
    return (words[i >> 6] >> (i & 63)) & 1ull;
}

inline void set_bit(std::vector<uint64_t>& words, size_t i, bool v) {
    uint64_t m = 1ull << (i & 63);
    if (v) words[i >> 6] |= m;
    else words[i >> 6] &= ~m;
}

} // namespace bitops
} // namespace tttrlib

#endif // TTTRLIB_BITOPS_H
