// SPDX-License-Identifier: BSD-3-Clause
#include "H2MM.h"
#include "Channel.h"
#include "BurstFilter.h"

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

namespace h2mm_detail {

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

}  // namespace h2mm_detail

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

/// Rescale every row of an (rows x cols) row-major matrix to sum to 1.
/// Zero rows become uniform.
void row_normalize(double* a, int rows, int cols) {
    for (int i = 0; i < rows; ++i) {
        double s = 0.0;
        for (int j = 0; j < cols; ++j) s += a[i * cols + j];
        if (s > 0.0) {
            for (int j = 0; j < cols; ++j) a[i * cols + j] /= s;
        } else {
            for (int j = 0; j < cols; ++j) a[i * cols + j] = 1.0 / cols;
        }
    }
}

void row_normalize(std::vector<double>& a, int rows, int cols) {
    row_normalize(a.data(), rows, cols);
}

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
        if (s > 0.0) {
            for (int j = 0; j < n; ++j) out[i * n + j] = static_cast<R>(out[i * n + j] / s);
        }
    }
}

// ρ tensor indexing: R[((k*n + m)*n + i)*n + j]  (order k,m,i,j)
inline int rho_idx(int k, int m, int i, int j, int n) {
    return ((k * n + m) * n + i) * n + j;
}

