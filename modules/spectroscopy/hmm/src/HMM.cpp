// SPDX-License-Identifier: BSD-3-Clause
#include "HMM.h"
#include "Channel.h"
#include "BurstFilter.h"
#include "Mat.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <numeric>
#include <random>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace tttrlib {

namespace {

/// Number of worker threads to use, capped by the work available.
int worker_count(int n_items) {
    unsigned hc = std::thread::hardware_concurrency();
    int nt = (hc == 0) ? 1 : static_cast<int>(hc);
    if (nt > std::max(1, n_items)) nt = std::max(1, n_items);
    return std::max(1, nt);
}

}  // namespace

namespace hmm_detail {

/**
 * @brief Persistent fork-join thread pool.
 *
 * The EM loop calls the parallel E-step and cache build once per map (hundreds
 * of times).  Spawning fresh std::threads each map costs more than the work
 * itself at typical burst-analysis sizes, so the pool keeps its workers alive
 * for the whole optimisation and dispatches each map's chunks to them via a
 * generation counter + barrier.  ``run`` blocks until all chunks finish
 * (fork-join), so callers see plain synchronous parallelism with no
 * per-map thread-creation overhead.  Portable, std-only (no OpenMP).
 */
class ForkJoinPool {
public:
    explicit ForkJoinPool(int nthreads) : nthreads_(std::max(1, nthreads)) {
        if (nthreads_ <= 1) return;
        workers_.reserve(nthreads_ - 1);
        for (int c = 1; c < nthreads_; ++c)
            workers_.emplace_back([this, c] { worker_loop(c); });
    }

    ~ForkJoinPool() {
        if (nthreads_ <= 1) return;
        {
            std::unique_lock<std::mutex> lk(m_);
            stop_ = true;
            ++gen_;
        }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }

    int size() const { return nthreads_; }

    /// Run ``body(thread_index, item_begin, item_end)`` over a static split of
    /// ``[0, n_items)`` across the persistent workers; blocks until all finish.
    void run(int n_items, const std::function<void(int, int, int)>& body) {
        if (nthreads_ <= 1 || n_items <= 1) {
            body(0, 0, n_items);
            return;
        }
        cur_n_ = n_items;
        body_ = &body;
        {
            std::unique_lock<std::mutex> lk(m_);
            remaining_ = nthreads_ - 1;
            ++gen_;
        }
        cv_.notify_all();
        run_chunk(0);  // calling thread runs chunk 0
        std::unique_lock<std::mutex> lk(m_);
        done_cv_.wait(lk, [this] { return remaining_ == 0; });
    }

private:
    void run_chunk(int c) {
        int b0 = static_cast<int>(static_cast<long long>(c) * cur_n_ / nthreads_);
        int b1 = static_cast<int>(static_cast<long long>(c + 1) * cur_n_ / nthreads_);
        (*body_)(c, b0, b1);
    }

    void worker_loop(int c) {
        uint64_t seen = 0;
        for (;;) {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [this, &seen] { return gen_ != seen || stop_; });
            seen = gen_;
            if (stop_) return;
            lk.unlock();
            run_chunk(c);
            lk.lock();
            if (--remaining_ == 0) done_cv_.notify_one();
        }
    }

    int nthreads_;
    std::vector<std::thread> workers_;
    const std::function<void(int, int, int)>* body_ = nullptr;
    int cur_n_ = 0;
    std::mutex m_;
    std::condition_variable cv_, done_cv_;
    uint64_t gen_ = 0;
    int remaining_ = 0;
    bool stop_ = false;
};

}  // namespace hmm_detail

namespace {
/// Fallback fork-join over one-shot std::threads (used off the hot EM path,
/// e.g. Viterbi which runs once).
template <class F>
void parallel_chunks(int nthreads, int n_items, F&& body) {
    if (nthreads <= 1 || n_items <= 1) {
        body(0, 0, n_items);
        return;
    }
    std::vector<std::thread> pool;
    pool.reserve(nthreads - 1);
    auto chunk = [&](int c) {
        int b0 = static_cast<int>(static_cast<long long>(c) * n_items / nthreads);
        int b1 = static_cast<int>(static_cast<long long>(c + 1) * n_items / nthreads);
        body(c, b0, b1);
    };
    for (int c = 1; c < nthreads; ++c) pool.emplace_back(chunk, c);
    chunk(0);
    for (auto& t : pool) t.join();
}
}  // namespace

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

namespace {

// row_normalize now lives in Mat.h (tttrlib::row_normalize) so HMM, burst,
// and anyone else with a transition matrix share one implementation.
using tttrlib::row_normalize;

/// Row-normalised matrix product out = norm(a @ b), all n x n row-major.
/// Templated on the numeric type so the caches can run in float (fast mode) or
/// double (exact); accumulation is always in double for stability.
template <class R>
void matmul_norm(const R* a, const R* b, R* out, int n) {
    for (int i = 0; i < n; ++i) {
        double s = 0.0;
        for (int j = 0; j < n; ++j) {
            double v = 0.0;
            for (int k = 0; k < n; ++k)
                v += static_cast<double>(a[i * n + k]) * static_cast<double>(b[k * n + j]);
            out[i * n + j] = static_cast<R>(v);
            s += v;
        }
        // load-bearing: ~log2(Δt) chained products drift off 1
        // side effect: HmmEval::score simplex-only
        if (s > 0.0) {
            for (int j = 0; j < n; ++j) out[i * n + j] = static_cast<R>(out[i * n + j] / s);
        }
    }
}

// ρ(Δt)[k,m,i,j] = expected one-tick i->j inside a Δt gap running k -> m.
// gap interior in closed form; states known only at photons.
//
// ρ tensor indexing: R[((k*n + m)*n + i)*n + j]  (order k,m,i,j)
// endpoints outermost -> estep_t's reduction sweeps a contiguous (i,j) block
inline int rho_idx(int k, int m, int i, int j, int n) {
    return ((k * n + m) * n + i) * n + j;
}

/// ρ(1)[k,m,i,j] = δ_{k,i}·A[i,j]·δ_{j,m}
template <class R>
void rho_base(const R* A, R* Rt, int n) {
    std::fill(Rt, Rt + n * n * n * n, R(0));
    // one tick -> only start->end possible, rest stays zero
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            Rt[rho_idx(i, j, i, j, n)] = A[i * n + j];
}

/// Compose interval propagators (Pa,Ra) then (Pb,Rb) -> (P,R).
/// P = norm(Pa @ Pb); R[k,m,i,j] = Σ_z Ra[k,z,i,j]·Pb[z,m] + Pa[k,z]·Rb[z,m,i,j]
template <class R>
void pair_compose(
    const R* Pa, const R* Ra,
    const R* Pb, const R* Rb,
    R* P, R* Rt, int n
) {
    matmul_norm<R>(Pa, Pb, P, n);
    const int n2 = n * n;
    std::fill(Rt, Rt + n2 * n2, R(0));
    // i->j fired in a or in b; expectations add, so the terms just sum
    // associative -> binary exponentiation in pair_pow is legal
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            for (int k = 0; k < n; ++k) {
                for (int m = 0; m < n; ++m) {
                    double v = 0.0;
                    for (int z = 0; z < n; ++z) {
                        v += static_cast<double>(Ra[rho_idx(k, z, i, j, n)]) * Pb[z * n + m] +
                             static_cast<double>(Pa[k * n + z]) * Rb[rho_idx(z, m, i, j, n)];
                    }
                    Rt[rho_idx(k, m, i, j, n)] = static_cast<R>(v);
                }
            }
        }
    }
}

/// Reusable scratch for the allocation-free pair-power (one per worker thread).
// ρ is n^4 doubles; per-slot allocation would dominate the build
template <class R>
struct PairPowScratch {
    std::vector<R> Pres, Rres, Pb, Rb, Ptmp, Rtmp, Pb2, Rb2;
    void resize(int n) {
        int n2 = n * n, n4 = n2 * n2;
        Pres.resize(n2); Rres.resize(n4);
        Pb.resize(n2);   Rb.resize(n4);
        Ptmp.resize(n2); Rtmp.resize(n4);
        Pb2.resize(n2);  Rb2.resize(n4);
    }
};

/// Binary-exponentiate the base pair (A, R1) to (A^power, ρ(power)),
/// allocation-free — scratch buffers are reused across slots.
template <class R>
void pair_pow(
    const R* A, const R* R1, long long power, int n,
    R* Pout, R* Rout, PairPowScratch<R>& s
) {
    const int n2 = n * n, n4 = n2 * n2;
    R *Pres = s.Pres.data(), *Rres = s.Rres.data();
    R *Pb = s.Pb.data(), *Rb = s.Rb.data();
    R *Ptmp = s.Ptmp.data(), *Rtmp = s.Rtmp.data();
    R *Pb2 = s.Pb2.data(), *Rb2 = s.Rb2.data();

    // Identity element: P = I, R = 0.
    std::fill(Pres, Pres + n2, R(0));
    std::fill(Rres, Rres + n4, R(0));
    for (int i = 0; i < n; ++i) Pres[i * n + i] = R(1);
    std::copy(A, A + n2, Pb);
    std::copy(R1, R1 + n4, Rb);

    long long e = power;
    while (e > 0) {
        if (e & 1) {
            pair_compose<R>(Pres, Rres, Pb, Rb, Ptmp, Rtmp, n);
            std::swap(Pres, Ptmp);
            std::swap(Rres, Rtmp);
        }
        e >>= 1;
        if (e > 0) {
            pair_compose<R>(Pb, Rb, Pb, Rb, Pb2, Rb2, n);
            std::swap(Pb, Pb2);
            std::swap(Rb, Rb2);
        }
    }
    std::copy(Pres, Pres + n2, Pout);
    std::copy(Rres, Rres + n4, Rout);
}

// ---- templated cache build + E-step (shared by the double and float paths) ----

