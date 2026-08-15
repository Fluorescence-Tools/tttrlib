// SPDX-License-Identifier: BSD-3-Clause
#include "DecayPatternFit.h"

#include <stdexcept>

#include "MaxEntQp.h"
#include "Nnls.h"

namespace tttrlib {

namespace {

// Column-major-by-pattern -> row-major design matrix: column k is pattern k.
std::vector<double> build_design_matrix(
    const std::vector<std::vector<double>>& patterns, int n_bins
) {
    const int n_patterns = static_cast<int>(patterns.size());
    std::vector<double> A(static_cast<size_t>(n_bins) * n_patterns);
    for (int k = 0; k < n_patterns; ++k) {
        if (static_cast<int>(patterns[k].size()) != n_bins)
            throw std::invalid_argument("decay_pattern_fit: every pattern must have data.size() bins");
        for (int i = 0; i < n_bins; ++i)
            A[static_cast<size_t>(i) * n_patterns + k] = patterns[k][i];
    }
    return A;
}

double compute_chisq(
    const std::vector<double>& A, const std::vector<double>& data,
    const std::vector<double>& amplitudes, int n_bins, int n_patterns
) {
    double chisq = 0.0;
    for (int i = 0; i < n_bins; ++i) {
        double model = 0.0;
        const double* row = &A[static_cast<size_t>(i) * n_patterns];
        for (int k = 0; k < n_patterns; ++k) model += row[k] * amplitudes[k];
        const double r = model - data[i];
        chisq += r * r;
    }
    return chisq;
}

} // anonymous namespace

PatternFitResult decay_pattern_fit(
    const std::vector<double>& data,
    const std::vector<std::vector<double>>& patterns,
    PatternFitMode mode,
    double reg_strength,
    const std::vector<double>& prior,
    int max_iter,
    double tol
) {
    PatternFitResult res;
    const int n_bins = static_cast<int>(data.size());
    const int n_patterns = static_cast<int>(patterns.size());
    if (n_bins == 0 || n_patterns == 0) { res.success = false; return res; }

    const std::vector<double> A = build_design_matrix(patterns, n_bins);

    switch (mode) {
        case PatternFitMode::kNone: {
            res.amplitudes = nnls(A, data, n_bins, n_patterns, max_iter,
                                   tol > 0.0 ? tol : 1e-10);
            break;
        }
        case PatternFitMode::kTikhonov: {
            std::vector<double> H, g0;
            double const_term = 0.0;
            build_normal_equations(A, data, {}, n_bins, n_patterns, H, g0, const_term);
            for (int k = 0; k < n_patterns; ++k)
                H[static_cast<size_t>(k) * n_patterns + k] += 2.0 * reg_strength;
            std::vector<double> ng0(g0);
            for (auto& v : ng0) v = -v;
            res.amplitudes = quadpr_bound(H, ng0, 0.0);
            break;
        }
        case PatternFitMode::kMaxEnt: {
            std::vector<double> H, g0;
            double const_term = 0.0;
            build_normal_equations(A, data, {}, n_bins, n_patterns, H, g0, const_term);
            std::vector<double> m = prior;
            if (m.empty()) m.assign(n_patterns, 1.0);
            else if (static_cast<int>(m.size()) != n_patterns)
                throw std::invalid_argument("decay_pattern_fit: prior.size() must equal patterns.size()");
            const MaxEntResult r = run_mem(H, g0, m, const_term, reg_strength,
                                            max_iter, tol > 0.0 ? tol : 1e-4, 1e-12);
            res.amplitudes = r.p;
            res.nu_used = reg_strength;
            break;
        }
        case PatternFitMode::kMaxEntTargetChisq: {
            std::vector<double> H, g0;
            double const_term = 0.0;
            build_normal_equations(A, data, {}, n_bins, n_patterns, H, g0, const_term);
            std::vector<double> m = prior;
            if (m.empty()) m.assign(n_patterns, 1.0);
            else if (static_cast<int>(m.size()) != n_patterns)
                throw std::invalid_argument("decay_pattern_fit: prior.size() must equal patterns.size()");
            const MemTargetChisqResult r = run_mem_target_chisq(
                H, g0, m, const_term, /*target_chisq=*/reg_strength,
                /*nu0=*/1e-5, /*max_iter=*/std::max(max_iter, 1000),
                /*chisq_tol=*/1e-2, tol > 0.0 ? tol : 1e-4, 1e-12);
            res.amplitudes = r.result.p;
            res.nu_used = r.nu;
            res.target_converged = r.converged;
            break;
        }
    }

    res.chisq = compute_chisq(A, data, res.amplitudes, n_bins, n_patterns);
    res.success = true;
    return res;
}

} // namespace tttrlib
