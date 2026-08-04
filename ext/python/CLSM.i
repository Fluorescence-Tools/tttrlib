// SPDX-License-Identifier: BSD-3-Clause
%{
#include "CLSMPixel.h"
#include "CLSMLine.h"
#include "CLSMFrame.h"
#include "CLSMImage.h"
#include "TTTR.h"
static int myErr = 0; // flag to save error state
%}

// Note: std_vector.i is already included in misc_types.i
// Only define custom vector templates for CLSM-specific types
%template(vector_CLSMFrame) std::vector<CLSMFrame*>;
%template(vector_CLSMLine) std::vector<CLSMLine*>;
%template(vector_CLSMPixel) std::vector<CLSMPixel*>;

%apply (unsigned char* INPLACE_ARRAY3, int DIM1, int DIM2, int DIM3) {
    (uint8_t* mask, int dmask1, int dmask2, int dmask3)
}
%apply (double* IN_ARRAY3, int DIM1, int DIM2, int DIM3) {
    (double *images, int input_frames, int input_lines, int input_pixel),
    (double *images_2, int input_frames_2, int input_lines_2, int input_pixel_2)
}
// Typemap for CLSMFrame::get_intensity (2D array output)
%apply (unsigned short** ARGOUTVIEW_ARRAY2, int* DIM1, int* DIM2) {
    (unsigned short** output, int* dim1, int* dim2)
}
// Typemap for CLSMFrame::get_intensity_u32 (2D array output, 32-bit counters).
// ARGOUTVIEWM (managed) so numpy takes ownership of the malloc'd buffer; the
// 3-D CLSMImage::get_intensity_u32 case is already covered by misc_types.i.
%apply (unsigned int** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {
    (unsigned int** output, int* dim1, int* dim2)
}

// Typemap for CLSMImage::get_memory_usage_detailed (size_t* output parameters)
%apply size_t *OUTPUT { size_t* overhead, size_t* indices, size_t* ranges }
// documentation see
// https://github.com/swig/swig/blob/6f2399e86da13a9feb436e3977e15d2b9738294e/Lib/typemaps/attribute.swg
%attribute(CLSMImage, int, n_frames, get_n_frames);
%attribute(CLSMImage, int, n_lines, get_n_lines);
%attribute(CLSMImage, int, n_pixel, get_n_pixel);
%attribute(CLSMImage, int, n_channels, get_n_channels);
%attribute(CLSMFrame, size_t, n_lines, size);
%attribute(CLSMLine, size_t, n_pixel, size);
%attribute(CLSMLine, unsigned long long, pixel_duration, get_pixel_duration);

// Python does not support overloading. Thus, ignore the copy constructor
%ignore CLSMImage();
%ignore CLSMImage(const CLSMImage& p2, bool fill=false);

// Keep the raw-pointer pixel-duration overloads C++-only; Python uses the
// nested-vector forms (VectorDouble_2D converts from nested lists)
%ignore CLSMImage::set_pixel_duration_matrix(double*, int, int);
%ignore CLSMImage::get_pixel_duration_matrix(double**, int*, int*) const;

%shared_ptr(CLSMImage)

// Release the Python GIL around the (OpenMP-parallelized) image fill, which
// touches no Python objects. Results are only returned once the fill completes.
TTTRLIB_NOGIL(CLSMImage::fill)      // src/CLSMImage.cpp:1394
TTTRLIB_NOGIL(CLSMImage::CLSMImage) // filling constructor calls fill() (src/CLSMImage.cpp:641)

%include "CLSMPixel.h"
%include "CLSMLine.h"
%include "CLSMFrame.h"
%include "CLSMImage.h"

// https://stackoverflow.com/questions/8776328/swig-interfacing-c-library-to-python-creating-iterable-python-data-type-from
%exception CLSMImage::__getitem__ {
    assert(!myErr);
    $action
    if (myErr) {
        myErr = 0; // clear flag for next time
        // You could also check the value in $result, but it's a PyObject here
        SWIG_exception(SWIG_IndexError, "Index out of bounds");
    }
}

%extend CLSMImage {

    // Cross-language phasor accessor. The native get_phasor() has a pointer-typed
    // default argument (TTTR* tttr_irf = nullptr) that R's SWIG overload dispatch
    // cannot resolve, so R could not call it. This wrapper takes only scalar
    // arguments and returns the phasor image flattened as [frame, line, pixel, 2]
    // (g and s interleaved per pixel), which every binding marshals as a plain
    // numeric vector. Length = n_frames*n_lines*n_pixel*2 (or n_lines*n_pixel*2
    // when stack_frames = true). The native get_phasor() is retained unchanged.
    std::vector<double> get_phasor_v(TTTR* tttr_data,
                                     double frequency = -1,
                                     int minimum_number_of_photons = 2,
                                     bool stack_frames = false,
                                     bool correct_irf_offset = false) {
        float* out = 0; int d1 = 0, d2 = 0, d3 = 0, d4 = 0;
        $self->get_phasor(&out, &d1, &d2, &d3, &d4, tttr_data, nullptr,
                          frequency, minimum_number_of_photons,
                          stack_frames, correct_irf_offset);
        size_t n = (size_t) d1 * d2 * d3 * d4;
        std::vector<double> v(n);
        for (size_t i = 0; i < n; ++i) v[i] = out[i];
        if (out) free(out);
        return v;
    }

    CLSMFrame* __getitem__(int i) {
        if (i >= $self->size()){
            myErr = 1;
            return 0;
        }
        if (i < 0) {
            i = $self->size() + i;
        }
        return (*($self))[i];
    }

    // Explicit frame accessor to allow Python wrapper to bypass overridden __getitem__
    CLSMFrame* frame_at(int i) {
        if (i >= $self->size()){
            myErr = 1;
            return 0;
        }
        if (i < 0) {
            i = $self->size() + i;
        }
        return (*($self))[i];
    }

    size_t __len__(){
        return $self->size();
    }

#ifdef SWIGPYTHON
    %pythoncode %{
        @property
        def tttr_indices(self):
            """Zero-copy access to TTTR indices as numpy array.
            
            Returns a view into C++ memory. The array is valid as long as this
            object exists. Store a reference to this object if you need the array
            to outlive the immediate scope.
            
            Example (safe):
                img = tttrlib.CLSMImage('file.ptu', fill=True)
                line = img[0][1]
                indices = line.tttr_indices  # Safe: 'line' and 'img' are alive
                
            Example (also safe - image keeps everything alive):
                img = tttrlib.CLSMImage('file.ptu', fill=True)
                indices = img[0][1].tttr_indices  # Safe: 'img' owns the line
            """
            return self.get_tttr_indices()
    %}

    %pythoncode "./ext/python/CLSMImage.py"
#endif
}

%exception CLSMFrame::__getitem__ {
    assert(!myErr);
    $action
    if (myErr) {
        myErr = 0; // clear flag for next time
        // You could also check the value in $result, but it's a PyObject here
        SWIG_exception(SWIG_IndexError, "Index out of bounds");
    }
}

%extend CLSMFrame {

    CLSMLine* __getitem__(int i) {
        if (i < 0){
            i = $self->size() + i;
        }
        if (i >= $self->size()){
            myErr = 1;
            return 0;
        }
        return (*($self))[i];
    }

    size_t __len__(){
        return $self->size();
    }
    
    CLSMFrame* copy(bool fill=true) {
        return new CLSMFrame(*$self, fill);
    }

#ifdef SWIGPYTHON
    %pythoncode %{
        @property
        def tttr_indices(self):
            """Zero-copy access to TTTR indices as numpy array.
            
            Returns a view into C++ memory. Keep the parent CLSMImage alive.
            """
            return self.get_tttr_indices()
    %}

    %pythoncode "./ext/python/CLSMFrame.py"
#endif
}

%exception CLSMLine::__getitem__ {
    assert(!myErr);
    $action
    if (myErr) {
        myErr = 0; // clear flag for next time
        // You could also check the value in $result, but it's a PyObject here
        SWIG_exception(SWIG_IndexError, "Index out of bounds");
    }
}

%extend CLSMLine {

    CLSMPixel* __getitem__(int i) {
        if (i < 0){
            i = $self->size() + i;
        }
        if (i >= $self->size()){
            myErr = 1;
            return 0;
        }
        return (*($self))[i];
    }
    size_t __len__(){
        return $self->size();
    }

#ifdef SWIGPYTHON
    %pythoncode %{
        @property
        def tttr_indices(self):
            """Zero-copy access to TTTR indices as numpy array.
            
            Returns a view into C++ memory. Keep the parent CLSMImage alive.
            """
            return self.get_tttr_indices()
    %}
#endif
}

%extend CLSMPixel {
#ifdef SWIGPYTHON
    %pythoncode %{
        @property
        def tttr_indices(self):
            """Zero-copy access to TTTR indices as numpy array.
            
            Returns a view into C++ memory. Keep the parent CLSMImage alive.
            """
            return self.get_tttr_indices()
    %}
#endif
}

