// SPDX-License-Identifier: BSD-3-Clause
//
// jarrays.i -- Java (JNI primitive array) marshalling for tttrlib.
//
// The Java analogue of numpy.i / rarrays.i: it defines the SAME multi-argument
// typemap names the shared fragments already reference via %apply (IN_ARRAY1/2/3,
// INPLACE_ARRAY1/2/3, ARGOUTVIEW_ARRAY1/2, ARGOUTVIEWM_ARRAY1/2/3/4), backed by
// Java primitive arrays (double[], int[], long[], ...).
//
// Semantics / limitations:
//   * INPUT (IN_ARRAY*): the Java array is borrowed for the call via
//     Get<T>ArrayElements and released with JNI_ABORT (no copy-back).
//   * INPLACE (INPLACE_ARRAY*): released with mode 0 so C++ writes propagate
//     back into the caller's array (Java arrays are references -> true in-place).
//   * OUTPUT (ARGOUTVIEW / ARGOUTVIEWM): the argout builds a fresh Java array
//     into $result and (for the memory-managed ARGOUTVIEWM variant) free()s the
//     C++ buffer. NOTE: unlike Python/R, a Java method has a single return bound
//     to the C++ return type, so a `void f(T** out, int* n)` function returns
//     `void` in Java even though the typemap fills $result. Such functions must
//     be exposed through a Java-specific %extend that returns the array (see
//     ext/java/tttrlib.i). Functions whose array output IS the return value work
//     directly. Multi-dimensional data is returned flat, row-major (Java, like
//     C, is row-major -- no transpose needed).

%{
#include <jni.h>
#include <stdlib.h>
#include <string.h>
%}

// ===========================================================================
// %java_numpy_typemaps(DATA_TYPE, JNIARRAY, JAVATYPE, JELEM,
//                      GETELEMS, RELEASEELEMS, NEWARRAY, SETREGION)
//   DATA_TYPE   : C element type (double, int, long long, ...)
//   JNIARRAY    : JNI array type (jdoubleArray, jintArray, jlongArray, ...)
//   JAVATYPE    : Java scalar type spelled for jtype/jstype (double, int, long)
//   JELEM       : JNI element type (jdouble, jint, jlong, ...)
//   GETELEMS    : Get<T>ArrayElements
//   RELEASEELEMS: Release<T>ArrayElements
//   NEWARRAY    : New<T>Array
//   SETREGION   : Set<T>ArrayRegion
// ===========================================================================
%define %java_numpy_typemaps(DATA_TYPE, JNIARRAY, JAVATYPE, JELEM,
                             GETELEMS, RELEASEELEMS, NEWARRAY, SETREGION)

/* ---- shared jni / jtype / jstype for the INPUT array params -------------- */
%typemap(jni)    (DATA_TYPE* IN_ARRAY1, int DIM1),
                 (DATA_TYPE* INPLACE_ARRAY1, int DIM1) "JNIARRAY"
%typemap(jtype)  (DATA_TYPE* IN_ARRAY1, int DIM1),
                 (DATA_TYPE* INPLACE_ARRAY1, int DIM1) "JAVATYPE[]"
%typemap(jstype) (DATA_TYPE* IN_ARRAY1, int DIM1),
                 (DATA_TYPE* INPLACE_ARRAY1, int DIM1) "JAVATYPE[]"

/* ---- (DATA_TYPE* IN_ARRAY1, int DIM1) : read-only input ------------------ */
%typemap(javain) (DATA_TYPE* IN_ARRAY1, int DIM1) "$javainput"
%typemap(in)     (DATA_TYPE* IN_ARRAY1, int DIM1) {
  if (!$input) {
    SWIG_JavaThrowException(jenv, SWIG_JavaNullPointerException, "array is null");
    return $null;
  }
  $1 = (DATA_TYPE*) JCALL2(GETELEMS, jenv, $input, 0);
  $2 = (int) JCALL1(GetArrayLength, jenv, $input);
}
%typemap(freearg) (DATA_TYPE* IN_ARRAY1, int DIM1) {
  if ($1) JCALL3(RELEASEELEMS, jenv, $input, (JELEM*)$1, JNI_ABORT);
}

