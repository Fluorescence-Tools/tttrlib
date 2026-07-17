// SPDX-License-Identifier: BSD-3-Clause
%{
#include "DecayFitNExp.h"
#include <exception>
#include <stdexcept>
%}

// Keep native validation failures inside the language boundary.  Python also
// releases the GIL while the CPU-only fit runs; Java and R need only exception
// translation because their runtimes do not have a Python-style global lock.
#ifdef SWIGPYTHON
TTTRLIB_NOGIL(DecayFitNExp::fit)
TTTRLIB_NOGIL(DecayFitNExp::fit_fixed_lifetimes)
TTTRLIB_NOGIL(DecayFitNExp::fit_buffers)
TTTRLIB_NOGIL(DecayFitNExp::fit_fixed_lifetimes_buffers)
TTTRLIB_NOGIL(DecayFitNExp::fit_batch_flat)
#elif defined(SWIGJAVA)
%define TTTRLIB_NEXP_JAVA_EXCEPTION(Method)
%exception Method {
    try {
        $action
    } catch (const std::invalid_argument& e) {
        SWIG_JavaThrowException(jenv, SWIG_JavaIllegalArgumentException, e.what());
        return $null;
    } catch (const std::exception& e) {
        SWIG_JavaThrowException(jenv, SWIG_JavaRuntimeException, e.what());
        return $null;
    }
}
%enddef
TTTRLIB_NEXP_JAVA_EXCEPTION(DecayFitNExp::fit)
TTTRLIB_NEXP_JAVA_EXCEPTION(DecayFitNExp::fit_fixed_lifetimes)
TTTRLIB_NEXP_JAVA_EXCEPTION(DecayFitNExp::fit_buffers)
TTTRLIB_NEXP_JAVA_EXCEPTION(DecayFitNExp::fit_fixed_lifetimes_buffers)
TTTRLIB_NEXP_JAVA_EXCEPTION(DecayFitNExp::fit_batch_flat)
#elif defined(SWIGR)
%define TTTRLIB_NEXP_R_EXCEPTION(Method)
%exception Method {
    try {
        $action
    } catch (const std::invalid_argument& e) {
        SWIG_exception(SWIG_ValueError, e.what());
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}
%enddef
TTTRLIB_NEXP_R_EXCEPTION(DecayFitNExp::fit)
TTTRLIB_NEXP_R_EXCEPTION(DecayFitNExp::fit_fixed_lifetimes)
TTTRLIB_NEXP_R_EXCEPTION(DecayFitNExp::fit_buffers)
TTTRLIB_NEXP_R_EXCEPTION(DecayFitNExp::fit_fixed_lifetimes_buffers)
TTTRLIB_NEXP_R_EXCEPTION(DecayFitNExp::fit_batch_flat)
#endif

// VectorDouble and VectorInt32 are declared by misc_types.i before this file is
// included from DecayFit.i.  Keeping the vector-based fit()/fit_fixed_lifetimes()
// avoids custom NumPy ownership rules and stays suitable for R/Java.
//
// The *_buffers() overloads take raw array buffers so Python can pass NumPy
// arrays directly (one copy each) instead of boxing every element through a
// Python list, which dominates the cost of a single small fit.
%apply (double* IN_ARRAY1, int DIM1) {
    (double* data, int n_data),
    (double* irf, int n_irf),
    (double* background, int n_background),
    (double* initial_lifetimes, int n_lifetimes),
    (double* initial_amplitudes, int n_amplitudes),
    (double* fdata, int n_fdata),
    (double* firf, int n_firf),
    (double* fbackground, int n_fbackground),
    (double* flifetimes, int n_flifetimes),
    (double* famplitudes, int n_famplitudes)
};
%apply (int* IN_ARRAY1, int DIM1) {(int* lifetime_fixed, int n_fixed)};
%include "DecayFitNExp.h"
