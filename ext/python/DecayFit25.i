// SPDX-License-Identifier: BSD-3-Clause
%{
#include "DecayFit25.h"
%}

%extend DecayFit25{

    // Cross-language fit25 entry point (Python/R/Java): plain numeric `x` and
    // `fixed` vectors + the DecayFitData container. Returns [2I*, fitted x...].
    // The classic in-place fit()/my_fit() entry points are unchanged.
    static std::vector<double> fit_v(std::vector<double> x,
                                     std::vector<int> fixed,
                                     DecayFitData* p){
        if (x.size() < 9) x.resize(9, 0.0);
        std::vector<short> f(fixed.begin(), fixed.end());
        if (f.size() < 6) f.resize(6, 0);
        double two_istar = DecayFit25::fit(x.data(), f.data(), p);
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
        return DecayFit25::targetf(x, p);
    }

    static double my_fit(double* x, int n_x, short* fixed, int n_fixed, DecayFitData* p){
        if (n_x != 9) {
            PyErr_Format(PyExc_ValueError,
                         "The length of the parameter vector must of length 9. "
                         "Arrays of length (%d) given",
                         n_x);
            return 0.0;
        }
        if (n_fixed < 5) {
            PyErr_Format(PyExc_ValueError,
                         "The length of the vector fixed be at least of length 5. "
                         "Array of lengths (%d) given",
                         n_fixed);
            return 0.0;
        }
        return DecayFit25::fit(x, fixed, p);
    }
}
%include "DecayFit25.h"