/* ---- (DATA_TYPE* INPLACE_ARRAY1, int DIM1) : mutate caller's array ------- */
%typemap(javain) (DATA_TYPE* INPLACE_ARRAY1, int DIM1) "$javainput"
%typemap(in)     (DATA_TYPE* INPLACE_ARRAY1, int DIM1) {
  if (!$input) {
    SWIG_JavaThrowException(jenv, SWIG_JavaNullPointerException, "array is null");
    return $null;
  }
  $1 = (DATA_TYPE*) JCALL2(GETELEMS, jenv, $input, 0);
  $2 = (int) JCALL1(GetArrayLength, jenv, $input);
}
%typemap(freearg) (DATA_TYPE* INPLACE_ARRAY1, int DIM1) {
  if ($1) JCALL3(RELEASEELEMS, jenv, $input, (JELEM*)$1, 0); /* mode 0: copy back */
}

/* ---- (DATA_TYPE* IN_ARRAY2, int DIM1, int DIM2) : 2-D read-only input ----- *
 *
 * Java has no flat-plus-shape array, so the natural spelling is JAVATYPE[][]
 * and the typemap flattens it row-major into one contiguous buffer -- which is
 * what the C++ side wants anyway, and what numpy.i / rarrays.i hand it. The
 * dimensions come from the array itself rather than from the caller, so there
 * is no way to pass a length that disagrees with the data.
 *
 * Ragged input is rejected rather than zero-padded or truncated: a short row
 * would otherwise be read past its end.
 *
 * A copy, unavoidably -- a Java 2-D array is an array OF arrays and its rows
 * are not adjacent in memory, so there is nothing contiguous to borrow.
 */
%typemap(jni)    (DATA_TYPE* IN_ARRAY2, int DIM1, int DIM2) "jobjectArray"
%typemap(jtype)  (DATA_TYPE* IN_ARRAY2, int DIM1, int DIM2) "JAVATYPE[][]"
%typemap(jstype) (DATA_TYPE* IN_ARRAY2, int DIM1, int DIM2) "JAVATYPE[][]"
%typemap(javain) (DATA_TYPE* IN_ARRAY2, int DIM1, int DIM2) "$javainput"
%typemap(in)     (DATA_TYPE* IN_ARRAY2, int DIM1, int DIM2) {
  if (!$input) {
    SWIG_JavaThrowException(jenv, SWIG_JavaNullPointerException, "array is null");
    return $null;
  }
  {
    const jsize n_rows_ = JCALL1(GetArrayLength, jenv, $input);
    jsize n_cols_ = 0;
    if (n_rows_ > 0) {
      JNIARRAY row_ = (JNIARRAY) JCALL2(GetObjectArrayElement, jenv, $input, 0);
      if (!row_) {
        SWIG_JavaThrowException(jenv, SWIG_JavaNullPointerException, "null row in 2-D array");
        return $null;
      }
      n_cols_ = JCALL1(GetArrayLength, jenv, row_);
      JCALL1(DeleteLocalRef, jenv, row_);
    }
    const size_t n_ = (size_t) n_rows_ * (size_t) n_cols_;
    DATA_TYPE *buf_ = (DATA_TYPE *) malloc(sizeof(DATA_TYPE) * (n_ ? n_ : 1));
    if (!buf_) {
      SWIG_JavaThrowException(jenv, SWIG_JavaOutOfMemoryError, "out of memory");
      return $null;
    }
    for (jsize r_ = 0; r_ < n_rows_; r_++) {
      JNIARRAY row_ = (JNIARRAY) JCALL2(GetObjectArrayElement, jenv, $input, r_);
      if (!row_ || JCALL1(GetArrayLength, jenv, row_) != n_cols_) {
        if (row_) JCALL1(DeleteLocalRef, jenv, row_);
        free(buf_);
        SWIG_JavaThrowException(jenv, SWIG_JavaIllegalArgumentException,
                                "ragged 2-D array: every row must have the same length");
        return $null;
      }
      {
        JELEM *p_ = JCALL2(GETELEMS, jenv, row_, 0);
        memcpy(buf_ + (size_t) r_ * (size_t) n_cols_, p_,
               sizeof(DATA_TYPE) * (size_t) n_cols_);
        JCALL3(RELEASEELEMS, jenv, row_, p_, JNI_ABORT);
      }
      JCALL1(DeleteLocalRef, jenv, row_);
    }
    $1 = buf_;
    $2 = (int) n_rows_;
    $3 = (int) n_cols_;
  }
}
%typemap(freearg) (DATA_TYPE* IN_ARRAY2, int DIM1, int DIM2) {
  free($1);
}

