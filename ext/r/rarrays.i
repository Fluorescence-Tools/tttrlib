// SPDX-License-Identifier: BSD-3-Clause
//
// rarrays.i -- R (SEXP) array marshalling for tttrlib.
//
// This is the R analogue of numpy.i: it defines the SAME multi-argument
// typemap *names* that the shared interface fragments already reference via
// %apply (IN_ARRAY1/2/3, INPLACE_ARRAY1/2/3, ARGOUTVIEW_ARRAY1/2,
// ARGOUTVIEWM_ARRAY1/2/3/4), but backed by R vectors (REALSXP / INTSXP /
// LGLSXP) instead of NumPy arrays.  Because only the typemap names matter,
// every %apply(...) line in the shared fragments is reused unchanged.
//
// Semantics vs NumPy (documented limitations):
//   * Inputs are copied into a freshly malloc'd C buffer (with an element-wise
//     cast), never borrowed -- R's copy-on-modify model has no borrowed view.
//   * ARGOUTVIEW / ARGOUTVIEWM outputs are always COPIED into a fresh R vector.
//     For the memory-managed (ARGOUTVIEWM) variant the C++ buffer is free()'d
//     after the copy (ownership transfer).  There is no zero-copy path.
//   * INPLACE_ARRAY* cannot mutate the caller's object in place (copy-on-modify);
//     the mutated buffer is returned as an additional output instead.
//   * Multi-dimensional arrays are transposed between R's column-major and
//     C's row-major layout so that the C++ side always sees C order.
//   * 64-bit integers (long long / unsigned long long) are carried through
//     REALSXP (double) and therefore lose exact precision above 2^53.
//   * unsigned int is carried through REALSXP to preserve its full 32-bit range.

%{
#include <R.h>
#include <Rinternals.h>
#include <stdlib.h>
#include <string.h>
%}

// ---------------------------------------------------------------------------
// Output accumulation helper.
//
// SWIG 4.2's R runtime does not provide a usable multi-output append function
// (SWIG_AppendOutput is referenced but undefined for -r), so we supply our own.
// The R wrapper initialises the result as an empty VECSXP; the first appended
// value is returned bare, the second promotes to a list(2), further values grow
// the list.
// ---------------------------------------------------------------------------
%fragment("SWIG_R_AppendOutput", "header") %{
SWIGINTERN SEXP SWIG_R_AppendOutput(SEXP result, SEXP obj) {
  if (result == R_NilValue ||
      (TYPEOF(result) == VECSXP && Rf_length(result) == 0)) {
    return obj;
  }
  if (TYPEOF(result) != VECSXP) {
    SEXP prev = result;
    PROTECT(prev);
    SEXP out = PROTECT(Rf_allocVector(VECSXP, 2));
    SET_VECTOR_ELT(out, 0, prev);
    SET_VECTOR_ELT(out, 1, obj);
    UNPROTECT(2);
    return out;
  }
  {
    int n = Rf_length(result);
    int i;
    SEXP out = PROTECT(Rf_allocVector(VECSXP, n + 1));
    for (i = 0; i < n; i++) SET_VECTOR_ELT(out, i, VECTOR_ELT(result, i));
    SET_VECTOR_ELT(out, n, obj);
    UNPROTECT(1);
    return out;
  }
}
%}

// ===========================================================================
// %r_numpy_typemaps(DATA_TYPE, R_SXP, R_ACCESS)
//   DATA_TYPE : the C element type (e.g. double, int, long long, bool)
//   R_SXP     : the R storage type carrying it (REALSXP / INTSXP / LGLSXP)
//   R_ACCESS  : the R accessor for that storage (REAL / INTEGER / LOGICAL)
// Emits every array typemap name used by the tttrlib interface for DATA_TYPE.
// ===========================================================================
%define %r_numpy_typemaps(DATA_TYPE, R_SXP, R_ACCESS)

