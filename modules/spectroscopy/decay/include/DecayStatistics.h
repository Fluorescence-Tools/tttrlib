// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_DECAYSTATISTICS_H
#define TTTRLIB_DECAYSTATISTICS_H


#include <cmath>
#include <vector>
#include <numeric>
#include <algorithm>
#include <iostream>
#include <cstring> /* strcmp */


/*!
 * @brief The model value below which a bin counts as driven out of range.
 *
 * Historical value, kept exactly: every evaluation with a model bin strictly
 * above this floor is bit-for-bit what it always was.
 */
constexpr double kModelFloor = 1.0e-12;

/*!
 * @brief `log(m)`, continued below ::kModelFloor rather than left undefined.
 *
 * The Poisson log-likelihood is undefined at `m <= 0`, and the likelihood
 * functions here used to handle that by **skipping the bin** -- contributing
 * zero. That is not a safe guard, it is a reward: the term a near-zero bin
 * contributes to the minimised objective is `-C*log(m)`, which is large and
 * *positive* (at `C = 30` and `m = 1e-12`, about +829). Dropping it hands the
 * optimiser a free discontinuous decrease of the same size for pushing the bin
 * the last step below the floor, and below it the objective is perfectly flat,
 * so nothing pulls the bin back. Measured, not inferred: the objective falls
 * 828.9 across the threshold and is identical for every negative model value.
 *
 * Today that corner is unreachable because the callers clamp their parameters
 * before building the model. It becomes reachable the moment a clamp is
 * replaced by a soft bound or a prior -- which is the point of doing this
 * first, separately, and on its own.
 *
 * The continuation is the first-order Taylor expansion of `log` at the floor:
 *
 * @f[ \log m \;\to\; \log m_0 + \frac{m - m_0}{m_0}, \qquad m \le m_0 @f]
 *
 * which agrees with `log m` in value *and* slope at `m_0`, so the objective is
 * C1 across the floor; is finite for every finite `m`, including negative ones;
 * and diverges to `+inf` in the objective as `m -> -inf`, with a real restoring
 * gradient the whole way. A line search that steps a bin negative now gets a
 * number it can compare and a direction home, instead of a cliff.
 *
 * Note the slope at the floor is `1/m_0 = 1e12`. That is not a tuning choice --
 * it is what `log` actually does there -- but it does mean a probe either side
 * of the floor shows a gap proportional to the probe step. Continuity is tested
 * by checking the gap *halves when the step halves*, which is what separates a
 * steep C1 join from the 828.9 cliff it replaced.
 *
 * @param m model value in a single bin
 * @return `log(m)` above the floor, its tangent continuation at or below it
 */
inline double log_m_ext(double m) {
    if (m > kModelFloor) return std::log(m);
    const double log_floor = std::log(kModelFloor);  // folded at compile time
    return log_floor + (m - kModelFloor) / kModelFloor;
}

/*!
 * Initialize an array containing pre-computed logratithms
 */
void init_fact();

/*!
 * Approximation of log(gamma function). See wikipedia
 *
 * https://en.wikipedia.org/wiki/Gamma_function#The_log-gamma_function
 *
 * @param t input of the gamma function
 * @return approximation of the logarithm of the gamma function
 */
double loggammaf(double t);

/*!
 * log-likelihood w(C|m) for Cp + 2Cs
 *
 * @param C number of counts in channel
 * @param m model function
 * @return log-likelihood w(C|m) for Cp + 2Cs
 */
double wcm(int C, double m);

/*!
 * Compute the -log-likelihood for Cp + 2Cs of a single micro time channel.
 *
 * Compute score of model counts in a parallel and perpendicular detection channel
 * and the experimental counts for a micro time channel.
 *
 * This function computes a score for the experimental counts (C) in a channel
 * where the experimental counts were computed by the sum of the counts in the
 * parallel (P) and the perpendicular (S) channel by the equation C = P + 2 S.
 *
 * This function considers that the number of counts C = P + 2S is not Poissonian.
 * The score relates to a maximum likelihood function.
 *
 * @param C number of experimental counts (P + 2 S) in a micro time channel
 * @param mp number of counts of the model in parallel detection channel
 * @param ms number of counts of the model in the perpendicular detection channel
 * @return
 */
double wcm_p2s(int C, double mp, double ms);

/*!
 * Compute the overall -log-likelihood for Cp + 2Cs for all micro time channels
 *
 * @param C array of experimental counts in Jordi format
 * @param M array model function in Jordi format
 * @param Nchannels number of micro time channels in parallel and perpendicular
 * (half the number of elements in C and M).
 * @return -log-likelihood for Cp + 2Cs for all micro time channels
 */
double Wcm_p2s(const int* C, double* M, int Nchannels);