/* ---- (DATA_TYPE* IN_ARRAY3, int DIM1, int DIM2, int DIM3) : 3-D input ----- *
 *
 * The same idea as IN_ARRAY2 one level down: JAVATYPE[][][] flattened
 * row-major, dimensions taken from the array, ragged input rejected at either
 * level rather than read past the end of a short row.
 */
%typemap(jni)    (DATA_TYPE* IN_ARRAY3, int DIM1, int DIM2, int DIM3) "jobjectArray"
%typemap(jtype)  (DATA_TYPE* IN_ARRAY3, int DIM1, int DIM2, int DIM3) "JAVATYPE[][][]"
%typemap(jstype) (DATA_TYPE* IN_ARRAY3, int DIM1, int DIM2, int DIM3) "JAVATYPE[][][]"
%typemap(javain) (DATA_TYPE* IN_ARRAY3, int DIM1, int DIM2, int DIM3) "$javainput"
%typemap(in)     (DATA_TYPE* IN_ARRAY3, int DIM1, int DIM2, int DIM3) {
  if (!$input) {
    SWIG_JavaThrowException(jenv, SWIG_JavaNullPointerException, "array is null");
    return $null;
  }
  {
    const jsize n0_ = JCALL1(GetArrayLength, jenv, $input);
    jsize n1_ = 0, n2_ = 0;
    if (n0_ > 0) {
      jobjectArray plane_ = (jobjectArray) JCALL2(GetObjectArrayElement, jenv, $input, 0);
      if (!plane_) {
        SWIG_JavaThrowException(jenv, SWIG_JavaNullPointerException, "null plane in 3-D array");
        return $null;
      }
      n1_ = JCALL1(GetArrayLength, jenv, plane_);
      if (n1_ > 0) {
        JNIARRAY row_ = (JNIARRAY) JCALL2(GetObjectArrayElement, jenv, plane_, 0);
        n2_ = row_ ? JCALL1(GetArrayLength, jenv, row_) : 0;
        if (row_) JCALL1(DeleteLocalRef, jenv, row_);
      }
      JCALL1(DeleteLocalRef, jenv, plane_);
    }
    const size_t n_ = (size_t) n0_ * (size_t) n1_ * (size_t) n2_;
    DATA_TYPE *buf_ = (DATA_TYPE *) malloc(sizeof(DATA_TYPE) * (n_ ? n_ : 1));
    if (!buf_) {
      SWIG_JavaThrowException(jenv, SWIG_JavaOutOfMemoryError, "out of memory");
      return $null;
    }
    for (jsize f_ = 0; f_ < n0_; f_++) {
      jobjectArray plane_ = (jobjectArray) JCALL2(GetObjectArrayElement, jenv, $input, f_);
      if (!plane_ || JCALL1(GetArrayLength, jenv, plane_) != n1_) {
        if (plane_) JCALL1(DeleteLocalRef, jenv, plane_);
        free(buf_);
        SWIG_JavaThrowException(jenv, SWIG_JavaIllegalArgumentException,
                                "ragged 3-D array: every plane must have the same height");
        return $null;
      }
      for (jsize r_ = 0; r_ < n1_; r_++) {
        JNIARRAY row_ = (JNIARRAY) JCALL2(GetObjectArrayElement, jenv, plane_, r_);
        if (!row_ || JCALL1(GetArrayLength, jenv, row_) != n2_) {
          if (row_) JCALL1(DeleteLocalRef, jenv, row_);
          JCALL1(DeleteLocalRef, jenv, plane_);
          free(buf_);
          SWIG_JavaThrowException(jenv, SWIG_JavaIllegalArgumentException,
                                  "ragged 3-D array: every row must have the same length");
          return $null;
        }
        {
          JELEM *p_ = JCALL2(GETELEMS, jenv, row_, 0);
          memcpy(buf_ + (((size_t) f_ * (size_t) n1_) + (size_t) r_) * (size_t) n2_,
                 p_, sizeof(DATA_TYPE) * (size_t) n2_);
          JCALL3(RELEASEELEMS, jenv, row_, p_, JNI_ABORT);
        }
        JCALL1(DeleteLocalRef, jenv, row_);
      }
      JCALL1(DeleteLocalRef, jenv, plane_);
    }
    $1 = buf_;
    $2 = (int) n0_;
    $3 = (int) n1_;
    $4 = (int) n2_;
  }
}
%typemap(freearg) (DATA_TYPE* IN_ARRAY3, int DIM1, int DIM2, int DIM3) {
  free($1);
}