/// Build A^Δt and ρ(Δt) caches in type R from a double trans matrix A.
template <class R>
void fill_caches_t(
    const std::vector<int64_t>& unique_dt, const double* A, int n,
    R* pow_cache, R* rho_cache, hmm_detail::ForkJoinPool* pool
) {
    const int n2 = n * n, n4 = n2 * n2;
    const int n_slots = static_cast<int>(unique_dt.size());
    if (n_slots == 0) return;
    std::vector<R> Ar(A, A + n2);      // A cast to R
    std::vector<R> R1(n4);
    rho_base<R>(Ar.data(), R1.data(), n);
    auto body = [&](int /*c*/, int s0, int s1) {
        PairPowScratch<R> scratch;
        scratch.resize(n);
        for (int s = s0; s < s1; ++s)
            pair_pow<R>(Ar.data(), R1.data(), unique_dt[s], n,
                        pow_cache + static_cast<size_t>(s) * n2,
                        rho_cache + static_cast<size_t>(s) * n4, scratch);
    };
    if (pool) pool->run(n_slots, body);
    else body(0, 0, n_slots);
}

/// A^power with every row renormalised at each step.
///
/// The P half of ``pair_pow``, operation for operation, without building the
/// n^4 ρ tensor beside it.  Decoding (Viterbi, γ, both samplers) needs the
/// propagator and never the expected-transition tensor, so it gets its cache
/// from here instead of paying ``n_slots * n^4`` doubles to throw them away.
void mat_pow_norm(const double* A, long long power, int n, double* out) {
    const int n2 = n * n;
    std::vector<double> Pres(n2, 0.0), Pb(A, A + n2), Ptmp(n2), Pb2(n2);
    for (int i = 0; i < n; ++i) Pres[i * n + i] = 1.0;
    long long e = power;
    while (e > 0) {
        if (e & 1) {
            matmul_norm<double>(Pres.data(), Pb.data(), Ptmp.data(), n);
            Pres.swap(Ptmp);
        }
        e >>= 1;
        if (e > 0) {
            matmul_norm<double>(Pb.data(), Pb.data(), Pb2.data(), n);
            Pb.swap(Pb2);
        }
    }
    std::copy(Pres.begin(), Pres.end(), out);
}

/// Scaled forward recursion over one burst's photons.
///
/// Fills ``alpha`` (m_len x n, row-major, each row normalised) and ``scale``
/// (the row sums), and returns the burst's log-likelihood contribution.  Shared
/// by the E-step and by every decoder, so the scaled recursion exists once
/// rather than in four subtly diverging copies.
template <class R>
double forward_burst(
    const int32_t* streams, const int32_t* gap_slot,
    int64_t s, int64_t m_len,
    const double* prior, const double* obs, const R* pow_cache,
    int n, int p, double* alpha, double* scale
) {
    double ll = 0.0;
    {
        const int y0 = streams[s];
        double tot = 0.0;
        for (int i = 0; i < n; ++i) {
            double a0 = prior[i] * obs[i * p + y0];
            alpha[i] = a0;
            tot += a0;
        }
        // scaled, not log-space: alpha underflows fast, and the sums get reused
        // -- logs are the loglik, backward pass wants 1/c_t
        scale[0] = tot;
        if (tot > 0.0) {
            for (int i = 0; i < n; ++i) alpha[i] /= tot;
            ll += std::log(tot);
        }
    }
    const int n2 = n * n;
    for (int64_t li = 1; li < m_len; ++li) {
        const int64_t nn = s + li;
        const int32_t slot = gap_slot[nn - 1];
        const int yn = streams[nn];
        const R* P = pow_cache + static_cast<size_t>(slot < 0 ? 0 : slot) * n2;
        const double* aprev = alpha + (li - 1) * n;
        double* acur = alpha + li * n;
        double tot = 0.0;
        if (slot < 0) {
            // coincident photons (Δt=0 after down-scaling): A = I, no propagation
            for (int i = 0; i < n; ++i) {
                double v = aprev[i] * obs[i * p + yn];
                acur[i] = v; tot += v;
            }
        } else {
            for (int i = 0; i < n; ++i) {
                double v = 0.0;
                for (int k = 0; k < n; ++k) v += aprev[k] * static_cast<double>(P[k * n + i]);
                v *= obs[i * p + yn];
                acur[i] = v; tot += v;
            }
        }
        scale[li] = tot;
        if (tot > 0.0) {
            for (int i = 0; i < n; ++i) acur[i] /= tot;
            ll += std::log(tot);
        }
    }
    return ll;
}

/// Counter-based bit mixer (SplitMix64 finaliser).
inline uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

/**
 * @brief One uniform in [0,1) keyed by (seed, draw, photon).
 *
 * Counter-based rather than sequential: each draw is a pure function of its
 * coordinates, so bursts can decode in any order on any number of threads and
 * the output is bit-identical.  A single shared generator would make the result
 * depend on thread scheduling.
 */
inline double rng_unit(uint64_t seed, uint64_t draw, uint64_t index) {
    uint64_t h = splitmix64(seed ^ 0xD1B54A32D192ED03ULL);
    h = splitmix64(h ^ (draw * 0xC2B2AE3D27D4EB4FULL));
    h = splitmix64(h ^ (index * 0x165667B19E3779F9ULL));
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

/// Inverse-CDF draw from an unnormalised weight vector; -1 if it sums to zero.
inline int draw_from(const double* w, int n, double u) {
    double tot = 0.0;
    for (int i = 0; i < n; ++i) tot += w[i];
    if (!(tot > 0.0)) return -1;
    const double x = u * tot;
    double acc = 0.0;
    for (int i = 0; i < n; ++i) {
        acc += w[i];
        if (x < acc) return i;
    }
    return n - 1;
}

/// Scaled forward-backward + Baum-Welch accumulation with caches of type R.
/// The hot loop runs in R (float for the fast mode); every reduction that feeds
/// the M-step (ξ, γ, prior, log-likelihood) accumulates in double.
template <class R>
double estep_t(
    const std::vector<int32_t>& streams, const std::vector<int32_t>& gap_slot,
    const std::vector<int64_t>& offsets, int n_bursts,
    const std::vector<double>& prior, const std::vector<double>& obs,
    const R* pow_cache, const R* rho_cache, int n, int p, int n_slots,
    std::vector<double>& xi_acc, std::vector<double>& gamma_obs_acc,
    std::vector<double>& prior_acc, bool have_dt, hmm_detail::ForkJoinPool* pool
) {
    const int n2 = n * n, n4 = n2 * n2;
    const int nthreads = pool ? pool->size() : 1;

    int64_t max_len = 0;
    for (int b = 0; b < n_bursts; ++b)
        max_len = std::max(max_len, offsets[b + 1] - offsets[b]);

    std::vector<std::vector<double>> W_p(nthreads, std::vector<double>(static_cast<size_t>(n_slots) * n2, 0.0));
    std::vector<std::vector<double>> gobs_p(nthreads, std::vector<double>(static_cast<size_t>(n) * p, 0.0));
    std::vector<std::vector<double>> prior_p(nthreads, std::vector<double>(n, 0.0));
    std::vector<double> ll_p(nthreads, 0.0);

    auto body = [&](int c, int b0, int b1) {
        std::vector<double> alpha(static_cast<size_t>(std::max<int64_t>(max_len, 1)) * n);
        std::vector<double> scale(std::max<int64_t>(max_len, 1));
        std::vector<double> w(n), beta_next(n), beta_cur(n);
        std::vector<double>& W_local = W_p[c];
        std::vector<double>& gobs_local = gobs_p[c];
        std::vector<double>& prior_local = prior_p[c];
        double ll_local = 0.0;

        for (int b = b0; b < b1; ++b) {
            const int64_t s = offsets[b];
            const int64_t e = offsets[b + 1];
            const int64_t m_len = e - s;

            ll_local += forward_burst<R>(
                streams.data(), gap_slot.data(), s, m_len,
                prior.data(), obs.data(), pow_cache, n, p,
                alpha.data(), scale.data());

            // backward with γ and W fused
            {
                const int yl = streams[e - 1];
                const double* alast = alpha.data() + (m_len - 1) * n;
                for (int i = 0; i < n; ++i) {
                    beta_next[i] = 1.0;
                    const double g = alast[i];
                    gobs_local[i * p + yl] += g;
                    if (m_len == 1) prior_local[i] += g;
                }
            }
            for (int64_t li = m_len - 2; li >= 0; --li) {
                const int64_t nn = s + li;
                const int32_t slot = gap_slot[nn];
                const int yn1 = streams[nn + 1];
                const double cc = scale[li + 1];
                const double inv_c = (cc > 0.0) ? 1.0 / cc : 0.0;
                for (int k = 0; k < n; ++k) w[k] = obs[k * p + yn1] * beta_next[k];
                const double* acur_row = alpha.data() + li * n;
                if (slot < 0) {
                    for (int i = 0; i < n; ++i) beta_cur[i] = w[i] * inv_c;
                } else {
                    const R* P = pow_cache + static_cast<size_t>(slot) * n2;
                    for (int i = 0; i < n; ++i) {
                        double v = 0.0;
                        for (int k = 0; k < n; ++k) v += static_cast<double>(P[i * n + k]) * w[k];
                        beta_cur[i] = v * inv_c;
                    }
                }
                const int yn = streams[nn];
                for (int i = 0; i < n; ++i) {
                    const double g = acur_row[i] * beta_cur[i];
                    gobs_local[i * p + yn] += g;
                    if (li == 0) prior_local[i] += g;
                }
                // deferred ρ: bank the (k,m) weight, contract against ρ later.
                // per-photon contraction would be O(N n^4); this is O(n^2)
                if (cc > 0.0 && slot >= 0) {
                    double* Wslot = W_local.data() + static_cast<size_t>(slot) * n2;
                    for (int k = 0; k < n; ++k) {
                        const double ak = acur_row[k] * inv_c;
                        if (ak == 0.0) continue;
                        double* Wk = Wslot + k * n;
                        for (int m = 0; m < n; ++m) Wk[m] += ak * w[m];
                    }
                }
                for (int i = 0; i < n; ++i) beta_next[i] = beta_cur[i];
            }
        }
        ll_p[c] = ll_local;
    };
    if (pool) pool->run(n_bursts, body);
    else body(0, 0, n_bursts);

    // serial reduction in thread order -> result independent of thread count
    double loglik = 0.0;
    for (int c = 0; c < nthreads; ++c) {
        loglik += ll_p[c];
        for (int i = 0; i < n; ++i) {
            prior_acc[i] += prior_p[c][i];
            for (int k = 0; k < p; ++k)
                gamma_obs_acc[i * p + k] += gobs_p[c][i * p + k];
        }
    }
    // ξ = Σ W·ρ -- the n^4 bill, once per slot instead of once per photon
    if (have_dt) {
        for (int slot = 0; slot < n_slots; ++slot) {
            const R* Rslot = rho_cache + static_cast<size_t>(slot) * n4;
            for (int k = 0; k < n; ++k) {
                for (int m = 0; m < n; ++m) {
                    double wkm = 0.0;
                    for (int c = 0; c < nthreads; ++c)
                        wkm += W_p[c][static_cast<size_t>(slot) * n2 + k * n + m];
                    if (wkm == 0.0) continue;
                    const R* Rkm = Rslot + rho_idx(k, m, 0, 0, n);
                    for (int i = 0; i < n; ++i)
                        for (int j = 0; j < n; ++j)
                            xi_acc[i * n + j] += wkm * static_cast<double>(Rkm[i * n + j]);
                }
            }
        }
    }
    return loglik;
}

}  // namespace

