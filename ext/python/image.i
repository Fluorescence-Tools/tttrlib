%module(directors="1") tttrlib
%{
    #include "../include/image.h"
    #include "../include/tttr.h"
%}

%include "std_vector.i";
%include "std_list.i";
%template(vector_CLSMFrame) std::vector<CLSMFrame*>;
%template(vector_CLSMLine) std::vector<CLSMLine*>;
%template(vector_CLSMPixel) std::vector<CLSMPixel*>;

%apply (unsigned int** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {
    (unsigned int** out, int* dim1, int* dim2)
}

%apply (unsigned int** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {
    (unsigned int** out, int* dim1, int* dim2, int* dim3)
}
%apply (double** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {
    (double** out, int* dim1, int* dim2, int* dim3)
}
%apply (
        unsigned char** ARGOUTVIEWM_ARRAY4,
        int* DIM1, int* DIM2, int* DIM3, int* DIM4
        ) {
    (unsigned char** out, int* dim1, int* dim2, int* dim3, int* dim4)
}

%apply (
        uint8_t* INPLACE_ARRAY3, int DIM1, int DIM2, int DIM3
) {
    (uint8_t* selection, int d_selection_1, int d_selection_2, int d_selection_3)
}

%extend CLSMImage {

        CLSMFrame* __getitem__(int i) {
            if (i < 0){
                i = $self->n_frames + i;
            }
            if (i >= $self->n_frames) {
                return nullptr;
            }
            return (*($self))[i];
        }
        size_t __len__(){
            return $self->n_frames;
        }
}

%extend CLSMFrame {

        CLSMLine* __getitem__(int i) {
            if (i < 0){
                i = $self->n_lines + i;
            }
            if (i >= $self->n_lines) {
                return nullptr;
            }
            return (*($self))[i];
        }
        size_t __len__(){
            return $self->n_lines;
        }
}

%extend CLSMLine {

        CLSMPixel* __getitem__(int i) {
            if (i < 0){
                i = $self->n_pixel + i;
            }
            if (i >= $self->n_pixel) {
                return nullptr;
            }
            return (*($self))[i];
        }
        size_t __len__(){
            return $self->n_pixel;
        }
}

%include "../include/tttr.h"
%include "../include/image.h"