/* ---------------------- 1D input: (T* IN_ARRAY1, int DIM1) ---------------- */
%typemap(in) (DATA_TYPE* IN_ARRAY1, int DIM1) {
  SEXP rv = PROTECT(Rf_coerceVector($input, R_SXP));
  int n = Rf_length(rv), i;
  $1 = (DATA_TYPE*) malloc(sizeof(DATA_TYPE) * (n > 0 ? n : 1));
  for (i = 0; i < n; ++i) $1[i] = (DATA_TYPE) R_ACCESS(rv)[i];
  $2 = n;
  UNPROTECT(1);
}
%typemap(freearg) (DATA_TYPE* IN_ARRAY1, int DIM1) {
  if ($1) free($1);
}

/* ---------------- 2D input: (T* IN_ARRAY2, int DIM1, int DIM2) ------------ */
/* R matrices are column-major; transpose into row-major for C++.            */
%typemap(in) (DATA_TYPE* IN_ARRAY2, int DIM1, int DIM2) {
  SEXP rv = PROTECT(Rf_coerceVector($input, R_SXP));
  SEXP dim = Rf_getAttrib(rv, R_DimSymbol);
  int n = Rf_length(rv), nr, nc, r, c;
  if (dim != R_NilValue && Rf_length(dim) == 2) {
    nr = INTEGER(dim)[0]; nc = INTEGER(dim)[1];
  } else { nr = n; nc = 1; }
  $1 = (DATA_TYPE*) malloc(sizeof(DATA_TYPE) * (n > 0 ? n : 1));
  for (r = 0; r < nr; ++r)
    for (c = 0; c < nc; ++c)
      $1[r * nc + c] = (DATA_TYPE) R_ACCESS(rv)[c * nr + r];
  $2 = nr; $3 = nc;
  UNPROTECT(1);
}
%typemap(freearg) (DATA_TYPE* IN_ARRAY2, int DIM1, int DIM2) {
  if ($1) free($1);
}

/* --------- 3D input: (T* IN_ARRAY3, int DIM1, int DIM2, int DIM3) --------- */
%typemap(in) (DATA_TYPE* IN_ARRAY3, int DIM1, int DIM2, int DIM3) {
  SEXP rv = PROTECT(Rf_coerceVector($input, R_SXP));
  SEXP dim = Rf_getAttrib(rv, R_DimSymbol);
  int n = Rf_length(rv), d0, d1, d2, i, j, k;
  if (dim != R_NilValue && Rf_length(dim) == 3) {
    d0 = INTEGER(dim)[0]; d1 = INTEGER(dim)[1]; d2 = INTEGER(dim)[2];
  } else { d0 = n; d1 = 1; d2 = 1; }
  $1 = (DATA_TYPE*) malloc(sizeof(DATA_TYPE) * (n > 0 ? n : 1));
  for (i = 0; i < d0; ++i)
    for (j = 0; j < d1; ++j)
      for (k = 0; k < d2; ++k)
        $1[(i * d1 + j) * d2 + k] =
          (DATA_TYPE) R_ACCESS(rv)[i + d0 * (j + d1 * k)];
  $2 = d0; $3 = d1; $4 = d2;
  UNPROTECT(1);
}
%typemap(freearg) (DATA_TYPE* IN_ARRAY3, int DIM1, int DIM2, int DIM3) {
  if ($1) free($1);
}

/* --------------- 1D in-place: (T* INPLACE_ARRAY1, int DIM1) --------------- *
 * R is copy-on-modify: we operate on a private copy and hand it back as an
 * additional output so the caller can capture the mutated array.            */
