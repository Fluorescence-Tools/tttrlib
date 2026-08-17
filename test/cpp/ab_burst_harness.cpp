// SPDX-License-Identifier: BSD-3-Clause
//
// ab_burst_harness -- reaches the burst-search kernels that have no Python
// binding, so test/python/burstfilter/test_ab_burst_reference.py can A/B them
// against independent references (astropy's Bayesian Blocks). Built by that
// test at run time against the installed libtttrlib_burst; see the test for the
// compile line. Protocol: one subcommand on argv[1], whitespace-separated
// numbers on stdin, whitespace-separated numbers on stdout.
//
//   bb   <tick_seconds> <ncp_prior> <n> t0 .. t_{n-1}     -> change-point indices
//   ncp  <p0> <n>                                          -> ncp_prior
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "BurstSearchBayesianBlocks.h"

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const std::string cmd = argv[1];
    std::ios::sync_with_stdio(false);
    if (cmd == "bb") {
        double tick = 0.0, ncp = 0.0;
        long long n = 0;
        std::cin >> tick >> ncp >> n;
        std::vector<int64_t> t(static_cast<size_t>(n));
        for (long long i = 0; i < n; ++i) std::cin >> t[static_cast<size_t>(i)];
        std::vector<int64_t> cp = tttrlib::bayesian_blocks_events(t.data(), n, tick, ncp);
        for (size_t i = 0; i < cp.size(); ++i) std::cout << cp[i] << (i + 1 < cp.size() ? ' ' : '\n');
        return 0;
    }
    if (cmd == "ncp") {
        double p0 = 0.0;
        long long n = 0;
        std::cin >> p0 >> n;
        std::printf("%.17g\n", tttrlib::ncp_prior_from_p0(p0, n));
        return 0;
    }
    return 2;
}
