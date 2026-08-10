// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file BurstML.h
 * \brief Maximum-likelihood analysis of multi-state FRET bursts with diffusion.
 *
 * Ports the FRET_burstML algorithm (Hoffmann et al.) from MATLAB/MEX (GSL-based)
 * to std-only C++17. The method models burst photon sequences from freely
 * diffusing molecules using a combined diffusion-kinetics-photon observation
 * operator, eigendecomposed once per parameter set.
 *
 * For an n-state, 2-colour model the (5n) fitting parameters are:
 *   1..n         : per-state molecular brightness n0(i)      [cnt/ms]
 *   n+1..2n      : per-state diffusion time tau(i)            [ms]
 *   2n+1..3n-1   : relaxation rates k(i) between adjacent states  [1/ms]
 *   3n..4n-2     : relative fractions f(i), reparameterised populations
 *   4n-1..5n-2   : per-state FRET efficiency E(i)
 *   5n-1..5n     : background rates [acceptor, donor]         [cnt/ms]
 *
 * The spatial coordinate q (radial distance from optical axis, normalised
 * by the beam waist) is discretised into jmax bins; the 3D Gaussian detection
 * profile enters as exp(-2*q^2). Photon arrival times are in ms.
 *
 * The combined evolution operator L = D + K - N (diffusion + kinetics -
 * observation) is eigendecomposed via Francis double-shift QR; the per-photon
 * propagation is then a diagonal scaling in the eigenbasis. The log-likelihood
 * of all bursts is minimised via Nelder-Mead simplex.
 *
 * Reference: T. Hoffmann et al., "Maximum-likelihood analysis of single-
 * molecule fluorescence burst data using a combined diffusion and photon
 * counting model", unpublished; see Manual_FRET_burstML.pdf.
 */
#ifndef TTTRLIB_BURSTML_H
#define TTTRLIB_BURSTML_H

#include <complex>
#include <cstdint>
#include <string>
#include <vector>

namespace tttrlib {

/// Fit result returned by BurstML::fit().
struct BurstMLFitResult {
    std::vector<double> params;     ///< fitted parameters (physical)
    std::vector<double> errors;     ///< parameter uncertainties (from Hessian)
    double log_likelihood;          ///< maximised log-likelihood
    double bic;                     ///< Bayesian Information Criterion
    int iterations;                 ///< optimiser iterations
    int status;                     ///< convergence code (2,3=converged, 0=maxiter)
};

/*!
 * \brief Maximum-likelihood FRET burst analysis with diffusion modelling.
 *
 * Set burst photon data, configure the spatial/analysis grid, then call
 * fit() to recover kinetic and photophysical parameters.
 */
class BurstML {
public:
    BurstML() = default;
    ~BurstML() = default;

    /// Photon data for a set of bursts. All bursts concatenated.
    /// @param times   Photon arrival times in ms (all bursts concatenated)
    /// @param colours Photon colour/channel indices: 0=acceptor, 1=donor
    /// @param offsets Burst boundaries; burst b spans [offsets[b], offsets[b+1])
    void set_burst_data(
        const std::vector<double>& times,
        const std::vector<int32_t>& colours,
        const std::vector<int64_t>& offsets
    );

    using FitResult = BurstMLFitResult;

    /*!
     * \brief Fit burst data to recover parameters.
     *
     * Uses Nelder-Mead simplex on the negative log-likelihood. The parameter
     * vector layout depends on n_states and n_colours (see file header).
     *
     * \param init_params  Initial parameter guess (unbounded physical values)
     * \param lower_bounds Lower bounds for each parameter
     * \param upper_bounds Upper bounds for each parameter
     * \param n_states     Number of conformational states
     * \param n_colours    Number of detection colours (2 or 3)
     * \param jmax         Number of radial spatial bins (default 50)
     * \param qmax         Max normalised radial coordinate (default 4)
     * \param t_th         Burst inter-photon time threshold in ms
     * \param n_th         Minimum photons per burst
     * \return             Fit result
     */
    FitResult fit(
        const std::vector<double>& init_params,
        const std::vector<double>& lower_bounds,
        const std::vector<double>& upper_bounds,
        int n_states,
        int n_colours = 2,
        int jmax = 50,
        double qmax = 4.0,
        double t_th = 0.3,
        double n_th = 30.0
    );

    /*!
     * \brief Evaluate negative log-likelihood for a given parameter set.
     * Exposed for testing and manual optimisation.
     */
    double neg_log_likelihood(const std::vector<double>& params) const;

    /// Number of bursts currently loaded.
    int n_bursts() const;

    /// Total photon count across all bursts.
    int n_photons() const;

    // ---- configuration ----
    void set_n_states(int n) { n_states_ = n; }
    void set_n_colours(int n) { n_colours_ = n; }
    void set_jmax(int j) { jmax_ = j; }
    void set_qmax(double q) { qmax_ = q; }
    void set_t_th(double t) { t_th_ = t; }
    void set_n_th(double n) { n_th_ = n; }

    int get_n_states() const { return n_states_; }
    int get_n_colours() const { return n_colours_; }
    int get_jmax() const { return jmax_; }
    double get_qmax() const { return qmax_; }
    double get_t_th() const { return t_th_; }
    double get_n_th() const { return n_th_; }

private:
    // burst data
    std::vector<double> times_;       // photon times in ms
    std::vector<int32_t> colours_;    // 0=A, 1=D for 2-colour
    std::vector<int64_t> offsets_;    // burst boundaries

    // config
    int n_states_ = 2;
    int n_colours_ = 2;
    int jmax_ = 50;
    double qmax_ = 4.0;
    double t_th_ = 0.3;   // ms
    double n_th_ = 30.0;

    /*!
     * \brief Core likelihood computation for a parameter vector.
     *
     * Decomposes the combined diffusion-kinetics-observation operator once,
     * then iterates over all bursts accumulating the log-likelihood.
     * The heavy lifting (eigendecomposition, matrix products) is done here;
     * the per-burst photon loop is OpenMP-parallel.
     *
     * \return The total log-likelihood (higher is better), or -inf on error.
     */
    double compute_log_likelihood(const std::vector<double>& params) const;
};

} // namespace tttrlib

#endif // TTTRLIB_BURSTML_H
