// SPDX-License-Identifier: BSD-3-Clause
#include "H2MMSurrogate.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>

#include "SimPcgRandom.h"
#include "nlohmann/json.hpp"

// Strict multiply-then-add rounding, for the whole file.
//
// The feature extractor must reproduce the NumPy/numba reference bit for bit so
// that a surrogate trained in Python gives identical answers here. Fusing
// ``wsum += streams[k] * scale`` into an FMA changes the sliding-window sum by
// ~1e-16 — which is enough to move a windowed value across a histogram bin edge
// and shift a whole count into the neighbouring bin. Measured: 33 of 200 photons
// differed with contraction on, 0 with it off. The loop is memory-bound, so
// giving up FMA costs nothing.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

namespace tttrlib {

namespace {

using json = nlohmann::json;

/// Photon-lag offsets of the emission autocorrelation feature.
const long long kAcLags[] = {1, 2, 4, 8, 16, 32};
const int kNAcLags = 6;
/// Half-width of the sliding local-FRET window (in photons).
const int kWindow = 12;
/// Histogram bins over the local-FRET range [0, 1].
const int kNBins = 10;
/// Quantiles of the local-FRET distribution.
const double kQuantiles[] = {0.1, 0.25, 0.5, 0.75, 0.9};
const int kNQuantiles = 5;
/// Transition probabilities below this are stored as this in log10 space.
const double kLogFloor = 1e-6;

/**
 * @brief ``numpy.quantile(a, q, method="linear")`` on an already-sorted array.
 *
 * NumPy places the quantile at the *virtual* index ``q*(n-1)`` and interpolates
 * linearly between its neighbours; reproducing that exactly matters because the
 * surrogate's features must match the Python implementation bit for bit.
 */
double quantile_sorted(const std::vector<double>& sorted, double q) {
    const size_t n = sorted.size();
    if (n == 0) return 0.0;
    if (n == 1) return sorted[0];
    const double idx = q * static_cast<double>(n - 1);
    const double lo = std::floor(idx);
    const double hi = std::ceil(idx);
    const double frac = idx - lo;
    const double a = sorted[static_cast<size_t>(lo)];
    const double b = sorted[static_cast<size_t>(hi)];
    return a + frac * (b - a);
}

/// Row-normalise ``a`` (``n x m``, row-major); a zero row becomes uniform.
void row_normalize(std::vector<double>& a, int n, int m) {
    for (int i = 0; i < n; ++i) {
        double s = 0.0;
        for (int j = 0; j < m; ++j) s += a[i * m + j];
        if (s > 0.0) {
            for (int j = 0; j < m; ++j) a[i * m + j] /= s;
        } else {
            for (int j = 0; j < m; ++j) a[i * m + j] = 1.0 / m;
        }
    }
}

/// Reorder states by descending stream-0 emission, making labels canonical.
H2mmModel canonical_order(const H2mmModel& model) {
    const int n = model.n_states();
    const int p = model.n_streams();
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    // Stable, so ties keep their original order; NumPy's argsort on the
    // distinct emission values these models carry gives the same permutation.
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return model.obs[a * p] > model.obs[b * p]; });

    H2mmModel out;
    out.prior.resize(n);
    out.trans.resize(static_cast<size_t>(n) * n);
    out.obs.resize(static_cast<size_t>(n) * p);
    for (int i = 0; i < n; ++i) {
        out.prior[i] = model.prior[order[i]];
        for (int j = 0; j < n; ++j) out.trans[i * n + j] = model.trans[order[i] * n + order[j]];
        for (int k = 0; k < p; ++k) out.obs[i * p + k] = model.obs[order[i] * p + k];
    }
    out.loglik = model.loglik;
    out.n_iter = model.n_iter;
    out.n_phot = model.n_phot;
    out.converged = model.converged;
    return out;
}