void HmmModel::normalize() {
    int n = n_states(), p = n_symbols();
    if (n <= 0) return;
    row_normalize(prior, 1, n);
    row_normalize(trans, n, n);
    row_normalize(obs, n, p);
}

// ---------------------------------------------------------------------------
// Data preparation (CSR layout + unique-Δt table)
// ---------------------------------------------------------------------------

void HMM::set_bursts(
    const std::vector<std::vector<long long>>& times,
    const std::vector<std::vector<int>>& streams,
    int n_streams
) {
    set_bursts_impl(times, streams, n_streams, nullptr, 0);
}

void HMM::set_bursts_micro(
    const std::vector<std::vector<long long>>& times,
    const std::vector<std::vector<int>>& streams,
    const std::vector<std::vector<int>>& micro_bins,
    int n_streams,
    int n_micro_bins,
    double bin_width_ns
) {
    if (micro_bins.size() != times.size())
        throw std::invalid_argument(
            "set_bursts_micro: micro_bins must have the same burst count as times");
    set_bursts_impl(times, streams, n_streams, nullptr, 0,
                    &micro_bins, n_micro_bins, bin_width_ns);
}

void HMM::set_bursts_impl(
    const std::vector<std::vector<long long>>& times,
    const std::vector<std::vector<int>>& streams,
    int n_streams,
    const std::vector<std::vector<int64_t>>* indices,
    long long n_source_photons,
    const std::vector<std::vector<int>>* micro_bins,
    int n_micro_bins,
    double bin_width_ns
) {
    if (times.size() != streams.size())
        throw std::invalid_argument("times and streams must have equal burst count");
    if (indices && indices->size() != times.size())
        throw std::invalid_argument("indices must have the same burst count as times");
    if (n_micro_bins < 1)
        throw std::invalid_argument("n_micro_bins must be >= 1");
    if (micro_bins && micro_bins->size() != times.size())
        throw std::invalid_argument("micro_bins must have the same burst count as times");

    n_streams_ = n_streams;
    n_micro_bins_ = n_micro_bins;
    micro_bin_width_ns_ = bin_width_ns;
    n_source_photons_ = n_source_photons;
    streams_.clear();
    gap_slot_.clear();
    offsets_.clear();
    unique_dt_.clear();
    photon_index_.clear();

    // Keep only non-empty bursts; build offsets and concatenated streams.
    std::vector<const std::vector<long long>*> kept_times;
    std::vector<const std::vector<int>*> kept_streams;
    std::vector<const std::vector<int64_t>*> kept_indices;
    std::vector<const std::vector<int>*> kept_micro;
    for (size_t b = 0; b < times.size(); ++b) {
        if (times[b].size() != streams[b].size())
            throw std::invalid_argument("each burst needs equal-length times and streams");
        if (indices && (*indices)[b].size() != times[b].size())
            throw std::invalid_argument("each burst needs as many indices as times");
        if (micro_bins && (*micro_bins)[b].size() != times[b].size())
            throw std::invalid_argument("each burst needs as many micro bins as times");
        if (times[b].empty()) continue;
        kept_times.push_back(&times[b]);
        kept_streams.push_back(&streams[b]);
        if (indices) kept_indices.push_back(&(*indices)[b]);
        if (micro_bins) kept_micro.push_back(&(*micro_bins)[b]);
    }

    // Total photons is known from the kept bursts; reserve once so the flat CSR
    // stream/gap arrays and the Δt scratch do not repeatedly reallocate.
    size_t total_photons = 0;
    for (const auto* sp : kept_streams) total_photons += sp->size();

    offsets_.push_back(0);
    offsets_.reserve(kept_times.size() + 1);
    streams_.reserve(total_photons);
    if (indices) photon_index_.reserve(total_photons);
    for (size_t b = 0; b < kept_times.size(); ++b) {
        const auto& s = *kept_streams[b];
        if (micro_bins) {
            // The product symbol.  Bins are clamped rather than rejected: a
            // photon just past the end of the axis is a real photon, and
            // dropping it would thin the stream a fit reads as a longer gap.
            const auto& m = *kept_micro[b];
            for (size_t k = 0; k < s.size(); ++k) {
                int bin = m[k];
                if (bin < 0) bin = 0;
                if (bin >= n_micro_bins) bin = n_micro_bins - 1;
                streams_.push_back(
                    static_cast<int32_t>(s[k] * n_micro_bins + bin));
            }
        } else {
            for (int v : s) streams_.push_back(static_cast<int32_t>(v));
        }
        if (indices) {
            const auto& ix = *kept_indices[b];
            photon_index_.insert(photon_index_.end(), ix.begin(), ix.end());
        }
        offsets_.push_back(static_cast<int64_t>(streams_.size()));
    }

    // the sparse trick: cache size follows distinct gaps, not the largest one,
    // so a long dark stretch costs one slot rather than dt_max of them
    std::vector<int64_t> all_dt;
    all_dt.reserve(total_photons);
    for (const auto* tp : kept_times) {
        const auto& t = *tp;
        for (size_t k = 1; k < t.size(); ++k) {
            int64_t d = static_cast<int64_t>(t[k]) - static_cast<int64_t>(t[k - 1]);
            if (d > 0) all_dt.push_back(d);
        }
    }
    std::sort(all_dt.begin(), all_dt.end());
    all_dt.erase(std::unique(all_dt.begin(), all_dt.end()), all_dt.end());
    unique_dt_ = std::move(all_dt);

    // gap_slot: for each photon, slot index into unique_dt_ of Δt to next photon
    // (-1 at the last photon of each burst).
    gap_slot_.assign(streams_.size(), -1);
    for (size_t b = 0; b < kept_times.size(); ++b) {
        const auto& t = *kept_times[b];
        int64_t start = offsets_[b];
        for (size_t k = 1; k < t.size(); ++k) {
            int64_t d = static_cast<int64_t>(t[k]) - static_cast<int64_t>(t[k - 1]);
            if (d <= 0) {
                gap_slot_[start + k - 1] = -1;  // coincident: no propagation
            } else {
                auto it = std::lower_bound(unique_dt_.begin(), unique_dt_.end(), d);
                gap_slot_[start + k - 1] =
                    static_cast<int32_t>(it - unique_dt_.begin());
            }
        }
    }
}

std::vector<long long> HMM::get_unique_dt() const {
    return std::vector<long long>(unique_dt_.begin(), unique_dt_.end());
}

