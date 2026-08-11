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

// A borrowed NumPy buffer as the vector the C++ signatures take. This is a
// memcpy-rate copy inside C++ (~0.5 ns/element); the thing being avoided is the
// *Python-side* conversion, which boxes one PyFloat per element.
static inline std::vector<double> mem_in(const double* p, int n) {
    return (p == nullptr || n <= 0) ? std::vector<double>()
                                    : std::vector<double>(p, p + n);
}
%}

// This file is absent from ext/{r,java,js}/tttrlib.i, so the tcspc_* family is
// Python-only today -- see T-20260811-09. It does not need a per-language
// surface to be added back: `IN_ARRAY1` and `ARGOUTVIEWM_ARRAY1` are
// implemented for R, Java and JavaScript too (ext/r/rarrays.i,
// ext/java/jarrays.i, ext/js/jsarrays.i) against native vectors, arrays and
// TypedArrays. Adding the %include is the whole job.

// Every array here crosses as a NumPy buffer, never as `VectorDouble`.
//
// SWIG's default `std::vector<double>` typemaps convert through the Python
// sequence protocol -- one boxed float per element, ~50 ns each, in and out.
// That made the *wrapper* set the runtime of these calls: a fractional IRF
// shift of ZERO channels cost 24.4 us of the 24.8 us a real shift cost, 98% of
// it conversion, on a 512-channel decay. Measured against the NumPy in-place
// `fconv` doing the same arithmetic: 9.1 vs 1.3 us at n=64, 26.5 vs 2.6 at 512,
// 335 vs 10.8 at 4096, 1360 vs 68 at 16384.
//
// The design-matrix builders had a second problem on top of the cost: they
// return four arrays through `std::vector&` out-parameters, which the default
// typemaps turn into four *required inputs* that no caller can supply. They
// shipped compiled, exported, documented -- and uncallable.
//
// Building the whole matrix in one call is the point of exposing the builders
// at all. Going per column costs 1668 us for 60 columns against 58.5 us for the
// one call, so a per-column binding is slower than not compiling at all. The
// kernels below are exposed so the port can be verified piece by piece; they
// are not an API to build a loop on. See okf/bindings/marshalling-cost.md.
%ignore tttrlib::tcspc_shift_lamp;
%ignore tttrlib::tcspc_fconv_single_shot;
%ignore tttrlib::tcspc_fconv_periodic;
%ignore tttrlib::tcspc_quadpr_bound;
%ignore tttrlib::tcspc_run_mem;
%ignore tttrlib::tcspc_build_fi_lifetimes;
%ignore tttrlib::tcspc_build_fi_distances;
%ignore tttrlib::solve_tcspc_mem_lifetime;
%ignore tttrlib::solve_tcspc_mem_fret;

%rename (tcspc_shift_lamp)          my_tcspc_shift_lamp;
%rename (tcspc_fconv_single_shot)   my_tcspc_fconv_single_shot;
%rename (tcspc_fconv_periodic)      my_tcspc_fconv_periodic;
%rename (tcspc_quadpr_bound)        my_tcspc_quadpr_bound;
%rename (tcspc_run_mem)             my_tcspc_run_mem;
%rename (tcspc_build_fi_lifetimes)  my_tcspc_build_fi_lifetimes;
%rename (tcspc_build_fi_distances)  my_tcspc_build_fi_distances;
%rename (solve_tcspc_mem_lifetime)  my_solve_tcspc_mem_lifetime;
%rename (solve_tcspc_mem_fret)      my_solve_tcspc_mem_fret;

// The argument names are the public keyword names, so they must match the
// documented signatures exactly -- callers pass `nu=`, `prior=`, `max_iter=`.
%apply(double* IN_ARRAY1, int DIM1) {(double* decay, int n_decay)}
%apply(double* IN_ARRAY1, int DIM1) {(double* lamp, int n_lamp)}
%apply(double* IN_ARRAY1, int DIM1) {(double* lampsh, int n_lampsh)}
%apply(double* IN_ARRAY1, int DIM1) {(double* amps, int n_amps)}
%apply(double* IN_ARRAY1, int DIM1) {(double* taus, int n_taus)}
%apply(double* IN_ARRAY1, int DIM1) {(double* tau, int n_tau)}
%apply(double* IN_ARRAY1, int DIM1) {(double* R, int n_R)}
%apply(double* IN_ARRAY1, int DIM1) {(double* donly, int n_donly)}
%apply(double* IN_ARRAY1, int DIM1) {(double* C, int n_C)}
%apply(double* IN_ARRAY1, int DIM1) {(double* d, int n_d)}
%apply(double* IN_ARRAY1, int DIM1) {(double* H, int n_H)}
%apply(double* IN_ARRAY1, int DIM1) {(double* g0, int n_g0)}
%apply(double* IN_ARRAY1, int DIM1) {(double* m, int n_m)}
%apply(double* IN_ARRAY1, int DIM1) {(double* prior, int n_prior)}

