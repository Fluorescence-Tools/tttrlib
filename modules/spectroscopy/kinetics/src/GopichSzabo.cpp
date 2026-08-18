// SPDX-License-Identifier: BSD-3-Clause
#include "CtmcKinetics.h"
#include "Registry.h"
#include "GopichSzabo.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include "QREigen.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>

namespace tttrlib {

using cdouble = std::complex<double>;

namespace {

constexpr double MAX_COND = 1e8;

double matrix_norm1(const cdouble* A, int n) {
    double best = 0.0;
    for (int j = 0; j < n; ++j) {
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += std::abs(A[i * n + j]);
        if (s > best) best = s;
    }
    return best;
}

// Full eigendecomposition of the rate matrix. Eigenvector j is column j.
//
// This used to be a local Faddeev-LeVerrier char-poly + Durand-Kerner root
// find + null-space read-off. It is now the shared QR eigensolver, for three
// reasons: the null-space read-off it used was wrong (it un-permuted the
// eigenvector by the row pivots, which permute equations, not unknowns), the
// root finder started every root at |z| = 0.5 regardless of the rates in the
// matrix, and the whole thing was O(n^4) where the QR path is O(n^3).
bool eigendecompose(
    const std::vector<double>& A, int n,
    std::vector<cdouble>& evals,
    std::vector<cdouble>& evecs,  // n*n, column i is eigenvector i
    std::vector<cdouble>& inv
) {
    if (!qr_eigendecompose(A.data(), n, evals, evecs, inv)) return false;
    double cond = matrix_norm1(evecs.data(), n) * matrix_norm1(inv.data(), n);
    return cond <= MAX_COND;
}

} // anonymous namespace


// ============================================================================
// CTMC utilities
// ============================================================================

std::vector<double> generator_from_rate_matrix(
    const std::vector<double>& rm, int n
) {
    std::vector<double> Q(rm);
    for (int i = 0; i < n; ++i) Q[i * n + i] = 0.0;
    for (auto& v : Q) v = std::max(v, 0.0);
    for (int j = 0; j < n; ++j) {
        double cs = 0.0;
        for (int i = 0; i < n; ++i) if (i != j) cs += Q[i * n + j];
        Q[j * n + j] = -cs;
    }
    return Q;
}

std::vector<double> equilibrium_populations(
    const std::vector<double>& rm, int n
) {
    auto Q = generator_from_rate_matrix(rm, n);
    // Solve [Q^T; 1^T] p = [0; 1] via normal equations
    std::vector<double> AtA(n * n), Atb(n, 1.0);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            double s = 0.0;
            for (int k = 0; k < n; ++k) s += Q[k * n + i] * Q[k * n + j];
            AtA[i * n + j] = s + 1.0;
        }
    // Solve
    std::vector<double> M = AtA, p = Atb;
    for (int k = 0; k < n; ++k) {
        int piv = k; double best = std::abs(M[k * n + k]);
        for (int i = k + 1; i < n; ++i) {
            double v = std::abs(M[i * n + k]);
            if (v > best) { best = v; piv = i; }
        }
        if (piv != k) {
            for (int j = 0; j < n; ++j) std::swap(M[k * n + j], M[piv * n + j]);
            std::swap(p[k], p[piv]);
        }
        if (std::abs(M[k * n + k]) < 1e-300) continue;
        for (int i = k + 1; i < n; ++i) {
            double f = M[i * n + k] / M[k * n + k];
            for (int j = k; j < n; ++j) M[i * n + j] -= f * M[k * n + j];
            p[i] -= f * p[k];
        }
    }
    for (int i = n - 1; i >= 0; --i) {
        double s = p[i];
        for (int j = i + 1; j < n; ++j) s -= M[i * n + j] * p[j];
        p[i] = (std::abs(M[i * n + i]) > 1e-300) ? s / M[i * n + i] : 1.0 / n;
    }
    double total = 0.0;
    for (auto& v : p) { v = std::max(v, 0.0); total += v; }
    if (total > 0) for (auto& v : p) v /= total;
    else for (auto& v : p) v = 1.0 / n;
    return p;
}

