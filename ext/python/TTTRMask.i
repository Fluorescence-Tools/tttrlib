%{
#include "TTTRMask.h"
%}

// Hide the std::vector<bool> version (slow in Python)
%ignore TTTRMask::get_mask_as_vector();

// Custom typemap for get_mask - returns numpy array VIEW (no ownership transfer)
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
    
    $result = SWIG_Python_AppendOutput($result, obj);
}

// Rename methods for Python
%rename(get_mask_array) TTTRMask::get_mask(unsigned char** output, int* n_output);
%rename(set_mask_array) TTTRMask::set_mask(unsigned char* input, int n_input);
%rename(_get_indices) TTTRMask::get_indices;
%rename(_get_selected_ranges) TTTRMask::get_selected_ranges;

%extend TTTRMask{%pythoncode "./ext/python/TTTRMask.py"}

%include "TTTRMask.h"
