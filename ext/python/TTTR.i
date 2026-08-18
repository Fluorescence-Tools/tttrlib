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
// Bulk routing-channel replacement (TTTR::set_routing_channel).
%apply (signed char * IN_ARRAY1, int DIM1) {(signed char *input, int n_input)}

// TTTR::decode_records -- a buffer of undecoded records. Bytes, because that is
// the one shape that also covers a format whose record is not word aligned
// (SPC-600 in 4096-channel mode is six bytes wide).
%apply (unsigned char* IN_ARRAY1, int DIM1) {(unsigned char* records, int n_bytes)}

// TTTRDecodeState is spelled in <cstdint> types, and only the Python and
// JavaScript backends resolve `std::uint64_t` on their own. In Java it stays an
// unknown type, so the overflow counter -- the whole reason the struct exists --
// comes back as an opaque SWIGTYPE proxy a caller can do nothing with.
// StoreFile.i says the same thing for the container types, but is parsed after
// this header, so it comes too late for these three members.
//
// Not for R: `unsigned long long` there goes through as.integer(), which is
// 32-bit and silently NA above 2^31 -- and an overflow count on a long
// acquisition reaches that. ext/r/tttrlib.i gives std::uint64_t its own
// double-backed typemaps, and %apply here would overwrite them.
#ifndef SWIGR
%apply unsigned long long { std::uint64_t };
#endif

// Release the Python GIL around heavy, Python-object-free file I/O so other
// threads can run while a file is loaded. numpy typemaps marshal under the GIL
// before/after $action; only the C++ read executes GIL-free.
TTTRLIB_NOGIL(TTTR::read_file)      // src/TTTR.cpp:494 — pure C file I/O
TTTRLIB_NOGIL(TTTR::read_records)   // all overloads, src/TTTR.cpp:796+
TTTRLIB_NOGIL(TTTR::read_hdf_file)  // src/TTTR.cpp:282
TTTRLIB_NOGIL(TTTR::read_sm_file)   // src/TTTR.cpp:392
TTTRLIB_NOGIL(TTTR::TTTR)           // reading constructors call read_file()
TTTRLIB_NOGIL(TTTR::decode_records) // pure record decoding, no Python objects

// The burst searches are TTTR members declared in core's TTTR.h but defined in
// the burst module (T-20260818-06 will make them free functions there). With
// WITH_BURST=OFF (-DTTTRLIB_WITHOUT_BURST) their definitions do not exist, so the
// wrapper must not reference them; the Python helpers in TTTR.py that call them
// then raise AttributeError at call time, which is the honest answer.
#ifdef TTTRLIB_WITHOUT_BURST
%ignore TTTR::burst_search;
%ignore TTTR::burst_search_sliding_window;
%ignore TTTR::burst_search_cusum_sprt;
%ignore TTTR::burst_search_maxtree;
%ignore TTTR::burst_search_kalman;
%ignore TTTR::burst_search_bocpd;
%ignore TTTR::burst_search_bayesian_blocks;
%ignore TTTR::burst_search_plugin;
%ignore TTTR::burst_search_algorithms_json;
%ignore TTTR::burst_confidence;
#endif

#ifdef SWIGPYTHON
// Burst boundaries come back as a NumPy int64 array directly (the flat
// [start, stop, start, stop, ...] layout) so callers reshape/slice without
// converting a wrapped-vector proxy first. Python-only: the C++ (and R/Java)
// surface keeps the released std::vector<long long> return.
%define TTTRLIB_NUMPY_INT64_RETURN(Method)
%feature("pythonappend") Method %{
    import numpy as _np
    val = _np.asarray(val, dtype=_np.int64)
%}
%enddef
TTTRLIB_NUMPY_INT64_RETURN(TTTR::burst_search)
TTTRLIB_NUMPY_INT64_RETURN(TTTR::burst_search_sliding_window)
TTTRLIB_NUMPY_INT64_RETURN(TTTR::burst_search_cusum_sprt)
TTTRLIB_NUMPY_INT64_RETURN(TTTR::burst_search_maxtree)
TTTRLIB_NUMPY_INT64_RETURN(TTTR::burst_search_kalman)
TTTRLIB_NUMPY_INT64_RETURN(TTTR::burst_search_bayesian_blocks)
#endif

// The Bayesian Blocks dynamic program is the one burst search long-running
// enough to be worth releasing the GIL for, and it touches no Python objects.
TTTRLIB_NOGIL(TTTR::burst_search_bayesian_blocks)

// TTTR validates its inputs by throwing (a mismatched array length, an
// unreadable file). Without a handler that unwinds through the wrapper and
// aborts the interpreter -- no traceback, no chance to catch it -- rather than
// raising a Python exception.
%exception {
    try {
        $action
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}

// A header (and the microtime linearizer) is a pointer INTO the TTTR that
// made it, and the proxy must keep that owner alive: `TTTR(path).header`
// otherwise frees the TTTR at the end of the expression and every read
// through the proxy after that is use-after-free -- a segfault, not an
// exception (BUGS 2026-08-11).
//
// Python-only: %pythonappend is an unknown directive to the R, Java and
// JavaScript backends, which parse this same file and stop at it. The other
// three bindings still have the underlying lifetime hole; it needs each
// backend's own equivalent.
#ifdef SWIGPYTHON
%pythonappend TTTR::get_header() %{
        if val is not None:
            val._keepalive_owner = self
%}
%pythonappend TTTR::get_mt_linearizer() %{
        if val is not None:
            val._keepalive_owner = self
%}
#endif

%include "TTTR.h"

#ifdef SWIGPYTHON
%extend TTTR{%pythoncode "./ext/python/TTTR.py"}
#endif


