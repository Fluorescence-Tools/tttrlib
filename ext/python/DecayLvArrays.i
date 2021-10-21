%{
static int LVI32ArrayErr = 0;    // flag to save LVI32Array struct error state
static int LVDoubleArrayErr = 0; // flag to save LVDoubleArrayErr struct error state
%}
%include "LvArrays.h"

%apply (int DIM1, double* INPLACE_ARRAY1) {(int len5, double* mfunction)}
%apply (int DIM1, double* IN_ARRAY1)
{
    (int len1, double* param),
    (int len2, double* irf),
    (int len3, double* bg),
    (int len4, double* corrections)
}

%extend MParam{

    LVDoubleArray* get_irf() {
        return *($self->irf);
    }

    LVDoubleArray* get_model() {
        return *($self->M);
    }

    LVI32Array* get_data() {
        return *($self->expdata);
    }

    LVDoubleArray* get_corrections() {
        return *($self->corrections);
    }

    LVDoubleArray* get_background() {
        return *($self->bg);
    }
}

//// Extend the LVI32Array struct
// set exception handling for __getitem__
%exception LVI32Array::__getitem__ {
    assert(!LVI32ArrayErr);
    $action
    if ( LVI32ArrayErr ){
        LVI32ArrayErr = 0; // clear flag for next time
        SWIG_exception(SWIG_IndexError, "Index out of bounds");
    }
}

// set exception handling for __setitem__
%exception LVI32Array::__setitem__ {
    assert(!LVI32ArrayErr);
    $action
    if ( LVI32ArrayErr ){
        LVI32ArrayErr = 0; // clear flag for next time
        SWIG_exception(SWIG_IndexError, "Index out of bounds");
    }
}

// set exception handling for insert()
%exception LVI32Array::insert {
    assert(!LVI32ArrayErr);
    $action
    if ( LVI32ArrayErr ){
        LVI32ArrayErr = 0; // clear flag for next time
        SWIG_exception(SWIG_IndexError, "Index out of bounds");
    }
}

%extend LVI32Array{

    int __getitem__(size_t i) {
        if (i >= $self->length) {
            LVI32ArrayErr = 1;
            return 0;
        }
        return $self->data[i];
    }

    void __setitem__(size_t i, int value) {
        if ( i >= $self->length ){
            LVI32ArrayErr = 1;
            return;
        }
        $self->data[i] = value;
    }

    size_t __len__(){
        return $self->length;
    }

    void insert(size_t i, int value) {
        if ( i >= $self->length ){
            LVI32ArrayErr = 1;
            return;
        }
        $self->data[i] = value;
    }

    void set_data(PyObject* input) {
        PyArrayObject* obj = (PyArrayObject*)input;
        int size = PyArray_SIZE(obj);
        int* data = (int*) PyArray_DATA(obj);
        int* temp = (int*) calloc(size, sizeof(int));
        for(int i=0; i<size;i++){
            temp[i] = data[i];
        }
        $self->data=temp;
        $self->length=size;
    }
}


//// Extend the LVDoubleArray struct
// set exception handling for __getitem__
%exception LVDoubleArray::__getitem__ {
    assert(!LVDoubleArrayErr);
    $action
    if ( LVDoubleArrayErr ){
        LVDoubleArrayErr = 0; // clear flag for next time
        SWIG_exception(SWIG_IndexError, "Index out of bounds");
    }
}

// set exception handling for __setitem__
%exception LVDoubleArray::__setitem__ {
    assert(!LVDoubleArrayErr);
    $action
    if ( LVDoubleArrayErr ){
        LVDoubleArrayErr = 0; // clear flag for next time
        SWIG_exception(SWIG_IndexError, "Index out of bounds");
    }
}

// set exception handling for insert()
%exception LVDoubleArray::insert {
    assert(!LVDoubleArrayErr);
    $action
    if ( LVDoubleArrayErr ){
        LVDoubleArrayErr = 0; // clear flag for next time
        SWIG_exception(SWIG_IndexError, "Index out of bounds");
    }
}

%extend LVDoubleArray{
    // add a __getitem__ method to the structure to get values from the data array
    double __getitem__(size_t i) {
        if (i >= $self->length) {
            LVDoubleArrayErr = 1;
            return 0;
        }
        return $self->data[i];
    }

    // add a __setitem__ method to the structure to set values in the data array
    void __setitem__(size_t i, double value) {
        if ( i >= $self->length ){
            LVDoubleArrayErr = 1;
            return;
        }
        $self->data[i] = value;
    }

    size_t __len__(){
        return $self->length;
    }

    void insert(size_t i, double value) {
        if ( i >= $self->length ){
            LVDoubleArrayErr = 1;
            return;
        }
        $self->data[i] = value;
    }

    void set_data(PyObject* input) {
        PyArrayObject* obj = (PyArrayObject*)input;
        int size = PyArray_SIZE(obj);
        double* data = (double*) PyArray_DATA(obj);
        double* temp = (double*) calloc(size, sizeof(double));
        for(int i=0; i<size;i++){
            temp[i] = data[i];
        }
        $self->data=temp;
        $self->length=size;
    }
}