/// ρ(1)[k,m,i,j] = δ_{k,i}·A[i,j]·δ_{j,m}
template <class R>
void rho_base(const R* A, R* Rt, int n) {
    std::fill(Rt, Rt + n * n * n * n, R(0));
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
    R* pow_cache, R* rho_cache, h2mm_detail::ForkJoinPool* pool
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
    std::vector<double>& prior_acc, bool have_dt, h2mm_detail::ForkJoinPool* pool
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

            // forward
            {
                const int y0 = streams[s];
                double tot = 0.0;
                for (int i = 0; i < n; ++i) {
                    double a0 = prior[i] * obs[i * p + y0];
                    alpha[i] = a0;
                    tot += a0;
                }
                scale[0] = tot;
                if (tot > 0.0) {
                    for (int i = 0; i < n; ++i) alpha[i] /= tot;
                    ll_local += std::log(tot);
                }
            }
            for (int64_t li = 1; li < m_len; ++li) {
                const int64_t nn = s + li;
                const int32_t slot = gap_slot[nn - 1];
                const int yn = streams[nn];
                const R* P = pow_cache + static_cast<size_t>(slot < 0 ? 0 : slot) * n2;
                const double* aprev = alpha.data() + (li - 1) * n;
                double* acur = alpha.data() + li * n;
                double tot = 0.0;
                if (slot < 0) {
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
                    ll_local += std::log(tot);
                }
            }

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

    double loglik = 0.0;
    for (int c = 0; c < nthreads; ++c) {
        loglik += ll_p[c];
        for (int i = 0; i < n; ++i) {
            prior_acc[i] += prior_p[c][i];
            for (int k = 0; k < p; ++k)
                gamma_obs_acc[i * p + k] += gobs_p[c][i * p + k];
        }
    }
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

void H2mmModel::normalize() {
    int n = n_states(), p = n_streams();
    if (n <= 0) return;
    row_normalize(prior, 1, n);
    row_normalize(trans, n, n);
    row_normalize(obs, n, p);
}

// ---------------------------------------------------------------------------
// Data preparation (CSR layout + unique-Δt table)
// ---------------------------------------------------------------------------

void H2MM::set_bursts(
    const std::vector<std::vector<long long>>& times,
    const std::vector<std::vector<int>>& streams,
    int n_streams
) {
    if (times.size() != streams.size())
        throw std::invalid_argument("times and streams must have equal burst count");

    n_streams_ = n_streams;
    streams_.clear();
    gap_slot_.clear();
    offsets_.clear();
    unique_dt_.clear();

    // Keep only non-empty bursts; build offsets and concatenated streams.
    std::vector<const std::vector<long long>*> kept_times;
    std::vector<const std::vector<int>*> kept_streams;
    for (size_t b = 0; b < times.size(); ++b) {
        if (times[b].size() != streams[b].size())
            throw std::invalid_argument("each burst needs equal-length times and streams");
        if (times[b].empty()) continue;
        kept_times.push_back(&times[b]);
        kept_streams.push_back(&streams[b]);
    }

    // Total photons is known from the kept bursts; reserve once so the flat CSR
    // stream/gap arrays and the Δt scratch do not repeatedly reallocate.
    size_t total_photons = 0;
    for (const auto* sp : kept_streams) total_photons += sp->size();

    offsets_.push_back(0);
    offsets_.reserve(kept_times.size() + 1);
    streams_.reserve(total_photons);
    for (size_t b = 0; b < kept_times.size(); ++b) {
        const auto& s = *kept_streams[b];
        for (int v : s) streams_.push_back(static_cast<int32_t>(v));
        offsets_.push_back(static_cast<int64_t>(streams_.size()));
    }

    // Unique inter-photon Δt (>0) across all bursts.
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

std::vector<long long> H2MM::get_unique_dt() const {
    return std::vector<long long>(unique_dt_.begin(), unique_dt_.end());
}

void H2MM::set_bursts_from_tttr(
    std::shared_ptr<TTTR> tttr,
    long long* bursts, int n_bursts, int n_cols,
    const std::vector<std::shared_ptr<Channel>>& stream_channels,
    int min_photons,
    long long time_scale
) {
    if (!tttr) throw std::invalid_argument("set_bursts_from_tttr: null TTTR");
    if (stream_channels.empty())
        throw std::invalid_argument("set_bursts_from_tttr: no stream definitions");
    const long long ts = std::max<long long>(1, time_scale);
    const int64_t n_total = static_cast<int64_t>(tttr->size());

    // Pre-extract each stream's (routing_channel, mt_start, mt_stop) components.
    std::vector<std::vector<std::tuple<int, int, int>>> comps(stream_channels.size());
    for (size_t si = 0; si < stream_channels.size(); ++si) {
        if (stream_channels[si]) comps[si] = stream_channels[si]->get_components();
    }

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
    // bursts is an (n_bursts, 2) [start, stop] array (row-major).
    const size_t n_pairs = (bursts == nullptr || n_bursts < 1 || n_cols != 2)
        ? 0 : static_cast<size_t>(n_bursts);
    for (size_t b = 0; b < n_pairs; ++b) {
        int64_t s = bursts[2 * b], e = bursts[2 * b + 1];
        if (s < 0) s = 0;
        if (e > n_total) e = n_total;
        std::vector<long long> bt;
        std::vector<int> bs;
        long long last_t = std::numeric_limits<long long>::min();
        for (int64_t idx = s; idx < e; ++idx) {
            const int ch = static_cast<int>(tttr->get_routing_channel_at(idx));
            const int mt = static_cast<int>(tttr->get_micro_time_at(idx));
            const int stream = match_stream(ch, mt);
            if (stream < 0) continue;
            long long t = static_cast<long long>(tttr->get_macro_time_at(idx)) / ts;
            if (t < last_t) t = last_t;  // enforce monotonic non-decreasing
            last_t = t;
            bt.push_back(t);
            bs.push_back(stream);
        }
        if (static_cast<int>(bt.size()) >= min_photons) {
            times.push_back(std::move(bt));
            strms.push_back(std::move(bs));
        }
    }
    set_bursts(times, strms, static_cast<int>(stream_channels.size()));
}

void H2MM::set_bursts_from_filter(
    std::shared_ptr<BurstFilter> burst_filter,
    const std::vector<std::shared_ptr<Channel>>& stream_channels,
    int min_photons,
    long long time_scale
) {
    if (!burst_filter) throw std::invalid_argument("set_bursts_from_filter: null BurstFilter");
    // get_burst_indices() is vector<int64_t>; on LP64 Linux that is a distinct
    // type from long long, so copy into the public pointer/length signature.
    std::vector<long long> b(burst_filter->get_burst_indices().begin(),
                             burst_filter->get_burst_indices().end());
    set_bursts_from_tttr(burst_filter->get_tttr(),
                         b.data(), static_cast<int>(b.size() / 2), 2,
                         stream_channels, min_photons, time_scale);
}

// ---------------------------------------------------------------------------
// Caches
// ---------------------------------------------------------------------------

void H2MM::fill_caches(
    const std::vector<double>& A, int n,
    std::vector<double>& pow_cache, std::vector<double>& rho_cache,
    h2mm_detail::ForkJoinPool* pool
) const {
    fill_caches_t<double>(unique_dt_, A.data(), n,
                          pow_cache.data(), rho_cache.data(), pool);
}

// ---------------------------------------------------------------------------
// E-step: scaled forward-backward + Baum-Welch accumulation
// ---------------------------------------------------------------------------

double H2MM::estep(
    const std::vector<double>& prior,
    const std::vector<double>& obs,
    const std::vector<double>& pow_cache,
    const std::vector<double>& rho_cache,
    int n, int p,
    std::vector<double>& xi_acc,
    std::vector<double>& gamma_obs_acc,
    std::vector<double>& prior_acc,
    h2mm_detail::ForkJoinPool* pool
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

// Project an extrapolated parameter vector back onto the feasible model set.
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

H2mmModel H2MM::optimize(
    const H2mmModel& init,
    int max_iter, double tol, double min_trans, bool accelerate,
    bool single_precision
) {
    const int n = init.n_states();
    const int p = n_streams_;
    const int n2 = n * n;
    const int n4 = n2 * n2;
    const int n_dt = static_cast<int>(unique_dt_.size());
    const int n_slots = std::max(n_dt, 1);
    const long long n_phot = get_n_photons();

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
    h2mm_detail::ForkJoinPool pool(worker_count(get_n_bursts()));

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
        row_normalize(new_prior, 1, n);
        row_normalize(new_trans, n, n);
        row_normalize(new_obs, n, p);
        if (min_trans > 0.0) {
            for (int i = 0; i < n; ++i)
                for (int j = 0; j < n; ++j)
                    if (i != j && new_trans[i * n + j] < min_trans)
                        new_trans[i * n + j] = min_trans;
            row_normalize(new_trans, n, n);
        }
        return EMResult{std::move(new_prior), std::move(new_trans), std::move(new_obs), ll};
    };

    double last_ll = -std::numeric_limits<double>::infinity();
    int it = 0;
    bool converged = false;

    if (!accelerate) {
        // ---- plain Baum-Welch ----
        double prev_ll = -std::numeric_limits<double>::infinity();
        for (it = 1; it <= max_iter; ++it) {
            EMResult r = em_step(prior, trans, obs);
            prior = std::move(r.prior);
            trans = std::move(r.trans);
            obs = std::move(r.obs);
            last_ll = r.ll;
            if (last_ll - prev_ll < tol && it > 1) { converged = true; prev_ll = last_ll; break; }
            prev_ll = last_ll;
        }
    } else {
        // ---- SQUAREM (Varadhan & Roland 2008, S3) ----
        auto em_vec = [&](const std::vector<double>& vec, double& ll_out) -> std::vector<double> {
            std::vector<double> pr, tr, ob;
            unpack(vec, n, p, pr, tr, ob);
            EMResult r = em_step(pr, tr, ob);
            ll_out = r.ll;
            return pack(r.prior, r.trans, r.obs);
        };

        std::vector<double> theta = pack(prior, trans, obs);
        double prev_ll = -std::numeric_limits<double>::infinity();
        int evals = 0;
        while (evals < max_iter) {
            double l0;
            std::vector<double> p1 = em_vec(theta, l0);
            ++evals;
            if (evals >= max_iter) { theta = std::move(p1); last_ll = l0; break; }

            std::vector<double> r(theta.size());
            for (size_t i = 0; i < theta.size(); ++i) r[i] = p1[i] - theta[i];
            double l1;
            std::vector<double> p2 = em_vec(p1, l1);
            ++evals;
            std::vector<double> v(theta.size());
            for (size_t i = 0; i < theta.size(); ++i) v[i] = (p2[i] - p1[i]) - r[i];

            double rn = 0.0, vn = 0.0;
            for (size_t i = 0; i < r.size(); ++i) { rn += r[i] * r[i]; vn += v[i] * v[i]; }
            rn = std::sqrt(rn); vn = std::sqrt(vn);
            if (vn < 1e-12 || rn < 1e-12) {
                theta = std::move(p2); last_ll = l1;
                if (l1 - prev_ll < tol) { converged = true; break; }
                prev_ll = l1;
                continue;
            }
            double a = -rn / vn;
            if (a > -1.0) a = -1.0;
            std::vector<double> theta_e_in(theta.size());
            for (size_t i = 0; i < theta.size(); ++i)
                theta_e_in[i] = theta[i] - 2.0 * a * r[i] + (a * a) * v[i];
            std::vector<double> theta_e = project(theta_e_in, n, p, min_trans);
            if (evals >= max_iter) { theta = std::move(p2); last_ll = l1; break; }
            double l2;
            std::vector<double> p3 = em_vec(theta_e, l2);
            ++evals;
            if (!std::isfinite(l2) || l2 < l1) { theta = std::move(p2); last_ll = l1; }
            else { theta = std::move(p3); last_ll = l2; }
            if (last_ll - prev_ll < tol && evals > 2) { converged = true; break; }
            prev_ll = last_ll;
        }
        it = evals;
        unpack(theta, n, p, prior, trans, obs);
    }

    H2mmModel out(prior, trans, obs);
    out.loglik = last_ll;
    out.n_iter = it;
    out.n_phot = n_phot;
    out.converged = converged;
    return out;
}

// ---------------------------------------------------------------------------
// Viterbi
// ---------------------------------------------------------------------------

void H2MM::viterbi(
    const H2mmModel& model,
    long long** output, int* n_output,
    double* icl
) {
    const int n = model.n_states();
    const int p = n_streams_;
    const int n2 = n * n;
    const int n4 = n2 * n2;
    const int n_dt = static_cast<int>(unique_dt_.size());
    const int n_slots = std::max(n_dt, 1);
    const long long N = get_n_photons();

    std::vector<double> pow_cache(static_cast<size_t>(n_slots) * n2, 0.0);
    std::vector<double> rho_cache(1, 0.0);  // not used by Viterbi
    if (n_dt > 0) {
        // Build only the A^Δt powers (ρ unused): reuse fill_caches then ignore ρ.
        rho_cache.assign(static_cast<size_t>(n_slots) * n4, 0.0);
        fill_caches(model.trans, n, pow_cache, rho_cache);
    }

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

    if (icl) {
        *icl = -2.0 * total_ll +
               model.n_free() * std::log(static_cast<double>(std::max<long long>(N, 1)));
    }
    *output = path;
    *n_output = static_cast<int>(N);
}

// ---------------------------------------------------------------------------
// Model init / simulation / fitting
// ---------------------------------------------------------------------------

H2mmModel H2MM::factory_model(int n_states, int n_streams, double trans_scale, int seed) {
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
    return H2mmModel(prior, trans, obs);
}

std::vector<std::vector<int>> H2MM::simulate_bursts(
    const H2mmModel& model,
    const std::vector<std::vector<long long>>& burst_times,
    int seed
) {
    std::mt19937_64 rng(seed < 0 ? std::random_device{}() : static_cast<uint64_t>(seed));
    const int n = model.n_states();
    const int p = model.n_streams();

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

H2mmModel H2MM::fit(
    int n_states, int n_restarts, int max_iter, double tol, int seed,
    bool accelerate, bool single_precision
) {
    H2mmModel best;
    bool have_best = false;
    const int restarts = std::max(1, n_restarts);
    for (int r = 0; r < restarts; ++r) {
        H2mmModel init = factory_model(n_states, n_streams_,
                                       1e-4, seed < 0 ? -1 : seed + r);
        H2mmModel fitr = optimize(init, max_iter, tol, 1e-12, accelerate, single_precision);
        if (!have_best || fitr.loglik > best.loglik) { best = fitr; have_best = true; }
    }
    return best;
}

} // namespace tttrlib