std::vector<double> rate_matrix_from_rates(
    const std::vector<double>& rates, int n
) {
    std::vector<double> m(n * n, 0.0);
    int k = 0;
    for (int s = 0; s < n; ++s)
        for (int t = 0; t < n; ++t)
            if (s != t) m[t * n + s] = rates[k++];
    return m;
}

std::vector<double> rates_from_rate_matrix(
    const std::vector<double>& m, int n
) {
    std::vector<double> r;
    r.reserve(n * (n - 1));
    for (int s = 0; s < n; ++s)
        for (int t = 0; t < n; ++t)
            if (s != t) r.push_back(m[t * n + s]);
    return r;
}


// ============================================================================
// GopichSzabo
// ============================================================================

std::vector<double> emission_from_efficiencies(
    const std::vector<double>& eff
) {
    int n = static_cast<int>(eff.size());
    std::vector<double> em(n * 2);
    for (int i = 0; i < n; ++i) {
        em[i * 2] = 1.0 - eff[i];
        em[i * 2 + 1] = eff[i];
    }
    return em;
}

bool GopichSzabo::set_scheme(
    const std::vector<double>& rate_matrix,
    const std::vector<double>& emission,
    int n_states, int n_colors
) {
    n_states_ = n_states;
    n_colors_ = n_colors;
    valid_ = false;
    emission_ = emission;

    auto Q = generator_from_rate_matrix(rate_matrix, n_states);
    std::vector<cdouble> evals, evecs, inv;
    if (!eigendecompose(Q, n_states, evals, evecs, inv)) return false;

    eigenvalues_.resize(n_states);
    for (int i = 0; i < n_states; ++i)
        eigenvalues_[i] = cdouble(std::min(evals[i].real(), 0.0), evals[i].imag());

    auto p_eq = equilibrium_populations(rate_matrix, n_states);

    // phi[c] = U^-1 * diag(em[:,c]) * U
    phi_.resize(n_colors * n_states * n_states);
    for (int c = 0; c < n_colors; ++c) {
        for (int a = 0; a < n_states; ++a)
            for (int b = 0; b < n_states; ++b) {
                cdouble s(0.0);
                for (int k = 0; k < n_states; ++k)
                    s += inv[a * n_states + k] * emission[k * n_colors + c]
                         * evecs[k * n_states + b];
                phi_[c * n_states * n_states + a * n_states + b] = s;
            }
    }

    p0_.resize(n_states);
    for (int a = 0; a < n_states; ++a) {
        cdouble s(0.0);
        for (int k = 0; k < n_states; ++k) s += inv[a * n_states + k] * p_eq[k];
        p0_[a] = s;
    }

    u_row_.resize(n_states);
    for (int k = 0; k < n_states; ++k) {
        cdouble s(0.0);
        for (int a = 0; a < n_states; ++a) s += evecs[a * n_states + k];
        u_row_[k] = s;
    }

    eigenvectors_ = evecs;
    inverse_ = inv;
    valid_ = true;
    return true;
}

