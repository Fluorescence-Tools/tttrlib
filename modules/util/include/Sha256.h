// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_UTIL_SHA256_H
#define TTTRLIB_UTIL_SHA256_H

// SHA-256, hand-rolled rather than pulled in: the library is deliberately
// std-only and this is the whole of what is needed. Straight from FIPS 180-4.
//
// Two callers so far, and they want different things from it. The plugin host
// digests a `.so` so a published result can say which binary produced it. The
// burst pipeline digests an analysis' canonical settings JSON, because that
// digest is the identity of a run: PTO.MFDB writes it as
// `_mmfdb_operation.settings_hash`, and re-running with the same settings must
// produce the same string ChiSurf's `_settings_hash` would, or the two write
// two artifacts where they mean one.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace tttrlib {
namespace util {

struct Sha256 {
    std::uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                          0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::uint64_t length = 0;
    unsigned char buffer[64] = {};
    std::size_t buffered = 0;

    static std::uint32_t rotr(std::uint32_t x, int n) {
        return (x >> n) | (x << (32 - n));
    }

    void block(const unsigned char* p) {
        static const std::uint32_t k[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,
            0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
            0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,
            0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,
            0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
            0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,
            0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,
            0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
            0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (std::uint32_t(p[4 * i]) << 24) | (std::uint32_t(p[4 * i + 1]) << 16) |
                   (std::uint32_t(p[4 * i + 2]) << 8) | std::uint32_t(p[4 * i + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ ((~e) & g);
            const std::uint32_t t1 = hh + s1 + ch + k[i] + w[i];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void update(const unsigned char* p, std::size_t n) {
        length += n;
        while (n > 0) {
            const std::size_t take = (n < sizeof(buffer) - buffered)
                                             ? n : sizeof(buffer) - buffered;
            std::memcpy(buffer + buffered, p, take);
            buffered += take; p += take; n -= take;
            if (buffered == sizeof(buffer)) { block(buffer); buffered = 0; }
        }
    }

    std::string hex() {
        const std::uint64_t bits = length * 8;
        unsigned char pad = 0x80;
        update(&pad, 1);
        pad = 0x00;
        while (buffered != 56) update(&pad, 1);
        unsigned char tail[8];
        for (int i = 0; i < 8; ++i)
            tail[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
        // update() would count these into `length`, which is already frozen in
        // `bits`; feed the final block directly.
        std::memcpy(buffer + buffered, tail, 8);
        block(buffer);

        static const char* digits = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (int i = 0; i < 8; ++i) {
            for (int b = 3; b >= 0; --b) {
                const unsigned char byte = static_cast<unsigned char>(h[i] >> (8 * b));
                out.push_back(digits[byte >> 4]);
                out.push_back(digits[byte & 0x0F]);
            }
        }
        return out;
    }
};

/// Lowercase hex SHA-256 of a string.
inline std::string sha256_hex(const std::string& s) {
    Sha256 d;
    d.update(reinterpret_cast<const unsigned char*>(s.data()), s.size());
    return d.hex();
}

/// Lowercase hex SHA-256 of a file's bytes; "" when it cannot be read.
inline std::string sha256_file_hex(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return {};
    Sha256 d;
    std::vector<unsigned char> buf(64 * 1024);
    for (;;) {
        const std::size_t got = std::fread(buf.data(), 1, buf.size(), f);
        if (got == 0) break;
        d.update(buf.data(), got);
    }
    std::fclose(f);
    return d.hex();
}

}  // namespace util
}  // namespace tttrlib

#endif  // TTTRLIB_UTIL_SHA256_H