%typemap(in) (DATA_TYPE* INPLACE_ARRAY1, int DIM1) (SEXP rv_, int n_) {
  rv_ = PROTECT(Rf_coerceVector($input, R_SXP));
  n_ = Rf_length(rv_);
  int i;
  $1 = (DATA_TYPE*) malloc(sizeof(DATA_TYPE) * (n_ > 0 ? n_ : 1));
  for (i = 0; i < n_; ++i) $1[i] = (DATA_TYPE) R_ACCESS(rv_)[i];
  $2 = n_;
}
%typemap(argout, fragment="SWIG_R_AppendOutput")
        (DATA_TYPE* INPLACE_ARRAY1, int DIM1) {
  SEXP out = PROTECT(Rf_allocVector(R_SXP, n_$argnum));
  int i;
  for (i = 0; i < n_$argnum; ++i) R_ACCESS(out)[i] = ($1)[i];
  $result = SWIG_R_AppendOutput($result, out);
  UNPROTECT(1);
}
%typemap(freearg) (DATA_TYPE* INPLACE_ARRAY1, int DIM1) {
  if ($1) free($1);
  UNPROTECT(1); /* rv_ */
}

/* ------- 2D/3D in-place (copy semantics, returned as extra output) -------- */
%typemap(in) (DATA_TYPE* INPLACE_ARRAY2, int DIM1, int DIM2) (SEXP rv_, int n_) {
  rv_ = PROTECT(Rf_coerceVector($input, R_SXP));
  SEXP dim = Rf_getAttrib(rv_, R_DimSymbol);
  int nr, nc, r, c; n_ = Rf_length(rv_);
  if (dim != R_NilValue && Rf_length(dim) == 2) { nr = INTEGER(dim)[0]; nc = INTEGER(dim)[1]; }
  else { nr = n_; nc = 1; }
  $1 = (DATA_TYPE*) malloc(sizeof(DATA_TYPE) * (n_ > 0 ? n_ : 1));
  for (r = 0; r < nr; ++r) for (c = 0; c < nc; ++c)
    $1[r * nc + c] = (DATA_TYPE) R_ACCESS(rv_)[c * nr + r];
  $2 = nr; $3 = nc;
}
%typemap(freearg) (DATA_TYPE* INPLACE_ARRAY2, int DIM1, int DIM2) {
  if ($1) free($1); UNPROTECT(1);
}
%typemap(in) (DATA_TYPE* INPLACE_ARRAY3, int DIM1, int DIM2, int DIM3) (SEXP rv_, int n_) {
  rv_ = PROTECT(Rf_coerceVector($input, R_SXP));
  SEXP dim = Rf_getAttrib(rv_, R_DimSymbol);
  int d0, d1, d2, i, j, k; n_ = Rf_length(rv_);
  if (dim != R_NilValue && Rf_length(dim) == 3) { d0 = INTEGER(dim)[0]; d1 = INTEGER(dim)[1]; d2 = INTEGER(dim)[2]; }
  else { d0 = n_; d1 = 1; d2 = 1; }
  $1 = (DATA_TYPE*) malloc(sizeof(DATA_TYPE) * (n_ > 0 ? n_ : 1));
  for (i = 0; i < d0; ++i) for (j = 0; j < d1; ++j) for (k = 0; k < d2; ++k)
    $1[(i * d1 + j) * d2 + k] = (DATA_TYPE) R_ACCESS(rv_)[i + d0 * (j + d1 * k)];
  $2 = d0; $3 = d1; $4 = d2;
}
%typemap(freearg) (DATA_TYPE* INPLACE_ARRAY3, int DIM1, int DIM2, int DIM3) {
  if ($1) free($1); UNPROTECT(1);
}

/* -------- 1D output view: (T** ARGOUTVIEW_ARRAY1, int* DIM1) -------------- *
 * C++ owns the buffer; R copies it (no borrowed view) and does NOT free.    */
%typemap(in, numinputs=0) (DATA_TYPE** ARGOUTVIEW_ARRAY1, int* DIM1)
        (DATA_TYPE* data_temp = 0, int dim_temp = 0) {
  $1 = &data_temp; $2 = &dim_temp;
}
%typemap(argout, fragment="SWIG_R_AppendOutput")
        (DATA_TYPE** ARGOUTVIEW_ARRAY1, int* DIM1) {
  int i, n = *$2;
  SEXP out = PROTECT(Rf_allocVector(R_SXP, n));
  for (i = 0; i < n; ++i) R_ACCESS(out)[i] = (*$1)[i];
  $result = SWIG_R_AppendOutput($result, out);
  UNPROTECT(1);
}

