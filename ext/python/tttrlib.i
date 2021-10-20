%{
#define SWIG_FILE_WITH_INIT
%}
%module(directors="1", package="tttrlib") tttrlib

#ifdef VERBOSE_TTTRLIB
// Warning 302: Identifier redefined (ignored) (Renamed from 'pair< std::shared_ptr< TTTR >,std::shared_ptr< TTTR > >'),
// Warning 389: operator[] ignored (consider using %extend)
// Warning 401: Nothing known about base class
// Warning 453: Can't apply (double *IN_ARRAY2,int DIM1,DIM2). No typemaps are defined.
// Warning 511: Ignore overloaded functions
#pragma SWIG nowarn= 302, 389, 401, 453, 501, 505, 511
#endif

%feature("kwargs", 1);
%include "documentation.i"

%pythonbegin "./ext/python/tttrlib_ext.py"

%include "typemaps.i";
%include "stl.i";
%include "std_string.i";
%include "std_wstring.i";
%include "std_map.i";
%include "std_vector.i";
%include "std_list.i";
%include "std_pair.i"; // tttrlib.Correlator.get_tttr
%include "std_shared_ptr.i";
%include "cpointer.i"
%include "numpy.i"
%include "exception.i"
%include "attribute.i"

%init %{
import_array();
%}

// Generic input arrays
// floating numbers
%apply(double* IN_ARRAY1, int DIM1) {(double *input, int n_input)}
%apply(double* IN_ARRAY2, int DIM1, DIM2) {(double *input, int n_input1, int n_input2)}
// integers
%apply(char* IN_ARRAY1, int DIM1) {(char *input, int n_input)}
%apply(short* IN_ARRAY1, int DIM1) {(short* input, int n_input)}
%apply(unsigned short* IN_ARRAY1, int DIM1) {(unsigned short* input, int n_input)}
%apply(int* IN_ARRAY1, int DIM1) {(int* input, int n_input)}
%apply(long long* IN_ARRAY1, int DIM1) {(long long *input, int n_input)}
%apply(unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *input, int n_input)}

// Generic output arrays views
// floating points
%apply(double** ARGOUTVIEW_ARRAY1, int* DIM1) {(double** output_view, int* n_output)}

// Generic output memory managed arrays
// floating points
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** output, int* n_output)}
%apply(double** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(double** output, int* n_output1, int* n_output2)}
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** output, int* n_output)}
%apply (double** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(double** output, int* dim1, int* dim2, int* dim3)}
%apply (float** ARGOUTVIEWM_ARRAY4, int* DIM1, int* DIM2, int* DIM3, int* DIM4) {(float** output, int* dim1, int* dim2, int* dim3, int* dim4)}
// integers
%apply(long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(long long **output, int *n_output)}
%apply(unsigned long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(unsigned long long** output, int* n_output)}
%apply(int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(int** output, int* n_output)}
%apply(unsigned int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(unsigned int** output, int* n_output)}
%apply(short** ARGOUTVIEWM_ARRAY1, int* DIM1) {(short** output, int* n_output)}
%apply(unsigned short** ARGOUTVIEWM_ARRAY1, int* DIM1) {(unsigned short** output, int* n_output)}
%apply(char** ARGOUTVIEWM_ARRAY1, int* DIM1) {(char** output, int* n_output)}
%apply(signed char** ARGOUTVIEW_ARRAY1, int* DIM1) {(signed char** output, int* n_output)}
%apply (unsigned int** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(unsigned int** output, int* dim1, int* dim2)}
%apply (unsigned int** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(unsigned int** output, int* dim1, int* dim2, int* dim3)}
%apply (unsigned char** ARGOUTVIEWM_ARRAY4, int* DIM1, int* DIM2, int* DIM3, int* DIM4) {(unsigned char** output, int* dim1, int* dim2, int* dim3, int* dim4)}

// Generic inplace arrays
%apply(double* INPLACE_ARRAY1, int DIM1) {(double* inplace_output, int n_output)}

// Templates
// Vector templates
%template(VectorDouble) std::vector<double>;
%template(VectorUint64) std::vector<unsigned long long>;
%template(VectorInt64) std::vector<long long>;
%template(VectorUint32) std::vector<unsigned int>;
%template(VectorInt32) std::vector<int>;
%template(VectorInt16) std::vector<short>;
%template(VectorUint32_3D) std::vector<std::vector<std::vector<unsigned int>>>;
%template(VectorDouble_2D) std::vector<std::vector<double>>;
%template(MapStringString) std::map<std::string, std::string>;
%template(MapShortVectorDouble) std::map<short, std::vector<double>>;
// Pair templates
%template(VectorPairInt) std::vector<std::pair<int,int>>;
%template(PairVectorDouble) std::pair<std::vector<double>, std::vector<double>>;
%template(PairVectorInt64) std::pair<std::vector<unsigned long long>, std::vector<unsigned long long>>;

%include "../include/info.h"
%include "TTTR.i"
%include "Histogram.i"
%include "Correlator.i"
%include "DecayPhasor.i"
%include "CLSM.i"
%include "Pda.i"
