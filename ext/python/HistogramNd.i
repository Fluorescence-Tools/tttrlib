// SPDX-License-Identifier: BSD-3-Clause
%module tttrlib
%{
#include "HistogramNd.h"
%}

%include "std_string.i"
%include "std_vector.i"

// No %exception here. The project already installs a GLOBAL one in
// MicrotimeLinearization.i that translates std::invalid_argument to ValueError
// and std::exception to RuntimeError -- exactly what the Axis factories need.
//
// Adding a duplicate is harmless; ending it with `%exception;` is not. That
// clears the handler for every interface file included AFTERWARDS, not just
// this one, and CLSMSuperRes then aborted the interpreter on the C++ exception
// it is supposed to raise as a Python error.
// Inputs. Named distinctly from every other interface file: %apply is global
// and keyed by parameter name, so a generic name here silently redefines that
// pair everywhere else (it has already broken fconv once).
%apply (double* IN_ARRAY1, int DIM1) {
    (const double* edges, int n_edges),
    (const double* x, int n_x),
    (const double* y, int n_y),
    (const double* weights, int n_weights),
    (const double* sample, int n_sample)
}
%apply (int* IN_ARRAY1, int DIM1) {
    (const int* values, int n_values),
    (const int* keep, int n_keep)
}
%apply (double* IN_ARRAY2, int DIM1, int DIM2) {
    (const double* data, int n_rows, int n_cols)
}

// Two kinds of output, and the difference matters.
//
// ARGOUTVIEWM ("M" for managed) wraps a freshly malloc'd array and numpy frees
// it -- one copy, no lifetime question. Used for the edge/centre/width arrays,
// which are computed rather than stored, and for the explicit copy accessors.
//
// ARGOUTVIEW wraps the C++ buffer itself -- zero copy, so looking at a large
// histogram is free. It carries no base object, so nothing here stops the
// histogram being collected under it; HistogramNd.py attaches the owner to the
// array, and C++ refuses the view outright when a growing axis could realloc.
%apply (double** ARGOUTVIEWM_ARRAY1, int* DIM1) { (double** out, int* n) }
%apply (int** ARGOUTVIEWM_ARRAY1, int* DIM1) { (int** out, int* n) }
%apply (double** ARGOUTVIEW_ARRAY1, int* DIM1) { (double** view, int* n) }

// The HistogramNd constructor takes a vector of axes, so Python needs a list
// type for it. Declared after the header so Axis is already known.
// Module-level support (numpy import, the owning-view ndarray subclass, the
// shape/flow helpers). Must be at module scope: a %pythoncode file inside
// %extend lands in the class namespace, where a method cannot see it by name.
%pythoncode "./ext/python/hist_support.py"

%extend tttrlib::hist::Axis { %pythoncode "./ext/python/Axis.py" }
%extend tttrlib::hist::HistogramNd { %pythoncode "./ext/python/HistogramNd.py" }

%include "HistogramNd.h"
%template(AxisVector) std::vector<tttrlib::hist::Axis>;

%clear (const double* edges, int n_edges);
%clear (const double* x, int n_x);
%clear (const double* y, int n_y);
%clear (const double* weights, int n_weights);
%clear (const double* sample, int n_sample);
%clear (const int* values, int n_values);
%clear (const int* keep, int n_keep);
%clear (const double* data, int n_rows, int n_cols);
%clear (double** out, int* n);
%clear (double** view, int* n);
%clear (int** out, int* n);