/* ---- INPLACE_ARRAY2 / INPLACE_ARRAY3 : mutate the caller's nested array --- *
 *
 * Same flattening as IN_ARRAY2/3, plus an argout that writes the buffer back
 * into the Java rows -- a Java 2-D array is an array OF arrays, so there is no
 * contiguous block to hand C++ and the copy has to go both ways. The argout
 * runs before freearg, which is what makes the write-back land before the
 * buffer is released.
 */
%typemap(jni)    (DATA_TYPE* INPLACE_ARRAY2, int DIM1, int DIM2) "jobjectArray"
%typemap(jtype)  (DATA_TYPE* INPLACE_ARRAY2, int DIM1, int DIM2) "JAVATYPE[][]"
%typemap(jstype) (DATA_TYPE* INPLACE_ARRAY2, int DIM1, int DIM2) "JAVATYPE[][]"
%typemap(javain) (DATA_TYPE* INPLACE_ARRAY2, int DIM1, int DIM2) "$javainput"
%typemap(in)     (DATA_TYPE* INPLACE_ARRAY2, int DIM1, int DIM2) {
  if (!$input) {
    SWIG_JavaThrowException(jenv, SWIG_JavaNullPointerException, "array is null");
    return $null;
  }
  {
    const jsize n_rows_ = JCALL1(GetArrayLength, jenv, $input);
    jsize n_cols_ = 0;
    if (n_rows_ > 0) {
      JNIARRAY row_ = (JNIARRAY) JCALL2(GetObjectArrayElement, jenv, $input, 0);
      if (!row_) {
        SWIG_JavaThrowException(jenv, SWIG_JavaNullPointerException, "null row in 2-D array");
        return $null;
      }
      n_cols_ = JCALL1(GetArrayLength, jenv, row_);
      JCALL1(DeleteLocalRef, jenv, row_);
    }
    const size_t n_ = (size_t) n_rows_ * (size_t) n_cols_;
    DATA_TYPE *buf_ = (DATA_TYPE *) malloc(sizeof(DATA_TYPE) * (n_ ? n_ : 1));
    if (!buf_) {
      SWIG_JavaThrowException(jenv, SWIG_JavaOutOfMemoryError, "out of memory");
      return $null;
    }
    for (jsize r_ = 0; r_ < n_rows_; r_++) {
      JNIARRAY row_ = (JNIARRAY) JCALL2(GetObjectArrayElement, jenv, $input, r_);
      if (!row_ || JCALL1(GetArrayLength, jenv, row_) != n_cols_) {
        if (row_) JCALL1(DeleteLocalRef, jenv, row_);
        free(buf_);
        SWIG_JavaThrowException(jenv, SWIG_JavaIllegalArgumentException,
                                "ragged 2-D array: every row must have the same length");
        return $null;
      }
      {
        JELEM *p_ = JCALL2(GETELEMS, jenv, row_, 0);
        memcpy(buf_ + (size_t) r_ * (size_t) n_cols_, p_,
               sizeof(DATA_TYPE) * (size_t) n_cols_);
        JCALL3(RELEASEELEMS, jenv, row_, p_, JNI_ABORT);
      }
      JCALL1(DeleteLocalRef, jenv, row_);
    }
    $1 = buf_;
    $2 = (int) n_rows_;
    $3 = (int) n_cols_;
  }
}
%typemap(argout) (DATA_TYPE* INPLACE_ARRAY2, int DIM1, int DIM2) {
  {
    jsize r_;
    for (r_ = 0; r_ < (jsize) $2; r_++) {
      JNIARRAY row_ = (JNIARRAY) JCALL2(GetObjectArrayElement, jenv, $input, r_);
      if (!row_) continue;
      JCALL4(SETREGION, jenv, row_, 0, (jsize) $3,
             (const JELEM *) ($1 + (size_t) r_ * (size_t) $3));
      JCALL1(DeleteLocalRef, jenv, row_);
    }
  }
}
%typemap(freearg) (DATA_TYPE* INPLACE_ARRAY2, int DIM1, int DIM2) {
  free($1);
}

