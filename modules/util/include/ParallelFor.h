// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file ParallelFor.h
 * \brief Minimal work-stealing parallel-for over an index range.
 *
 * tttrlib builds with OpenMP where it is available, but OpenMP is routinely
 * missing on macOS toolchains and the library has to stay useful without it. This
 * is the std::thread fallback used by the burst searches and BVA: a fixed pool
 * drawing indices from one atomic counter, so uneven work per index (which is the
 * normal case — bursts and regions differ in size by orders of magnitude) still
 * balances across threads.
 *
 * It is deliberately not a general task framework. The pool is created and joined
 * inside the call, so use it around a body that does real work per index rather
 * than in a tight inner loop.
 */
#ifndef TTTRLIB_PARALLELFOR_H
#define TTTRLIB_PARALLELFOR_H

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace tttrlib {

/*!
 * \brief Run ``body(i)`` for every ``i`` in ``[0, n)``, in parallel.
 *
 * Falls back to a plain serial loop for a single item or a single hardware
 * thread, so callers need no special case for small inputs.
 *
 * \param n number of indices.
 * \param body callable invoked as ``body(int)``; must be safe to call
 *        concurrently for distinct indices.
 */
template <class F>
void parallel_for(int n, F&& body) {
    unsigned hc = std::thread::hardware_concurrency();
    int nt = (hc == 0) ? 1 : static_cast<int>(hc);
    if (nt > n) nt = std::max(1, n);
    if (nt <= 1 || n <= 1) {
        for (int i = 0; i < n; ++i) body(i);
        return;
    }
    std::atomic<int> next{0};
    auto worker = [&]() {
        int i;
        while ((i = next.fetch_add(1)) < n) body(i);
    };
    std::vector<std::thread> pool;
    pool.reserve(static_cast<size_t>(nt - 1));
    for (int c = 1; c < nt; ++c) pool.emplace_back(worker);
    worker();  // the calling thread pulls its share rather than idling
    for (auto& t : pool) t.join();
}

/// Number of worker threads parallel_for() would use for \a n items.
inline int parallel_for_threads(int n) {
    unsigned hc = std::thread::hardware_concurrency();
    int nt = (hc == 0) ? 1 : static_cast<int>(hc);
    if (nt > n) nt = std::max(1, n);
    return (n <= 1) ? 1 : nt;
}

}  // namespace tttrlib

#endif  // TTTRLIB_PARALLELFOR_H