void HMM::set_bursts_from_tttr(
    std::shared_ptr<TTTR> tttr,
    long long* bursts, int n_bursts, int n_cols,
    const std::vector<std::shared_ptr<Channel>>& stream_channels,
    int min_photons,
    long long time_scale,
    int n_micro_bins
) {
    if (!tttr) throw std::invalid_argument("set_bursts_from_tttr: null TTTR");
    if (stream_channels.empty())
        throw std::invalid_argument("set_bursts_from_tttr: no stream definitions");
    if (n_micro_bins < 1)
        throw std::invalid_argument("set_bursts_from_tttr: n_micro_bins must be >= 1");
    const long long ts = std::max<long long>(1, time_scale);
    const int64_t n_total = static_cast<int64_t>(tttr->size());

    // Micro-time axis: bin the raw TAC channel over the file's full range, so
    // the bins are a coarsening of the instrument's own grid rather than of
    // whatever window the stream Channels happen to select.  Two streams with
    // different micro-time windows then share one axis, which is what lets a
    // single decay be scored across them.
    //
    // Three sources, in order of how much they are trusted.  The *effective*
    // channel count is best -- it stops at one laser period, and TAC channels
    // past that are empty, so binning over them would waste most of the axis on
    // nothing.  Failing that the header's total, and failing *that* the data
    // themselves: a synthetic or header-stripped file reports 1 effective
    // channel, and taking that literally would put every photon in the last bin
    // -- a silent, total loss of the micro-time information, which is precisely
    // what this argument was asked for.
    TTTRHeader* hdr = tttr->get_header();
    int n_tac = 0;
    double tac_res = hdr ? hdr->get_micro_time_resolution() : 0.0;
    if (n_micro_bins > 1) {
        n_tac = static_cast<int>(tttr->get_number_of_micro_time_channels());
        if (n_tac <= 1 && hdr)
            n_tac = static_cast<int>(hdr->get_number_of_micro_time_channels());
        if (n_tac <= 1) {
            int mx = 0;
            for (int64_t i = 0; i < n_total; ++i)
                mx = std::max(mx, static_cast<int>(tttr->get_micro_time_at(i)));
            n_tac = mx + 1;
        }
        if (n_tac < 1) n_tac = 1;
    }
    const double bin_width_ns = (n_micro_bins > 1 && tac_res > 0.0)
        ? tac_res * 1e9 * double(n_tac) / double(n_micro_bins) : 0.0;

    // Pre-extract each stream's (routing_channel, mt_start, mt_stop) components.
    std::vector<std::vector<std::tuple<int, int, int>>> comps(stream_channels.size());
    for (size_t si = 0; si < stream_channels.size(); ++si) {
        if (stream_channels[si]) comps[si] = stream_channels[si]->get_components();
    }

    // first match wins; overlapping Channel defs resolve by order, no match drops
    auto match_stream = [&](int ch, int mt) -> int {
        for (size_t si = 0; si < comps.size(); ++si) {
            for (const auto& c : comps[si]) {
                int rc = std::get<0>(c), a = std::get<1>(c), z = std::get<2>(c);
                if (ch == rc && mt >= a && mt <= z) return static_cast<int>(si);
            }
        }
        return -1;
    };

    std::vector<std::vector<long long>> times;
    std::vector<std::vector<int>> strms;
    // Index in the source file of every photon kept, so a decoded state can be
    // written back to the right record / mask bit later.
    std::vector<std::vector<int64_t>> idxs;
    std::vector<std::vector<int>> micro;   // empty unless n_micro_bins > 1
    // bursts is an (n_bursts, 2) [start, stop] array (row-major).
    const size_t n_pairs = (bursts == nullptr || n_bursts < 1 || n_cols != 2)
        ? 0 : static_cast<size_t>(n_bursts);
    for (size_t b = 0; b < n_pairs; ++b) {
        int64_t s = bursts[2 * b], e = bursts[2 * b + 1];
        if (s < 0) s = 0;
        if (e > n_total - 1) e = n_total - 1;
        std::vector<long long> bt;
        std::vector<int> bs;
        std::vector<int64_t> bi;
        std::vector<int> bm;
        long long last_t = std::numeric_limits<long long>::min();
        for (int64_t idx = s; idx <= e; ++idx) {
            const int ch = static_cast<int>(tttr->get_routing_channel_at(idx));
            const int mt = static_cast<int>(tttr->get_micro_time_at(idx));
            const int stream = match_stream(ch, mt);
            if (stream < 0) continue;
            long long t = static_cast<long long>(tttr->get_macro_time_at(idx)) / ts;
            if (t < last_t) t = last_t;  // enforce monotonic non-decreasing
            last_t = t;
            bt.push_back(t);
            bs.push_back(stream);
            bi.push_back(idx);
            if (n_micro_bins > 1) {
                int bin = static_cast<int>(
                    (static_cast<int64_t>(mt) * n_micro_bins) / n_tac);
                if (bin < 0) bin = 0;
                if (bin >= n_micro_bins) bin = n_micro_bins - 1;
                bm.push_back(bin);
            }
        }
        if (static_cast<int>(bt.size()) >= min_photons) {
            times.push_back(std::move(bt));
            strms.push_back(std::move(bs));
            idxs.push_back(std::move(bi));
            if (n_micro_bins > 1) micro.push_back(std::move(bm));
        }
    }
    set_bursts_impl(times, strms, static_cast<int>(stream_channels.size()),
                    &idxs, static_cast<long long>(n_total),
                    n_micro_bins > 1 ? &micro : nullptr, n_micro_bins,
                    bin_width_ns);
}

void HMM::set_bursts_from_filter(
    std::shared_ptr<BurstFilter> burst_filter,
    const std::vector<std::shared_ptr<Channel>>& stream_channels,
    int min_photons,
    long long time_scale,
    int n_micro_bins
) {
    if (!burst_filter) throw std::invalid_argument("set_bursts_from_filter: null BurstFilter");
    // get_burst_indices() is vector<int64_t>; on LP64 Linux that is a distinct
    // type from long long, so copy into the public pointer/length signature.
    std::vector<long long> b(burst_filter->get_burst_indices().begin(),
                             burst_filter->get_burst_indices().end());
    set_bursts_from_tttr(burst_filter->get_tttr(),
                         b.data(), static_cast<int>(b.size() / 2), 2,
                         stream_channels, min_photons, time_scale, n_micro_bins);
}

// ---------------------------------------------------------------------------
// Caches
// ---------------------------------------------------------------------------

void HMM::fill_caches(
    const std::vector<double>& A, int n,
    std::vector<double>& pow_cache, std::vector<double>& rho_cache,
    hmm_detail::ForkJoinPool* pool
) const {
    fill_caches_t<double>(unique_dt_, A.data(), n,
                          pow_cache.data(), rho_cache.data(), pool);
}

void HMM::fill_pow_cache(
    const std::vector<double>& A, int n, std::vector<double>& pow_cache
) const {
    const int n2 = n * n;
    const int n_slots = static_cast<int>(unique_dt_.size());
    pow_cache.assign(static_cast<size_t>(std::max(n_slots, 1)) * n2, 0.0);
    if (n_slots == 0) return;
    parallel_chunks(worker_count(n_slots), n_slots, [&](int, int s0, int s1) {
        for (int s = s0; s < s1; ++s)
            mat_pow_norm(A.data(), unique_dt_[s], n,
                         pow_cache.data() + static_cast<size_t>(s) * n2);
    });
}

// ---------------------------------------------------------------------------
// E-step: scaled forward-backward + Baum-Welch accumulation
// ---------------------------------------------------------------------------

double HMM::estep(
    const std::vector<double>& prior,
    const std::vector<double>& obs,
    const std::vector<double>& pow_cache,
    const std::vector<double>& rho_cache,
    int n, int p,
    std::vector<double>& xi_acc,
    std::vector<double>& gamma_obs_acc,
    std::vector<double>& prior_acc,
    hmm_detail::ForkJoinPool* pool
) const {
    const int n_slots = std::max<int>(1, static_cast<int>(unique_dt_.size()));
    return estep_t<double>(
        streams_, gap_slot_, offsets_, get_n_bursts(),
        prior, obs, pow_cache.data(), rho_cache.data(), n, p, n_slots,
        xi_acc, gamma_obs_acc, prior_acc, !unique_dt_.empty(), pool);
}

// ---------------------------------------------------------------------------
// EM optimiser (plain + SQUAREM), matching the ChiSurf numba engine
// ---------------------------------------------------------------------------

namespace {

struct EMResult {
    std::vector<double> prior, trans, obs;
    double ll;
};

// Pack / unpack a (prior, trans, obs) triple into one parameter vector.
std::vector<double> pack(
    const std::vector<double>& prior,
    const std::vector<double>& trans,
    const std::vector<double>& obs
) {
    std::vector<double> v;
    v.reserve(prior.size() + trans.size() + obs.size());
    v.insert(v.end(), prior.begin(), prior.end());
    v.insert(v.end(), trans.begin(), trans.end());
    v.insert(v.end(), obs.begin(), obs.end());
    return v;
}

void unpack(
    const std::vector<double>& v, int n, int p,
    std::vector<double>& prior, std::vector<double>& trans, std::vector<double>& obs
) {
    prior.assign(v.begin(), v.begin() + n);
    trans.assign(v.begin() + n, v.begin() + n + n * n);
    obs.assign(v.begin() + n + n * n, v.end());
}

// SQUAREM extrapolates in raw R^n -> negative probabilities, rows off 1.
// clamp, renormalise, re-floor, or the next E-step gets a non-model.
std::vector<double> project(
    const std::vector<double>& v, int n, int p, double min_trans
) {
    std::vector<double> prior(n), trans(n * n), obs(n * p);
    for (int i = 0; i < n; ++i) prior[i] = std::max(0.0, v[i]);
    for (int i = 0; i < n * n; ++i) trans[i] = std::max(0.0, v[n + i]);
    for (int i = 0; i < n * p; ++i) obs[i] = std::max(0.0, v[n + n * n + i]);
    row_normalize(prior, 1, n);
    row_normalize(trans, n, n);
    row_normalize(obs, n, p);
    if (min_trans > 0.0) {
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                if (i != j && trans[i * n + j] < min_trans) trans[i * n + j] = min_trans;
        row_normalize(trans, n, n);
    }
    return pack(prior, trans, obs);
}

}  // namespace

