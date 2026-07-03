// SPDX-License-Identifier: BSD-3-Clause
%{ 
#include "TTTR.h" 
%} 
// Use shared_prt for TTTR to pass TTTR around 
%shared_ptr(TTTR) 

// Numpy array type mappings (applied globally before any %include) 
%apply(float* IN_ARRAY2, int DIM1, int DIM2) { 
    (const float* luts, int n_channels, int lut_size) 
} 
%apply(int* IN_ARRAY1, int DIM1) { 
    (const int* shifts, int n_channels) 
} 
%apply(float** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) { 
    (float** luts, int* n_channels, int* lut_size) 
} 
%apply(int** ARGOUTVIEWM_ARRAY1, int* DIM1) { 
    (int** shifts, int* n_channels) 
} 

// used in selection and ranges 
%apply (unsigned long long* IN_ARRAY1, int DIM1) { 
    (unsigned long long *time, int n_time), 
    (unsigned long long *input, int n_input) 
} 
%apply (int* IN_ARRAY1, int DIM1) {(int *selection, int n_selection)} 

%apply (int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(int **selection, int *n_selection)} 
%apply (unsigned int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(unsigned int **ranges, int *n_range)} 

// for microtime_histogram 
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1){ 
    (double** histogram, int* n_histogram), 
    (double** time, int* n_time) 
}; 

// Used in a TTTR constructor 
%apply (unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *macro_times, int n_macrotimes)} 
%apply (unsigned short* IN_ARRAY1, int DIM1) {(unsigned short *micro_times, int n_microtimes)} 
%apply (signed char * IN_ARRAY1, int DIM1) {(signed char *routing_channels, int n_routing_channels)} 
%apply (signed char * IN_ARRAY1, int DIM1) {(signed char *event_types, int n_event_types)} 

// Release the Python GIL around heavy, Python-object-free file I/O so other
// threads can run while a file is loaded. numpy typemaps marshal under the GIL
// before/after $action; only the C++ read executes GIL-free.
TTTRLIB_NOGIL(TTTR::read_file)      // src/TTTR.cpp:494 — pure C file I/O
TTTRLIB_NOGIL(TTTR::read_records)   // all overloads, src/TTTR.cpp:796+
TTTRLIB_NOGIL(TTTR::read_hdf_file)  // src/TTTR.cpp:282
TTTRLIB_NOGIL(TTTR::read_sm_file)   // src/TTTR.cpp:392
TTTRLIB_NOGIL(TTTR::TTTR)           // reading constructors call read_file()

%include "TTTR.h"

#ifdef SWIGPYTHON
%extend TTTR{%pythoncode "./ext/python/TTTR.py"}
#endif