double GopichSzabo::log_likelihood(
    const std::vector<double>& times,
    const std::vector<int32_t>& colors,
    const std::vector<int64_t>& offsets
) const {
    if (!valid_) return -std::numeric_limits<double>::infinity();
    const int n = n_states_;
    int n_bursts = static_cast<int>(offsets.size()) - 1;
    double total_ll = 0.0;

    #pragma omp parallel for reduction(+:total_ll) schedule(dynamic)
    for (int b = 0; b < n_bursts; ++b) {
        int64_t start = offsets[b];
        int64_t stop = offsets[b + 1];
        if (stop - start < 2) continue;

        std::vector<cdouble> vec(n), scr(n);
        int32_t c0 = colors[start];
        const cdouble* ph = &phi_[c0 * n * n];
        for (int a = 0; a < n; ++a) {
            cdouble acc(0.0);
            for (int k = 0; k < n; ++k) acc += ph[a * n + k] * p0_[k];
            vec[a] = acc;
        }

        double log_scale = 0.0;
        bool failed = false;
        for (int64_t i = start + 1; i < stop; ++i) {
            double dt = times[i] - times[i - 1];
            for (int k = 0; k < n; ++k)
                scr[k] = std::exp(eigenvalues_[k] * dt) * vec[k];
            int32_t c = colors[i];
            const cdouble* pc = &phi_[c * n * n];
            cdouble mag(0.0);
            for (int a = 0; a < n; ++a) {
                cdouble acc(0.0);
                for (int k = 0; k < n; ++k) acc += pc[a * n + k] * scr[k];
                vec[a] = acc;
                mag += acc;
            }
            double m = std::abs(mag);
            if (m <= 0.0) { failed = true; break; }
            double inv_m = 1.0 / m;
            for (int a = 0; a < n; ++a) vec[a] *= inv_m;
            log_scale += std::log(m);
        }
        if (failed) { total_ll += -std::numeric_limits<double>::infinity(); continue; }
        cdouble fv(0.0);
        for (int a = 0; a < n; ++a) fv += u_row_[a] * vec[a];
        if (fv.real() <= 0.0) total_ll += -std::numeric_limits<double>::infinity();
        else total_ll += std::log(fv.real()) + log_scale;
    }
    return total_ll;
}

// One burst, decoded into `path[first .. last)`. Both public overloads run this;
// the multi-burst one runs it once per burst, which is what keeps a burst's
// decoding independent of the burst before it.
void GopichSzabo::viterbi_range(
    const std::vector<double>& times,
    const std::vector<int32_t>& colors,
    std::size_t first, std::size_t last,
    std::vector<int32_t>& path
) const {
    const int n_ph = static_cast<int>(last - first);
    const int n = n_states_;
    if (!valid_ || n_ph <= 0) return;

    // log emission: log_em[c * n + s] = log(emission[s * n_colors_ + c])
    std::vector<double> log_em(n_colors_ * n);
    for (int c = 0; c < n_colors_; ++c)
        for (int s = 0; s < n; ++s) {
            double p = emission_[s * n_colors_ + c];
            log_em[c * n + s] = (p > 1e-300) ? std::log(p) : -700.0;
        }

    // The prior is the equilibrium population in the original basis, p = U * p0.
    // (A call to equilibrium_populations() stood here, passed a zero matrix and
    // with its result discarded -- dead, and its own comments said so.)
    std::vector<double> log_prior(n);
    for (int s = 0; s < n; ++s) {
        cdouble pe(0.0);
        for (int k = 0; k < n; ++k)
            pe += eigenvectors_[s * n + k] * p0_[k];
        double pv = std::max(pe.real(), 1e-300);
        log_prior[s] = std::log(pv);
    }

    std::vector<double> delta(n_ph * n);
    std::vector<int32_t> back(n_ph * n, 0);

    for (int j = 0; j < n; ++j)
        delta[j] = log_prior[j] + log_em[colors[first] * n + j];

    std::vector<cdouble> prop(n * n);
    for (int i = 1; i < n_ph; ++i) {
        double dt = times[first + i] - times[first + i - 1];
        // P = U * diag(exp(lam*dt)) * U^-1
        for (int a = 0; a < n; ++a)
            for (int b = 0; b < n; ++b) {
                cdouble s(0.0);
                for (int k = 0; k < n; ++k)
                    s += eigenvectors_[a * n + k]
                         * std::exp(eigenvalues_[k] * dt)
                         * inverse_[k * n + b];
                prop[a * n + b] = s;
            }
        for (int j = 0; j < n; ++j) {
            double best = -1e300; int best_k = 0;
            for (int k = 0; k < n; ++k) {
                double val = prop[j * n + k].real();
                if (val <= 0.0) continue;
                double cand = delta[(i - 1) * n + k] + std::log(val);
                if (cand > best) { best = cand; best_k = k; }
            }
            delta[i * n + j] = best + log_em[colors[first + i] * n + j];
            back[i * n + j] = best_k;
        }
    }

    double best = -1e300; int best_j = 0;
    for (int j = 0; j < n; ++j)
        if (delta[(n_ph - 1) * n + j] > best) {
            best = delta[(n_ph - 1) * n + j]; best_j = j;
        }
    // Trace back within the burst, writing into the caller's slice.
    path[first + n_ph - 1] = best_j;
    for (int i = n_ph - 1; i > 0; --i)
        path[first + i - 1] = back[i * n + path[first + i]];
}

