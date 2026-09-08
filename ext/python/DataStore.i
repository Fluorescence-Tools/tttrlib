// SPDX-License-Identifier: BSD-3-Clause
%module tttrlib
%{
#include "DataStore.h"
%}

// The DataStore itself is ptolib's, wrapped once by Ptolib.i (with every
// typemap that used to live here in front of it). What is left of DataStore.h
// is tttrlib's: the HistogramNd fills over columns.
%include "Ptolib.i"

%include "DataStore.h"


// ...and the module-level support AFTER it, because it names the generated
// enum constants at import time and they do not exist until the header has
// been wrapped. (The methods above only name them when called, so their order
// does not matter.)
// Python-only; see the note above.
#ifdef SWIGPYTHON
%pythoncode "./ext/python/datastore_support.py"
#endif  // SWIGPYTHON

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