%typemap(jni)    (DATA_TYPE* INPLACE_ARRAY3, int DIM1, int DIM2, int DIM3) "jobjectArray"
%typemap(jtype)  (DATA_TYPE* INPLACE_ARRAY3, int DIM1, int DIM2, int DIM3) "JAVATYPE[][][]"
%typemap(jstype) (DATA_TYPE* INPLACE_ARRAY3, int DIM1, int DIM2, int DIM3) "JAVATYPE[][][]"
%typemap(javain) (DATA_TYPE* INPLACE_ARRAY3, int DIM1, int DIM2, int DIM3) "$javainput"
%typemap(in)     (DATA_TYPE* INPLACE_ARRAY3, int DIM1, int DIM2, int DIM3) {
  if (!$input) {
    SWIG_JavaThrowException(jenv, SWIG_JavaNullPointerException, "array is null");
    return $null;
  }
  {
    const jsize n0_ = JCALL1(GetArrayLength, jenv, $input);
    jsize n1_ = 0, n2_ = 0;
    if (n0_ > 0) {
      jobjectArray plane_ = (jobjectArray) JCALL2(GetObjectArrayElement, jenv, $input, 0);
      if (!plane_) {
        SWIG_JavaThrowException(jenv, SWIG_JavaNullPointerException, "null plane in 3-D array");
        return $null;
      }
      n1_ = JCALL1(GetArrayLength, jenv, plane_);
      if (n1_ > 0) {
        JNIARRAY row_ = (JNIARRAY) JCALL2(GetObjectArrayElement, jenv, plane_, 0);
        n2_ = row_ ? JCALL1(GetArrayLength, jenv, row_) : 0;
        if (row_) JCALL1(DeleteLocalRef, jenv, row_);
      }
      JCALL1(DeleteLocalRef, jenv, plane_);
    }
    const size_t n_ = (size_t) n0_ * (size_t) n1_ * (size_t) n2_;
    DATA_TYPE *buf_ = (DATA_TYPE *) malloc(sizeof(DATA_TYPE) * (n_ ? n_ : 1));
    if (!buf_) {
      SWIG_JavaThrowException(jenv, SWIG_JavaOutOfMemoryError, "out of memory");
      return $null;
    }
    for (jsize f_ = 0; f_ < n0_; f_++) {
      jobjectArray plane_ = (jobjectArray) JCALL2(GetObjectArrayElement, jenv, $input, f_);
      if (!plane_ || JCALL1(GetArrayLength, jenv, plane_) != n1_) {
        if (plane_) JCALL1(DeleteLocalRef, jenv, plane_);
        free(buf_);
        SWIG_JavaThrowException(jenv, SWIG_JavaIllegalArgumentException,
                                "ragged 3-D array: every plane must have the same height");
        return $null;
      }
      for (jsize r_ = 0; r_ < n1_; r_++) {
        JNIARRAY row_ = (JNIARRAY) JCALL2(GetObjectArrayElement, jenv, plane_, r_);
        if (!row_ || JCALL1(GetArrayLength, jenv, row_) != n2_) {
          if (row_) JCALL1(DeleteLocalRef, jenv, row_);
          JCALL1(DeleteLocalRef, jenv, plane_);
          free(buf_);
          SWIG_JavaThrowException(jenv, SWIG_JavaIllegalArgumentException,
                                  "ragged 3-D array: every row must have the same length");
          return $null;
        }
        {
          JELEM *p_ = JCALL2(GETELEMS, jenv, row_, 0);
          memcpy(buf_ + (((size_t) f_ * (size_t) n1_) + (size_t) r_) * (size_t) n2_,
                 p_, sizeof(DATA_TYPE) * (size_t) n2_);
          JCALL3(RELEASEELEMS, jenv, row_, p_, JNI_ABORT);
        }
        JCALL1(DeleteLocalRef, jenv, row_);
      }
      JCALL1(DeleteLocalRef, jenv, plane_);
    }
    $1 = buf_;
    $2 = (int) n0_;
    $3 = (int) n1_;
    $4 = (int) n2_;
  }
}
%typemap(argout) (DATA_TYPE* INPLACE_ARRAY3, int DIM1, int DIM2, int DIM3) {
  {
    jsize f_, r_;
    for (f_ = 0; f_ < (jsize) $2; f_++) {
      jobjectArray plane_ = (jobjectArray) JCALL2(GetObjectArrayElement, jenv, $input, f_);
      if (!plane_) continue;
      for (r_ = 0; r_ < (jsize) $3; r_++) {
        JNIARRAY row_ = (JNIARRAY) JCALL2(GetObjectArrayElement, jenv, plane_, r_);
        if (!row_) continue;
        JCALL4(SETREGION, jenv, row_, 0, (jsize) $4,
               (const JELEM *) ($1 + (((size_t) f_ * (size_t) $3) + (size_t) r_) * (size_t) $4));
        JCALL1(DeleteLocalRef, jenv, row_);
      }
      JCALL1(DeleteLocalRef, jenv, plane_);
    }
  }
}
%typemap(freearg) (DATA_TYPE* INPLACE_ARRAY3, int DIM1, int DIM2, int DIM3) {
  free($1);
}

