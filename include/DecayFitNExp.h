// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_DECAYFITNEXP_H
#define TTTRLIB_DECAYFITNEXP_H

#include <cstddef>
#include <vector>

/**
 * Controls the bounded multi-exponential Poisson reconvolution fit.
 *
 * Lifetimes use the same time unit as dt and period.  A non-positive period
 * selects one histogram span (number of bins * dt).  The exponential
 * amplitudes and the optional background amplitude are constrained to the
 * probability simplex by the EM variable-projection step.
 */
struct DecayFitNExpOptions {
    double dt = 1.0;
    double period = 0.0;
    int convolution_stop = -1;

    double tau_min = 1.0e-3;
    double tau_max = 100.0;
    double lifetime_tolerance = 1.0e-4;
    double likelihood_tolerance = 1.0e-9;
    double em_tolerance = 1.0e-10;

    int max_outer_iterations = 20;
    int max_em_iterations = 500;
    double initial_background_fraction = 0.01;
    bool include_model = true;

    /// Number of log-spaced starting points for the per-lifetime global search.
    /// Higher is more robust against local minima but linearly slower; each
    /// visible basin is still Brent-refined. 24 is the robust default; a
    /// well-conditioned 1-2 exponential fit with a decent initial guess is
    /// typically fine at 8 (about 1.7x faster).
    int coordinate_grid_intervals = 24;
};


/** Result of one profiled multi-exponential Poisson fit. */
struct DecayFitNExpResult {
    bool converged = false;
    int outer_iterations = 0;
    int em_iterations = 0;

    // Profile shape negative log likelihood: -sum_i data_i log(probability_i).
    // Terms depending only on the observed data are intentionally omitted.
    double negative_log_likelihood = 0.0;
    double photon_count = 0.0;
    double background_amplitude = 0.0;

    std::vector<double> lifetimes;
    std::vector<double> amplitudes;

    // Expected counts in the same layout as the input.  For Jordi input this
    // is [VV, VH], with each detector normalized to its observed total.
    std::vector<double> model;
};


/**
 * General bounded multi-exponential reconvolution by Poisson maximum
 * likelihood.
 *
 * The IRF and background arguments describe one shared temporal shape.  Data
 * may contain one channel (data.size() == irf.size()) or Jordi VV+VH channels
 * (data.size() == 2*irf.size()).  Jordi channels are pooled while optimizing
 * the shared lifetime shape.  This is exactly equivalent to an independent
 * two-channel Poisson fit when each detector has its own profiled total count
 * and both detectors share the same normalized IRF/background shape.  The
 * returned Jordi model retains those two independently profiled totals.
 *
 * For fixed lifetimes set the corresponding lifetime_fixed entry to a nonzero
 * value.  Free lifetimes are updated by deterministic coordinate-wise Brent
 * minimization.  At every lifetime trial, nonnegative normalized exponential
 * and background amplitudes are profiled by EM.
 */
class DecayFitNExp {
public:
    static DecayFitNExpResult fit(
            const std::vector<double>& data,
            const std::vector<double>& irf,
            const std::vector<double>& background,
            const std::vector<double>& initial_lifetimes,
            const std::vector<double>& initial_amplitudes,
            const std::vector<int>& lifetime_fixed,
            const DecayFitNExpOptions& options);

    /// numpy-buffer overload of fit(): the language binding passes array buffers
    /// directly (single copy each), avoiding the per-element Python list <->
    /// std::vector marshalling that dominates a single small fit.
    static DecayFitNExpResult fit_buffers(
            double* data, int n_data,
            double* irf, int n_irf,
            double* background, int n_background,
            double* initial_lifetimes, int n_lifetimes,
            double* initial_amplitudes, int n_amplitudes,
            int* lifetime_fixed, int n_fixed,
            const DecayFitNExpOptions& options);

    /// numpy-buffer overload of fit_fixed_lifetimes().
    static DecayFitNExpResult fit_fixed_lifetimes_buffers(
            double* fdata, int n_fdata,
            double* firf, int n_firf,
            double* fbackground, int n_fbackground,
            double* flifetimes, int n_flifetimes,
            double* famplitudes, int n_famplitudes,
            const DecayFitNExpOptions& options);

    /**
     * Profile amplitudes by EM with every supplied lifetime held fixed.
     * Fixed lifetimes must lie within the configured tau bounds; out-of-range
     * values are rejected rather than silently clamped.  This avoids
     * constructing a mask at language-binding call sites and guarantees that
     * no coordinate-search work is performed.
     */
    static DecayFitNExpResult fit_fixed_lifetimes(
            const std::vector<double>& data,
            const std::vector<double>& irf,
            const std::vector<double>& background,
            const std::vector<double>& lifetimes,
            const std::vector<double>& initial_amplitudes,
            const DecayFitNExpOptions& options);

    /**
     * Fit a row-major matrix without per-row model allocation in the returned
     * value.  n_cols must equal irf.size() or 2*irf.size().
     *
     * Each output row has width 4 + 2*n_exp and layout:
     * [negative_log_likelihood, background_amplitude, converged,
     *  outer_iterations, lifetime_0..lifetime_n-1,
     *  amplitude_0..amplitude_n-1].
     *
     * Rows are partitioned across native C++ workers for sufficiently large
     * batches. TTTRLIB_NUM_THREADS, OMP_NUM_THREADS, and TTTRLIB_USE_OPENMP
     * use the same meanings as the Fit23 batch surface; small batches stay
     * serial to avoid worker startup overhead.
     */
    static std::vector<double> fit_batch_flat(
            const std::vector<double>& data_matrix,
            std::size_t n_rows,
            std::size_t n_cols,
            const std::vector<double>& irf,
            const std::vector<double>& background,
            const std::vector<double>& initial_lifetimes,
            const std::vector<double>& initial_amplitudes,
            const std::vector<int>& lifetime_fixed,
            const DecayFitNExpOptions& options);
};

#endif // TTTRLIB_DECAYFITNEXP_H
