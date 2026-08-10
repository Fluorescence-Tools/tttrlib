// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file GopichSzabo.h
 * \brief Photon-by-photon maximum likelihood for continuous-time Markov chains.
 *
 * The Gopich-Szabo likelihood evaluates how probable a kinetic scheme is given
 * a burst of photons with their individual arrival times and colours. Unlike
 * a discrete-time HMM (which works on macro-time ticks), this is continuous-
 * time: the rate matrix K is the free parameter, propagated as e^{K*dt} for
 * the exact real-valued gap between photons.
 *
 * The algorithm diagonalises K = U * Lambda * U^-1 once, then each per-photon
 * propagation is an elementwise exp(lambda * dt). The alternating product of
 * emission and propagation matrices is renormalised at each photon to avoid
 * underflow (the standard HMM scaling trick).
 *
 * For non-reversible cycles the eigenvalues form complex conjugate pairs.
 * The arithmetic stays complex throughout and the real part is taken only
 * once, of the final scalar — the reference implementation takes Re() on the
 * emission transform prematurely, which is wrong for circulating schemes.
 *
 * Reference: Gopich & Szabo, J. Chem. Phys. 124, 154712 (2006);
 * J. Phys. Chem. B 113, 10965 (2009).
 */
#ifndef TTTRLIB_GOPICHSZABO_H
#define TTTRLIB_GOPICHSZABO_H

#include <vector>
#include <complex>
#include <cstdint>

namespace tttrlib {

/*!
 * \brief Gopich-Szabo photon-by-photon likelihood engine.
 *
 * Precompute the spectral decomposition of the rate matrix once, then score
 * many bursts cheaply. The per-burst cost is O(n_photons * n_states^2).
 */
class GopichSzabo {
public:
    /// Set the kinetic scheme. rate_matrix is column-major (n*n), emission is
    /// row-major (n_states * n_colors). Returns false if the rate matrix is
    /// too degenerate to diagonalise.
    bool set_scheme(
        const std::vector<double>& rate_matrix,
        const std::vector<double>& emission,
        int n_states,
        int n_colors
    );

    /*!
     * \brief Total log-likelihood of all bursts.
     *
     * \param times flat photon arrival times in seconds (all bursts concatenated)
     * \param colors flat photon colour indices in [0, n_colors)
     * \param offsets burst boundaries: burst b is [offsets[b], offsets[b+1])
     * \return summed log-likelihood, or -HUGE_VAL if any burst is impossible
     */
    double log_likelihood(
        const std::vector<double>& times,
        const std::vector<int32_t>& colors,
        const std::vector<int64_t>& offsets
    ) const;

    /*!
     * \brief Viterbi decoding of a single burst.
     *
     * \param times photon arrival times for one burst
     * \param colors photon colours for one burst
     * \return most likely state per photon
     */
    std::vector<int32_t> viterbi(
        const std::vector<double>& times,
        const std::vector<int32_t>& colors
    ) const;

    int n_states() const { return n_states_; }
    int n_colors() const { return n_colors_; }
    bool is_valid() const { return valid_; }

    /// Relaxation times: -1/Re(lambda) for non-zero eigenvalues
    std::vector<double> relaxation_times() const;

private:
    int n_states_ = 0;
    int n_colors_ = 0;
    bool valid_ = false;

    // Stored for Viterbi
    std::vector<double> emission_;  // n_states * n_colors, row-major

    // Spectral quantities (all complex, size n_states)
    std::vector<std::complex<double>> eigenvalues_;   // [n]
    std::vector<std::complex<double>> eigenvectors_;  // [n*n] column-major
    std::vector<std::complex<double>> inverse_;       // [n*n] U^-1
    std::vector<std::complex<double>> phi_;           // [n_colors * n * n]
    std::vector<std::complex<double>> p0_;            // [n] U^-1 @ p_eq
    std::vector<std::complex<double>> u_row_;         // [n] 1^T @ U
};

/// Build a two-colour emission matrix from per-state FRET efficiencies.
/// Returns (n_states * 2): column 0 = donor (1-E), column 1 = acceptor (E).
std::vector<double> emission_from_efficiencies(
    const std::vector<double>& efficiencies
);

} // namespace tttrlib

#endif // TTTRLIB_GOPICHSZABO_H