/// ``C = A * B`` for row-major ``n x n`` matrices.
std::vector<double> mat_mul(const std::vector<double>& A, const std::vector<double>& B, int n) {
    std::vector<double> C(static_cast<size_t>(n) * n, 0.0);
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < n; ++k) {
            const double a = A[i * n + k];
            if (a == 0.0) continue;
            for (int j = 0; j < n; ++j) C[i * n + j] += a * B[k * n + j];
        }
    return C;
}

/// @f$ A^e @f$ by binary exponentiation.
std::vector<double> mat_pow(const std::vector<double>& A, long long e, int n) {
    std::vector<double> result(static_cast<size_t>(n) * n, 0.0);
    for (int i = 0; i < n; ++i) result[i * n + i] = 1.0;
    std::vector<double> base = A;
    while (e > 0) {
        if (e & 1LL) result = mat_mul(result, base, n);
        e >>= 1;
        if (e) base = mat_mul(base, base, n);
    }
    return result;
}

/// Draw a random, well-ordered model over a realistic FRET/kinetics range.
H2mmModel random_model(int n, int p, SimPcgRandom& rng) {
    std::vector<double> fret(n);
    for (int i = 0; i < n; ++i) fret[i] = 0.08 + rng.random0i1e() * (0.92 - 0.08);
    std::sort(fret.begin(), fret.end());

    std::vector<double> obs(static_cast<size_t>(n) * p);
    if (p == 2) {
        for (int i = 0; i < n; ++i) {
            obs[i * p + 0] = 1.0 - fret[i];
            obs[i * p + 1] = fret[i];
        }
    } else {
        // Dirichlet(1,...,1) == uniform on the simplex, sampled via exponentials.
        for (int i = 0; i < n; ++i) {
            double s = 0.0;
            for (int k = 0; k < p; ++k) {
                const double u = std::max(rng.random0e1e(), 1e-300);
                obs[i * p + k] = -std::log(u);
                s += obs[i * p + k];
            }
            for (int k = 0; k < p; ++k) obs[i * p + k] /= s;
        }
    }

    std::vector<double> trans(static_cast<size_t>(n) * n, 0.0);
    for (int i = 0; i < n; ++i) {
        double off = 0.0;
        for (int j = 0; j < n; ++j) {
            if (i == j) continue;
            trans[i * n + j] = std::pow(10.0, -2.8 + rng.random0i1e() * (-1.3 + 2.8));
            off += trans[i * n + j];
        }
        trans[i * n + i] = 1.0 - off;
    }

    std::vector<double> prior(n, 1.0 / n);
    row_normalize(trans, n, n);
    row_normalize(obs, n, p);
    return H2mmModel(std::move(prior), std::move(trans), std::move(obs));
}

