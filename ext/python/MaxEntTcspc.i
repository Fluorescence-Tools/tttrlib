// SPDX-License-Identifier: BSD-3-Clause
%{
#include "MaxEntTcspc.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

// ARGOUTVIEWM hands ownership to NumPy and frees with free(), so the copy out
// of the std::vector has to be malloc'd rather than new'd.
static double* mem_copy_out(const std::vector<double>& v) {
    double* p = (double*) std::malloc(std::max<size_t>(v.size(), 1) * sizeof(double));
    if (p != nullptr && !v.empty())
        std::memcpy(p, v.data(), v.size() * sizeof(double));
    return p;
}
%}

// The design-matrix builders return four arrays through reference parameters,
// which SWIG's std::vector typemaps turn into four *required inputs* that no
// caller can supply -- the functions were unusable from Python. They are the
// entry point ChiSurf's maximum-entropy plugin builds on, so they get flat
// NumPy bindings: arrays in, `(Fi, y, sigma, fit_additive)` out.
//
// Building the whole matrix in one call is the point. Going per column costs
// more in argument marshalling than the convolution itself (~30 us against a
// ~3 us kernel), so a per-column binding is slower than not compiling at all.
%apply(double* IN_ARRAY1, int DIM1) {(double* mem_decay, int n_mem_decay)}
%apply(double* IN_ARRAY1, int DIM1) {(double* mem_lamp, int n_mem_lamp)}
%apply(double* IN_ARRAY1, int DIM1) {(double* mem_tau, int n_mem_tau)}
%apply(double* IN_ARRAY1, int DIM1) {(double* mem_r, int n_mem_r)}
%apply(double* IN_ARRAY1, int DIM1) {(double* mem_donly, int n_mem_donly)}
%apply(double** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(double** mem_fi, int* n_mem_fi1, int* n_mem_fi2)}
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** mem_y, int* n_mem_y)}
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** mem_sigma, int* n_mem_sigma)}
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** mem_add, int* n_mem_add)}

%ignore tttrlib::tcspc_build_fi_lifetimes;
%ignore tttrlib::tcspc_build_fi_distances;
%rename (tcspc_build_fi_lifetimes) my_tcspc_build_fi_lifetimes;
%rename (tcspc_build_fi_distances) my_tcspc_build_fi_distances;

%inline %{

void my_tcspc_build_fi_lifetimes(
        double* mem_decay, int n_mem_decay,
        double* mem_lamp, int n_mem_lamp,
        double dt,
        double* mem_tau, int n_mem_tau,
        double timeshift, double background, double lamp_scatter,
        int fitstart, int fitstop, double period,
        double** mem_fi, int* n_mem_fi1, int* n_mem_fi2,
        double** mem_y, int* n_mem_y,
        double** mem_sigma, int* n_mem_sigma,
        double** mem_add, int* n_mem_add
){
    std::vector<double> Fi, y, sigma, add;
    tttrlib::tcspc_build_fi_lifetimes(
        std::vector<double>(mem_decay, mem_decay + n_mem_decay),
        std::vector<double>(mem_lamp, mem_lamp + n_mem_lamp),
        dt,
        std::vector<double>(mem_tau, mem_tau + n_mem_tau),
        timeshift, background, lamp_scatter,
        fitstart, fitstop, period,
        Fi, y, sigma, add
    );
    *n_mem_fi1 = (int) y.size();
    *n_mem_fi2 = n_mem_tau;
    *mem_fi = mem_copy_out(Fi);
    *n_mem_y = (int) y.size();
    *mem_y = mem_copy_out(y);
    *n_mem_sigma = (int) sigma.size();
    *mem_sigma = mem_copy_out(sigma);
    *n_mem_add = (int) add.size();
    *mem_add = mem_copy_out(add);
}

void my_tcspc_build_fi_distances(
        double* mem_decay, int n_mem_decay,
        double* mem_lamp, int n_mem_lamp,
        double dt,
        double* mem_r, int n_mem_r,
        double tau0, double R0,
        double* mem_donly, int n_mem_donly,
        double x_donly,
        double timeshift, double background, double lamp_scatter,
        int fitstart, int fitstop, double period,
        double irf_background,
        double** mem_fi, int* n_mem_fi1, int* n_mem_fi2,
        double** mem_y, int* n_mem_y,
        double** mem_sigma, int* n_mem_sigma,
        double** mem_add, int* n_mem_add
){
    std::vector<double> Fi, y, sigma, add;
    tttrlib::tcspc_build_fi_distances(
        std::vector<double>(mem_decay, mem_decay + n_mem_decay),
        std::vector<double>(mem_lamp, mem_lamp + n_mem_lamp),
        dt,
        std::vector<double>(mem_r, mem_r + n_mem_r),
        tau0, R0,
        std::vector<double>(mem_donly, mem_donly + n_mem_donly),
        x_donly,
        timeshift, background, lamp_scatter,
        fitstart, fitstop, period, irf_background,
        Fi, y, sigma, add
    );
    *n_mem_fi1 = (int) y.size();
    *n_mem_fi2 = n_mem_r;
    *mem_fi = mem_copy_out(Fi);
    *n_mem_y = (int) y.size();
    *mem_y = mem_copy_out(y);
    *n_mem_sigma = (int) sigma.size();
    *mem_sigma = mem_copy_out(sigma);
    *n_mem_add = (int) add.size();
    *mem_add = mem_copy_out(add);
}

%}

%include "MaxEntTcspc.h"

%clear (double* mem_decay, int n_mem_decay);
%clear (double* mem_lamp, int n_mem_lamp);
%clear (double* mem_tau, int n_mem_tau);
%clear (double* mem_r, int n_mem_r);
%clear (double* mem_donly, int n_mem_donly);
%clear (double** mem_fi, int* n_mem_fi1, int* n_mem_fi2);
%clear (double** mem_y, int* n_mem_y);
%clear (double** mem_sigma, int* n_mem_sigma);
%clear (double** mem_add, int* n_mem_add);