std::vector<int32_t> GopichSzabo::viterbi(
    const std::vector<double>& times,
    const std::vector<int32_t>& colors
) const {
    std::vector<int32_t> path(times.size(), 0);
    viterbi_range(times, colors, 0, times.size(), path);
    return path;
}

std::vector<int32_t> GopichSzabo::viterbi(
    const std::vector<double>& times,
    const std::vector<int32_t>& colors,
    const std::vector<int64_t>& offsets
) const {
    if (colors.size() != times.size())
        throw std::invalid_argument(
            "gopich-szabo viterbi: times and colors must be the same length");
    std::vector<int32_t> path(times.size(), 0);
    if (offsets.empty()) return path;
    for (std::size_t b = 0; b + 1 < offsets.size(); ++b) {
        const long long lo = offsets[b], hi = offsets[b + 1];
        if (lo < 0 || hi < lo || static_cast<std::size_t>(hi) > times.size())
            throw std::invalid_argument(
                "gopich-szabo viterbi: offsets must be ascending and within the "
                "photon arrays -- burst bounds that run past the end decode a "
                "burst that is not there");
        viterbi_range(times, colors, static_cast<std::size_t>(lo),
                      static_cast<std::size_t>(hi), path);
    }
    return path;
}


namespace {
// A borrowed buffer as the vector the C++ surface takes. A memcpy-rate copy
// inside C++ (~0.5 ns/element); what it avoids is the host-language conversion.
template <typename T, typename U>
std::vector<T> borrow(const U* p, int n) {
    return (p == nullptr || n <= 0) ? std::vector<T>()
                                    : std::vector<T>(p, p + n);
}
}  // namespace

double GopichSzabo::log_likelihood_flat(
    double* times, int n_times,
    int* colors, int n_colors_in,
    long long* offsets, int n_offsets
) const {
    auto t = borrow<double>(times, n_times);
    auto c = borrow<int32_t>(colors, n_colors_in);
    auto o = borrow<int64_t>(offsets, n_offsets);
    if (o.empty()) o = {0, static_cast<int64_t>(t.size())};
    return log_likelihood(t, c, o);
}

void GopichSzabo::viterbi_flat(
    double* times, int n_times,
    int* colors, int n_colors_in,
    long long* offsets, int n_offsets,
    int** out, int* n_out
) const {
    auto t = borrow<double>(times, n_times);
    auto c = borrow<int32_t>(colors, n_colors_in);
    auto o = borrow<int64_t>(offsets, n_offsets);
    const std::vector<int32_t> path =
        o.empty() ? viterbi(t, c) : viterbi(t, c, o);

    // ARGOUTVIEWM hands ownership to the host language, which frees with
    // free() -- so this is malloc'd, not new'd.
    const std::size_t n = path.size();
    int* p = static_cast<int*>(std::malloc(std::max<std::size_t>(n, 1) * sizeof(int)));
    if (p != nullptr && n > 0) std::memcpy(p, path.data(), n * sizeof(int));
    *out = p;
    *n_out = static_cast<int>(n);
}

