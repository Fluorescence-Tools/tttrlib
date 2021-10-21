#ifndef FIT2X_DECAYFIT24_H
#define FIT2X_DECAYFIT24_H

#include "DecayFit.h"
#include "fsconv.h"
#include "DecayStatistics.h"



class DecayFit24 : DecayFit {


public:

    /*!
     * @brief Bi-exponential model function
     *
     * Bi-exponential model function with two fluorescence lifetimes
     * tau1, tau2 and amplitude of the second lifetime A2, fraction scattered
     * light gamma, and a constant offset. A2 (A1 + A2 = 1)
     *
     * The model function does not describe anisotropy. The decays passed as a
     * Jordi format are treated identical in first and the second channel of the
     * stacked arrays.
     *
     * mfunction[i] * (1. - gamma) / sum_m + bg[i] * gamma / sum_s + offset
     *
     * @param[in] param array containing the parameters of the model
     * [0] tau1, [1] gamma, [2] tau2, [3] A2, [4] offset
     * @param[in] irf instrument response function in Jordi format
     * @param bg[in] background pattern in Jordi format
     * @param Nchannels[in] number of channels (half the length of the Jordi arrays)
     * @param dt[in] time difference between two consecutive counting channels
     * @param corrections[in] [0] excitation period, [1] unused, [2] unused,
     * [3] unused, [4] convolution stop channel.
     * @param mfunction[out] output array of the computed decay in Jordi format. The
     * output array has to have twice the number of channels. It needs to be allocated
     * by beforehand.
     * @return
     */
    static int modelf(double *param,            // here: [tau1 gamma tau2 A2 offset]
               double *irf,
               double *bg,
               int Nchannels,
               double dt,            // time per channel
               double *corrections,        // [period g l1 l2]
               double *mfunction        // out: model function in Jordi-girl format
    );


    /*!
     * @brief Target function (to minimize) for fit23
     *
     * @param[in] x array containing the parameters of the model
     * [0] tau1, [1] gamma, [2] tau2, [3] A2, [4] offset
     * @param pv[in] a pointer to a MParam structure that contains the data and
     * a set of corrections.
     * @return a normalized chi2
     */
    static double targetf(double *x, void *pv);


    /*!
     * Fit a bi-exponential decay model
     *
     * This function fits a bi-exponential decay model to two decays
     * that are stacked using global parameters for the lifetimes and
     * amplitudes.
     *
     * Bi-exponential model function with two fluorescence lifetimes
     * tau1, tau2 and amplitude of the second lifetime A2, fraction scattered
     * light gamma, and a constant offset. A2 (A1 + A2 = 1)
     *
     * The model function does not describe anisotropy. The decays passed as a
     * Jordi format are treated identical in first and the second channel of the
     * stacked arrays.
     *
     * The anisotropy is computed assuming that the first and the second part
     * of the Jordi input arrays are for parallel and perpendicular using the
     * correction array of the attribute p of the type MParam.
     *
     *
     * @param[in,out] x array containing the parameters of the model
     * [0] tau1, [1] gamma, [2] tau2, [3] A2, [4] offset, [5] BIFL scatter fit?
     * (flag) - if smaller than 0 uses soft bifl scatter fit (seems to be unused)
     * [6] r Scatter (output only), [7] r Experimental (output only)
     * @param fixed an array at least of length 5 for the parameters [0] tau1,
     * [1] gamma, [2] tau2, [3] A2, [4] offset. If a value is not set to fixed
     * the parameter is optimized.
     * @param p an instance of MParam that contains relevant information. Here,
     * experimental data, the instrument response function, and the background decay
     * are used.
     * @return Quality parameter 2I*
     */
    static double fit(double *x, short *fixed, MParam *p);


    /*!
     * @brief Correct input parameters and compute anisotropy for fit24
     *
     * limits (0.001 < A2 < 0.999), (0.001 < gamma < 0.999),
     * (tau1 > 0), (tau2 > 0), background > 0 (called offset in other places)
     *
     * @param x[in,out] [0] tau1, [1] gamma  [2] tau2, [3] A2, [4] background,
     * [5] BIFL scatter fit? (flag, not used), [6] anisotropy r (scatter corrected,
     * output), [7] anisotropy (no scatter correction, output)
     * @param xm[out] array for corrected parameters (amplied range)
     * @param corrections [1] g factor, [2] l1, [3] l3
     * @param return_r if true computes the anisotropy.
     * @return
     */
    static void correct_input(double *x, double *xm, LVDoubleArray *corrections, int return_r);


};


#endif //FIT2X_DECAYFIT24_H
