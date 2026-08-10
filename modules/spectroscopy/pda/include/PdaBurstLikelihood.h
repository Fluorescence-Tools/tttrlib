// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_PDABURSTLIKELIHOOD_H
#define TTTRLIB_PDABURSTLIKELIHOOD_H

#include <vector>
#include <cstdlib>

/*!
 * \class PdaBurstLikelihood
 * \brief Burst-wise photon-partition likelihood for K-channel PDA.
 *
 * The observable is a burst's photon counts split across detection channels.
 * For a molecule with fixed transfer efficiencies the signal photons partition
 * multinomially, and each channel additionally collects uncorrelated Poisson
 * background:
 *
 * \f[
 *   L(F \mid p, B) = \sum_{b \le F} \Big[\prod_c \mathrm{Pois}(b_c; B_c)\Big]
 *                    P(n)\, \mathrm{Multinom}(F - b;\, p),
 *   \qquad n = N - \textstyle\sum_c b_c .
 * \f]
 *
 * \par Why this is not Pda
 * Pda builds a dense \f$(N_{max}+1)^2\f$ count matrix and fits a binned 1-D
 * projection of it. That representation does not generalise: a dense
 * three-channel simplex at \f$N_{max}=300\f$ is 217 MB and a four-channel one
 * is 65 GB. This class never forms a matrix -- it evaluates the likelihood per
 * burst, so it is defined for any K, has no photon cap, and is a maximum
 * likelihood objective rather than a chi-square on a histogram. With K = 2 it
 * is an alternative to Pda's histogram fit that stays correct where a bin holds
 * a handful of bursts.
 *
 * \par Evaluation
 * Only the multinomial's leading \f$n!\f$ couples the channels, and it depends
 * on the total background count \f$m=\sum_c b_c\f$ rather than on how the
 * background is distributed. Grouping by \f$m\f$ turns the nested sum into a
 * one-dimensional sum whose coefficients are the discrete convolution of the
 * per-channel series -- so the cost is \f$O(K\,b\,m_{max})\f$ rather than the
 * \f$O(\prod_c b_c)\f$ of summing over the background box, and no box is ever
 * materialised. At \f$K=2\f$ the two are the same work; past that the box grows
 * as the \f$K\f$-th power of the cutoff and the convolution does not.
 *
 * Everything that depends only on the bursts -- falling factorials, the Poisson
 * series, the multinomial constant -- is computed once and reused for every
 * model point and every fit iteration. Both it and the model-side
 * \f$p_c^{-b}\f$ table are peak-shifted onto \f$(0,1]\f$ per channel, which
 * leaves the per burst-and-point inner loop free of transcendentals: there are
 * \f$\sum_c b_c\f$ exponentials per burst rather than \f$\prod_c b_c\f$.
 *
 * \par Truncation
 * The per-channel series may NOT be truncated on Poisson tail mass. The summand
 * behaves like \f$\mathrm{Pois}(b;B_c)\,(F_c/(N p_c))^b\f$, which is order one
 * near the optimum but grows for many steps wherever a channel collected far
 * more photons than the model allows -- and there the background is the whole
 * likelihood. The cutoff is taken at an effective rate
 * \f$B_c \max_j (F_{jc}/(N_j p_c))\f$, capped at the largest count the channel
 * actually saw. log_background_correction() is deliberately untruncated and
 * exists as the oracle for the fast path.
 */
class PdaBurstLikelihood {

private:
    int _n_bursts = 0;
    int _n_channels = 0;
    double _tolerance = 1e-12;

    /// Per-burst per-channel photon counts, row-major (n_bursts x K).
    std::vector<double> _counts;
    /// Per-burst total photon count.
    std::vector<double> _total;
    /// lgamma(N+1) - sum_c lgamma(F_c+1): the burst-only part of the multinomial.
    std::vector<double> _log_mult_const;

    std::vector<double> _background;
    std::vector<double> _photon_number_pmf;

    /// log[Pois(b;B_c) * F_c!/(F_c-b)!] for every burst, channel and b, padded
    /// to _series_width. The expensive part (lgamma, Poisson) of the background
    /// kernel and independent of the model, so it is built once.
    std::vector<double> _log_a;
    int _series_width = 0;

