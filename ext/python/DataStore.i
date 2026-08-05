// SPDX-License-Identifier: BSD-3-Clause
%module tttrlib
%{
#include "DataStore.h"
%}

%include "std_string.i"
%include "std_vector.i"

// Distinctive parameter names throughout: %apply is global and keyed by name,
// so a generic (double* v, int n) here would redefine that pair for every other
// interface file in the project.
%apply (double* IN_ARRAY1, int DIM1)    { (const double* v, int n) }
%apply (float* IN_ARRAY1, int DIM1)     { (const float* v, int n) }
%apply (long long* IN_ARRAY1, int DIM1) { (const long long* v, int n) }
%apply (int* IN_ARRAY1, int DIM1)       { (const int* v, int n) }
%apply (short* IN_ARRAY1, int DIM1)     { (const short* v, int n) }
// Polygon vertices and a painted mask, for the drawn regions.
%apply (double* IN_ARRAY1, int DIM1) {
    (const double* xs, int n_xs),
    (const double* ys, int n_ys)
}
%apply (unsigned char* IN_ARRAY2, int DIM1, int DIM2) {
    (const unsigned char* image, int nx, int ny)
}
%apply (signed char* IN_ARRAY1, int DIM1) { (const signed char* v, int n) }
%apply (unsigned long long* IN_ARRAY1, int DIM1) { (const unsigned long long* v, int n) }
%apply (unsigned int* IN_ARRAY1, int DIM1) { (const unsigned int* v, int n) }
%apply (unsigned short* IN_ARRAY1, int DIM1) { (const unsigned short* v, int n) }
%apply (unsigned char* IN_ARRAY1, int DIM1) {
    (const unsigned char* v, int n),
    (const unsigned char* m, int n)
}
// BitMask::to_bytes writes into the caller's array.
%apply (unsigned char* INPLACE_ARRAY1, int DIM1) {
    (unsigned char* out_bytes, int n_out)
}

// Zero-copy views into the column buffers. Non-owning, like the histogram's --
// the Python layer attaches the owner so they cannot dangle.
%apply (double** ARGOUTVIEW_ARRAY1, int* DIM1)    { (double** view, int* n) }
%apply (float** ARGOUTVIEW_ARRAY1, int* DIM1)     { (float** view, int* n) }
%apply (long long** ARGOUTVIEW_ARRAY1, int* DIM1) { (long long** view, int* n) }
%apply (int** ARGOUTVIEW_ARRAY1, int* DIM1)       { (int** view, int* n) }
%apply (short** ARGOUTVIEW_ARRAY1, int* DIM1)     { (short** view, int* n) }
%apply (signed char** ARGOUTVIEW_ARRAY1, int* DIM1) { (signed char** view, int* n) }
%apply (unsigned long long** ARGOUTVIEW_ARRAY1, int* DIM1) { (unsigned long long** view, int* n) }
%apply (unsigned int** ARGOUTVIEW_ARRAY1, int* DIM1) { (unsigned int** view, int* n) }
%apply (unsigned short** ARGOUTVIEW_ARRAY1, int* DIM1) { (unsigned short** view, int* n) }
%apply (unsigned char** ARGOUTVIEW_ARRAY1, int* DIM1) { (unsigned char** view, int* n) }

// %extend must come BEFORE the header it extends...
%extend tttrlib::data::Column { %pythoncode "./ext/python/Column.py" }
%extend tttrlib::data::DataStore { %pythoncode "./ext/python/DataStore.py" }

%include "DataStore.h"

%template(DataStoreInfoVector) std::vector<tttrlib::data::DataStoreInfo>;

// ...and the module-level support AFTER it, because it names the generated
// enum constants at import time and they do not exist until the header has
// been wrapped. (The methods above only name them when called, so their order
// does not matter.)
%pythoncode "./ext/python/datastore_support.py"

// std::vector<int> and std::vector<std::string> are already templated in
// misc_types.i as VectorInt32 / VectorString. Declaring them again is silently
// skipped by SWIG, so the second name never exists -- use the first ones.

%clear (const double* v, int n);
%clear (const float* v, int n);
%clear (const long long* v, int n);
%clear (const int* v, int n);
%clear (const unsigned char* v, int n);
%clear (const unsigned char* m, int n);
%clear (unsigned char* out_bytes, int n_out);
%clear (double** view, int* n);
%clear (float** view, int* n);
%clear (long long** view, int* n);
%clear (int** view, int* n);
%clear (short** view, int* n);
%clear (signed char** view, int* n);
%clear (unsigned long long** view, int* n);
%clear (unsigned int** view, int* n);
%clear (unsigned short** view, int* n);
%clear (unsigned char** view, int* n);
