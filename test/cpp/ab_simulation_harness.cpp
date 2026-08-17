// SPDX-License-Identifier: BSD-3-Clause
//
// ab_simulation_harness.cpp -- expose the header-only samplers of modules/simulation
// to test/python/simulation/test_ab_simulation_reference.py so they can be A/B'd
// against canonical reference implementations (xoshiro256++, Philox via Random.h,
// Marsaglia-Tsang ziggurat, the boundary-flux samplers, alias sampling of a decay
// pattern). One subcommand per kernel; parameters on stdin, whitespace-separated
// numbers on stdout.
//
// Build (the pytest does this itself):
//   c++ -std=c++17 -O2 -I modules/simulation/include -I modules/math/include \
//       -I modules/util/include modules/simulation/src/SimRandom.cpp \
//       test/cpp/ab_simulation_harness.cpp -o ab_simulation_harness
#include <cinttypes>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "SimRandom.h"
#include "SimXoshiroRandom.h"
#include "SimCounterRandom.h"
#include "SimZiggurat.h"
#include "SimInjection.h"
#include "SimDecay.h"

using namespace tttrlib;

static uint64_t rd_u64() { uint64_t v; std::cin >> v; return v; }
static double rd_d() { double v; std::cin >> v; return v; }
static int rd_i() { int v; std::cin >> v; return v; }

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <cmd>\n", argv[0]); return 2; }
    const std::string cmd = argv[1];

    if (cmd == "xoshiro") {                 // base id counter n -> n raw u64
        uint32_t base = uint32_t(rd_u64()), id = uint32_t(rd_u64());
        uint64_t ctr = rd_u64(); int n = rd_i();
        SimXoshiroRandom r; r.reset(base, id, ctr);
        for (int i = 0; i < n; ++i) std::printf("%" PRIu64 "\n", r.next_u64());
        return 0;
    }
    if (cmd == "xoshiro_norm") {            // base id counter n -> n randomNorm()
        uint32_t base = uint32_t(rd_u64()), id = uint32_t(rd_u64());
        uint64_t ctr = rd_u64(); int n = rd_i();
        SimXoshiroRandom r; r.reset(base, id, ctr);
        for (int i = 0; i < n; ++i) std::printf("%.17g\n", r.randomNorm());
        return 0;
    }
    if (cmd == "counter") {                 // base id counter n -> n raw u32 (Philox)
        uint32_t base = uint32_t(rd_u64()), id = uint32_t(rd_u64());
        uint64_t ctr = rd_u64(); int n = rd_i();
        SimCounterRandom r; r.reset(base, id, ctr);
        for (int i = 0; i < n; ++i) std::printf("%u\n", unsigned(r.next_u32()));
        return 0;
    }
    if (cmd == "mt_raw") {                  // seed n -> n randomUInt (MT19937)
        uint32_t seed = uint32_t(rd_u64()); int n = rd_i();
        SimRandom r(seed);
        for (int i = 0; i < n; ++i) std::printf("%u\n", unsigned(r.randomUInt()));
        return 0;
    }
    if (cmd == "mt_init_by_array") {        // klen key[klen] n -> n randomUInt
        int klen = rd_i(); std::vector<uint32_t> key(klen);
        for (int i = 0; i < klen; ++i) key[i] = uint32_t(rd_u64());
        int n = rd_i();
        SimRandom r; r.init_by_array(key.data(), klen);
        for (int i = 0; i < n; ++i) std::printf("%u\n", unsigned(r.randomUInt()));
        return 0;
    }
    if (cmd == "zigg") {                    // seed n -> n sim_randn over MT19937(seed), plus the raw stream
        uint32_t seed = uint32_t(rd_u64()); int n = rd_i();
        SimRandom r(seed);
        for (int i = 0; i < n; ++i) std::printf("%.17g\n", sim_randn(r));
        return 0;
    }
    if (cmd == "zigg_tables") {             // -> kn[128] wn[128] fn[128]
        const SimZigguratTables& z = sim_ziggurat_tables();
        for (int i = 0; i < 128; ++i) std::printf("%u\n", unsigned(z.kn[i]));
        for (int i = 0; i < 128; ++i) std::printf("%.17g\n", z.wn[i]);
        for (int i = 0; i < 128; ++i) std::printf("%.17g\n", z.fn[i]);
        return 0;
    }
    if (cmd == "erfc") {                    // seed n -> n random_erfc over MT19937(seed)
        uint32_t seed = uint32_t(rd_u64()); int n = rd_i();
        SimRandom r(seed);
        for (int i = 0; i < n; ++i) std::printf("%.17g\n", sim_detail::random_erfc(r));
        return 0;
    }
    if (cmd == "entry") {                   // seed mu sigma n -> n random_entry_depth
        uint32_t seed = uint32_t(rd_u64()); double mu = rd_d(), sigma = rd_d(); int n = rd_i();
        SimRandom r(seed);
        for (int i = 0; i < n; ++i) std::printf("%.17g\n", sim_detail::random_entry_depth(r, mu, sigma));
        return 0;
    }
    if (cmd == "qnorm") {                   // n x... -> qnorm(x)
        int n = rd_i();
        for (int i = 0; i < n; ++i) std::printf("%.17g\n", sim_detail::qnorm(rd_d()));
        return 0;
    }
    if (cmd == "influx") {                  // n (mu sigma)... -> influx_weight
        int n = rd_i();
        for (int i = 0; i < n; ++i) { double mu = rd_d(), s = rd_d(); std::printf("%.17g\n", sim_detail::influx_weight(mu, s)); }
        return 0;
    }
    if (cmd == "decay_sample") {            // seed dt t0 nbins w[nbins] n -> n sample_ns
        uint32_t seed = uint32_t(rd_u64()); double dt = rd_d(), t0 = rd_d(); int nb = rd_i();
        std::vector<double> w(nb); for (int i = 0; i < nb; ++i) w[i] = rd_d();
        int n = rd_i();
        SimDecay d = SimDecay::from_pattern(w, dt, t0);
        SimRandom r(seed);
        for (int i = 0; i < n; ++i) std::printf("%.17g\n", d.sample_ns(r));
        return 0;
    }
    std::fprintf(stderr, "unknown command %s\n", cmd.c_str());
    return 2;
}