    /// Largest F_c/N seen in each channel; fixes the effective truncation rate.
    std::vector<double> _max_ratio;
    /// Largest count seen in each channel.
    std::vector<int> _max_count;

    /// Current background box, and the flattened exponent table it implies.
    std::vector<int> _boxes;

    /// Per-channel exponentials of the burst factor, peak-shifted onto (0,1],
    /// laid out (n_bursts x sum_c box_c) with _box_offset giving each channel's
    /// slice. Together with _ew (the shifted w_m row) these hold every
    /// transcendental the background path needs, so the per burst-and-point
    /// inner loop is multiply-add only. Rebuilt when the box changes.
    std::vector<double> _ea, _log_a_shift, _ew, _log_w_shift;
    std::vector<int> _box_offset, _ea_boxes;
    int _box_m_max = 0;

    void build_channel_tables();

    bool _has_background = false;

    void build_burst_tables();
    /// Grows _boxes to cover this probability grid; rebuilds the exponent
    /// table only when the box actually changes.
    void ensure_box(const double* p, int n_points);

public:

    /*!
     * @param counts[in] Per-burst per-channel photon counts, row-major.
     * @param n_bursts[in] Number of bursts.
     * @param n_channels[in] Number of detection channels K.
     * @param background[in] Mean background counts per channel (length K).
     *        Empty or all-zero takes the fast multinomial-only path.
     * @param photon_number_pmf[in] P(n) for the signal photon number. Empty
     *        means flat.
     * @param tolerance[in] Poisson tail mass discarded per channel.
     */
    PdaBurstLikelihood(
            int* counts, int n_bursts, int n_channels,
            std::vector<double> background = std::vector<double>(),
            std::vector<double> photon_number_pmf = std::vector<double>(),
            double tolerance = 1e-12
    );

    /*!
     * \brief log L of every burst under one set of channel probabilities.
     * @param input[in] Channel probabilities, length K.
     * @param n_input[in] Must equal the number of channels.
     * @param output[out] log likelihood per burst.
     * @param n_output[out] Number of bursts.
     */
    void log_likelihood(
            double* input, int n_input,
            double** output, int* n_output
    );

    /*!
     * \brief Sum over bursts of log L, for a grid of probability vectors.
     *
     * The reduction is fused into the matrix product, so the
     * (points x bursts) grid is never materialised.
     *
     * @param input[in] Channel probabilities, row-major (n_points x K).
     * @param n_input1[in] Number of model points.
     * @param n_input2[in] Must equal the number of channels.
     * @param output[out] Summed log likelihood per model point.
     * @param n_output[out] Number of model points.
     */
    void total_log_likelihood(
            double* input, int n_input1, int n_input2,
            double** output, int* n_output
    );

    /*!
     * \brief The full (n_points x n_bursts) grid of log likelihoods.
     * @param input[in] Channel probabilities, row-major (n_points x K).
     * @param n_input1,n_input2[in] Grid shape; n_input2 must equal K.
     * @param output[out] Row-major (n_points x n_bursts).
     * @param n_output1,n_output2[out] Output shape.
     */
    void log_likelihood_grid(
            double* input, int n_input1, int n_input2,
            double** output, int* n_output1, int* n_output2
    );

    int get_n_bursts() const { return _n_bursts; }
    int get_n_channels() const { return _n_channels; }

    /// Background box currently in use, one entry per channel.
    void get_boxes(int** output, int* n_output) const;

    /*!
     * \brief Log multinomial probability of `counts` under probabilities `p`.
     *
     * The zero-background term, and on its own the whole likelihood when the
     * background is negligible.
     */
    static double log_multinomial_pmf(
            std::vector<double> counts, std::vector<double> p
    );

    /*!
     * \brief Exact, untruncated log background correction for one burst.
     *
     * Convolves the per-channel series directly in log space. This is the
     * oracle the truncated fast path is checked against, and the fallback
     * wherever the fast path's shifted sum underflows.
     */
    static double log_background_correction(
            std::vector<double> counts,
            std::vector<double> background,
            std::vector<double> p,
            std::vector<double> photon_number_pmf = std::vector<double>()
    );
};

#endif //TTTRLIB_PDABURSTLIKELIHOOD_H
