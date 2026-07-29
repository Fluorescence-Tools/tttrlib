// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_DECAYFIT26_H
#define TTTRLIB_DECAYFIT26_H

#include <nlohmann/json.hpp>
#include "DecayFit.h"
#include "DecayConvolution.h"
#include "DecayStatistics.h"

using json = nlohmann::json;

class DecayFit26 : DecayFit {


public:

    /*!
    * Correct input for fit 26
    *
    * Constrains the fraction x1 of the first pattern to (0 < x1 < 1).
    *
    * @param x[in] x[0] fraction of first pattern
    * @param xm[out] xm[0] corrected fraction of first pattern
    */
    static void correct_input(double* x, double* xm);

    static double targetf(double* x, void* pv);

    /*!
     * Pattern-fit
     *
     * Fits the fraction of a mixture of two patterns
     *
     * The two patterns are set by the attributes irf and background of the DecayFitContext
     * structure.
     *
     * @param x [0] fraction of pattern 1
     * @param fixed not used
     * @param p an instance of DecayFitContext that contains the patterns. The fist pattern is
     * contained in the instrument response function array, the second in the background,
     * array, the experimental data is in the array expdata.
     * @return
     */
    /*! \brief Score \p x without optimising (runs the preamble ``fit`` does). */
    static double evaluate(double* x, short* fixed, DecayFitContext* p);
    static double fit(double* x, short* fixed, DecayFitContext* p);

};


#endif //TTTRLIB_DECAYFIT26_H