%enddef

// NOTE: output typemaps (ARGOUTVIEW / ARGOUTVIEWM) are intentionally NOT defined
// for Java. A Java method's return is bound to the C++ return type, so a
// void-returning "output pointer" function has no jresult to assign -- an argout
// that set the result would fail to compile. Such output parameters fall through
// to SWIG's default (an opaque handle) which compiles but is not usable as an
// array. Returning arrays from Java needs per-method extend wrappers (or nio
// buffers) -- tracked as a follow-up. Java INPUT array marshalling is functional.

// ===========================================================================
// Concrete instantiations for every element type used by the interface.
// long long -> Java long (64-bit clean, unlike R). unsigned types map to their
// signed Java carriers (same bit pattern; values above the signed max read as
// negative -- documented caveat).
// ===========================================================================
%java_numpy_typemaps(double,             jdoubleArray, double, jdouble, GetDoubleArrayElements, ReleaseDoubleArrayElements, NewDoubleArray, SetDoubleArrayRegion)
%java_numpy_typemaps(float,              jfloatArray,  float,  jfloat,  GetFloatArrayElements,  ReleaseFloatArrayElements,  NewFloatArray,  SetFloatArrayRegion)
%java_numpy_typemaps(int,                jintArray,    int,    jint,    GetIntArrayElements,    ReleaseIntArrayElements,    NewIntArray,    SetIntArrayRegion)
%java_numpy_typemaps(unsigned int,       jintArray,    int,    jint,    GetIntArrayElements,    ReleaseIntArrayElements,    NewIntArray,    SetIntArrayRegion)
%java_numpy_typemaps(short,              jshortArray,  short,  jshort,  GetShortArrayElements,  ReleaseShortArrayElements,  NewShortArray,  SetShortArrayRegion)
%java_numpy_typemaps(unsigned short,     jshortArray,  short,  jshort,  GetShortArrayElements,  ReleaseShortArrayElements,  NewShortArray,  SetShortArrayRegion)
%java_numpy_typemaps(char,               jbyteArray,   byte,   jbyte,   GetByteArrayElements,   ReleaseByteArrayElements,   NewByteArray,   SetByteArrayRegion)
%java_numpy_typemaps(signed char,        jbyteArray,   byte,   jbyte,   GetByteArrayElements,   ReleaseByteArrayElements,   NewByteArray,   SetByteArrayRegion)
%java_numpy_typemaps(unsigned char,      jbyteArray,   byte,   jbyte,   GetByteArrayElements,   ReleaseByteArrayElements,   NewByteArray,   SetByteArrayRegion)
%java_numpy_typemaps(long long,          jlongArray,   long,   jlong,   GetLongArrayElements,   ReleaseLongArrayElements,   NewLongArray,   SetLongArrayRegion)
%java_numpy_typemaps(unsigned long long, jlongArray,   long,   jlong,   GetLongArrayElements,   ReleaseLongArrayElements,   NewLongArray,   SetLongArrayRegion)
%java_numpy_typemaps(bool,               jbooleanArray, boolean, jboolean, GetBooleanArrayElements, ReleaseBooleanArrayElements, NewBooleanArray, SetBooleanArrayRegion)