/// Index of the first element of ``cum`` strictly greater than ``u``.
int searchsorted(const double* cum, int n, double u) {
    int lo = 0, hi = n;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (cum[mid] < u) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction / validation
// ---------------------------------------------------------------------------

H2mmSurrogate::H2mmSurrogate(NeuralNet net, int n_states, int n_streams, int features_version)
    : net_(std::move(net)),
      n_states_(n_states),
      n_streams_(n_streams),
      features_version_(features_version) {
    if (n_states_ < 1 || n_streams_ < 1)
        throw std::runtime_error("H2mmSurrogate: n_states and n_streams must be positive");
    if (net_.n_inputs() != N_FEATURES)
        throw std::runtime_error(
            "H2mmSurrogate: net takes " + std::to_string(net_.n_inputs()) +
            " inputs, expected " + std::to_string(N_FEATURES));
    const int want = n_targets(n_states_, n_streams_);
    if (net_.n_outputs() != want)
        throw std::runtime_error(
            "H2mmSurrogate: net produces " + std::to_string(net_.n_outputs()) +
            " outputs, expected " + std::to_string(want) + " for n_states=" +
            std::to_string(n_states_) + ", n_streams=" + std::to_string(n_streams_));
}

int H2mmSurrogate::n_targets(int n, int p) { return n * p + n * (n - 1) + n; }

// ---------------------------------------------------------------------------
// Feature extraction
// ---------------------------------------------------------------------------

std::vector<double> H2mmSurrogate::extract_features(const H2MM& data) {
    const std::vector<int32_t>& streams = data.get_streams();
    const std::vector<int32_t>& gap_slot = data.get_gap_slot();
    const std::vector<int64_t>& offsets = data.get_offsets();
    const std::vector<long long> unique_dt = data.get_unique_dt();
    const int p = data.get_n_streams();

    const size_t n = streams.size();
    const double scale = 1.0 / std::max(p - 1, 1);

    // --- mean per-photon FRET signal
    double mu = 0.0;
    if (n > 0) {
        double tot = 0.0;
        for (size_t i = 0; i < n; ++i) tot += streams[i] * scale;
        mu = tot / static_cast<double>(n);
    }

    // --- windowed local FRET, via a sliding two-pointer sum (O(N), not O(N*win))
    std::vector<double> loc(n, 0.0);
    const size_t n_bursts = offsets.empty() ? 0 : offsets.size() - 1;
    for (size_t b = 0; b < n_bursts; ++b) {
        const int64_t s = offsets[b], e = offsets[b + 1];
        int64_t a = s;
        int64_t c = (s + kWindow + 1 > e) ? e : s + kWindow + 1;
        double wsum = 0.0;
        for (int64_t k = a; k < c; ++k) wsum += streams[k] * scale;
        for (int64_t j = s; j < e; ++j) {
            const int64_t na = (j - kWindow < s) ? s : j - kWindow;
            const int64_t nc = (j + kWindow + 1 > e) ? e : j + kWindow + 1;
            while (a < na) { wsum -= streams[a] * scale; ++a; }
            while (c < nc) { wsum += streams[c] * scale; ++c; }
            loc[j] = wsum / static_cast<double>(c - a);
        }
    }

    // --- photon-lag autocorrelation (per-burst mean, averaged over bursts)
    std::vector<double> ac(kNAcLags, 0.0);
    for (int li = 0; li < kNAcLags; ++li) {
        const long long lag = kAcLags[li];
        double num = 0.0;
        long long cnt = 0;
        for (size_t b = 0; b < n_bursts; ++b) {
            const int64_t s = offsets[b], e = offsets[b + 1];
            const int64_t m = e - s;
            if (m > lag) {
                double ss = 0.0;
                for (int64_t j = s; j < e - lag; ++j)
                    ss += (streams[j] * scale - mu) * (streams[j + lag] * scale - mu);
                num += ss / static_cast<double>(m - lag);
                ++cnt;
            }
        }
        ac[li] = cnt > 0 ? num / static_cast<double>(cnt) : 0.0;
    }

    std::vector<double> feats;
    feats.reserve(N_FEATURES);
    feats.push_back(mu);

    // --- density-normalised histogram over [0, 1]
    //
    // This reproduces numpy.histogram's uniform-bin path exactly, which is
    // finicky enough to be worth spelling out: the bin index is computed as
    // ``(v-lo)/(hi-lo) * nbins`` (NOT ``(v-lo)/width`` — the two round
    // differently and disagree on values that land on a bin edge), and is then
    // *corrected* against the edges linspace actually produced. Density
    // normalisation divides by the count of in-range samples, not by n.
    {
        const double lo = 0.0, hi = 1.0;
        const double width = (hi - lo) / kNBins;

        // Edges as numpy.linspace(lo, hi, kNBins+1) computes them: lo + i*step,
        // with the last edge forced to exactly hi.
        std::vector<double> edges(kNBins + 1);
        for (int i = 0; i <= kNBins; ++i) edges[i] = lo + i * width;
        edges[kNBins] = hi;

        std::vector<double> counts(kNBins, 0.0);
        double in_range = 0.0;
        for (size_t i = 0; i < n; ++i) {
            const double v = loc[i];
            if (v < lo || v > hi) continue;   // numpy drops out-of-range samples
            int bin = static_cast<int>((v - lo) / (hi - lo) * kNBins);
            if (bin == kNBins) --bin;
            // Correct for floating-point error in the index above.
            if (v < edges[bin]) --bin;
            if (bin != kNBins - 1 && v >= edges[bin + 1]) ++bin;
            counts[bin] += 1.0;
            in_range += 1.0;
        }
        // numpy evaluates this as (n/db)/n.sum(); the other grouping,
        // n/(n.sum()*db), can differ in the last bit.
        for (int i = 0; i < kNBins; ++i)
            feats.push_back(in_range > 0.0 ? (counts[i] / width) / in_range : 0.0);
    }

    // --- quantiles of the local-FRET distribution
    {
        std::vector<double> sorted = loc;
        std::sort(sorted.begin(), sorted.end());
        for (int i = 0; i < kNQuantiles; ++i)
            feats.push_back(quantile_sorted(sorted, kQuantiles[i]));
    }

    for (int i = 0; i < kNAcLags; ++i) feats.push_back(ac[i]);

    // --- inter-photon gap statistics (population std, matching NumPy's default)
    {
        double sum = 0.0, sumsq = 0.0;
        long long cnt = 0;
        if (!unique_dt.empty()) {
            for (size_t i = 0; i < gap_slot.size(); ++i) {
                if (gap_slot[i] < 0) continue;
                const double v = static_cast<double>(unique_dt[gap_slot[i]]);
                sum += v;
                sumsq += v * v;
                ++cnt;
            }
        }
        if (cnt > 0) {
            const double mean = sum / static_cast<double>(cnt);
            const double var = std::max(sumsq / static_cast<double>(cnt) - mean * mean, 0.0);
            feats.push_back(mean);
            feats.push_back(std::sqrt(var));
        } else {
            // Python falls back to a single zero when no gaps exist.
            feats.push_back(0.0);
            feats.push_back(0.0);
        }
    }

    return feats;
}

// ---------------------------------------------------------------------------
// Encode / decode
// ---------------------------------------------------------------------------

std::vector<double> H2mmSurrogate::encode(const H2mmModel& model) {
    const H2mmModel m = canonical_order(model);
    const int n = m.n_states(), p = m.n_streams();
    std::vector<double> out;
    out.reserve(n_targets(n, p));
    for (int i = 0; i < n * p; ++i) out.push_back(m.obs[i]);
    // Off-diagonal transitions span orders of magnitude, so they are regressed
    // in log10 space; a linear target would effectively ignore them.
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            if (i != j) out.push_back(std::log10(std::max(m.trans[i * n + j], kLogFloor)));
    for (int i = 0; i < n; ++i) out.push_back(m.prior[i]);
    return out;
}

H2mmModel H2mmSurrogate::decode(const std::vector<double>& vec, int n, int p) {
    if (static_cast<int>(vec.size()) != n_targets(n, p))
        throw std::runtime_error("H2mmSurrogate::decode: vector has " +
                                 std::to_string(vec.size()) + " entries, expected " +
                                 std::to_string(n_targets(n, p)));
    size_t k = 0;

    std::vector<double> obs(static_cast<size_t>(n) * p);
    for (int i = 0; i < n * p; ++i) obs[i] = std::max(vec[k + i], 1e-6);
    k += static_cast<size_t>(n) * p;
    row_normalize(obs, n, p);

    std::vector<double> trans(static_cast<size_t>(n) * n, 0.0);
    for (int i = 0; i < n; ++i) trans[i * n + i] = 1.0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            if (i != j) trans[i * n + j] = std::pow(10.0, vec[k++]);
    for (int i = 0; i < n; ++i) {
        double off = 0.0;
        for (int j = 0; j < n; ++j)
            if (i != j) off += trans[i * n + j];
        trans[i * n + i] = std::max(1.0 - off, 1e-6);
    }
    row_normalize(trans, n, n);

    std::vector<double> prior(n);
    for (int i = 0; i < n; ++i) prior[i] = std::max(vec[k + i], 1e-9);
    row_normalize(prior, 1, n);

    return canonical_order(H2mmModel(std::move(prior), std::move(trans), std::move(obs)));
}

// ---------------------------------------------------------------------------
// Prediction
// ---------------------------------------------------------------------------

H2mmModel H2mmSurrogate::predict(const H2MM& data) const {
    if (data.get_n_streams() != n_streams_)
        throw std::runtime_error(
            "H2mmSurrogate: trained for n_streams=" + std::to_string(n_streams_) +
            ", got " + std::to_string(data.get_n_streams()));
    const std::vector<double> feats = extract_features(data);
    const std::vector<double> y = net_.predict(feats);
    H2mmModel out = decode(y, n_states_, n_streams_);
    out.n_phot = data.get_n_photons();
    return out;
}

// ---------------------------------------------------------------------------
// Training-set generation and training
// ---------------------------------------------------------------------------

void H2mmSurrogate::generate_training_set(
    int n_states, int n_streams, int n_samples,
    int n_bursts, int burst_len, double mean_dt, int seed,
    std::vector<double>& X, std::vector<double>& Y
) {
    if (n_samples <= 0 || n_bursts <= 0 || burst_len < 2)
        throw std::runtime_error("H2mmSurrogate::generate_training_set: degenerate problem size");

    const int n_y = n_targets(n_states, n_streams);
    X.assign(static_cast<size_t>(n_samples) * N_FEATURES, 0.0);
    Y.assign(static_cast<size_t>(n_samples) * n_y, 0.0);

    SimPcgRandom rng;
    rng.reset(static_cast<uint32_t>(seed), 0, 0);

    for (int s = 0; s < n_samples; ++s) {
        const H2mmModel model = random_model(n_states, n_streams, rng);

        // --- Poisson-spaced macro times per burst
        std::vector<std::vector<long long>> times(n_bursts);
        for (int b = 0; b < n_bursts; ++b) {
            std::vector<long long>& t = times[b];
            t.resize(burst_len);
            t[0] = 0;
            for (int j = 1; j < burst_len; ++j) {
                // Knuth's method; mean_dt is small so this stays cheap.
                const double L = std::exp(-mean_dt);
                double prod = rng.random0e1e();
                long long k = 0;
                while (prod > L) { prod *= rng.random0e1e(); ++k; }
                t[j] = t[j - 1] + k + 1;
            }
        }

        // --- sample the hidden state at each photon from the cached A^dt
        // propagator: O(photons), not O(clock ticks).
        std::vector<std::vector<int>> streams(n_bursts);
        std::vector<double> cum_obs(static_cast<size_t>(n_states) * n_streams);
        for (int i = 0; i < n_states; ++i) {
            double acc = 0.0;
            for (int k = 0; k < n_streams; ++k) {
                acc += model.obs[i * n_streams + k];
                cum_obs[i * n_streams + k] = acc;
            }
        }
        std::vector<double> cum_prior(n_states);
        {
            double acc = 0.0;
            for (int i = 0; i < n_states; ++i) { acc += model.prior[i]; cum_prior[i] = acc; }
        }

        std::map<long long, std::vector<double>> cum_pow;  // dt -> cumsum of A^dt rows
        for (int b = 0; b < n_bursts; ++b) {
            const std::vector<long long>& t = times[b];
            std::vector<int>& out = streams[b];
            out.resize(t.size());
            int st = searchsorted(cum_prior.data(), n_states, rng.random0i1e());
            if (st >= n_states) st = n_states - 1;
            for (size_t j = 0; j < t.size(); ++j) {
                if (j > 0) {
                    const long long dt = t[j] - t[j - 1];
                    auto it = cum_pow.find(dt);
                    if (it == cum_pow.end()) {
                        std::vector<double> P = mat_pow(model.trans, dt, n_states);
                        for (int r = 0; r < n_states; ++r) {
                            double acc = 0.0;
                            for (int c = 0; c < n_states; ++c) {
                                acc += P[r * n_states + c];
                                P[r * n_states + c] = acc;
                            }
                        }
                        it = cum_pow.emplace(dt, std::move(P)).first;
                    }
                    st = searchsorted(it->second.data() + st * n_states, n_states,
                                      rng.random0i1e());
                    if (st >= n_states) st = n_states - 1;
                }
                int k = searchsorted(cum_obs.data() + st * n_streams, n_streams,
                                     rng.random0i1e());
                if (k >= n_streams) k = n_streams - 1;
                out[j] = k;
            }
        }

        // Reuse the engine's CSR builder so the surrogate sees exactly the
        // layout a real dataset would produce.
        H2MM engine;
        engine.set_bursts(times, streams, n_streams);

        const std::vector<double> feats = extract_features(engine);
        const std::vector<double> target = encode(model);
        std::copy(feats.begin(), feats.end(), X.begin() + static_cast<size_t>(s) * N_FEATURES);
        std::copy(target.begin(), target.end(), Y.begin() + static_cast<size_t>(s) * n_y);
    }
}

H2mmSurrogate H2mmSurrogate::train(
    int n_states, int n_streams, int n_samples,
    int n_bursts, int burst_len, double mean_dt,
    const TrainOptions& options, int seed
) {
    std::vector<double> X, Y;
    generate_training_set(n_states, n_streams, n_samples,
                          n_bursts, burst_len, mean_dt, seed, X, Y);
    const int n_y = n_targets(n_states, n_streams);
    TrainOptions opt = options;
    opt.seed = seed;
    NeuralNet net = NeuralNet::train(X.data(), n_samples, N_FEATURES,
                                     Y.data(), n_samples, n_y, opt);
    return H2mmSurrogate(std::move(net), n_states, n_streams, FEATURES_VERSION);
}

// ---------------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------------

H2mmSurrogate H2mmSurrogate::from_json_string(const std::string& text) {
    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("H2mmSurrogate: invalid JSON: ") + e.what());
    }
    if (j.contains("format") && j.at("format").get<std::string>() != "tttrlib.h2mm_surrogate")
        throw std::runtime_error("H2mmSurrogate: unexpected format '" +
                                 j.at("format").get<std::string>() +
                                 "', expected 'tttrlib.h2mm_surrogate'");

    const int fv = j.contains("features_version") ? j.at("features_version").get<int>()
                                                  : FEATURES_VERSION;
    if (fv != FEATURES_VERSION)
        throw std::runtime_error(
            "H2mmSurrogate: features_version " + std::to_string(fv) + " != current " +
            std::to_string(FEATURES_VERSION) + "; retrain the surrogate");

    if (!j.contains("net"))
        throw std::runtime_error("H2mmSurrogate: document has no 'net' object");

    NeuralNet net = NeuralNet::from_json_string(j.at("net").dump());
    return H2mmSurrogate(std::move(net),
                         j.at("n_states").get<int>(),
                         j.at("n_streams").get<int>(),
                         fv);
}

H2mmSurrogate H2mmSurrogate::from_json_file(const std::string& path) {
    std::ifstream fh(path);
    if (!fh) throw std::runtime_error("H2mmSurrogate: cannot open '" + path + "'");
    std::stringstream ss;
    ss << fh.rdbuf();
    return from_json_string(ss.str());
}

std::string H2mmSurrogate::to_json_string(int indent) const {
    json j;
    j["format"] = "tttrlib.h2mm_surrogate";
    j["version"] = 1;
    j["features_version"] = features_version_;
    j["n_states"] = n_states_;
    j["n_streams"] = n_streams_;
    j["net"] = json::parse(net_.to_json_string());
    return j.dump(indent);
}

void H2mmSurrogate::to_json_file(const std::string& path, int indent) const {
    std::ofstream fh(path);
    if (!fh) throw std::runtime_error("H2mmSurrogate: cannot write '" + path + "'");
    fh << to_json_string(indent);
}

} // namespace tttrlib