/* -------- 1D managed output: (T** ARGOUTVIEWM_ARRAY1, int* DIM1) ---------- *
 * Ownership transfer: copy then free() the C++ buffer.                       */
%typemap(in, numinputs=0) (DATA_TYPE** ARGOUTVIEWM_ARRAY1, int* DIM1)
        (DATA_TYPE* data_temp = 0, int dim_temp = 0) {
  $1 = &data_temp; $2 = &dim_temp;
}
%typemap(argout, fragment="SWIG_R_AppendOutput")
        (DATA_TYPE** ARGOUTVIEWM_ARRAY1, int* DIM1) {
  int i, n = *$2;
  SEXP out = PROTECT(Rf_allocVector(R_SXP, n));
  for (i = 0; i < n; ++i) R_ACCESS(out)[i] = (*$1)[i];
  if (*$1) free(*$1);
  $result = SWIG_R_AppendOutput($result, out);
  UNPROTECT(1);
}

/* -------- 2D output views: (T** ARGOUTVIEW[M]_ARRAY2, int*, int*) --------- *
 * Fill R column-major from C row-major and attach a dim attribute.          */
%typemap(in, numinputs=0) (DATA_TYPE** ARGOUTVIEW_ARRAY2, int* DIM1, int* DIM2)
        (DATA_TYPE* data_temp = 0, int d1_temp = 0, int d2_temp = 0) {
  $1 = &data_temp; $2 = &d1_temp; $3 = &d2_temp;
}
%typemap(argout, fragment="SWIG_R_AppendOutput")
        (DATA_TYPE** ARGOUTVIEW_ARRAY2, int* DIM1, int* DIM2) {
  int nr = *$2, nc = *$3, r, c;
  SEXP out = PROTECT(Rf_allocVector(R_SXP, nr * nc));
  SEXP dim = PROTECT(Rf_allocVector(INTSXP, 2));
  INTEGER(dim)[0] = nr; INTEGER(dim)[1] = nc;
  for (r = 0; r < nr; ++r) for (c = 0; c < nc; ++c)
    R_ACCESS(out)[c * nr + r] = (*$1)[r * nc + c];
  Rf_setAttrib(out, R_DimSymbol, dim);
  $result = SWIG_R_AppendOutput($result, out);
  UNPROTECT(2);
}
%typemap(in, numinputs=0) (DATA_TYPE** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2)
        (DATA_TYPE* data_temp = 0, int d1_temp = 0, int d2_temp = 0) {
  $1 = &data_temp; $2 = &d1_temp; $3 = &d2_temp;
}
%typemap(argout, fragment="SWIG_R_AppendOutput")
        (DATA_TYPE** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {
  int nr = *$2, nc = *$3, r, c;
  SEXP out = PROTECT(Rf_allocVector(R_SXP, nr * nc));
  SEXP dim = PROTECT(Rf_allocVector(INTSXP, 2));
  INTEGER(dim)[0] = nr; INTEGER(dim)[1] = nc;
  for (r = 0; r < nr; ++r) for (c = 0; c < nc; ++c)
    R_ACCESS(out)[c * nr + r] = (*$1)[r * nc + c];
  Rf_setAttrib(out, R_DimSymbol, dim);
  if (*$1) free(*$1);
  $result = SWIG_R_AppendOutput($result, out);
  UNPROTECT(2);
}

/* -------- 3D managed output: (T** ARGOUTVIEWM_ARRAY3, int*, int*, int*) --- */
%typemap(in, numinputs=0)
        (DATA_TYPE** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3)
        (DATA_TYPE* data_temp = 0, int d1_temp = 0, int d2_temp = 0, int d3_temp = 0) {
  $1 = &data_temp; $2 = &d1_temp; $3 = &d2_temp; $4 = &d3_temp;
}
%typemap(argout, fragment="SWIG_R_AppendOutput")
        (DATA_TYPE** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {
  int d0 = *$2, d1 = *$3, d2 = *$4, i, j, k;
  SEXP out = PROTECT(Rf_allocVector(R_SXP, d0 * d1 * d2));
  SEXP dim = PROTECT(Rf_allocVector(INTSXP, 3));
  INTEGER(dim)[0] = d0; INTEGER(dim)[1] = d1; INTEGER(dim)[2] = d2;
  for (i = 0; i < d0; ++i) for (j = 0; j < d1; ++j) for (k = 0; k < d2; ++k)
    R_ACCESS(out)[i + d0 * (j + d1 * k)] = (*$1)[(i * d1 + j) * d2 + k];
  Rf_setAttrib(out, R_DimSymbol, dim);
  if (*$1) free(*$1);
  $result = SWIG_R_AppendOutput($result, out);
  UNPROTECT(2);
}

/* -------- 4D managed output: (T** ARGOUTVIEWM_ARRAY4, int x4) ------------- */
%typemap(in, numinputs=0)
        (DATA_TYPE** ARGOUTVIEWM_ARRAY4, int* DIM1, int* DIM2, int* DIM3, int* DIM4)
        (DATA_TYPE* data_temp = 0, int d1_temp = 0, int d2_temp = 0, int d3_temp = 0, int d4_temp = 0) {
  $1 = &data_temp; $2 = &d1_temp; $3 = &d2_temp; $4 = &d3_temp; $5 = &d4_temp;
}
%typemap(argout, fragment="SWIG_R_AppendOutput")
        (DATA_TYPE** ARGOUTVIEWM_ARRAY4, int* DIM1, int* DIM2, int* DIM3, int* DIM4) {
  int d0 = *$2, d1 = *$3, d2 = *$4, d3 = *$5, i, j, k, l;
  SEXP out = PROTECT(Rf_allocVector(R_SXP, d0 * d1 * d2 * d3));
  SEXP dim = PROTECT(Rf_allocVector(INTSXP, 4));
  INTEGER(dim)[0] = d0; INTEGER(dim)[1] = d1; INTEGER(dim)[2] = d2; INTEGER(dim)[3] = d3;
  for (i = 0; i < d0; ++i) for (j = 0; j < d1; ++j) for (k = 0; k < d2; ++k) for (l = 0; l < d3; ++l)
    R_ACCESS(out)[i + d0 * (j + d1 * (k + d2 * l))] =
      (*$1)[((i * d1 + j) * d2 + k) * d3 + l];
  Rf_setAttrib(out, R_DimSymbol, dim);
  if (*$1) free(*$1);
  $result = SWIG_R_AppendOutput($result, out);
  UNPROTECT(2);
}

%enddef

// ===========================================================================
// Concrete instantiations.
//   REALSXP/REAL   : double, float, and the wide integers (precision caveats)
//   INTSXP/INTEGER : int and the narrow integer types (all fit in int32)
//   LGLSXP/LOGICAL : bool
// ===========================================================================
%r_numpy_typemaps(double,             REALSXP, REAL)
%r_numpy_typemaps(float,              REALSXP, REAL)
%r_numpy_typemaps(long long,          REALSXP, REAL)
%r_numpy_typemaps(unsigned long long, REALSXP, REAL)
%r_numpy_typemaps(unsigned int,       REALSXP, REAL)

%r_numpy_typemaps(int,                INTSXP,  INTEGER)
%r_numpy_typemaps(short,              INTSXP,  INTEGER)
%r_numpy_typemaps(unsigned short,     INTSXP,  INTEGER)
%r_numpy_typemaps(char,               INTSXP,  INTEGER)
%r_numpy_typemaps(signed char,        INTSXP,  INTEGER)
%r_numpy_typemaps(unsigned char,      INTSXP,  INTEGER)

%r_numpy_typemaps(bool,               LGLSXP,  LOGICAL)