HmmModel HMM::optimize(
    const HmmModel& init,
    int max_iter, double tol, double min_trans, bool accelerate,
    bool single_precision, const HmmRestraints* restraints,
    const HmmConstraints* constraints, HmmEmissionSpec* emission
) {
    const int n = init.n_states();
    const int p = get_n_symbols();
    // SQUAREM extrapolates in the flattened (prior, trans, obs) space, which a
    // parameterised emission is not free to move in -- `obs` there is a
    // function of a handful of decay parameters, so an extrapolated table need
    // not be reachable at all.  Plain EM instead; it converges in a few maps
    // here anyway, because there is so little left to fit.
    if (emission) accelerate = false;
    const int n2 = n * n;
    const int n4 = n2 * n2;
    const int n_dt = static_cast<int>(unique_dt_.size());
    const int n_slots = std::max(n_dt, 1);
    const long long n_phot = get_n_photons();

    // The emission table has to span the alphabet the data were loaded on --
    // otherwise every read past its end is out of bounds, and a stream-only
    // model handed to a micro-time engine is exactly that mistake.
    if (n > 0 && init.n_symbols() != p)
        throw std::invalid_argument(
            "HMM::optimize: model has " + std::to_string(init.n_symbols()) +
            " emission columns but the data span " + std::to_string(p) +
            " symbols (" + std::to_string(n_streams_) + " streams x " +
            std::to_string(n_micro_bins_) + " micro-time bins)");

    // float32 round-off swamps a tight logL threshold, so raise the floor.
    if (single_precision) tol = std::max(tol, 1e-3);

    std::vector<double> prior = init.prior;
    std::vector<double> trans = init.trans;
    std::vector<double> obs = init.obs;
    row_normalize(prior, 1, n);
    row_normalize(trans, n, n);
    row_normalize(obs, n, p);

    // Caches in double (exact) or float (approximate fast mode); the float
    // buffers roughly halve the cache-build / E-step memory bandwidth.
    std::vector<double> pow_cache_d, rho_cache_d;
    std::vector<float> pow_cache_f, rho_cache_f;
    if (single_precision) {
        pow_cache_f.assign(static_cast<size_t>(n_slots) * n2, 0.0f);
        rho_cache_f.assign(static_cast<size_t>(n_slots) * n4, 0.0f);
    } else {
        pow_cache_d.assign(static_cast<size_t>(n_slots) * n2, 0.0);
        rho_cache_d.assign(static_cast<size_t>(n_slots) * n4, 0.0);
    }

    // Persistent worker pool reused across every EM map (no per-map thread spawn).
    hmm_detail::ForkJoinPool pool(worker_count(get_n_bursts()));

    // One EM map: caches from trans_, forward-backward, M-step. Returns logL(input).
    auto em_step = [&](const std::vector<double>& prior_,
                       const std::vector<double>& trans_,
                       const std::vector<double>& obs_) -> EMResult {
        std::vector<double> xi_acc(n2, 0.0);
        std::vector<double> gamma_obs_acc(static_cast<size_t>(n) * p, 0.0);
        std::vector<double> prior_acc(n, 0.0);
        double ll;
        if (single_precision) {
            if (n_dt > 0)
                fill_caches_t<float>(unique_dt_, trans_.data(), n,
                                     pow_cache_f.data(), rho_cache_f.data(), &pool);
            ll = estep_t<float>(streams_, gap_slot_, offsets_, get_n_bursts(),
                                prior_, obs_, pow_cache_f.data(), rho_cache_f.data(),
                                n, p, n_slots, xi_acc, gamma_obs_acc, prior_acc,
                                n_dt > 0, &pool);
        } else {
            if (n_dt > 0)
                fill_caches_t<double>(unique_dt_, trans_.data(), n,
                                      pow_cache_d.data(), rho_cache_d.data(), &pool);
            ll = estep_t<double>(streams_, gap_slot_, offsets_, get_n_bursts(),
                                 prior_, obs_, pow_cache_d.data(), rho_cache_d.data(),
                                 n, p, n_slots, xi_acc, gamma_obs_acc, prior_acc,
                                 n_dt > 0, &pool);
        }

        // M-step
        std::vector<double> new_prior(n), new_trans = xi_acc, new_obs = gamma_obs_acc;
        const int n_bursts = std::max(1, get_n_bursts());
        for (int i = 0; i < n; ++i) new_prior[i] = prior_acc[i] / n_bursts;
        // Order matters: restrain (add pseudo-counts), then normalise, then
        // constrain.  Imposing before normalising would rescale a pinned value.
        if (restraints) {
            restraints->add_pseudocounts(new_prior, restraints->alpha_prior());
            restraints->add_pseudocounts(new_trans, restraints->alpha_trans());
            // Not on `obs` under a parameterised emission: the M-step there
            // maximises over decay parameters from the raw counts, so
            // pseudo-counts added here would simply be discarded below.  A
            // prior on a *lifetime* belongs on the lifetime, not on the table
            // the lifetime generates.
            if (!emission)
                restraints->add_pseudocounts(new_obs, restraints->alpha_obs());
        }
        row_normalize(new_prior, 1, n);
        row_normalize(new_trans, n, n);
        if (emission) {
            // Parameterised emission: re-fit the decay parameters from the raw
            // counts instead of freeing every column.  Restraints and
            // constraints on `obs` do not apply -- the family *is* the
            // constraint, and a far stronger one: no lifetime spectrum can put
            // an exact zero in the interior of a decay, which is the degenerate
            // solution a free M-step can reach and never leave.
            new_obs = emission->fit_counts(gamma_obs_acc);
        } else {
            row_normalize(new_obs, n, p);
        }
        if (constraints) {
            HmmConstraints::impose(new_prior, 1, n, constraints->fixed_prior());
            HmmConstraints::impose(new_trans, n, n, constraints->fixed_trans());
            if (!emission)
                HmmConstraints::impose(new_obs, n, p, constraints->fixed_obs());
        }
        if (min_trans > 0.0) {
            for (int i = 0; i < n; ++i)
                for (int j = 0; j < n; ++j)
                    if (i != j && new_trans[i * n + j] < min_trans)
                        new_trans[i * n + j] = min_trans;
            // Unconditional, exactly as the unconstrained path has always done:
            // skipping it when nothing was clamped is arithmetically harmless
            // but changes the last bit, and bit-identity with plain EM is the
            // property that makes sharing one loop safe.
            row_normalize(new_trans, n, n);
            // renormalising would otherwise perturb a pinned entry
            if (constraints)
                HmmConstraints::impose(new_trans, n, n, constraints->fixed_trans());
        }
        return EMResult{std::move(new_prior), std::move(new_trans), std::move(new_obs), ll};
    };

    // Under a prior the EM fixed point belongs to the *penalised* map, so every
    // stopping and accept test below compares logL + log p, not logL.  With no
    // constraints the penalty is identically zero and this is the classic loop.
    auto penalty = [&](const std::vector<double>& pr, const std::vector<double>& tr,
                       const std::vector<double>& ob) -> double {
        return restraints ? restraints->log_prior(pr, tr, ob) : 0.0;
    };

    double last_ll = -std::numeric_limits<double>::infinity();
    // Tracked beside last_ll rather than recomputed at the end, so both always
    // describe the *same* model: EM's reported loglik is that of the iterate
    // before the final M-step, and a penalty evaluated on the final parameters
    // would silently pair a likelihood and a prior from different iterates.
    double last_obj = -std::numeric_limits<double>::infinity();
    int it = 0;
    bool converged = false;

    if (!accelerate) {
        // ---- plain Baum-Welch ----
        double prev_obj = -std::numeric_limits<double>::infinity();
        for (it = 1; it <= max_iter; ++it) {
            const double lp = penalty(prior, trans, obs);   // of the input model
            EMResult r = em_step(prior, trans, obs);
            prior = std::move(r.prior);
            trans = std::move(r.trans);
            obs = std::move(r.obs);
            last_ll = r.ll;
            const double obj = r.ll + lp;
            last_obj = obj;
            if (obj - prev_obj < tol && it > 1) { converged = true; break; }
            prev_obj = obj;
        }
    } else {
        // ---- SQUAREM (Varadhan & Roland 2008, S3) ----
        // Returns the penalised objective of the *input* model in `obj_out`, so
        // every comparison below is on the objective EM is actually climbing.
        auto em_vec = [&](const std::vector<double>& vec, double& ll_out,
                          double& obj_out) -> std::vector<double> {
            std::vector<double> pr, tr, ob;
            unpack(vec, n, p, pr, tr, ob);
            const double lp = penalty(pr, tr, ob);
            EMResult r = em_step(pr, tr, ob);
            ll_out = r.ll;
            obj_out = r.ll + lp;
            return pack(r.prior, r.trans, r.obs);
        };

        std::vector<double> theta = pack(prior, trans, obs);
        double prev_obj = -std::numeric_limits<double>::infinity();
        int evals = 0;
        while (evals < max_iter) {
            double l0, o0;
            std::vector<double> p1 = em_vec(theta, l0, o0);
            ++evals;
            if (evals >= max_iter) { theta = std::move(p1); last_ll = l0; last_obj = o0; break; }

            std::vector<double> r(theta.size());
            for (size_t i = 0; i < theta.size(); ++i) r[i] = p1[i] - theta[i];
            double l1, o1;
            std::vector<double> p2 = em_vec(p1, l1, o1);
            ++evals;
            std::vector<double> v(theta.size());
            for (size_t i = 0; i < theta.size(); ++i) v[i] = (p2[i] - p1[i]) - r[i];

            double rn = 0.0, vn = 0.0;
            for (size_t i = 0; i < r.size(); ++i) { rn += r[i] * r[i]; vn += v[i] * v[i]; }
            rn = std::sqrt(rn); vn = std::sqrt(vn);
            if (vn < 1e-12 || rn < 1e-12) {
                theta = std::move(p2); last_ll = l1; last_obj = o1;
                if (o1 - prev_obj < tol) { converged = true; break; }
                prev_obj = o1;
                continue;
            }
            double a = -rn / vn;
            if (a > -1.0) a = -1.0;
            std::vector<double> theta_e_in(theta.size());
            for (size_t i = 0; i < theta.size(); ++i)
                theta_e_in[i] = theta[i] - 2.0 * a * r[i] + (a * a) * v[i];
            std::vector<double> theta_e = project(theta_e_in, n, p, min_trans);
            if (constraints) {
                // the projection ignores pinned entries; restore them or an
                // accepted extrapolation would silently break the constraint
                std::vector<double> epr, etr, eob;
                unpack(theta_e, n, p, epr, etr, eob);
                HmmConstraints::impose(epr, 1, n, constraints->fixed_prior());
                HmmConstraints::impose(etr, n, n, constraints->fixed_trans());
                HmmConstraints::impose(eob, n, p, constraints->fixed_obs());
                theta_e = pack(epr, etr, eob);
            }
            if (evals >= max_iter) { theta = std::move(p2); last_ll = l1; last_obj = o1; break; }
            double l2, o2;
            std::vector<double> p3 = em_vec(theta_e, l2, o2);
            ++evals;
            // Accept the extrapolation only if it improves the PENALISED
            // objective: comparing marginal likelihoods here would accept steps
            // that lower the posterior and reject ones that raise it.
            double cur_obj;
            if (!std::isfinite(o2) || o2 < o1) { theta = std::move(p2); last_ll = l1; cur_obj = o1; }
            else { theta = std::move(p3); last_ll = l2; cur_obj = o2; }
            last_obj = cur_obj;
            if (cur_obj - prev_obj < tol && evals > 2) { converged = true; break; }
            prev_obj = cur_obj;
        }
        it = evals;
        unpack(theta, n, p, prior, trans, obs);
    }

    HmmModel out(prior, trans, obs);
    out.n_micro_bins = n_micro_bins_;   // the alphabet the fit ran on
    out.loglik = last_ll;
    out.logpost = last_obj;
    out.n_iter = it;
    out.n_phot = n_phot;
    out.converged = converged;
    return out;
}

// ---------------------------------------------------------------------------
// Viterbi
// ---------------------------------------------------------------------------

