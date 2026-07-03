// SPDX-License-Identifier: BSD-3-Clause
%{
#include "TTTRMask.h"
%}

// Hide the std::vector<bool> version (slow in Python)
%ignore TTTRMask::get_mask_as_vector();

// Custom typemap for get_mask - returns numpy array VIEW (no ownership transfer).
// The mask is stored bit-packed internally; the view points at a cached
// unpacked snapshot that is refreshed on every get_mask call.
#ifdef SWIGPYTHON
%typemap(in, numinputs=0) (unsigned char** output, int* n_output) (unsigned char* temp_ptr, int temp_size) {
    $1 = &temp_ptr;
    $2 = &temp_size;
}

%typemap(argout, fragment="NumPy_Backward_Compatibility") (unsigned char** output, int* n_output) {
    npy_intp dims[1] = { *$2 };
    PyObject* obj = PyArray_SimpleNewFromData(1, dims, NPY_UINT8, (void*)(*$1));
    if (!obj) SWIG_fail;

    // Set the array to NOT own the data (it's owned by the C++ vector)
    PyArray_CLEARFLAGS((PyArrayObject*)obj, NPY_ARRAY_OWNDATA);

    $result = SWIG_AppendOutput($result, obj);
}
#endif

// For non-Python targets the global misc_types.i mapping sends this signature to
// ARGOUTVIEWM (which frees the buffer). get_mask returns a VIEW into a C++-owned
// cached snapshot, so re-map it to ARGOUTVIEW (copy, no free) instead.
#ifndef SWIGPYTHON
%apply(unsigned char** ARGOUTVIEW_ARRAY1, int* DIM1) {(unsigned char** output, int* n_output)}
#endif

// Rename methods for Python
%rename(get_mask_array) TTTRMask::get_mask(unsigned char** output, int* n_output);
%rename(set_mask_array) TTTRMask::set_mask(unsigned char* input, int n_input);
%rename(_get_indices) TTTRMask::get_indices;
%rename(_get_selected_ranges) TTTRMask::get_selected_ranges;

#ifdef SWIGPYTHON
%extend TTTRMask{%pythoncode "./ext/python/TTTRMask.py"}
#endif

%include "TTTRMask.h"
