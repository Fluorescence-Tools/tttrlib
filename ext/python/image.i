%{
#include "../include/image.h"
#include "../include/tttr.h"
%}

%template(vector_CLSMFrame) std::vector<CLSMFrame*>;
%template(vector_CLSMLine) std::vector<CLSMLine*>;
%template(vector_CLSMPixel) std::vector<CLSMPixel*>;

%apply (unsigned int** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(unsigned int** output, int* dim1, int* dim2)}
%apply (unsigned int** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(unsigned int** output, int* dim1, int* dim2, int* dim3)}
%apply (double** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(double** output, int* dim1, int* dim2, int* dim3)}
%apply (unsigned char** ARGOUTVIEWM_ARRAY4, int* DIM1, int* DIM2, int* DIM3, int* DIM4) {(unsigned char** output, int* dim1, int* dim2, int* dim3, int* dim4)}
%apply (float** ARGOUTVIEWM_ARRAY4, int* DIM1, int* DIM2, int* DIM3, int* DIM4) {(float** output, int* dim1, int* dim2, int* dim3, int* dim4)}
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


%extend CLSMImage {

        %pythoncode "./ext/python/image/image_extension.py"
        CLSMFrame* __getitem__(int i) {
            if (i < 0) {
                i = $self->get_n_frames() + i;
            }
            if (i >= $self->get_n_frames()) {
                return nullptr;
            }
            return (*($self))[i];
        }

        size_t __len__(){
            return $self->get_n_frames();
        }
}

%extend CLSMFrame {

        CLSMLine* __getitem__(int i) {
            if (i < 0){
                i = $self->get_n_lines() + i;
            }
            if (i >= $self->get_n_lines()) {
                return nullptr;
            }
            return (*($self))[i];
        }

        size_t __len__(){
            return $self->get_n_lines();
        }
}

%extend CLSMLine {

        CLSMPixel* __getitem__(int i) {
            if (i < 0){
                i = $self->get_n_pixel() + i;
            }
            if (i >= $self->get_n_pixel()) {
                return nullptr;
            }
            return (*($self))[i];
        }
        size_t __len__(){
            return $self->get_n_pixel();
        }
}

%include "../include/tttr.h"
%include "../include/image.h"