void HMM::viterbi(
    const HmmModel& model,
    long long** output, int* n_output,
    double* icl
) {
    const int n = model.n_states();
    const int p = get_n_symbols();
    const int n2 = n * n;
    const int n_slots = std::max<int>(static_cast<int>(unique_dt_.size()), 1);
    const long long N = get_n_photons();

    // Only the A^Δt powers: Viterbi never touches the ρ tensor, and building it
    // would cost n_slots * n^4 doubles to discard.
    std::vector<double> pow_cache;
    fill_pow_cache(model.trans, n, pow_cache);

    // log space here -- max-product has no row sum to scale by. tiny not 0:
    // -inf + -inf = NaN, which kills every comparison in the argmax below.
    // logged up front so std::log never runs inside the n^2 inner loop.
    const double tiny = std::numeric_limits<double>::min();
    std::vector<double> log_prior(n), log_obs(static_cast<size_t>(n) * p);
    for (int i = 0; i < n; ++i) log_prior[i] = std::log(std::max(model.prior[i], tiny));
    for (int i = 0; i < n * p; ++i) log_obs[i] = std::log(std::max(model.obs[i], tiny));
    std::vector<double> log_pow(static_cast<size_t>(n_slots) * n2);
    for (size_t i = 0; i < log_pow.size(); ++i)
        log_pow[i] = std::log(std::max(pow_cache[i], tiny));

    auto* path = static_cast<long long*>(malloc(sizeof(long long) * std::max<long long>(N, 1)));
    const int n_bursts = get_n_bursts();
    const int nthreads = worker_count(n_bursts);
    std::vector<double> ll_p(nthreads, 0.0);

    parallel_chunks(nthreads, n_bursts, [&](int c, int bb0, int bb1) {
      double ll_local = 0.0;
      for (int b = bb0; b < bb1; ++b) {
        const int64_t s = offsets_[b];
        const int64_t e = offsets_[b + 1];
        const int64_t m_len = e - s;
        std::vector<double> delta(static_cast<size_t>(m_len) * n);
        // psi holds back-pointer state indices in [0, n); int32 halves this
        // per-burst buffer vs int64 (n is a handful of states).
        std::vector<int32_t> psi(static_cast<size_t>(m_len) * n, 0);

        const int y0 = streams_[s];
        for (int i = 0; i < n; ++i) delta[i] = log_prior[i] + log_obs[i * p + y0];

        for (int64_t rel = 1; rel < m_len; ++rel) {
            const int64_t nn = s + rel;
            const int32_t slot = gap_slot_[nn - 1];
            const int yn = streams_[nn];
            const double* LP = (slot < 0) ? nullptr
                : log_pow.data() + static_cast<size_t>(slot) * n2;
            const double* dprev = delta.data() + (rel - 1) * n;
            double* dcur = delta.data() + rel * n;
            int32_t* pcur = psi.data() + rel * n;
            for (int j = 0; j < n; ++j) {
                double best = -std::numeric_limits<double>::infinity();
                int arg = 0;
                for (int i = 0; i < n; ++i) {
                    // coincident (slot<0): A = I -> log A[i,j] = 0 on diagonal, -inf off
                    double trans_term;
                    if (slot < 0) trans_term = (i == j) ? 0.0 : -std::numeric_limits<double>::infinity();
                    else trans_term = LP[i * n + j];
                    const double cand = dprev[i] + trans_term;
                    if (cand > best) { best = cand; arg = i; }
                }
                dcur[j] = best + log_obs[j * p + yn];
                pcur[j] = arg;
            }
        }

        double best = -std::numeric_limits<double>::infinity();
        int arg = 0;
        const double* dlast = delta.data() + (m_len - 1) * n;
        for (int i = 0; i < n; ++i)
            if (dlast[i] > best) { best = dlast[i]; arg = i; }
        path[e - 1] = arg;
        for (int64_t rel = m_len - 1; rel > 0; --rel) {
            arg = static_cast<int>(psi[rel * n + arg]);
            path[s + rel - 1] = arg;
        }
        ll_local += best;
      }
      ll_p[c] = ll_local;
    });

    double total_ll = 0.0;
    for (double v : ll_p) total_ll += v;

    // ICL not BIC: total_ll is the complete-data likelihood along the best path,
    // so this penalises states the decoder cannot separate. bic() does not.
    if (icl) {
        *icl = -2.0 * total_ll +
               model.n_free() * std::log(static_cast<double>(std::max<long long>(N, 1)));
    }
    *output = path;
    *n_output = static_cast<int>(N);
}

// ---------------------------------------------------------------------------
// Faithful decoders: γ, the marginal draw, and FFBS path sampling
// ---------------------------------------------------------------------------

template <class Sink>
static void hmm_backward_gamma(
    const std::vector<int32_t>& streams, const std::vector<int32_t>& gap_slot,
    int64_t s, int64_t m_len, const double* obs, const double* pow_cache,
    int n, int p, int n2, const double* alpha, const double* scale,
    double* w, double* beta_next, double* beta_cur, double* g, Sink&& sink
) {
    {
        const double* alast = alpha + (m_len - 1) * n;
        for (int i = 0; i < n; ++i) {
            beta_next[i] = 1.0;
            g[i] = alast[i];
        }
        sink(m_len - 1, s + m_len - 1, g);
    }
    for (int64_t li = m_len - 2; li >= 0; --li) {
        const int64_t nn = s + li;
        const int32_t slot = gap_slot[nn];
        const int yn1 = streams[nn + 1];
        const double cc = scale[li + 1];
        const double inv_c = (cc > 0.0) ? 1.0 / cc : 0.0;
        for (int k = 0; k < n; ++k) w[k] = obs[k * p + yn1] * beta_next[k];
        const double* acur_row = alpha + li * n;
        if (slot < 0) {
            for (int i = 0; i < n; ++i) beta_cur[i] = w[i] * inv_c;
        } else {
            const double* P = pow_cache + static_cast<size_t>(slot) * n2;
            for (int i = 0; i < n; ++i) {
                double v = 0.0;
                for (int k = 0; k < n; ++k) v += P[i * n + k] * w[k];
                beta_cur[i] = v * inv_c;
            }
        }
        for (int i = 0; i < n; ++i) g[i] = acur_row[i] * beta_cur[i];
        sink(li, nn, g);
        for (int i = 0; i < n; ++i) beta_next[i] = beta_cur[i];
    }
}

void HMM::posterior(
    const HmmModel& model,
    float** gamma_out, int* gamma_rows, int* gamma_cols,
    long long* n_underflow
) {
    const int n = model.n_states();
    const int p = get_n_symbols();
    const int n2 = n * n;
    const long long N = get_n_photons();

    std::vector<double> pow_cache;
    fill_pow_cache(model.trans, n, pow_cache);

    auto* gamma = static_cast<float*>(
        malloc(sizeof(float) * static_cast<size_t>(std::max<long long>(N * n, 1))));
    if (!gamma) throw std::bad_alloc();

    int64_t max_len = 0;
    const int n_bursts = get_n_bursts();
    for (int b = 0; b < n_bursts; ++b)
        max_len = std::max(max_len, offsets_[b + 1] - offsets_[b]);

    const int nthreads = worker_count(n_bursts);
    std::vector<long long> uf_p(nthreads, 0);
    const double uniform = (n > 0) ? 1.0 / n : 0.0;

    parallel_chunks(nthreads, n_bursts, [&](int c, int b0, int b1) {
        std::vector<double> alpha(static_cast<size_t>(std::max<int64_t>(max_len, 1)) * n);
        std::vector<double> scale(std::max<int64_t>(max_len, 1));
        std::vector<double> w(n), beta_next(n), beta_cur(n), g(n);
        long long uf = 0;
        for (int b = b0; b < b1; ++b) {
            const int64_t s = offsets_[b], e = offsets_[b + 1];
            const int64_t m_len = e - s;
            forward_burst<double>(streams_.data(), gap_slot_.data(), s, m_len,
                                  model.prior.data(), model.obs.data(),
                                  pow_cache.data(), n, p,
                                  alpha.data(), scale.data());
            hmm_backward_gamma(
                streams_, gap_slot_, s, m_len, model.obs.data(), pow_cache.data(),
                n, p, n2, alpha.data(), scale.data(),
                w.data(), beta_next.data(), beta_cur.data(), g.data(),
                [&](int64_t /*li*/, int64_t nn, const double* row) {
                    double tot = 0.0;
                    for (int i = 0; i < n; ++i) tot += row[i];
                    float* out = gamma + static_cast<size_t>(nn) * n;
                    if (tot > 0.0) {
                        for (int i = 0; i < n; ++i)
                            out[i] = static_cast<float>(row[i] / tot);
                    } else {
                        for (int i = 0; i < n; ++i) out[i] = static_cast<float>(uniform);
                        ++uf;
                    }
                });
        }
        uf_p[c] = uf;
    });

    if (n_underflow) {
        long long total = 0;
        for (long long v : uf_p) total += v;
        *n_underflow = total;
    }
    *gamma_out = gamma;
    *gamma_rows = static_cast<int>(N);
    *gamma_cols = n;
}

