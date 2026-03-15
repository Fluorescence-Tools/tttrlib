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
#include "LvArrays.h"
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

    static double fit(double *x, short *fixed, MParam *p);

    static void correct_input(double *x, double *xm, LVDoubleArray *corrections, int return_r);

    static std::string fit_to_json(const double *x,
                                   const short *fixed,
                                   const MParam *p,
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
                               const MParam *p,
                               double result);

    static void from_json(const json &j,
                         double *x,
                         short *fixed);
};


#endif // TTTRLIB_DECAYFIT23_H
