// SPDX-License-Identifier: BSD-3-Clause
%{
#include "DecayFit23.h"
%}


// Release the Python GIL around the (pure C++, callback-free) single fit and
// the batch fit so they can be parallelised across Python threads. The batch
// entry point releases the GIL for the *whole* loop, avoiding per-fit GIL
// handoff (which otherwise prevents threaded batch fitting from scaling).
%exception DecayFit23::fit {
  Py_BEGIN_ALLOW_THREADS
  $action
  Py_END_ALLOW_THREADS
}
%exception DecayFit23::fit_matrix {
  Py_BEGIN_ALLOW_THREADS
  $action
  Py_END_ALLOW_THREADS
}

%apply (double* IN_ARRAY2, int DIM1, int DIM2) {(double* data_in, int n_rows, int n_cols)};
%apply (double* INPLACE_ARRAY2, int DIM1, int DIM2) {(double* out, int n_out_rows, int n_out_cols)};
%apply (double* IN_ARRAY1, int DIM1) {(double* x0, int n_x0)};
%apply (short* IN_ARRAY1, int DIM1) {(short* fixed_in, int n_fixed)};

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

    // Batch maximum-likelihood fit of many decays in a single call.
    //
    // `data_in` is a (n_rows x n_cols) row-major matrix of counting histograms
    // in Jordi format (n_cols = 2 * n_channels). Every row is fitted from the
    // shared start values `x0` = [tau, gamma, r0, rho] with the `fixed` mask,
    // using the IRF / background / corrections held in `p`. Results are written
    // to `out`, a (n_rows x n_out_cols) matrix with n_out_cols >= 5:
    // [tau, gamma, r0, rho, 2I*] (+ [rS, rE] when n_out_cols >= 7).
    //
    // The whole loop is pure C++ and runs with the GIL released (see the
    // %exception above), so several threads can each fit a chunk of rows
    // concurrently without the per-fit GIL handoff that serialises the
    // single-fit entry point. Input shapes are validated by the Python wrapper;
    // on a bad shape this is a silent no-op (no Python C-API is touched while
    // the GIL is released).
    static void fit_matrix(
        double* data_in, int n_rows, int n_cols,
        double* x0, int n_x0,
        short* fixed_in, int n_fixed,
        double bifl_scatter, double p2s_flag,
        DecayFitData* p,
        double* out, int n_out_rows, int n_out_cols
    ){
        if (n_x0 < 4 || n_fixed < 4 || n_out_rows != n_rows || n_out_cols < 5)
            return;
        std::vector<short> f(fixed_in, fixed_in + n_fixed);
        if (f.size() < 6) f.resize(6, 0);
        if (static_cast<int>(p->model.size()) < n_cols) p->model.resize(n_cols, 0.0);
        p->data.resize(n_cols);
        for (int i = 0; i < n_rows; ++i) {
            const double* row = data_in + static_cast<size_t>(i) * n_cols;
            for (int c = 0; c < n_cols; ++c) p->data[c] = static_cast<int>(row[c]);
            double x[8];
            x[0] = x0[0]; x[1] = x0[1]; x[2] = x0[2]; x[3] = x0[3];
            x[4] = bifl_scatter; x[5] = p2s_flag; x[6] = 0.0; x[7] = 0.0;
            double ti = DecayFit23::fit(x, f.data(), p);
            double* o = out + static_cast<size_t>(i) * n_out_cols;
            o[0] = x[0]; o[1] = x[1]; o[2] = x[2]; o[3] = x[3]; o[4] = ti;
            if (n_out_cols > 5) o[5] = x[6];
            if (n_out_cols > 6) o[6] = x[7];
        }
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

%clear (double* data_in, int n_rows, int n_cols);
%clear (double* out, int n_out_rows, int n_out_cols);
%clear (double* x0, int n_x0);
%clear (short* fixed_in, int n_fixed);
%exception;
