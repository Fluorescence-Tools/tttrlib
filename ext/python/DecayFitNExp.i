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
TTTRLIB_NEXP_R_EXCEPTION(DecayFitNExp::fit_batch_flat)
#endif

// VectorDouble and VectorInt32 are declared by misc_types.i before this file is
// included from DecayFit.i.  Keeping the interface vector-based avoids custom
// NumPy ownership or lifetime rules and is also suitable for the R/Java SWIG
// surfaces if they opt into this core later.
%include "DecayFitNExp.h"