/*!
 * Compute overall 2I* for Cp + 2Cs
 *
 * This function computes the overall 2I* for the model function Cp + 2Cs that
 * is computed by parallel signal (Cp) and the perpendicular signal (Cs). For
 * the definition of function 2I* see "An Experimental Comparison of the
 * Maximum Likelihood Estimation and Nonlinear Least-Squares Fluorescence Lifetime
 * Analysis of Single Molecules, Michael Maus, Mircea Cotlet, Johan Hofkens,
 * Thomas Gensch, Frans C. De Schryver, J. Schaffer, and C. A. M. Seidel, Anal.
 * Chem. 2001, 73, 9, 2078–2086".
 *
 * @param C array of experimental counts in Jordi format
 * @param M array model function in Jordi format
 * @param Nchannels number of micro time channels in parallel and perpendicular
 * (half the number of elements in C and M).
 * @return 2I* for Cp + 2Cs
 */
double twoIstar_p2s(const int* C, double* M, int Nchannels);

/*!
 * Compute overall 2I* for Cp & Cs
 *
 * This function computes 2I* for Cp and Cs. Cp and Cs are the model signals in
 * the parallel and perpendicular channel. Contrary to twoIstar_p2s the overall
 * 2I* is the sum of 2I* for Cp and Cs.
 *
 * @param C array of experimental counts in Jordi format
 * @param M array model function in Jordi format
 * @param Nchannels number of micro time channels in parallel and perpendicular
 * (half the number of elements in C and M).
 * @return 2I* for Cp & Cs
 */
double twoIstar(const int* C, double* M, int Nchannels);

/*!
 * Compute overall -log-likelihood for Cp & Cs
 *
 * @param C array of experimental counts in Jordi format
 * @param M array model function in Jordi format
 * @param Nchannels number of micro time channels in parallel and perpendicular
 * (half the number of elements in C and M).
 * @return -log-likelihood for Cp & Cs
 */
double Wcm(const int* C, double* M, int Nchannels);


namespace statistics{

    inline double neyman(double* data, double *model, int start, int stop){
        double chi2 = 0.0;
        for(int i = start; i < stop; i++){
            double mu = model[i];
            double m = std::max(1., data[i]);
            chi2 += (mu - m) * (mu - m) / m;
        }
        return chi2;
    }

    inline double poisson(double* data, double *model, int start, int stop){
        double chi2 = 0.0;
        for(int i = start; i < stop; i++){
            double mu = model[i];
            double m = data[i];
            chi2 += 2 * std::abs(mu);
            chi2 -= 2 * m * (1 + log(std::max(1.0, mu) / std::max(1.0, m)));
        }
        return chi2;
    }

    inline double pearson(double* data, double *model, int start, int stop){
        double chi2 = 0.0;
        for(int i = start; i < stop; i++){
            double m = model[i];
            double d = data[i];
            if (m > 0) {
                chi2 += (m-d) / m;
            }
        }
        return chi2;
    }

    inline double gauss(double* data, double *model, int start, int stop){
        double chi2 = 0.0;
        for(int i = start; i < stop; i++){
            double mu = model[i];
            double m = data[i];
            double mu_p = std::sqrt(.25 + m * m) - 0.5;
            if(mu_p <= 1.e-12) continue;
            chi2 += (mu - m) * (mu - m) / mu + std::log(mu/mu_p) - (mu_p - m) * (mu_p - m) / mu_p;
        }
        return chi2;
    }

    inline double cnp(double* data, double *model, int start, int stop){
        double chi2 = 0.0;
        for(int i = start; i < stop; i++){
            double m = data[i];
            double mu = model[i];
            if(m <= 1e-12) continue;
            chi2 += (mu - m) * (mu - m) / (3. / (1./m + 2./mu));
        }
        return chi2;
    }

    /// Sum of squared weighted residuals
    inline double sswr(double* data, double *model, double *data_noise, int start, int stop){
        double chi2 = 0.0;
        for(int i=start;i<stop;i++){
            double d = (data[i] - model[i]) / data_noise[i];
            chi2 += d * d;
        }
        return chi2;
    }

    /*!
     * @brief Computes different chi2 measures for counting data.
     *
     * Reference: https://arxiv.org/pdf/1903.07185.pdf
     *
     * @param data Vector containing the data.
     * @param model Vector containing the model.
     * @param weights Vector containing the weights.
     * @param x_min Minimum index for computation.
     * @param x_max Maximum index for computation.
     * @param type Type of chi2 measure (default is "neyman").
     * @return The computed chi2 value.
     */
    double chi2_counting(
            std::vector<double> &data,
            std::vector<double> &model,
            std::vector<double> &weights,
            int x_min = -1,
            int x_max = -1,
            const char* type = "neyman"
    );

}

#endif //TTTRLIB_DECAYSTATISTICS_H
