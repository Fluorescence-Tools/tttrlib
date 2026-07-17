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
//
// The b*/f* parameter names are unique on purpose: patterns like
// (double* data, int n_data) or (double* irf, int n_irf) already carry
// INPLACE typemaps from DecayConvolution.i / DecayFit.i, and re-%apply-ing
// IN_ARRAY1 onto them leaves the INPLACE argout typemap attached in the R
// wrapper (it references locals the IN_ARRAY1 in-typemap does not declare,
// so the generated C++ does not compile).
%apply (double* IN_ARRAY1, int DIM1) {
    (double* bdata, int n_bdata),
    (double* birf, int n_birf),
    (double* bbackground, int n_bbackground),
    (double* blifetimes, int n_blifetimes),
    (double* bamplitudes, int n_bamplitudes),
    (double* fdata, int n_fdata),
    (double* firf, int n_firf),
    (double* fbackground, int n_fbackground),
    (double* flifetimes, int n_flifetimes),
    (double* famplitudes, int n_famplitudes)
};
%apply (int* IN_ARRAY1, int DIM1) {(int* blifetime_fixed, int n_bfixed)};
%include "DecayFitNExp.h"
