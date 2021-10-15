%{
#include "../include/CLSMPixel.h"
#include "../include/CLSMLine.h"
#include "../include/CLSMFrame.h"
#include "../include/CLSMImage.h"
#include "../include/TTTR.h"
static int myErr = 0; // flag to save error state
%}

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
// documentation see
// https://github.com/swig/swig/blob/6f2399e86da13a9feb436e3977e15d2b9738294e/Lib/typemaps/attribute.swg
%attribute(CLSMImage, int, n_frames, get_n_frames);
%attribute(CLSMImage, int, n_lines, get_n_lines);
%attribute(CLSMImage, int, n_pixel, get_n_pixel);
%attribute(CLSMFrame, size_t, n_lines, size);
%attribute(CLSMLine, size_t, n_pixel, size);
%attribute(CLSMLine, unsigned long long, pixel_duration, get_pixel_duration);

// Python does not support overloading. Thus, ignore the copy constructor
%ignore CLSMImage();
%ignore CLSMImage(const CLSMImage& p2, bool fill=false);

// Use shared_prt for CLSMImage to pass CLSMImage around
%shared_ptr(CLSMImage)

%include "../include/CLSMPixel.h"
%include "../include/CLSMLine.h"
%include "../include/CLSMFrame.h"
%include "../include/CLSMImage.h"

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

    size_t __len__(){
        return $self->size();
    }

    %pythoncode "./ext/python/CLSMImage.py"
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
}

