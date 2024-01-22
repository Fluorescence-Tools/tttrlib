#ifndef TTTRLIB_DECAYFIT23_H
#define TTTRLIB_DECAYFIT23_H

#include <iostream>
#include <cmath>
#include <algorithm>
#include <string>
#include <sstream>
#include <iomanip>      // std::setprecision

#include "i_lbfgs.h"
#include "LvArrays.h"
#include "DecayConvolution.h"
#include "DecayStatistics.h"
#include "DecayFit.h"



class DecayFit23 {

public:

    /*!
     * @brief Single exponential model function with single rotational
     * correlation time, with scatter contribution (BIFL scatter model)
     *
     * This function computes the fluorescence decay in the parallel and perpendicular
     * detection channel for a single exponential decay with a fluorescence lifetime
     * tau, a single rotational correlation time rho, and separate instrument response
     * functions for the parallel and perpendicular detection channels. The model
     * considers the faction of scattered light in the two detection channels by the
     * parameter gamma. The scattered light contribution is handled by patterns for the
     * light in the parallel and perpendicular detection channel.
     *
     * The instrument response function, the background, and the computed model function
     * are in the Jordi format, i.e., stacked one-dimensional arrays of the parallel and
     * perpendicular detection channel.
     *
     * @param[in] param array containing the model parameters [0] tau, [1] gamma, [2] r0, [3] rho
     * @param[in] irf instrument response function in Jordi format (parallel, perpendicular)
     * @param bg[in] background pattern in Jordi format (parallel, perpendicular)
     * @param Nchannels[in] number of channels (half the length of the Jordi arrays)
     * @param dt[in] time difference between two consecutive counting channels
     * @param corrections[in] [0] excitation period, [1] g factor, [2] l1, [3] l2,
     * [4] convolution stop channel number
     * @param mfunction[out] output array of the computed decay in Jordi format. The
     * output array has to have twice the number of channels. It needs to be allocated
     * by beforehand.
     * @return integer (not used, 0 by default)
     */
    static int modelf(
            double *param,
            double *irf,
            double *bg,
            int Nchannels,
            double dt,
            double *corrections,
            double *mfunction
    );

    /*!
     * @brief Target function (to minimize) for fit23
     *
     * Computes the model function 23 and returns a score that quantifies
     * the discrepancy between the data and the model.
     *
     * @param x[in,out] a vector of length that that contains the starting parameters
     * for the optimization and is used to return the optimized parameters.
     * [0] fluorescence lifetime - tau (in)
     * [1] fraction of scattered light - gamma (in)
     * [2] fundamental anisotropy - r0 (in)
     * [3] rotational correlation time - rho (in)
     * [4] if negative reduce contribution of background photons from scoring
     * function - Soft BIFL scatter fit? (flag) (in)
     * [5] specifies type of score that is returned - 2I*: P+2S? (flag), this parameter
     * only affects the returned score and does not influence the fitting (in)
     * [6] background corrected anisotropy - r Scatter (out)
     * [7] anisotropy without background correction - r Experimental (out)
     * @param pv[in] a pointer to a MParam structure that contains the data and
     * a set of corrections.
     * @return a normalized chi2
     */
    static double targetf(double *x, void *pv);


    /*!
     * Function that optimizes parameters of model23 to data.
     *
     * @param x[in,out] a vector of length that that contains the starting parameters
     * for the optimization and is used to return the optimized parameters.
     * [0] fluorescence lifetime - tau (in,out)
     * [1] fraction of scattered light - gamma (in,out)
     * [2] fundamental anisotropy - r0 ()
     * [3] rotational correlation time - rho (in,out)
     * [4] if negative reduce contribution of background photons from scoring
     * function - Soft BIFL scatter fit? (flag) (in)
     * [5] specifies type of score that is returned - 2I*: P+2S? (flag), this parameter
     * only affects the returned score and does not influence the fitting (in)
     * [6] background corrected anisotropy - r Scatter (out)
     * [7] anisotropy without background correction - r Experimental (out)
     * @param fixed an array at least of length 4 for the parameters tau, gamma, r0,
     * and rho that specifies if a parameter is optimized. If a value is set to 1,
     * the parameter is optimized.
     * @param p an instance of MParam that contains all relevant information, i.e.,
     * experimental data, the instrument response function, the needed corrections (
     * g-factor, l1, l2)
     * @return
     */
    static double fit(double* x, short* fixed, MParam* p);


    /*!
     * Correct input parameters and compute anisotropy
     *
     * This function corrects the input parameters for fit23 and takes care of
     * unreasonable values. The fluorescence lifetime is constraint to positive
     * values, gamma (the fraction of scattered light) is constraint to values between
     * 0.0 and 0.999, the rotational correlation time rho is (if the global variable
     * fixedrho is set to true) to the value that corresponds to the Perrin equation
     * (for the computed, experimental anisotropy). Moreover, this function computes
     * the anisotropy based on the corrected (g-factor, l1, l2, background) intensities,
     * if the variable return_r is set to true.
     *
     * @param x[in,out] array of length 8 that contains parameters
     * x[0] fluorescence lifetime - tau;
     * x[1] fraction of scattered light - gamma;
     * x[2] fundamental anisotropy - r0
     * x[3] rotational time - rho;
     * x[4] softbifl - flag specifying the type of bifl fit (not used here)
     * x[5] p2s_twoIstar - flag specifying the type of chi2 calculation (not used here)
     * x[6] background corrected anisotropy
     * x[7] anisotropy without background correction
     * @param xm[in,out] array that will contain the corrected parameters
     * @param corrections[in]
     * @param return_r[in] if set to true (positive) computes the anisotropy and returns
     * the scatter corrected and the signal (no scatter correction) anisotropy and
     * writes the values to the input/output vector x.
     */
    static void correct_input(double *x, double *xm, LVDoubleArray *corrections, int return_r);

};


#endif //TTTRLIB_DECAYFIT23_H
