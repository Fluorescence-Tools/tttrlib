// SPDX-License-Identifier: BSD-3-Clause
// NOTE: do NOT %include "stdint.i" here. Making the int64_t -> long long
// typedef visible before the API declarations makes SWIG < 4.4 (e.g. the
// Ubuntu 24.04 swig 4.2.0 used by the R/Java CI jobs) resolve int64_t in the
// generated wrappers, which then do not compile against glibc's
// int64_t == long. Sim.i includes stdint.i after all other modules for its
// uint64_t parameters; SWIG >= 4.4 has the stdint types built in and gets
// -DSWIGWORDSIZE64 on Linux instead (see ext/CMakeLists.txt).
%include "stl.i";
%include "typemaps.i";
%include "std_string.i";
#if !defined(SWIGR) && !defined(SWIGJAVASCRIPT)
%include "std_wstring.i";  // SWIG's R and JavaScript libraries ship no std_wstring.i
#endif
%include "std_map.i";
%include "std_vector.i";
#if !defined(SWIGR) && !defined(SWIGJAVASCRIPT)
%include "std_set.i";      // SWIG's R and JavaScript libraries ship no std_set.i
#endif
#ifndef SWIGJAVASCRIPT
%include "std_list.i";     // SWIG's JavaScript library ships no std_list.i
#endif
%include "std_pair.i"; // tttrlib.Correlator.get_tttr
#ifdef SWIGJAVASCRIPT
// SWIG 4.2 ships no boost_shared_ptr.i for the Node-API backend; ext/js supplies
// the missing SWIG_SHARED_PTR_TYPEMAPS so %shared_ptr() works there too.
%include "js_shared_ptr.i";
#else
%include "std_shared_ptr.i";
#endif

%include "cpointer.i"
%include "attribute.i"
%include "exception.i"

// Array marshalling library: chosen per target language. Each library defines
// the SAME typemap names (IN_ARRAY1/2/3, INPLACE_ARRAY*, ARGOUTVIEW[M]_ARRAY*)
// so the %apply(...) directives below are reused unchanged across languages.
#if defined(SWIGPYTHON)
%include "numpy.i"

%init %{
import_array();
%}
#elif defined(SWIGR)
%include "rarrays.i"
#elif defined(SWIGJAVA)
%include "jarrays.i"
#elif defined(SWIGJAVASCRIPT)
%include "jsarrays.i"
#endif

// Templates
#if !defined(SWIGR) && !defined(SWIGJAVASCRIPT)
%template(SetInt32) std::set<int>;  // std::set unsupported by SWIG's R and JavaScript libraries
#endif

// Vector templates
%template(VectorBool) std::vector<bool>;
%template(VectorDouble) std::vector<double>;
%template(VectorFloat) std::vector<float>;
%template(VectorInt16) std::vector<short>;
%template(VectorInt32) std::vector<int>;
%template(VectorInt64) std::vector<long long>;
%template(VectorUint32) std::vector<unsigned int>;
%template(VectorUint64) std::vector<unsigned long>;
%template(VectorUint128) std::vector<unsigned long long>;
%template(VectorUint32_3D) std::vector<std::vector<std::vector<unsigned int>>>;
%template(VectorDouble_2D) std::vector<std::vector<double>>;
%template(MapShortVectorDouble) std::map<short, std::vector<double>>;
%template(MapStringString) std::map<std::string, std::string>;
%template(MapIntVectorFloat) std::map<int, std::vector<float>>;
%template(MapIntInt) std::map<int, int>;
%template(MapSignedCharInt) std::map<signed char, int>;
%template(VectorString) std::vector<std::string>;



// swig::from is provided by the Python and R std_vector runtimes but not by Java
// or JavaScript; exclude these overrides there so they use the default
// std::vector wrapping (JavaScript overrides it in ext/js/jsarrays.i instead).
#if !defined(SWIGJAVA) && !defined(SWIGJAVASCRIPT)
%typemap(out) std::vector< long long,std::allocator< long long > > * {
$result = swig::from(static_cast<std::vector< long long,std::allocator< long long > > >(*($1)));
}

%typemap(out) std::vector< long long > {
$result = swig::from($1);
}
#endif

// Pair templates
%template(VectorPairInt) std::vector<std::pair<int,int>>;
%template(VectorPairInt64) std::vector<std::pair<long long, long long>>;

// With SWIGWORDSIZE64 (Linux, see ext/CMakeLists.txt) int64_t is 'long' and
// therefore a different type than the 'long long' containers above.
// Instantiate the int64_t-based containers there so the APIs spelled with
// int64_t (BurstFilter, BurstFeatureExtractor) convert to/from native lists
// just like they do on Windows/macOS, where int64_t is 'long long'.
#ifdef SWIGWORDSIZE64
%template(VectorInt64T) std::vector<int64_t>;
%template(VectorPairInt64T) std::vector<std::pair<int64_t, int64_t>>;
#endif
%template(PairVectorDouble) std::pair<std::vector<double>, std::vector<double>>;
%template(PairVectorInt64) std::pair<std::vector<unsigned long long>, std::vector<unsigned long long>>;

/*---------------------*/
// Generic numpy arrays
/*---------------------*/

// Inplace arrays
/*---------------------*/

