%{
#include "../include/image.h"
#include "../include/tttr.h"
static int myErr = 0; // flag to save error state
%}

%template(vector_CLSMFrame) std::vector<CLSMFrame*>;
%template(vector_CLSMLine) std::vector<CLSMLine*>;
%template(vector_CLSMPixel) std::vector<CLSMPixel*>;

%apply (uint8_t* INPLACE_ARRAY3, int DIM1, int DIM2, int DIM3) {(uint8_t* selection, int d_selection_1, int d_selection_2, int d_selection_3)}

// documentation see
// https://github.com/swig/swig/blob/6f2399e86da13a9feb436e3977e15d2b9738294e/Lib/typemaps/attribute.swg
%attribute(CLSMImage, int, n_frames, get_n_frames);
%attribute(CLSMImage, int, n_lines, get_n_lines);
%attribute(CLSMImage, int, n_pixel, get_n_pixel);
%attribute(CLSMFrame, int, n_lines, get_n_lines);
%attribute(CLSMLine, int, n_pixel, get_n_pixel);
%attribute(CLSMLine, unsigned long long, pixel_duration, get_pixel_duration);

// Python does not support overloading. Thus, ignore the copy constructor
%ignore CLSMImage();
%ignore CLSMImage(const CLSMImage& p2, bool fill=false);

%include "../include/tttr.h"
%include "../include/image.h"


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
        if (i >= $self->get_n_frames()){
            myErr = 1;
            return 0;
        }
        if (i < 0) {
            i = $self->get_n_frames() + i;
        }
        return (*($self))[i];
    }

    size_t __len__(){
        return $self->get_n_frames();
    }

    %pythoncode "./ext/python/image/image_extension.py"
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
            i = $self->get_n_lines() + i;
        }
        if (i >= $self->get_n_lines()){
            myErr = 1;
            return 0;
        }
        return (*($self))[i];
    }

    size_t __len__(){
        return $self->get_n_lines();
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
            i = $self->get_n_pixel() + i;
        }
        if (i >= $self->get_n_pixel()){
            myErr = 1;
            return 0;
        }
        return (*($self))[i];
    }
    size_t __len__(){
        return $self->get_n_pixel();
    }
}