std::vector<double> GopichSzabo::relaxation_times() const {
    // A rate matrix has EXACTLY ONE zero eigenvalue -- the stationary
    // distribution -- and the relaxation times are the other n-1. Dropping it
    // by an absolute threshold (|re| > 1e-15) is wrong: how close to zero that
    // eigenvalue lands is a property of the QR iteration on the platform's
    // arithmetic, not of the model. A four-state scheme whose rates are ~1e4
    // gave 1.8e-12 on macOS/clang and an exact 0 here, so the same model
    // returned four times on one machine and three on another, the extra one
    // being 5.5e11 s of "relaxation" that means nothing.
    //
    // The threshold is therefore RELATIVE to the spectrum: drop the single
    // eigenvalue nearest zero, and guard the rest against the scale of the
    // largest. That is the mathematical statement ("one zero mode") rather
    // than a guess about how small zero looks today.
    if (n_states_ <= 0) return {};
    double largest = 0.0;
    int zero_mode = 0;
    double smallest = std::abs(eigenvalues_[0].real());
    for (int i = 0; i < n_states_; ++i) {
        const double re = std::abs(eigenvalues_[i].real());
        if (re > largest) largest = re;
        if (re < smallest) { smallest = re; zero_mode = i; }
    }
    const double floor_re = largest * 1e-10;   // below this, it IS the zero mode
    std::vector<double> t;
    for (int i = 0; i < n_states_; ++i) {
        if (i == zero_mode) continue;          // the stationary distribution
        const double re = eigenvalues_[i].real();
        if (std::abs(re) <= floor_re) continue;  // a second (degenerate) zero mode
        t.push_back(-1.0 / re);
    }
    return t;
}

} // namespace tttrlib

// ---- registry entries (Registry.h, core): declared next to the code, registered
// when this library loads; a static consumer links the archive whole.
namespace {
const char* const kGopichSzaboEntry = R"JSON({
  "name": "gopich_szabo",
  "label": "Gopich-Szabo photon-colour likelihood for kinetic schemes",
  "summary": "Likelihood and Viterbi decoding of a photon colour sequence under a kinetic scheme with per-state efficiencies and interconversion rates.",
  "description": "The generator-matrix likelihood of Gopich & Szabo: between photons the state evolves by exp(K dt), at a photon the emission probability of its colour is applied, so the inter-photon time carries information about the rates. Any number of colours and states, schemes set by name or matrix; `relaxation_times` are the eigenvalues of the rate matrix. It is the per-molecule building block that `burst_ml` sums over bursts.",
  "operation_type": "analysis",
  "method": "log_likelihood",
  "params_schema": {
    "type": "object",
    "properties": {
      "n_states": {
        "type": "integer",
        "title": "States",
        "default": 2
      },
      "n_colors": {
        "type": "integer",
        "title": "Colours",
        "default": 2
      }
    }
  },
  "inputs": {
    "required": [
      "photon_colours",
      "photon_times"
    ]
  },
  "outputs": {
    "columns": [
      "log_likelihood",
      "viterbi_path"
    ]
  },
  "row_grain": "photon",
  "references": [
    {
      "type": "journal",
      "authors": "Gopich, I. V., Szabo, A.",
      "title": "Decoding the pattern of photon colors in single-molecule FRET",
      "journal": "J Phys Chem B",
      "year": 2009,
      "volume": "113",
      "pages": "10965-10973"
    },
    {
      "type": "journal",
      "authors": "Gopich, I. V., Szabo, A.",
      "title": "Theory of the energy transfer efficiency and fluorescence lifetime distribution in single-molecule FRET",
      "journal": "Proc Natl Acad Sci USA",
      "year": 2012,
      "volume": "109",
      "pages": "7747-7752"
    }
  ],
  "api": [
    "GopichSzabo",
    "rate_matrix_from_rates",
    "rates_from_rate_matrix",
    "generator_from_rate_matrix",
    "equilibrium_populations",
    "emission_from_efficiencies"
  ],
  "can_replay": true
})JSON";
bool register_gopichszabo_entries() {
    tttrlib::register_algorithm_json("kinetics", "gopich_szabo", kGopichSzaboEntry);
    return true;
}
const bool kGopichSzaboRegistered = register_gopichszabo_entries();
}  // namespace
