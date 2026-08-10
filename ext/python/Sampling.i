// SPDX-License-Identifier: BSD-3-Clause
%{
#include <stdexcept>
#include <string>
#include "Sampling.h"
%}

// The parameter names below are deliberately prefixed. `%apply` is global and
// keyed by parameter *name*, so a plain `(double* weights, int n_weights)`
// here would silently redefine the mapping Histogram.i already establishes for
// its own `weights` argument. Prefixed names cannot collide with anything.
%apply(double* IN_ARRAY1, int DIM1) {(double* sampling_weights, int n_sampling_weights)}
%apply(double* IN_ARRAY1, int DIM1) {(double* sampling_axis, int n_sampling_axis)}
%apply(double* IN_ARRAY1, int DIM1) {(double* sampling_cdf, int n_sampling_cdf)}
%apply(unsigned int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(unsigned int** sampling_idx_out, int* n_sampling_idx_out)}
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** sampling_val_out, int* n_sampling_val_out)}

// A bad argument is a ValueError, not a RuntimeError. Setting a Python error
// with PyErr_Format and then returning normally is not enough -- SWIG sees a
// result *and* a pending exception and raises SystemError on top, which buries
// the message that says what was actually wrong.
%exception {
    try {
        $action
    } catch (const std::invalid_argument& e) {
        SWIG_exception(SWIG_ValueError, e.what());
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}

// The header takes caller-provided output buffers, which is the right C++
// surface. Python would rather be handed a fresh array, so the bindings
// allocate and return one.
%ignore tttrlib::weighted_choice;
%ignore tttrlib::sample_from_cdf;

%inline %{

/*!
 * \brief Draw `n` indices in proportion to `weights`.
 * \return A uint32 array of length `n`.
 */
void weighted_choice(
        double* sampling_weights, int n_sampling_weights,
        int n,
        unsigned int** sampling_idx_out, int* n_sampling_idx_out
){
    if (n < 0) {
        throw std::invalid_argument(
            "n must not be negative (got " + std::to_string(n) + ").");
    }
    auto* buffer = static_cast<unsigned int*>(malloc(sizeof(unsigned int) * (n > 0 ? n : 1)));
    tttrlib::weighted_choice(sampling_weights, n_sampling_weights, buffer, n);
    *sampling_idx_out = buffer;
    *n_sampling_idx_out = n;
}

/*!
 * \brief Draw `n` samples by inverting the tabulated CDF over `axis`.
 * \return A float64 array of length `n`.
 */
void sample_from_cdf(
        double* sampling_axis, int n_sampling_axis,
        double* sampling_cdf, int n_sampling_cdf,
        int n,
        double** sampling_val_out, int* n_sampling_val_out,
        bool normalize = true
){
    if (n_sampling_axis != n_sampling_cdf) {
        throw std::invalid_argument(
            "axis and cdf must have the same length (got "
            + std::to_string(n_sampling_axis) + " and "
            + std::to_string(n_sampling_cdf) + ").");
    }
    if (n < 0) {
        throw std::invalid_argument(
            "n must not be negative (got " + std::to_string(n) + ").");
    }
    auto* buffer = static_cast<double*>(malloc(sizeof(double) * (n > 0 ? n : 1)));
    tttrlib::sample_from_cdf(sampling_axis, n_sampling_axis,
                             sampling_cdf, n_sampling_cdf,
                             buffer, n, normalize);
    *sampling_val_out = buffer;
    *n_sampling_val_out = n;
}

%}

%exception;

%clear (double* sampling_weights, int n_sampling_weights);
%clear (double* sampling_axis, int n_sampling_axis);
%clear (double* sampling_cdf, int n_sampling_cdf);
%clear (unsigned int** sampling_idx_out, int* n_sampling_idx_out);
%clear (double** sampling_val_out, int* n_sampling_val_out);
