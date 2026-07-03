// SPDX-License-Identifier: BSD-3-Clause
%{
#include "DecayFit26.h"
%}

%extend DecayFit26{

    // Cross-language fit26 entry point (Python/R/Java): plain numeric `x` and
    // `fixed` vectors + the DecayFitData container. Returns [2I*, fitted x...].
    // The classic in-place fit()/my_fit() entry points are unchanged.
    static std::vector<double> fit_v(std::vector<double> x,
                                     std::vector<int> fixed,
                                     DecayFitData* p){
        if (x.size() < 2) x.resize(2, 0.0);
        std::vector<short> f(fixed.begin(), fixed.end());
        if (f.size() < 6) f.resize(6, 0);
        double two_istar = DecayFit26::fit(x.data(), f.data(), p);
        std::vector<double> out;
        out.reserve(1 + x.size());
        out.push_back(two_istar);
        for (double v : x) out.push_back(v);
        return out;
    }


    static double my_targetf(double* x, int n_x, DecayFitData* p){
        if (n_x != 8) {
            PyErr_Format(PyExc_ValueError,
                         "The length of the parameter vector must of length 8. "
                         "Arrays of length (%d) given",
                         n_x);
            return 0.0;
        }
        return DecayFit26::targetf(x, p);
    }

    static double my_fit(double* x, int n_x, short* fixed, int n_fixed, DecayFitData* p){
        // fit26 writes the fitted fraction to x[0] and the complementary
        // fraction to x[1]; a 1-element array would overflow (heap corruption)
        if (n_x < 2) {
            PyErr_Format(PyExc_ValueError,
                         "The parameter vector must be at least of length 2 "
                         "(x[0] fraction in, x[1] complementary fraction out). "
                         "Array of length (%d) given",
                         n_x);
            return 0.0;
        }
        if (n_fixed < 1) {
            PyErr_Format(PyExc_ValueError,
                         "The length of the vector fixed be at least of length 1. "
                         "Array of lengths (%d) given",
                         n_fixed);
            return 0.0;
        }
        return DecayFit26::fit(x, fixed, p);
    }
};

%include "DecayFit26.h"