// `prior` is optional -- empty means a uniform prior. A numpy `IN_ARRAY1` pair
// cannot simply be left off, so give the pair a `default` typemap: SWIG then
// treats the argument as optional and runs the `in` typemap only when it is
// actually supplied.
%typemap(default) (double* prior, int n_prior) {
    $1 = nullptr;
    $2 = 0;
}

%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** out, int* n_out)}
%apply(double** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(double** Fi, int* n_Fi1, int* n_Fi2)}
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** y, int* n_y)}
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** sigma, int* n_sigma)}
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** fit_additive, int* n_fit_additive)}

// Included before the %inline block so `MemTcspcResult` is a known type by the
// time the solver wrappers return one by value -- otherwise SWIG hands back an
// opaque pointer and `result.p` stops working.
%include "MaxEntTcspc.h"

%inline %{

void my_tcspc_shift_lamp(
        double* lamp, int n_lamp, double ts_channels,
        double** out, int* n_out
){
    auto v = tttrlib::tcspc_shift_lamp(mem_in(lamp, n_lamp), ts_channels);
    *n_out = (int) v.size();
    *out = mem_copy_out(v);
}

void my_tcspc_fconv_single_shot(
        double* lampsh, int n_lampsh, double dt,
        double* amps, int n_amps,
        double* taus, int n_taus,
        int stop,
        double** out, int* n_out
){
    auto v = tttrlib::tcspc_fconv_single_shot(
        mem_in(lampsh, n_lampsh), dt,
        mem_in(amps, n_amps), mem_in(taus, n_taus), stop
    );
    *n_out = (int) v.size();
    *out = mem_copy_out(v);
}

void my_tcspc_fconv_periodic(
        double* lampsh, int n_lampsh, double dt,
        double* amps, int n_amps,
        double* taus, int n_taus,
        int start, int stop, double period,
        double** out, int* n_out
){
    auto v = tttrlib::tcspc_fconv_periodic(
        mem_in(lampsh, n_lampsh), dt,
        mem_in(amps, n_amps), mem_in(taus, n_taus), start, stop, period
    );
    *n_out = (int) v.size();
    *out = mem_copy_out(v);
}

void my_tcspc_quadpr_bound(
        double* C, int n_C,
        double* d, int n_d,
        double lower_bound,
        double** out, int* n_out
){
    auto v = tttrlib::tcspc_quadpr_bound(
        mem_in(C, n_C), mem_in(d, n_d), lower_bound
    );
    *n_out = (int) v.size();
    *out = mem_copy_out(v);
}

tttrlib::MemTcspcResult my_tcspc_run_mem(
        double* H, int n_H,
        double* g0, int n_g0,
        double* m, int n_m,
        double const_chi2, double nu,
        int max_iter = 200, double tol = 1e-4, double min_prob = 1e-12
){
    return tttrlib::tcspc_run_mem(
        mem_in(H, n_H), mem_in(g0, n_g0), mem_in(m, n_m),
        const_chi2, nu, max_iter, tol, min_prob
    );
}

void my_tcspc_build_fi_lifetimes(
        double* decay, int n_decay,
        double* lamp, int n_lamp,
        double dt,
        double* tau, int n_tau,
        double timeshift, double background, double lamp_scatter,
        int fitstart, int fitstop, double period,
        double** Fi, int* n_Fi1, int* n_Fi2,
        double** y, int* n_y,
        double** sigma, int* n_sigma,
        double** fit_additive, int* n_fit_additive
){
    std::vector<double> vFi, vy, vsigma, vadd;
    tttrlib::tcspc_build_fi_lifetimes(
        mem_in(decay, n_decay), mem_in(lamp, n_lamp), dt, mem_in(tau, n_tau),
        timeshift, background, lamp_scatter,
        fitstart, fitstop, period,
        vFi, vy, vsigma, vadd
    );
    *n_Fi1 = (int) vy.size();
    *n_Fi2 = n_tau;
    *Fi = mem_copy_out(vFi);
    *n_y = (int) vy.size();
    *y = mem_copy_out(vy);
    *n_sigma = (int) vsigma.size();
    *sigma = mem_copy_out(vsigma);
    *n_fit_additive = (int) vadd.size();
    *fit_additive = mem_copy_out(vadd);
}

void my_tcspc_build_fi_distances(
        double* decay, int n_decay,
        double* lamp, int n_lamp,
        double dt,
        double* R, int n_R,
        double tau0, double R0,
        double* donly, int n_donly,
        double x_donly,
        double timeshift, double background, double lamp_scatter,
        int fitstart, int fitstop, double period,
        double irf_background,
        double** Fi, int* n_Fi1, int* n_Fi2,
        double** y, int* n_y,
        double** sigma, int* n_sigma,
        double** fit_additive, int* n_fit_additive
){
    std::vector<double> vFi, vy, vsigma, vadd;
    tttrlib::tcspc_build_fi_distances(
        mem_in(decay, n_decay), mem_in(lamp, n_lamp), dt, mem_in(R, n_R),
        tau0, R0, mem_in(donly, n_donly), x_donly,
        timeshift, background, lamp_scatter,
        fitstart, fitstop, period, irf_background,
        vFi, vy, vsigma, vadd
    );
    *n_Fi1 = (int) vy.size();
    *n_Fi2 = n_R;
    *Fi = mem_copy_out(vFi);
    *n_y = (int) vy.size();
    *y = mem_copy_out(vy);
    *n_sigma = (int) vsigma.size();
    *sigma = mem_copy_out(vsigma);
    *n_fit_additive = (int) vadd.size();
    *fit_additive = mem_copy_out(vadd);
}

tttrlib::MemTcspcResult my_solve_tcspc_mem_lifetime(
        double* decay, int n_decay,
        double* lamp, int n_lamp,
        double dt,
        double* tau, int n_tau,
        double timeshift, double background, double lamp_scatter,
        int fitstart, int fitstop, double period,
        double nu = 1e-5,
        int max_iter = 200, double tol = 1e-4, double min_prob = 1e-12,
        double* prior = nullptr, int n_prior = 0
){
    return tttrlib::solve_tcspc_mem_lifetime(
        mem_in(decay, n_decay), mem_in(lamp, n_lamp), dt, mem_in(tau, n_tau),
        timeshift, background, lamp_scatter,
        fitstart, fitstop, period,
        nu, max_iter, tol, min_prob, mem_in(prior, n_prior)
    );
}

tttrlib::MemTcspcResult my_solve_tcspc_mem_fret(
        double* decay, int n_decay,
        double* lamp, int n_lamp,
        double dt,
        double* R, int n_R,
        double tau0, double R0,
        double* donly, int n_donly,
        double x_donly,
        double timeshift, double background, double lamp_scatter,
        int fitstart, int fitstop, double period,
        double irf_background = 0.0,
        double nu = 1e-5,
        int max_iter = 200, double tol = 1e-4, double min_prob = 1e-12,
        double* prior = nullptr, int n_prior = 0
){
    return tttrlib::solve_tcspc_mem_fret(
        mem_in(decay, n_decay), mem_in(lamp, n_lamp), dt, mem_in(R, n_R),
        tau0, R0, mem_in(donly, n_donly), x_donly,
        timeshift, background, lamp_scatter,
        fitstart, fitstop, period, irf_background,
        nu, max_iter, tol, min_prob, mem_in(prior, n_prior)
    );
}

%}

%clear (double* decay, int n_decay);
%clear (double* lamp, int n_lamp);
%clear (double* lampsh, int n_lampsh);
%clear (double* amps, int n_amps);
%clear (double* taus, int n_taus);
%clear (double* tau, int n_tau);
%clear (double* R, int n_R);
%clear (double* donly, int n_donly);
%clear (double* C, int n_C);
%clear (double* d, int n_d);
%clear (double* H, int n_H);
%clear (double* g0, int n_g0);
%clear (double* m, int n_m);
%clear (double* prior, int n_prior);
%clear (double** out, int* n_out);
%clear (double** Fi, int* n_Fi1, int* n_Fi2);
%clear (double** y, int* n_y);
%clear (double** sigma, int* n_sigma);
%clear (double** fit_additive, int* n_fit_additive);