// Float/Double
%apply(double* INPLACE_ARRAY1, int DIM1) {(double* inplace_output, int n_output)}
%apply(double* INPLACE_ARRAY2, int DIM1, int DIM2) {(double* inplace_output, int n_output1, int n_output2)}
%apply(double* INPLACE_ARRAY3, int DIM1, int DIM2, int DIM3) {(double* inplace_output, int n_output1, int n_output2, int n_output3)}
// Input array
/*---------------------*/

// Float/Double
%apply(double* IN_ARRAY1, int DIM1) {(double *input, int n_input)}
%apply(double* IN_ARRAY2, int DIM1, int DIM2) {(double *input, int n_input1, int n_input2)}
%apply(double* IN_ARRAY2, int DIM1, int DIM2) {(const double* img, int ny, int nx)}
%apply(double* IN_ARRAY3, int DIM1, int DIM2, int DIM3) {(const double* data, int dim0, int dim1, int dim2), (const double* stack, int n_frames, int ny, int nx)}
%apply(double** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(double** output, int* dim1, int* dim2)}

// Integers
%apply(char* IN_ARRAY1, int DIM1) {(char *input, int n_input)}
%apply(unsigned char* IN_ARRAY1, int DIM1) {(unsigned char* input, int n_input)}
%apply(short* IN_ARRAY1, int DIM1) {(short* input, int n_input)}
%apply(signed char* IN_ARRAY1, int DIM1) {(signed char* input, int n_input)}
%apply(unsigned short* IN_ARRAY1, int DIM1) {(unsigned short* input, int n_input)}
%apply(int* IN_ARRAY1, int DIM1) {(int* input, int n_input)}
%apply(unsigned int* IN_ARRAY1, int DIM1) {(unsigned int* input, int n_input)}
%apply(long long* IN_ARRAY1, int DIM1) {(long long *input, int n_input)}
%apply(unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *input, int n_input)}

// Bool arrays (for micro-time bitmap).
// numpy.i does not instantiate bool typemaps by default (see numpy.i:3164);
// without this line the %apply below silently matches nothing and the
// micro_time_bitmap parameter is not callable from Python.
#ifdef SWIGPYTHON
%numpy_typemaps(bool, NPY_BOOL, int)
#endif
%apply(bool* IN_ARRAY1, int DIM1) {(bool* micro_time_bitmap, int n_micro_time_bitmap)}

// Output arrays views
/*---------------------*/

// floating points
%apply(double** ARGOUTVIEW_ARRAY1, int* DIM1) {(double** output_view, int* n_output)}
%apply(float** ARGOUTVIEW_ARRAY1, int* DIM1, int* DIM2, int* DIM3, int* DIM4) {(float **output, int *dim1, int *dim2, int *dim3, int *dim4)}
// Generic output memory managed arrays
/*---------------------*/

// float and double
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** output, int* n_output)}
%apply(double** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(double** output, int* n_output1, int* n_output2)}
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** output, int* n_output)}
%apply (double** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(double** output, int* dim1, int* dim2, int* dim3)}
%apply (double** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(double** focus_out, int* focus_nz, int* focus_ny, int* focus_nx)}
%apply (double** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(double** background_out, int* background_nz, int* background_ny, int* background_nx)}
%apply (double** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(double** output, int* out_dim1, int* out_dim2, int* out_dim3)}
%apply (float** ARGOUTVIEWM_ARRAY4, int* DIM1, int* DIM2, int* DIM3, int* DIM4) {(float **output, int *dim1, int *dim2, int *dim3, int *dim4)}

// integers
%apply(long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(long long **output, int *n_output)}
%apply(unsigned long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(unsigned long long** output, int* n_output)}
%apply(int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(int** output, int* n_output)}
%apply(unsigned int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(unsigned int** output, int* n_output)}
%apply(short** ARGOUTVIEWM_ARRAY1, int* DIM1) {(short** output, int* n_output)}
%apply(unsigned short** ARGOUTVIEWM_ARRAY1, int* DIM1) {(unsigned short** output, int* n_output)}
%apply(char** ARGOUTVIEWM_ARRAY1, int* DIM1) {(char** output, int* n_output)}
%apply(unsigned char** ARGOUTVIEWM_ARRAY1, int* DIM1) {(unsigned char** output, int* n_output)}
%apply(signed char** ARGOUTVIEW_ARRAY1, int* DIM1) {(signed char** output, int* n_output)}
%apply (unsigned int** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(unsigned int** output, int* dim1, int* dim2)}
%apply (unsigned char** ARGOUTVIEWM_ARRAY4, int* DIM1, int* DIM2, int* DIM3, int* DIM4) {(unsigned char** output, int* dim1, int* dim2, int* dim3, int* dim4)}
%apply (unsigned short** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(unsigned short** output, int* dim1, int* dim2, int* dim3)}
%apply (unsigned int** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(unsigned int** output, int* dim1, int* dim2, int* dim3)}

// Bool (input arrays)
%apply(bool* IN_ARRAY1, int DIM1) {(bool* input, int n_input)}

// Bool (output arrays)
%apply(bool** ARGOUTVIEWM_ARRAY1, int* DIM1) {(bool **output, int *n_output)}


// ---- Additional STL templates for maps (avoid duplicate int64_t pair specializations) ----
%template(MapStringInt) std::map<std::string,int>;