void HMM::sample_states(
    const HmmModel& model, long long seed,
    long long** output, int* n_output,
    long long* n_underflow
) {
    const int n = model.n_states();
    const int p = get_n_symbols();
    const int n2 = n * n;
    const long long N = get_n_photons();

    std::vector<double> pow_cache;
    fill_pow_cache(model.trans, n, pow_cache);

    auto* path = static_cast<long long*>(
        malloc(sizeof(long long) * static_cast<size_t>(std::max<long long>(N, 1))));
    if (!path) throw std::bad_alloc();

    int64_t max_len = 0;
    const int n_bursts = get_n_bursts();
    for (int b = 0; b < n_bursts; ++b)
        max_len = std::max(max_len, offsets_[b + 1] - offsets_[b]);

    const int nthreads = worker_count(n_bursts);
    std::vector<long long> uf_p(nthreads, 0);
    const uint64_t useed = static_cast<uint64_t>(seed);

    parallel_chunks(nthreads, n_bursts, [&](int c, int b0, int b1) {
        std::vector<double> alpha(static_cast<size_t>(std::max<int64_t>(max_len, 1)) * n);
        std::vector<double> scale(std::max<int64_t>(max_len, 1));
        std::vector<double> w(n), beta_next(n), beta_cur(n), g(n);
        long long uf = 0;
        for (int b = b0; b < b1; ++b) {
            const int64_t s = offsets_[b], e = offsets_[b + 1];
            const int64_t m_len = e - s;
            forward_burst<double>(streams_.data(), gap_slot_.data(), s, m_len,
                                  model.prior.data(), model.obs.data(),
                                  pow_cache.data(), n, p,
                                  alpha.data(), scale.data());
            hmm_backward_gamma(
                streams_, gap_slot_, s, m_len, model.obs.data(), pow_cache.data(),
                n, p, n2, alpha.data(), scale.data(),
                w.data(), beta_next.data(), beta_cur.data(), g.data(),
                [&](int64_t /*li*/, int64_t nn, const double* row) {
                    const double u = rng_unit(useed, 0, static_cast<uint64_t>(nn));
                    int st = draw_from(row, n, u);
                    if (st < 0) {
                        st = std::min(n - 1, static_cast<int>(u * n));
                        ++uf;
                    }
                    path[nn] = st;
                });
        }
        uf_p[c] = uf;
    });

    if (n_underflow) {
        long long total = 0;
        for (long long v : uf_p) total += v;
        *n_underflow = total;
    }
    *output = path;
    *n_output = static_cast<int>(N);
}

void HMM::sample_paths(
    const HmmModel& model, long long seed, int n_samples,
    long long** paths_out, int* path_rows, int* path_cols
) {
    const int n = model.n_states();
    const int p = get_n_symbols();
    const int n2 = n * n;
    const long long N = get_n_photons();
    const int draws = std::max(1, n_samples);

    std::vector<double> pow_cache;
    fill_pow_cache(model.trans, n, pow_cache);

    auto* paths = static_cast<long long*>(
        malloc(sizeof(long long) * static_cast<size_t>(std::max<long long>(N * draws, 1))));
    if (!paths) throw std::bad_alloc();

    int64_t max_len = 0;
    const int n_bursts = get_n_bursts();
    for (int b = 0; b < n_bursts; ++b)
        max_len = std::max(max_len, offsets_[b + 1] - offsets_[b]);

    const int nthreads = worker_count(n_bursts);
    const uint64_t useed = static_cast<uint64_t>(seed);

    parallel_chunks(nthreads, n_bursts, [&](int /*c*/, int b0, int b1) {
        std::vector<double> alpha(static_cast<size_t>(std::max<int64_t>(max_len, 1)) * n);
        std::vector<double> scale(std::max<int64_t>(max_len, 1));
        std::vector<double> w(n);
        for (int b = b0; b < b1; ++b) {
            const int64_t s = offsets_[b], e = offsets_[b + 1];
            const int64_t m_len = e - s;
            forward_burst<double>(streams_.data(), gap_slot_.data(), s, m_len,
                                  model.prior.data(), model.obs.data(),
                                  pow_cache.data(), n, p,
                                  alpha.data(), scale.data());
            for (int d = 0; d < draws; ++d) {
                long long* out = paths + static_cast<size_t>(d) * N;
                const uint64_t ud = static_cast<uint64_t>(d);
                const double* alast = alpha.data() + (m_len - 1) * n;
                double u = rng_unit(useed, ud, static_cast<uint64_t>(s + m_len - 1));
                int j = draw_from(alast, n, u);
                if (j < 0) j = std::min(n - 1, static_cast<int>(u * n));
                out[e - 1] = j;
                for (int64_t li = m_len - 2; li >= 0; --li) {
                    const int64_t nn = s + li;
                    const int32_t slot = gap_slot_[nn];
                    if (slot < 0) {
                        out[nn] = j;
                        continue;
                    }
                    const double* P = pow_cache.data() + static_cast<size_t>(slot) * n2;
                    const double* arow = alpha.data() + li * n;
                    for (int i = 0; i < n; ++i) w[i] = arow[i] * P[i * n + j];
                    u = rng_unit(useed, ud, static_cast<uint64_t>(nn));
                    int i = draw_from(w.data(), n, u);
                    if (i < 0) i = std::min(n - 1, static_cast<int>(u * n));
                    j = i;
                    out[nn] = j;
                }
            }
        }
    });

    *paths_out = paths;
    *path_rows = draws;
    *path_cols = static_cast<int>(N);
}

// ---------------------------------------------------------------------------
// Model init / simulation / fitting
// ---------------------------------------------------------------------------

HmmModel HMM::factory_model(int n_states, int n_symbols, double trans_scale,
                            int seed, int n_micro_bins) {
    const int n_streams = n_symbols;
    std::mt19937_64 rng(seed < 0 ? std::random_device{}() : static_cast<uint64_t>(seed));
    std::normal_distribution<double> normal(0.0, 1.0);

    std::vector<double> prior(n_states, 1.0 / n_states);
    std::vector<double> trans(static_cast<size_t>(n_states) * n_states, trans_scale);
    for (int i = 0; i < n_states; ++i)
        trans[i * n_states + i] = 1.0 - trans_scale * (n_states - 1);
    row_normalize(trans, n_states, n_states);

    std::vector<double> obs(static_cast<size_t>(n_states) * n_streams, 1.0 / n_streams);
    if (n_states > 1 && n_streams > 1) {
        for (int i = 0; i < n_states; ++i) {
            const double frac = static_cast<double>(i + 1) / (n_states + 1);
            for (int k = 0; k < n_streams; ++k) {
                double lin = (n_streams == 1) ? 1.0
                    : (1.0 - frac) + (frac - (1.0 - frac)) * k / (n_streams - 1);
                double val = lin + 0.05 * normal(rng);
                obs[i * n_streams + k] = std::max(val, 1e-3);
            }
        }
    }
    row_normalize(obs, n_states, n_streams);
    HmmModel m(prior, trans, obs);
    m.n_micro_bins = n_micro_bins > 0 ? n_micro_bins : 1;
    return m;
}

