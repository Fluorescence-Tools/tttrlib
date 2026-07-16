// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_DECAYFIT23_H
#define TTTRLIB_DECAYFIT23_H

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <sstream>

#include <nlohmann/json.hpp>

#include "i_lbfgs.h"
#include "DecayFitData.h"
#include "DecayConvolution.h"
#include "DecayStatistics.h"
#include "DecayFit.h"

using json = nlohmann::json;


class DecayFit23 {

public:

    static int modelf(
            double *param,
            double *irf,
            double *bg,
            int Nchannels,
            double dt,
            double *corrections,
            double *mfunction
    );

    static double targetf(double *x, void *pv);

    static double fit(double *x, short *fixed, DecayFitData *p);

    /**
     * Fast, allocation-free row fit for the unpolarized tau-only case.
     *
     * This is the exact single-exponential specialization used by batch
     * callers when gamma and r0 are zero, only tau is free, and the two IRF
     * halves are identical. The input row is converted with the same
     * double-to-int semantics as the generic Python batch wrapper. Returns
     * false when the preconditions are not met so callers can fall back to
     * fit(). On success, out contains [tau, gamma, r0, rho, 2I*] and, when
     * requested, the corrected and uncorrected anisotropies.
     */
    static bool fit_tau_only_unpolarized_row(
            const double *data,
            int n_cols,
            const double *x0,
            int n_x0,
            const short *fixed,
            int n_fixed,
            double bifl_scatter,
            double p2s_flag,
            DecayFitData *p,
            double *out,
            int n_out_cols,
            bool retain_model = true);

    /// Batch Fit23 entry point shared by all language bindings.
    static void fit_matrix(
            double *data_in,
            int n_rows,
            int n_cols,
            double *x0,
            int n_x0,
            short *fixed_in,
            int n_fixed,
            double bifl_scatter,
            double p2s_flag,
            DecayFitData *p,
            double *out,
            int n_out_rows,
            int n_out_cols);

    static void correct_input(double *x, double *xm, double *corrections, int return_r);

    static std::string fit_to_json(const double *x,
                                   const short *fixed,
                                   const DecayFitData *p,
                                   double result);

    static std::string modelf_to_json(const double *param,
                                      const double *irf,
                                      const double *bg,
                                      int Nchannels,
                                      double dt,
                                      const double *corrections,
                                      const double *mfunction,
                                      int result);

    static std::string to_json(const double *x,
                               const short *fixed,
                               const DecayFitData *p,
                               double result);

    static void from_json(const json &j,
                         double *x,
                         short *fixed);
};


#endif // TTTRLIB_DECAYFIT23_H
