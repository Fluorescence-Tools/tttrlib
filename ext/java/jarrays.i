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
