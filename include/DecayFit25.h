#ifndef TTTRLIB_DECAYFIT25_H
#define TTTRLIB_DECAYFIT25_H

#include "DecayConvolution.h"

#include "DecayStatistics.h"
#include "DecayFit.h"
#include "DecayFit23.h"


class DecayFit25 : DecayFit {


public:

    /*!
     * @brief adjust parameters for fit25 and compute anisotropy
     *
     * Makes sure that (0 < gamma < 0.999) and (0<rho).
     *
     * @param x
     * @param xm
     * @param corrections
     * @param return_r
     * @return
     */
    static void correct_input(double *x, double *xm, LVDoubleArray *corrections, int return_r);


    /*!
     * Function used to compute the target value in fit 25
     *
     * This is misleadingly named target25. Fit25 selects out of a set of 4 lifetimes
     * the lifetime that describes best the data.
     *
     * @param x
     * @param pv
     * @return
     */
    static double targetf(double *x, void *pv);


    /*!
     * @brief Selects the lifetime out of a set of 4 fixed lifetimes that best
     * describes the data.
     *
     * This function selects out of a set of 4 lifetimes tau the lifetime that fits
     * best the data and returns the lifetime through the parameter x[0].
     *
     * If softBIFL flag is set to (x[6] < 0) and fixed[4] is zero gamma is optimized
     * for each lifetime tau and the best gamma is returned by x[4]. The gamma is
     * fitted with fit23.
     *
     * @param[in,out] x array containing the parameters [0] tau1 output for best
     * tau (always fixed), [1] tau2 (always fixed), [2] tau3 (always fixed),
     * [3] tau4 (always fixed), [4] gamma (input, output),
     * [5] fundamental anisotropy r0, [6] BIFL scatter fit? (flag),
     * [7] r Scatter (output only), [8] r Experimental (output only)
     * @param fixed array that is of least of length 5. Only the element fixed[4]
     * is used. If fixed[4] is zero gamma is optimized for each lifetime.
     * @param p an instance of MParam that contains all relevant information, i.e.,
     * experimental data, the instrument response function, the needed corrections for
     * the anisotropy (g-factor, l1, l2)
     * @return
     */
    static double fit(double *x, short *fixed, MParam *p);

};

#endif //TTTRLIB_DECAYFIT25_H
