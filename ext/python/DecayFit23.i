// SPDX-License-Identifier: BSD-3-Clause
%{
#include "DecayFit23.h"
%}


%extend DecayFit23{

    // Cross-language fit23 entry point (Python/R/Java). Takes the 8-parameter
    // start vector `x` and the `fixed` flags as plain numeric vectors (which every
    // binding marshals: numpy / R vectors / VectorDouble+VectorInt32), plus the
    // DecayFitData container. Returns a vector whose element 0 is 2I* (the fit
    // quality) followed by the 8 fitted parameters. The classic in-place
    // fit()/my_fit() entry points are unchanged.
    static std::vector<double> fit_v(std::vector<double> x,
                                     std::vector<int> fixed,
                                     DecayFitData* p){
        if (x.size() < 8) x.resize(8, 0.0);
        std::vector<short> f(fixed.begin(), fixed.end());
        if (f.size() < 6) f.resize(6, 0);
        double two_istar = DecayFit23::fit(x.data(), f.data(), p);
        std::vector<double> out;
        out.reserve(1 + x.size());
        out.push_back(two_istar);
        for (double v : x) out.push_back(v);
        return out;
    }

    static double my_fit(double* x, int n_x, short* fixed, int n_fixed, DecayFitData* p){
        if (n_x != 8) {
            PyErr_Format(
                    PyExc_ValueError,
                    "The length of the parameter vector must of length 8 Arrays of length (%d) given", n_x);
            return 0.0;
        }
        if (n_fixed < 4) {
            PyErr_Format(
                    PyExc_ValueError,
                    "The length of the vector fixed be at least of length 6 Arrays of lengths (%d) given", n_fixed);
            return 0.0;
        }
        return DecayFit23::fit(x, fixed, p);
    }

    static double my_targetf(double* x, int n_x, DecayFitData* p){
        if (n_x != 8) {
            PyErr_Format(
                    PyExc_ValueError,
                     "The length of the parameter vector must of length 8. Arrays of length (%d) given", n_x);
            return 0.0;
        }
        return DecayFit23::targetf(x, p);
    }

    static int my_modelf(
        double* param, int n_param,
        double* irf,int n_irf,
        double* bg,int n_bg,
        double dt,
        double* corrections,int n_corrections,
        double* mfunction, int n_mfunction
    ){
            if (n_irf != n_bg) {
                PyErr_Format(
                        PyExc_ValueError,
                        "IRF and Bg array should have same length. Arrays of lengths (%d,%d) given", n_irf, n_bg);
                return 0.0;
            }
            if (n_mfunction != n_bg) {
                PyErr_Format(
                        PyExc_ValueError,
                        "Output array should be of length inputs. Arrays of lengths (%d,%d) given", n_mfunction, n_bg);
                return 0.0;
            }
            if (n_param != 4) {
                PyErr_Format(
                        PyExc_ValueError,
                        "Parameter array should be of length 4. Arrays of length (%d) given", n_param);
                return 0.0;
            }
            if (n_corrections != 5) {
                PyErr_Format(
                        PyExc_ValueError,
                        "Corrections array should be of length 4. Arrays of length (%d) given", n_param);
                return 0.0;
            }
            return DecayFit23::modelf(param, irf, bg, n_mfunction / 2, dt, corrections, mfunction);
        }
}

%include "DecayFit23.h"