std::vector<std::vector<int>> HMM::simulate_bursts(
    const HmmModel& model,
    const std::vector<std::vector<long long>>& burst_times,
    int seed
) {
    std::mt19937_64 rng(seed < 0 ? std::random_device{}() : static_cast<uint64_t>(seed));
    const int n = model.n_states();
    const int p = model.n_symbols();

    auto sample = [&](const double* probs, int len) -> int {
        std::uniform_real_distribution<double> u(0.0, 1.0);
        double x = u(rng), acc = 0.0;
        for (int i = 0; i < len; ++i) { acc += probs[i]; if (x <= acc) return i; }
        return len - 1;
    };

    std::vector<std::vector<int>> out;
    out.reserve(burst_times.size());
    for (const auto& t : burst_times) {
        const int m = static_cast<int>(t.size());
        std::vector<int> s(m);
        int state = sample(model.prior.data(), n);
        for (int k = 0; k < m; ++k) {
            if (k > 0) {
                long long gap = t[k] - t[k - 1];
                for (long long g = 0; g < gap; ++g)
                    state = sample(model.trans.data() + state * n, n);
            }
            s[k] = sample(model.obs.data() + state * p, p);
        }
        out.push_back(std::move(s));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Blocked Gibbs
// ---------------------------------------------------------------------------

namespace {

void sample_bridge(
    const double* A, int n, int64_t dt, int u_state, int v_state,
    double* col, double* w, double* trans_cnt,
    uint64_t key, uint64_t& counter
) {
    for (int i = 0; i < n; ++i) col[i] = A[i * n + v_state];
    for (int64_t s = 1; s + 1 < dt; ++s) {
        const double* prev = col + (s - 1) * n;
        double* cur = col + s * n;
        for (int i = 0; i < n; ++i) {
            double acc = 0.0;
            for (int j = 0; j < n; ++j) acc += A[i * n + j] * prev[j];
            cur[i] = acc;
        }
    }
    int prev_state = u_state;
    for (int64_t t = 1; t < dt; ++t) {
        const double* bcol = col + (dt - t - 1) * n;
        double total = 0.0;
        for (int i = 0; i < n; ++i) {
            w[i] = A[prev_state * n + i] * bcol[i];
            total += w[i];
        }
        int s;
        if (total <= 0.0) {
            s = v_state;
        } else {
            const double target = hmm_rand::unit(key, counter++) * total;
            double acc = 0.0;
            s = n - 1;
            for (int i = 0; i < n; ++i) { acc += w[i]; if (target <= acc) { s = i; break; } }
        }
        trans_cnt[prev_state * n + s] += 1.0;
        prev_state = s;
    }
    trans_cnt[prev_state * n + v_state] += 1.0;
}

}  // namespace

HmmPosterior HMM::sample(
    const HmmModel& init, int n_draws, int n_burnin, int n_chains,
    long long seed, const HmmRestraints* restraints, int thin,
    HmmEmissionSpec* emission
) const {
    const int n = init.n_states();
    const int p = get_n_symbols();
    if (n <= 0) throw std::invalid_argument("HMM::sample: model has no states");
    if (init.n_symbols() != p)
        throw std::invalid_argument(
            "HMM::sample: model has " + std::to_string(init.n_symbols()) +
            " emission columns but the data span " + std::to_string(p) + " symbols");
    const int draws = std::max(1, n_draws);
    const int chains = std::max(1, n_chains);
    const int keep = std::max(1, thin);
    const int n_bursts = get_n_bursts();

    HmmPosterior post;
    post.n_states = n;
    post.n_symbols = p;
    post.n_chains = chains;
    post.n_par = n + n * n + n * p;
    post.draws.assign(size_t(chains) * draws * post.n_par, 0.0);
    post.loglik.assign(size_t(chains) * draws, 0.0);

    std::vector<double> a_prior(n, 1.0), a_trans(size_t(n) * n, 1.0),
                        a_obs(size_t(n) * p, 1.0);
    if (restraints) {
        a_prior = restraints->alpha_prior();
        a_trans = restraints->alpha_trans();
        a_obs = restraints->alpha_obs();
    }

    int64_t max_len = 0, max_gap = 1;
    for (int b = 0; b < n_bursts; ++b)
        max_len = std::max(max_len, offsets_[b + 1] - offsets_[b]);
    for (int64_t d : unique_dt_) max_gap = std::max(max_gap, d);

    for (int c = 0; c < chains; ++c) {
        std::vector<double> prior = init.prior, trans = init.trans, obs = init.obs;
        row_normalize(prior, 1, n);
        row_normalize(trans, n, n);
        row_normalize(obs, n, p);
        if (c > 0) {
            const double kDisperse = 20.0;
            uint64_t k = static_cast<uint64_t>(seed) ^ (uint64_t(c) * 0x9E3779B97F4A7C15ULL);
            uint64_t ctr = 0;
            std::vector<double> conc(std::max(n, p));
            const double floor_a = 1e-3;
            for (int i = 0; i < n; ++i) conc[i] = kDisperse * prior[i] + floor_a;
            hmm_rand::dirichlet(conc.data(), n, prior.data(), k, ctr);
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j)
                    conc[j] = kDisperse * init.trans[size_t(i) * n + j] + floor_a;
                hmm_rand::dirichlet(conc.data(), n, &trans[size_t(i) * n], k, ctr);
            }
            for (int i = 0; i < n; ++i) {
                for (int y = 0; y < p; ++y)
                    conc[y] = kDisperse * init.obs[size_t(i) * p + y] + floor_a;
                hmm_rand::dirichlet(conc.data(), p, &obs[size_t(i) * p], k, ctr);
            }
        }

        std::vector<double> pow_cache, alpha(size_t(std::max<int64_t>(max_len, 1)) * n),
                            scale(std::max<int64_t>(max_len, 1)), w(n);
        std::vector<double> col(size_t(max_gap) * n);
        std::vector<int> path(size_t(std::max<int64_t>(max_len, 1)));
        std::vector<double> prior_cnt(n), trans_cnt(size_t(n) * n), obs_cnt(size_t(n) * p);

        const int total_sweeps = n_burnin + draws * keep;
        int kept = 0;
        for (int sweep = 0; sweep < total_sweeps; ++sweep) {
            fill_pow_cache(trans, n, pow_cache);
            std::fill(prior_cnt.begin(), prior_cnt.end(), 0.0);
            std::fill(trans_cnt.begin(), trans_cnt.end(), 0.0);
            std::fill(obs_cnt.begin(), obs_cnt.end(), 0.0);
            double ll = 0.0;

            for (int b = 0; b < n_bursts; ++b) {
                const int64_t s0 = offsets_[b], e0 = offsets_[b + 1];
                const int64_t m_len = e0 - s0;
                if (m_len <= 0) continue;
                uint64_t key = static_cast<uint64_t>(seed)
                             ^ (uint64_t(c + 1) * 0xD1B54A32D192ED03ULL)
                             ^ (uint64_t(sweep + 1) * 0x9E3779B97F4A7C15ULL)
                             ^ (uint64_t(b + 1) * 0xC2B2AE3D27D4EB4FULL);
                uint64_t ctr = 0;

                ll += forward_burst<double>(streams_.data(), gap_slot_.data(), s0, m_len,
                                            prior.data(), obs.data(), pow_cache.data(),
                                            n, p, alpha.data(), scale.data());

                const double* alast = alpha.data() + (m_len - 1) * n;
                double u = hmm_rand::unit(key, ctr++);
                int j = draw_from(alast, n, u);
                if (j < 0) j = std::min(n - 1, int(u * n));
                path[m_len - 1] = j;
                for (int64_t li = m_len - 2; li >= 0; --li) {
                    const int32_t slot = gap_slot_[s0 + li];
                    if (slot < 0) { path[li] = j; continue; }
                    const double* P = pow_cache.data() + size_t(slot) * n * n;
                    const double* arow = alpha.data() + li * n;
                    for (int i = 0; i < n; ++i) w[i] = arow[i] * P[i * n + j];
                    u = hmm_rand::unit(key, ctr++);
                    int jj = draw_from(w.data(), n, u);
                    if (jj < 0) jj = j;
                    path[li] = jj;
                    j = jj;
                }

                prior_cnt[path[0]] += 1.0;
                for (int64_t li = 0; li < m_len; ++li)
                    obs_cnt[size_t(path[li]) * p + streams_[s0 + li]] += 1.0;

                for (int64_t li = 0; li + 1 < m_len; ++li) {
                    const int32_t slot = gap_slot_[s0 + li];
                    if (slot < 0) continue;
                    const int64_t dt = unique_dt_[slot];
                    if (dt <= 0) continue;
                    sample_bridge(trans.data(), n, dt, path[li], path[li + 1],
                                  col.data(), w.data(), trans_cnt.data(), key, ctr);
                }
            }

            uint64_t dkey = static_cast<uint64_t>(seed)
                          ^ (uint64_t(c + 1) * 0xA24BAED4963EE407ULL)
                          ^ (uint64_t(sweep + 1) * 0x9FB21C651E98DF25ULL);
            uint64_t dctr = 0;
            std::vector<double> post_a(std::max(n, p));
            for (int i = 0; i < n; ++i) post_a[i] = prior_cnt[i] + a_prior[i];
            hmm_rand::dirichlet(post_a.data(), n, prior.data(), dkey, dctr);
            for (int i = 0; i < n; ++i) {
                for (int j2 = 0; j2 < n; ++j2)
                    post_a[j2] = trans_cnt[size_t(i) * n + j2] + a_trans[size_t(i) * n + j2];
                hmm_rand::dirichlet(post_a.data(), n, &trans[size_t(i) * n], dkey, dctr);
            }
            if (emission) {
                std::vector<double> a_split;
                if (restraints) {
                    a_split.assign(size_t(n) * emission->n_streams, 1.0);
                    for (int i = 0; i < n; ++i)
                        for (int k = 0; k < emission->n_streams; ++k) {
                            double s = 0.0;
                            for (int b = 0; b < emission->n_micro_bins; ++b)
                                s += a_obs[size_t(i) * p + size_t(k) * emission->n_micro_bins + b];
                            a_split[size_t(i) * emission->n_streams + k] = s;
                        }
                }
                obs = emission->sample_counts(obs_cnt, a_split, dkey, dctr);
            } else {
                for (int i = 0; i < n; ++i) {
                    for (int y = 0; y < p; ++y)
                        post_a[y] = obs_cnt[size_t(i) * p + y] + a_obs[size_t(i) * p + y];
                    hmm_rand::dirichlet(post_a.data(), p, &obs[size_t(i) * p], dkey, dctr);
                }
            }

            if (sweep >= n_burnin && ((sweep - n_burnin) % keep) == 0 && kept < draws) {
                double* out = post.draws.data()
                            + (size_t(c) * draws + kept) * post.n_par;
                std::copy(prior.begin(), prior.end(), out);
                std::copy(trans.begin(), trans.end(), out + n);
                std::copy(obs.begin(), obs.end(), out + n + n * n);
                post.loglik[size_t(c) * draws + kept] = ll;
                ++kept;
            }
        }
    }
    return post;
}

HmmEval HMM::evaluate(const HmmModel& model) const {
    const int n = model.n_states();
    const int p = get_n_symbols();
    if (n <= 0) throw std::invalid_argument("HMM::evaluate: model has no states");
    if (model.n_symbols() != p)
        throw std::invalid_argument(
            "HMM::evaluate: model has " + std::to_string(model.n_symbols()) +
            " emission columns but the data span " + std::to_string(p) + " symbols");

    const int n2 = n * n, n4 = n2 * n2;
    const int n_dt = static_cast<int>(unique_dt_.size());
    const int n_slots = std::max(n_dt, 1);

    std::vector<double> pow_cache(static_cast<size_t>(n_slots) * n2, 0.0);
    std::vector<double> rho_cache(static_cast<size_t>(n_slots) * n4, 0.0);
    hmm_detail::ForkJoinPool pool(worker_count(get_n_bursts()));

    HmmEval out;
    out.xi.assign(n2, 0.0);
    out.gamma_obs.assign(static_cast<size_t>(n) * p, 0.0);
    out.prior_counts.assign(n, 0.0);

    if (n_dt > 0)
        fill_caches_t<double>(unique_dt_, model.trans.data(), n,
                              pow_cache.data(), rho_cache.data(), &pool);
    out.loglik = estep_t<double>(
        streams_, gap_slot_, offsets_, get_n_bursts(),
        model.prior, model.obs, pow_cache.data(), rho_cache.data(),
        n, p, n_slots, out.xi, out.gamma_obs, out.prior_counts, n_dt > 0, &pool);

    out.score.assign(size_t(n) + n2 + size_t(n) * p, 0.0);
    size_t o = 0;
    for (int i = 0; i < n; ++i, ++o)
        if (model.prior[i] > 0.0) out.score[o] = out.prior_counts[i] / model.prior[i];

    for (int i = 0; i < n2; ++i, ++o)
        if (model.trans[i] > 0.0) out.score[o] = out.xi[i] / model.trans[i];
    for (size_t i = 0; i < out.gamma_obs.size(); ++i, ++o)
        if (model.obs[i] > 0.0) out.score[o] = out.gamma_obs[i] / model.obs[i];
    return out;
}

HmmModel HMM::fit(
    int n_states, int n_restarts, int max_iter, double tol, int seed,
    bool accelerate, bool single_precision
) {
    HmmModel best;
    bool have_best = false;
    const int restarts = std::max(1, n_restarts);
    for (int r = 0; r < restarts; ++r) {
        HmmModel init = factory_model(n_states, get_n_symbols(),
                                       1e-4, seed < 0 ? -1 : seed + r,
                                       n_micro_bins_);
        HmmModel fitr = optimize(init, max_iter, tol, 1e-12, accelerate, single_precision);
        if (!have_best || fitr.loglik > best.loglik) { best = fitr; have_best = true; }
    }
    return best;
}

} // namespace tttrlib
