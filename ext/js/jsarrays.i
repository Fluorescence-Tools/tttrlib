// SPDX-License-Identifier: BSD-3-Clause
//
// jsarrays.i -- JavaScript (Node-API TypedArray) marshalling for tttrlib.
//
// The JavaScript analogue of numpy.i / rarrays.i / jarrays.i: it defines the
// SAME multi-argument typemap names the shared interface fragments already
// reference via %apply -- IN_ARRAY1/2/3/4, INPLACE_ARRAY1/2/3,
// ARGOUTVIEW_ARRAY1/2/3 and ARGOUTVIEWM_ARRAY1/2/3/4 -- so ~150 %apply lines in
// ext/python/misc_types.i and friends are reused verbatim.
//
// Element-type mapping (chosen so no photon ever loses bits):
//
//   double             <-> Float64Array
//   float              <-> Float32Array
//   char, signed char  <-> Int8Array
//   unsigned char      <-> Uint8Array
//   short              <-> Int16Array
//   unsigned short     <-> Uint16Array
//   int                <-> Int32Array
//   unsigned int       <-> Uint32Array
//   long long          <-> BigInt64Array
//   unsigned long long <-> BigUint64Array
//   bool               <-> Uint8Array   (0 / 1; sizeof(bool) == 1)
//
// Macro times are 64-bit and JavaScript numbers are IEEE doubles, exact only
// below 2^53. So macro-time arrays cross as BigInt64Array / BigUint64Array
// rather than Float64Array: awkward to do arithmetic with, and correct. This is
// the JavaScript form of the rule R already lives under. Reductions users
// actually want (sums, min/max over macro times) are computed in C++.
//
// Semantics:
//
//   IN_ARRAY*       A TypedArray of the matching type is BORROWED -- its data
//                   pointer is handed to C++ with no copy. ByteOffset() is
//                   honoured, so `arr.subarray(k)` reads the right photons
//                   instead of overrunning the buffer. A TypedArray of the
//                   WRONG type is a TypeError, never a silent reinterpretation.
//                   A plain JS Array (or a nested Array, for 2-D/3-D) is copied
//                   and freed by the freearg typemap.
//
//   INPLACE_ARRAY*  Same borrow, but C++ writes through the pointer, so the
//                   caller's TypedArray is mutated. Only a TypedArray of the
//                   matching type is accepted -- copying a plain Array in would
//                   silently discard the writes.
//
//   ARGOUTVIEW_*    A TypedArray over C++-OWNED memory, zero copy. The owning
//                   object must outlive the view; see the warning below.
//
//   ARGOUTVIEWM_*   Same, plus a finalizer that free()s the buffer when the
//                   TypedArray is collected -- the C++ allocation follows JS GC.
//
// Multi-dimensional data is flat and row-major (C order), with the shape
// attached to the returned TypedArray as a plain `shape` property:
//
//     const img = clsm.intensity();     // Uint32Array
//     img.shape                          // [n_frames, n_lines, n_pixel]
//
// which round-trips: a TypedArray carrying `shape` is accepted directly by the
// 2-D/3-D input typemaps. `{data, shape}` objects and nested Arrays are also
// accepted as input.
//
// ---------------------------------------------------------------------------
// LIFETIME WARNING (ARGOUTVIEW only, not ARGOUTVIEWM)
//
// An ARGOUTVIEW TypedArray points into memory owned by the C++ object it came
// from. Dropping the owner while a view is alive and then reading the view is a
// use-after-free -- a segfault, not an exception. The same hazard exists in
// numpy.i (ARGOUTVIEW does not take a reference there either), so this matches
// Python's behaviour rather than inventing a new one. Copy the view (`.slice()`)
// if it must outlive its owner.
// ---------------------------------------------------------------------------
//
// External ArrayBuffers: zero-copy output rests on
// napi_create_external_arraybuffer, which some Node-API runtimes decline
// (Electron restricts it; V8-sandbox builds cannot honour it). Rather than
// crash there, every output path checks the status and falls back to a copy at
// RUNTIME. Compile with -DTTTRLIB_JS_COPY_ARRAYS to force copying everywhere.
// `tttrlib.arraysAreZeroCopy()` reports which mode is live.

%{
#include <napi.h>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <type_traits>

namespace tttrlib_js {

// --- finalizers ------------------------------------------------------------
// napi_finalize signature: (napi_env, void* data, void* hint)
inline void free_finalizer(napi_env, void *data, void *) { std::free(data); }
inline void noop_finalizer(napi_env, void *, void *) {}

// Whether zero-copy views are compiled in at all. The runtime may still fall
// back to a copy per call if the host refuses an external ArrayBuffer.
inline bool zero_copy_compiled_in() {
#ifdef TTTRLIB_JS_COPY_ARRAYS
  return false;
#else
  return true;
#endif
}

// --- element conversion for the plain-JS-Array input path ------------------
// One overload per C++ element type actually used by the interface. `char`,
// `signed char` and `unsigned char` are three distinct C++ types, hence three
// overloads even though two of them are int8_t/uint8_t.
inline bool js_get(const Napi::Value &v, double *o) { *o = v.ToNumber().DoubleValue(); return true; }
inline bool js_get(const Napi::Value &v, float *o) { *o = static_cast<float>(v.ToNumber().DoubleValue()); return true; }
inline bool js_get(const Napi::Value &v, char *o) { *o = static_cast<char>(v.ToNumber().Int32Value()); return true; }
inline bool js_get(const Napi::Value &v, signed char *o) { *o = static_cast<signed char>(v.ToNumber().Int32Value()); return true; }
inline bool js_get(const Napi::Value &v, unsigned char *o) { *o = static_cast<unsigned char>(v.ToNumber().Uint32Value()); return true; }
inline bool js_get(const Napi::Value &v, short *o) { *o = static_cast<short>(v.ToNumber().Int32Value()); return true; }
inline bool js_get(const Napi::Value &v, unsigned short *o) { *o = static_cast<unsigned short>(v.ToNumber().Uint32Value()); return true; }
inline bool js_get(const Napi::Value &v, int *o) { *o = v.ToNumber().Int32Value(); return true; }
inline bool js_get(const Napi::Value &v, unsigned int *o) { *o = v.ToNumber().Uint32Value(); return true; }
inline bool js_get(const Napi::Value &v, bool *o) { *o = v.ToBoolean().Value(); return true; }
// 64-bit: accept a BigInt exactly, or a Number below 2^53 as a convenience.
inline bool js_get(const Napi::Value &v, long long *o) {
  if (v.IsBigInt()) { bool lossless = false; *o = static_cast<long long>(v.As<Napi::BigInt>().Int64Value(&lossless)); return lossless; }
  *o = static_cast<long long>(v.ToNumber().Int64Value());
  return true;
}
inline bool js_get(const Napi::Value &v, unsigned long long *o) {
  if (v.IsBigInt()) { bool lossless = false; *o = static_cast<unsigned long long>(v.As<Napi::BigInt>().Uint64Value(&lossless)); return lossless; }
  double d = v.ToNumber().DoubleValue();
  if (d < 0) return false;
  *o = static_cast<unsigned long long>(d);
  return true;
}
// LP64 Linux: long/unsigned long are 64-bit and distinct from long long.
// Add overloads so js_get resolves for types that SWIG maps to long on LP64.
inline bool js_get(const Napi::Value &v, long *o) {
  long long tmp;
  if (!js_get(v, &tmp)) return false;
  *o = static_cast<long>(tmp);
  return true;
}
inline bool js_get(const Napi::Value &v, unsigned long *o) {
  unsigned long long tmp;
  if (!js_get(v, &tmp)) return false;
  *o = static_cast<unsigned long>(tmp);
  return true;
}

// std::vector<bool> is the bit-packed specialisation: no .data(), and no
// contiguous bytes to hand C++ a `bool*` to. Accumulate booleans in a
// vector<unsigned char> instead, which is byte-for-byte what a `bool*` array is.
template <typename T> struct store_type { typedef T type; };
template <> struct store_type<bool> { typedef unsigned char type; };

// --- generic value -> JavaScript, for the std::map conversions -------------
// The TypedArray kind is derived from the element type's size and signedness
// rather than from a per-type overload, so `int64_t` works whether the platform
// spells it `long` (LP64 Linux) or `long long` (macOS, Windows).
template <class T>
struct napi_type_of {
  static constexpr napi_typedarray_type value =
      std::is_floating_point<T>::value
          ? (sizeof(T) == 8 ? napi_float64_array : napi_float32_array)
          : (std::is_signed<T>::value
                 ? (sizeof(T) == 1 ? napi_int8_array
                    : sizeof(T) == 2 ? napi_int16_array
                    : sizeof(T) == 4 ? napi_int32_array
                                     : napi_bigint64_array)
                 : (sizeof(T) == 1 ? napi_uint8_array
                    : sizeof(T) == 2 ? napi_uint16_array
                    : sizeof(T) == 4 ? napi_uint32_array
                                     : napi_biguint64_array));
};

inline Napi::Value make_view(Napi::Env env, void *data, size_t nelem, size_t elsize,
                             napi_typedarray_type type, bool own,
                             const size_t *dims, int ndim);

/// A numeric vector as a TypedArray (copied: the source is usually a temporary).
template <class T>
inline Napi::Value js_from(Napi::Env env, const std::vector<T> &v) {
  size_t n = v.size();
  size_t dims[1] = {n};
  void *buf = std::malloc(n * sizeof(T) + 1);
  if (!buf) return env.Null();
  if (n) std::memcpy(buf, v.data(), n * sizeof(T));
  return make_view(env, buf, n, sizeof(T), napi_type_of<T>::value, true, dims, 1);
}

/// A ragged vector-of-vectors as an Array of TypedArrays (Python: list of lists).
template <class T>
inline Napi::Value js_from(Napi::Env env, const std::vector<std::vector<T> > &v) {
  Napi::Array out = Napi::Array::New(env, v.size());
  for (size_t i = 0; i < v.size(); i++)
    out.Set(static_cast<uint32_t>(i), js_from(env, v[i]));
  return out;
}

inline Napi::Value js_from(Napi::Env env, const std::string &s) {
  return Napi::String::New(env, s);
}
inline Napi::Value js_from(Napi::Env env, const std::vector<std::string> &v) {
  Napi::Array out = Napi::Array::New(env, v.size());
  for (size_t i = 0; i < v.size(); i++)
    out.Set(static_cast<uint32_t>(i), Napi::String::New(env, v[i]));
  return out;
}
/// Scalars. 64-bit ones become BigInt for the same reason arrays do.
template <class T>
inline typename std::enable_if<std::is_arithmetic<T>::value, Napi::Value>::type
js_from(Napi::Env env, T v) {
  if (sizeof(T) == 8 && std::is_integral<T>::value) {
    return std::is_signed<T>::value
               ? Napi::BigInt::New(env, static_cast<int64_t>(v)).As<Napi::Value>()
               : Napi::BigInt::New(env, static_cast<uint64_t>(v)).As<Napi::Value>();
  }
  return Napi::Number::New(env, static_cast<double>(v));
}

/// Map keys. JavaScript object keys are strings, which is also what a Python
/// dict becomes once it is serialised, so a numeric key is stringified rather
/// than preserved as a number -- the same shape either binding's JSON gives.
inline std::string js_key(const std::string &k) { return k; }
template <class T>
inline typename std::enable_if<std::is_arithmetic<T>::value, std::string>::type
js_key(T k) { return std::to_string(static_cast<long long>(k)); }

// --- input descriptor ------------------------------------------------------
struct NdIn {
  void *data = nullptr;      // borrowed or owned, see `owned`
  bool owned = false;        // true -> freearg must free()
  size_t dims[4] = {0, 0, 0, 0};
};

// Read a `shape` property ([d0, d1, ...]) off a JS object, if present.
inline bool read_shape(const Napi::Value &v, int ndim, size_t *dims) {
  if (!v.IsObject()) return false;
  Napi::Value s = v.As<Napi::Object>().Get("shape");
  if (!s.IsArray()) return false;
  Napi::Array a = s.As<Napi::Array>();
  if (static_cast<int>(a.Length()) != ndim) return false;
  for (int i = 0; i < ndim; i++) dims[i] = static_cast<size_t>(a.Get(i).ToNumber().Int64Value());
  return true;
}

// Borrow a TypedArray's element pointer, honouring ByteOffset (sliced views
// share the underlying ArrayBuffer, and ignoring the offset reads the wrong
// data or runs off the end).
//
// Returns false only for a type mismatch. A null `*data` with `*nelem == 0` is
// a SUCCESS: an empty ArrayBuffer has no data pointer, so `new Float64Array(0)`
// borrows as (nullptr, 0). Reporting that through the return value rather than
// through a null pointer is what keeps "wrong element type" from being the
// message for a perfectly good empty array -- a zero-row table is a normal
// thing to write, and it used to be refused.
inline bool borrow_typed(const Napi::Value &v, napi_typedarray_type want,
                         void **data, size_t *nelem) {
  if (!v.IsTypedArray()) return false;
  Napi::TypedArray ta = v.As<Napi::TypedArray>();
  if (ta.TypedArrayType() != want) return false;
  *nelem = ta.ElementLength();
  char *base = static_cast<char *>(ta.ArrayBuffer().Data());
  *data = base ? base + ta.ByteOffset() : nullptr;
  return true;
}

// Cheap predicate for overload resolution (typecheck typemaps).
inline bool could_be_array(const Napi::Value &v, napi_typedarray_type want) {
  if (v.IsTypedArray()) return v.As<Napi::TypedArray>().TypedArrayType() == want;
  if (v.IsArray()) return true;
  if (v.IsObject() && !v.IsFunction()) return v.As<Napi::Object>().Get("data").IsTypedArray();
  return false;
}

// Flatten a nested JS Array into `out`, inferring dims from the first element
// of each level. Ragged input is rejected rather than zero-padded.
template <typename T>
bool flatten(const Napi::Value &v, int depth, int ndim, size_t *dims,
             std::vector<typename store_type<T>::type> &out, std::string *err) {
  if (depth == ndim) {
    typename store_type<T>::type tmp;
    if (!js_get(v, &tmp)) { *err = "element is not exactly representable"; return false; }
    out.push_back(tmp);
    return true;
  }
  if (!v.IsArray()) { *err = "expected a nested Array of depth " + std::to_string(ndim); return false; }
  Napi::Array a = v.As<Napi::Array>();
  size_t n = a.Length();
  if (dims[depth] == 0) dims[depth] = n;
  else if (dims[depth] != n) { *err = "ragged nested Array: inconsistent length at depth " + std::to_string(depth); return false; }
  for (size_t i = 0; i < n; i++)
    if (!flatten<T>(a.Get(static_cast<uint32_t>(i)), depth + 1, ndim, dims, out, err)) return false;
  return true;
}

// Resolve any accepted JS input form to a flat, C-contiguous buffer of `ndim`
// dimensions. Returns false and fills `err` on a type/shape problem.
template <typename T>
bool nd_input(const Napi::Value &v, napi_typedarray_type want, int ndim,
              NdIn *out, std::string *err) {
  // 1. {data: TypedArray, shape: [...]} -- unwrap to the TypedArray, keeping
  //    the outer object's shape (which the inner array need not carry).
  Napi::Value arr = v;
  bool have_shape = read_shape(v, ndim, out->dims);
  if (v.IsObject() && !v.IsTypedArray() && !v.IsArray()) {
    Napi::Value d = v.As<Napi::Object>().Get("data");
    if (!d.IsTypedArray()) { *err = "object input needs a `data` TypedArray"; return false; }
    arr = d;
  }

  // 2. TypedArray -- borrow, no copy.
  if (arr.IsTypedArray()) {
    size_t nelem = 0;
    void *p = nullptr;
    if (!borrow_typed(arr, want, &p, &nelem)) {
      *err = "TypedArray has the wrong element type";
      return false;
    }
    if (!have_shape) have_shape = read_shape(arr, ndim, out->dims);
    if (!have_shape) {
      if (ndim != 1) { *err = "a flat TypedArray needs a `shape` property for a multi-dimensional argument"; return false; }
      out->dims[0] = nelem;
    } else {
      size_t want_n = 1;
      for (int i = 0; i < ndim; i++) want_n *= out->dims[i];
      if (want_n != nelem) { *err = "`shape` does not match the TypedArray length"; return false; }
    }
    out->data = p;
    out->owned = false;
    return true;
  }

  // 3. Plain (possibly nested) Array -- copy.
  if (arr.IsArray()) {
    std::vector<typename store_type<T>::type> tmp;
    for (int i = 0; i < ndim; i++) out->dims[i] = 0;
    if (!flatten<T>(arr, 0, ndim, out->dims, tmp, err)) return false;
    size_t bytes = tmp.size() * sizeof(typename store_type<T>::type);
    void *p = std::malloc(bytes ? bytes : 1);
    if (!p) { *err = "out of memory"; return false; }
    if (bytes) std::memcpy(p, tmp.data(), bytes);
    out->data = p;
    out->owned = true;
    return true;
  }

  *err = "expected a TypedArray, an Array, or {data, shape}";
  return false;
}

// Same, but for INPLACE: only a real TypedArray will do, since a copy would
// throw the C++ writes away.
template <typename T>
bool nd_inplace(const Napi::Value &v, napi_typedarray_type want, int ndim,
                NdIn *out, std::string *err) {
  Napi::Value arr = v;
  bool have_shape = read_shape(v, ndim, out->dims);
  if (v.IsObject() && !v.IsTypedArray() && !v.IsArray()) {
    Napi::Value d = v.As<Napi::Object>().Get("data");
    if (!d.IsTypedArray()) { *err = "in-place argument must be a TypedArray"; return false; }
    arr = d;
  }
  if (!arr.IsTypedArray()) { *err = "in-place argument must be a TypedArray (a plain Array cannot be written back)"; return false; }
  size_t nelem = 0;
  void *p = nullptr;
  if (!borrow_typed(arr, want, &p, &nelem)) {
    *err = "TypedArray has the wrong element type"; return false;
  }
  if (!have_shape) have_shape = read_shape(arr, ndim, out->dims);
  if (!have_shape) {
    if (ndim != 1) { *err = "a flat TypedArray needs a `shape` property for a multi-dimensional argument"; return false; }
    out->dims[0] = nelem;
  }
  out->data = p;
  out->owned = false;
  return true;
}

// --- output ----------------------------------------------------------------
// Build a TypedArray over `data`. Zero-copy when the host allows an external
// ArrayBuffer; a copy otherwise (and `own` memory is freed either way, so the
// fallback never leaks).
inline Napi::Value make_view(Napi::Env env, void *data, size_t nelem, size_t elsize,
                             napi_typedarray_type type, bool own,
                             const size_t *dims, int ndim) {
  napi_value ab = nullptr;
  napi_status st = napi_generic_failure;
  size_t bytes = nelem * elsize;
  bool copied = false;

#ifndef TTTRLIB_JS_COPY_ARRAYS
  if (data != nullptr && bytes != 0) {
    st = napi_create_external_arraybuffer(
        env, data, bytes, own ? free_finalizer : noop_finalizer, nullptr, &ab);
  }
#endif
  if (st != napi_ok) {
    // Either the build forces copies, the array is empty, or the host declined
    // an external buffer (Electron, V8 sandbox). Copy, then release the C++
    // allocation ourselves since no finalizer will.
    void *dst = nullptr;
    st = napi_create_arraybuffer(env, bytes, &dst, &ab);
    if (st != napi_ok) {
      if (own) std::free(data);
      NAPI_THROW(Napi::Error::New(env, "tttrlib: could not allocate an ArrayBuffer"), Napi::Value());
    }
    if (bytes && data) std::memcpy(dst, data, bytes);
    if (own) std::free(data);
    copied = true;
  }
  (void)copied;

  napi_value ta = nullptr;
  st = napi_create_typedarray(env, type, nelem, ab, 0, &ta);
  if (st != napi_ok)
    NAPI_THROW(Napi::Error::New(env, "tttrlib: could not create a TypedArray"), Napi::Value());

  Napi::Value out(env, ta);
  if (ndim > 1) {
    Napi::Array shape = Napi::Array::New(env, ndim);
    for (int i = 0; i < ndim; i++)
      shape.Set(static_cast<uint32_t>(i), Napi::Number::New(env, static_cast<double>(dims[i])));
    out.As<Napi::Object>().Set("shape", shape);
  }
  return out;
}

}  // namespace tttrlib_js
%}

// Report the marshalling mode -- part of the public surface because the answer
// changes what a caller may assume about aliasing.
%inline %{
/// True when array outputs are zero-copy views over C++ memory (the default),
/// false in a -DTTTRLIB_JS_COPY_ARRAYS build.
bool arraysAreZeroCopy() { return tttrlib_js::zero_copy_compiled_in(); }
%}

// ===========================================================================
// 64-bit integer SCALARS as BigInt
// ===========================================================================
//
// The arrays have always been exact: a macro-time channel comes back as a
// BigUint64Array and sums past 2^53 without loss. The SCALARS did not --
// SWIG's Node-API backend routes long long and unsigned long long through
// Napi::Number, which is an IEEE double, so anything above 2^53 came back
// silently rounded:
//
//     python  uid  14523661926200792394
//     js      uid  14523661926200793000     off by 606, no warning
//
// That is not a JavaScript limitation. BigInt is ES2020 and this binding
// already relies on it for the arrays; only the scalar path was missing.
//
// Scoped to `long long` and `unsigned long long` deliberately. `size_t` and
// `unsigned long` are also 64-bit on LP64, but they carry counts and lengths --
// n_rows(), size() -- which every caller does arithmetic on. Turning those into
// BigInt would break `n + 1` everywhere for no correctness gain, since a count
// that large is not reachable. std::uint64_t resolves to unsigned long long on
// macOS and to unsigned long on Linux, so the %apply below names both spellings.
%typemap(out) long long, const long long&
  %{ $result = Napi::BigInt::New(env, (int64_t) $1); %}
%typemap(out) unsigned long long, const unsigned long long&
  %{ $result = Napi::BigInt::New(env, (uint64_t) $1); %}

// On LP64 Linux std::uint64_t resolves to unsigned long, which the
// typemaps above do not match -- an API spelled uint64_t then silently
// returns a Number there and a BigInt on macOS. Matching the fixed-width
// spelling keeps the API BigInt on every platform while leaving size_t
// and plain long as the Numbers they are meant to be.
%typemap(out) int64_t, const int64_t&
  %{ $result = Napi::BigInt::New(env, (int64_t) $1); %}
%typemap(out) uint64_t, const uint64_t&
  %{ $result = Napi::BigInt::New(env, (uint64_t) $1); %}
%typemap(in) int64_t = long long;
%typemap(in) const int64_t& = long long;
%typemap(in) uint64_t = unsigned long long;
%typemap(in) const uint64_t& = unsigned long long;
%typemap(typecheck, precedence=SWIG_TYPECHECK_INT64) int64_t, uint64_t, const int64_t&, const uint64_t&
  %{ $1 = $input.IsBigInt() || $input.IsNumber(); %}

// Input accepts a BigInt or a Number. A Number is allowed because most values
// in this API are small and a caller should not have to write 0n everywhere;
// it is rejected when it is not an exact integer, so nothing is lost quietly.
%typemap(in) long long %{
  if ($input.IsBigInt()) {
    bool lossless_ = false;
    $1 = (long long) $input.As<Napi::BigInt>().Int64Value(&lossless_);
    if (!lossless_) SWIG_exception_fail(SWIG_OverflowError,
        "BigInt does not fit in a signed 64-bit integer");
  } else if ($input.IsNumber()) {
    double d_ = $input.As<Napi::Number>().DoubleValue();
    if (d_ != (double)(long long) d_) SWIG_exception_fail(SWIG_ValueError,
        "a 64-bit integer argument needs a BigInt when it is not an exact integer");
    $1 = (long long) d_;
  } else {
    SWIG_exception_fail(SWIG_TypeError, "expected a BigInt or a Number");
  }
%}
%typemap(in) unsigned long long %{
  if ($input.IsBigInt()) {
    bool lossless_ = false;
    $1 = (unsigned long long) $input.As<Napi::BigInt>().Uint64Value(&lossless_);
    if (!lossless_) SWIG_exception_fail(SWIG_OverflowError,
        "BigInt does not fit in an unsigned 64-bit integer");
  } else if ($input.IsNumber()) {
    double d_ = $input.As<Napi::Number>().DoubleValue();
    if (d_ < 0 || d_ != (double)(unsigned long long) d_)
      SWIG_exception_fail(SWIG_ValueError,
        "a 64-bit integer argument needs a BigInt when it is not an exact integer");
    $1 = (unsigned long long) d_;
  } else {
    SWIG_exception_fail(SWIG_TypeError, "expected a BigInt or a Number");
  }
%}

// Overload resolution has to accept both spellings too, or a BigInt argument
// makes every candidate fail to match.
%typemap(typecheck, precedence=SWIG_TYPECHECK_INT64) long long, unsigned long long
  %{ $1 = $input.IsBigInt() || $input.IsNumber(); %}

// ===========================================================================
// %js_numpy_typemaps(DATA_TYPE, NAPI_TYPE, JS_NAME)
//   DATA_TYPE : C++ element type (double, unsigned long long, ...)
//   NAPI_TYPE : napi_typedarray_type enumerator (napi_float64_array, ...)
//   JS_NAME   : the JavaScript constructor name, for error messages
// ===========================================================================
%define %js_numpy_typemaps(DATA_TYPE, NAPI_TYPE, JS_NAME)

/* ------------------------------- IN_ARRAY1 ------------------------------- */
%typemap(in) (DATA_TYPE* IN_ARRAY1, int DIM1)
             (tttrlib_js::NdIn nd_, std::string err_) {
  if (!tttrlib_js::nd_input<DATA_TYPE>($input, NAPI_TYPE, 1, &nd_, &err_)) {
    SWIG_exception_fail(SWIG_TypeError,
      ("in method '$symname', argument $argnum: expected a " JS_NAME " -- " + err_).c_str());
  }
  $1 = static_cast<DATA_TYPE*>(nd_.data);
  $2 = static_cast<int>(nd_.dims[0]);
}
%typemap(freearg) (DATA_TYPE* IN_ARRAY1, int DIM1) {
  if (nd_$argnum.owned) std::free(nd_$argnum.data);
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_DOUBLE_ARRAY)
             (DATA_TYPE* IN_ARRAY1, int DIM1) {
  $1 = tttrlib_js::could_be_array($input, NAPI_TYPE) ? 1 : 0;
}

/* ------------------------------- IN_ARRAY2 ------------------------------- */
%typemap(in) (DATA_TYPE* IN_ARRAY2, int DIM1, int DIM2)
             (tttrlib_js::NdIn nd_, std::string err_) {
  if (!tttrlib_js::nd_input<DATA_TYPE>($input, NAPI_TYPE, 2, &nd_, &err_)) {
    SWIG_exception_fail(SWIG_TypeError,
      ("in method '$symname', argument $argnum: expected a 2-D " JS_NAME " -- " + err_).c_str());
  }
  $1 = static_cast<DATA_TYPE*>(nd_.data);
  $2 = static_cast<int>(nd_.dims[0]);
  $3 = static_cast<int>(nd_.dims[1]);
}
%typemap(freearg) (DATA_TYPE* IN_ARRAY2, int DIM1, int DIM2) {
  if (nd_$argnum.owned) std::free(nd_$argnum.data);
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_DOUBLE_ARRAY)
             (DATA_TYPE* IN_ARRAY2, int DIM1, int DIM2) {
  $1 = tttrlib_js::could_be_array($input, NAPI_TYPE) ? 1 : 0;
}

/* ------------------------------- IN_ARRAY3 ------------------------------- */
%typemap(in) (DATA_TYPE* IN_ARRAY3, int DIM1, int DIM2, int DIM3)
             (tttrlib_js::NdIn nd_, std::string err_) {
  if (!tttrlib_js::nd_input<DATA_TYPE>($input, NAPI_TYPE, 3, &nd_, &err_)) {
    SWIG_exception_fail(SWIG_TypeError,
      ("in method '$symname', argument $argnum: expected a 3-D " JS_NAME " -- " + err_).c_str());
  }
  $1 = static_cast<DATA_TYPE*>(nd_.data);
  $2 = static_cast<int>(nd_.dims[0]);
  $3 = static_cast<int>(nd_.dims[1]);
  $4 = static_cast<int>(nd_.dims[2]);
}
%typemap(freearg) (DATA_TYPE* IN_ARRAY3, int DIM1, int DIM2, int DIM3) {
  if (nd_$argnum.owned) std::free(nd_$argnum.data);
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_DOUBLE_ARRAY)
             (DATA_TYPE* IN_ARRAY3, int DIM1, int DIM2, int DIM3) {
  $1 = tttrlib_js::could_be_array($input, NAPI_TYPE) ? 1 : 0;
}

/* ------------------------------- IN_ARRAY4 ------------------------------- */
%typemap(in) (DATA_TYPE* IN_ARRAY4, int DIM1, int DIM2, int DIM3, int DIM4)
             (tttrlib_js::NdIn nd_, std::string err_) {
  if (!tttrlib_js::nd_input<DATA_TYPE>($input, NAPI_TYPE, 4, &nd_, &err_)) {
    SWIG_exception_fail(SWIG_TypeError,
      ("in method '$symname', argument $argnum: expected a 4-D " JS_NAME " -- " + err_).c_str());
  }
  $1 = static_cast<DATA_TYPE*>(nd_.data);
  $2 = static_cast<int>(nd_.dims[0]);
  $3 = static_cast<int>(nd_.dims[1]);
  $4 = static_cast<int>(nd_.dims[2]);
  $5 = static_cast<int>(nd_.dims[3]);
}
%typemap(freearg) (DATA_TYPE* IN_ARRAY4, int DIM1, int DIM2, int DIM3, int DIM4) {
  if (nd_$argnum.owned) std::free(nd_$argnum.data);
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_DOUBLE_ARRAY)
             (DATA_TYPE* IN_ARRAY4, int DIM1, int DIM2, int DIM3, int DIM4) {
  $1 = tttrlib_js::could_be_array($input, NAPI_TYPE) ? 1 : 0;
}

/* ----------------------------- INPLACE_ARRAY1 ---------------------------- */
%typemap(in) (DATA_TYPE* INPLACE_ARRAY1, int DIM1)
             (tttrlib_js::NdIn nd_, std::string err_) {
  if (!tttrlib_js::nd_inplace<DATA_TYPE>($input, NAPI_TYPE, 1, &nd_, &err_)) {
    SWIG_exception_fail(SWIG_TypeError,
      ("in method '$symname', argument $argnum: expected a writable " JS_NAME " -- " + err_).c_str());
  }
  $1 = static_cast<DATA_TYPE*>(nd_.data);
  $2 = static_cast<int>(nd_.dims[0]);
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_DOUBLE_ARRAY)
             (DATA_TYPE* INPLACE_ARRAY1, int DIM1) {
  $1 = ($input.IsTypedArray() && $input.As<Napi::TypedArray>().TypedArrayType() == NAPI_TYPE) ? 1 : 0;
}

/* ----------------------------- INPLACE_ARRAY2 ---------------------------- */
%typemap(in) (DATA_TYPE* INPLACE_ARRAY2, int DIM1, int DIM2)
             (tttrlib_js::NdIn nd_, std::string err_) {
  if (!tttrlib_js::nd_inplace<DATA_TYPE>($input, NAPI_TYPE, 2, &nd_, &err_)) {
    SWIG_exception_fail(SWIG_TypeError,
      ("in method '$symname', argument $argnum: expected a writable 2-D " JS_NAME " -- " + err_).c_str());
  }
  $1 = static_cast<DATA_TYPE*>(nd_.data);
  $2 = static_cast<int>(nd_.dims[0]);
  $3 = static_cast<int>(nd_.dims[1]);
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_DOUBLE_ARRAY)
             (DATA_TYPE* INPLACE_ARRAY2, int DIM1, int DIM2) {
  $1 = ($input.IsTypedArray() && $input.As<Napi::TypedArray>().TypedArrayType() == NAPI_TYPE) ? 1 : 0;
}

/* ----------------------------- INPLACE_ARRAY3 ---------------------------- */
%typemap(in) (DATA_TYPE* INPLACE_ARRAY3, int DIM1, int DIM2, int DIM3)
             (tttrlib_js::NdIn nd_, std::string err_) {
  if (!tttrlib_js::nd_inplace<DATA_TYPE>($input, NAPI_TYPE, 3, &nd_, &err_)) {
    SWIG_exception_fail(SWIG_TypeError,
      ("in method '$symname', argument $argnum: expected a writable 3-D " JS_NAME " -- " + err_).c_str());
  }
  $1 = static_cast<DATA_TYPE*>(nd_.data);
  $2 = static_cast<int>(nd_.dims[0]);
  $3 = static_cast<int>(nd_.dims[1]);
  $4 = static_cast<int>(nd_.dims[2]);
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_DOUBLE_ARRAY)
             (DATA_TYPE* INPLACE_ARRAY3, int DIM1, int DIM2, int DIM3) {
  $1 = ($input.IsTypedArray() && $input.As<Napi::TypedArray>().TypedArrayType() == NAPI_TYPE) ? 1 : 0;
}

/* ------------------------- ARGOUTVIEW / ARGOUTVIEWM ----------------------- */
// The `in` typemaps take no JS argument (numinputs=0) and point the C++ output
// parameters at stack temporaries; `argout` turns those into a TypedArray.
//
// $result carries the function's own return value when it has one. A void
// function's `out` typemap leaves it Undefined, and in that case the array IS
// the result rather than a one-element list -- which is what numpy.i does for
// Python and what makes `tttr.macroTimes()` return an array and not `[array]`.
// Two argouts on one function append, giving `[a, b]`, again as in Python.

/* --- 1-D --- */
// One %typemap per pattern, NOT a comma-separated list. SWIG attaches typemap
// locals to the LAST pattern only, so declaring ARGOUTVIEW and ARGOUTVIEWM
// together silently left every ARGOUTVIEW wrapper referring to an undeclared
// `data_temp`.
%typemap(in, numinputs=0) (DATA_TYPE** ARGOUTVIEW_ARRAY1, int* DIM1)
             (DATA_TYPE* data_temp = 0, int dim_temp1 = 0) {
  $1 = &data_temp; $2 = &dim_temp1;
}
%typemap(in, numinputs=0) (DATA_TYPE** ARGOUTVIEWM_ARRAY1, int* DIM1)
             (DATA_TYPE* data_temp = 0, int dim_temp1 = 0) {
  $1 = &data_temp; $2 = &dim_temp1;
}
%typemap(argout) (DATA_TYPE** ARGOUTVIEW_ARRAY1, int* DIM1) {
  size_t dims_[1] = { (size_t)(*$2) };
  Napi::Value view_ = tttrlib_js::make_view(env, (void*)(*$1), dims_[0], sizeof(DATA_TYPE), NAPI_TYPE, false, dims_, 1);
  if ($result.IsEmpty() || $result.IsUndefined()) $result = view_; else $result = SWIG_AppendOutput($result, view_);
}
%typemap(argout) (DATA_TYPE** ARGOUTVIEWM_ARRAY1, int* DIM1) {
  size_t dims_[1] = { (size_t)(*$2) };
  Napi::Value view_ = tttrlib_js::make_view(env, (void*)(*$1), dims_[0], sizeof(DATA_TYPE), NAPI_TYPE, true, dims_, 1);
  if ($result.IsEmpty() || $result.IsUndefined()) $result = view_; else $result = SWIG_AppendOutput($result, view_);
}

/* --- 2-D --- */
%typemap(in, numinputs=0) (DATA_TYPE** ARGOUTVIEW_ARRAY2, int* DIM1, int* DIM2)
             (DATA_TYPE* data_temp = 0, int dim_temp1 = 0, int dim_temp2 = 0) {
  $1 = &data_temp; $2 = &dim_temp1; $3 = &dim_temp2;
}
%typemap(in, numinputs=0) (DATA_TYPE** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2)
             (DATA_TYPE* data_temp = 0, int dim_temp1 = 0, int dim_temp2 = 0) {
  $1 = &data_temp; $2 = &dim_temp1; $3 = &dim_temp2;
}
%typemap(argout) (DATA_TYPE** ARGOUTVIEW_ARRAY2, int* DIM1, int* DIM2) {
  size_t dims_[2] = { (size_t)(*$2), (size_t)(*$3) };
  Napi::Value view_ = tttrlib_js::make_view(env, (void*)(*$1), dims_[0]*dims_[1], sizeof(DATA_TYPE), NAPI_TYPE, false, dims_, 2);
  if ($result.IsEmpty() || $result.IsUndefined()) $result = view_; else $result = SWIG_AppendOutput($result, view_);
}
%typemap(argout) (DATA_TYPE** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {
  size_t dims_[2] = { (size_t)(*$2), (size_t)(*$3) };
  Napi::Value view_ = tttrlib_js::make_view(env, (void*)(*$1), dims_[0]*dims_[1], sizeof(DATA_TYPE), NAPI_TYPE, true, dims_, 2);
  if ($result.IsEmpty() || $result.IsUndefined()) $result = view_; else $result = SWIG_AppendOutput($result, view_);
}

/* --- 3-D --- */
%typemap(in, numinputs=0) (DATA_TYPE** ARGOUTVIEW_ARRAY3, int* DIM1, int* DIM2, int* DIM3)
             (DATA_TYPE* data_temp = 0, int dim_temp1 = 0, int dim_temp2 = 0, int dim_temp3 = 0) {
  $1 = &data_temp; $2 = &dim_temp1; $3 = &dim_temp2; $4 = &dim_temp3;
}
%typemap(in, numinputs=0) (DATA_TYPE** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3)
             (DATA_TYPE* data_temp = 0, int dim_temp1 = 0, int dim_temp2 = 0, int dim_temp3 = 0) {
  $1 = &data_temp; $2 = &dim_temp1; $3 = &dim_temp2; $4 = &dim_temp3;
}
%typemap(argout) (DATA_TYPE** ARGOUTVIEW_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {
  size_t dims_[3] = { (size_t)(*$2), (size_t)(*$3), (size_t)(*$4) };
  Napi::Value view_ = tttrlib_js::make_view(env, (void*)(*$1), dims_[0]*dims_[1]*dims_[2], sizeof(DATA_TYPE), NAPI_TYPE, false, dims_, 3);
  if ($result.IsEmpty() || $result.IsUndefined()) $result = view_; else $result = SWIG_AppendOutput($result, view_);
}
%typemap(argout) (DATA_TYPE** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {
  size_t dims_[3] = { (size_t)(*$2), (size_t)(*$3), (size_t)(*$4) };
  Napi::Value view_ = tttrlib_js::make_view(env, (void*)(*$1), dims_[0]*dims_[1]*dims_[2], sizeof(DATA_TYPE), NAPI_TYPE, true, dims_, 3);
  if ($result.IsEmpty() || $result.IsUndefined()) $result = view_; else $result = SWIG_AppendOutput($result, view_);
}

/* --- 4-D --- */
%typemap(in, numinputs=0) (DATA_TYPE** ARGOUTVIEW_ARRAY4, int* DIM1, int* DIM2, int* DIM3, int* DIM4)
             (DATA_TYPE* data_temp = 0, int dim_temp1 = 0, int dim_temp2 = 0, int dim_temp3 = 0, int dim_temp4 = 0) {
  $1 = &data_temp; $2 = &dim_temp1; $3 = &dim_temp2; $4 = &dim_temp3; $5 = &dim_temp4;
}
%typemap(in, numinputs=0) (DATA_TYPE** ARGOUTVIEWM_ARRAY4, int* DIM1, int* DIM2, int* DIM3, int* DIM4)
             (DATA_TYPE* data_temp = 0, int dim_temp1 = 0, int dim_temp2 = 0, int dim_temp3 = 0, int dim_temp4 = 0) {
  $1 = &data_temp; $2 = &dim_temp1; $3 = &dim_temp2; $4 = &dim_temp3; $5 = &dim_temp4;
}
%typemap(argout) (DATA_TYPE** ARGOUTVIEW_ARRAY4, int* DIM1, int* DIM2, int* DIM3, int* DIM4) {
  size_t dims_[4] = { (size_t)(*$2), (size_t)(*$3), (size_t)(*$4), (size_t)(*$5) };
  Napi::Value view_ = tttrlib_js::make_view(env, (void*)(*$1), dims_[0]*dims_[1]*dims_[2]*dims_[3], sizeof(DATA_TYPE), NAPI_TYPE, false, dims_, 4);
  if ($result.IsEmpty() || $result.IsUndefined()) $result = view_; else $result = SWIG_AppendOutput($result, view_);
}
%typemap(argout) (DATA_TYPE** ARGOUTVIEWM_ARRAY4, int* DIM1, int* DIM2, int* DIM3, int* DIM4) {
  size_t dims_[4] = { (size_t)(*$2), (size_t)(*$3), (size_t)(*$4), (size_t)(*$5) };
  Napi::Value view_ = tttrlib_js::make_view(env, (void*)(*$1), dims_[0]*dims_[1]*dims_[2]*dims_[3], sizeof(DATA_TYPE), NAPI_TYPE, true, dims_, 4);
  if ($result.IsEmpty() || $result.IsUndefined()) $result = view_; else $result = SWIG_AppendOutput($result, view_);
}

%enddef

// ===========================================================================
// %js_vector_typemaps(DATA_TYPE, NAPI_TYPE, JS_NAME)
//
// std::vector<T> <-> TypedArray.
//
// SWIG's Node-API std_vector.i wraps std::vector as an opaque proxy class with
// .size()/.get()/.set(), the way the Java backend does. Python's std_vector.i
// converts to and from a list instead, which is why `tttr.get_macro_times()`
// reads naturally there and `burst_search()` hands back something indexable.
// Without the typemaps below, every vector-returning method in tttrlib -- the
// six burst searches, the correlator curves, the decay histograms -- would come
// back as a proxy the caller has to loop over element by element, which is both
// unusable and 100x slower than a memcpy.
//
// So: vectors of a numeric type cross as TypedArrays, copied once (a vector
// return is a temporary; there is nothing to make a zero-copy view of). The
// %template(VectorDouble) proxy classes still exist for code that wants them.
// ===========================================================================
%define %js_vector_typemaps(DATA_TYPE, NAPI_TYPE, JS_NAME)

%typemap(out) std::vector<DATA_TYPE> {
  size_t n_ = $1.size();
  size_t dims_[1] = { n_ };
  void *buf_ = std::malloc(n_ * sizeof(DATA_TYPE) + 1);
  if (!buf_) SWIG_exception_fail(SWIG_MemoryError, "out of memory building a " JS_NAME);
  if (n_) std::memcpy(buf_, $1.data(), n_ * sizeof(DATA_TYPE));
  $result = tttrlib_js::make_view(env, buf_, n_, sizeof(DATA_TYPE), NAPI_TYPE, true, dims_, 1);
}
%typemap(out) const std::vector<DATA_TYPE>&, std::vector<DATA_TYPE>& {
  size_t n_ = $1->size();
  size_t dims_[1] = { n_ };
  void *buf_ = std::malloc(n_ * sizeof(DATA_TYPE) + 1);
  if (!buf_) SWIG_exception_fail(SWIG_MemoryError, "out of memory building a " JS_NAME);
  if (n_) std::memcpy(buf_, $1->data(), n_ * sizeof(DATA_TYPE));
  $result = tttrlib_js::make_view(env, buf_, n_, sizeof(DATA_TYPE), NAPI_TYPE, true, dims_, 1);
}

// No typemap LOCALS below, deliberately. SWIG does not emit them for the
// constructors of a %template()-instantiated class -- so `new VectorDouble(x)`
// generated a body referring to locals that were never declared, and the
// wrapper did not compile. `$1` is always a real wrapper variable, so the
// reference form allocates and hands the freearg the pointer to release.
%typemap(in) std::vector<DATA_TYPE> {
  tttrlib_js::NdIn nd_;
  std::string err_;
  if (!tttrlib_js::nd_input<DATA_TYPE>($input, NAPI_TYPE, 1, &nd_, &err_)) {
    SWIG_exception_fail(SWIG_TypeError,
      ("in method '$symname', argument $argnum: expected a " JS_NAME " -- " + err_).c_str());
  }
  DATA_TYPE *p_ = static_cast<DATA_TYPE*>(nd_.data);
  $1.assign(p_, p_ + nd_.dims[0]);
  if (nd_.owned) std::free(nd_.data);
}
%typemap(in) const std::vector<DATA_TYPE>&, std::vector<DATA_TYPE>& {
  tttrlib_js::NdIn nd_;
  std::string err_;
  if (!tttrlib_js::nd_input<DATA_TYPE>($input, NAPI_TYPE, 1, &nd_, &err_)) {
    SWIG_exception_fail(SWIG_TypeError,
      ("in method '$symname', argument $argnum: expected a " JS_NAME " -- " + err_).c_str());
  }
  DATA_TYPE *p_ = static_cast<DATA_TYPE*>(nd_.data);
  $1 = new std::vector<DATA_TYPE>(p_, p_ + nd_.dims[0]);
  if (nd_.owned) std::free(nd_.data);
}
%typemap(freearg) const std::vector<DATA_TYPE>&, std::vector<DATA_TYPE>& {
  delete $1;
}
// The POINTER form is what SWIG's generated member-variable setter takes: for a
// data member of class type it emits `SimSpecies_q_set(SimSpecies *, vector<double> *)`
// and assigns `*arg2`. Without this, `species.q = new Float64Array(...)` fails
// with "argument 2 of type 'std::vector< double > *'" -- which is every
// vector-valued field in the simulator.
%typemap(in) const std::vector<DATA_TYPE>*, std::vector<DATA_TYPE>* {
  tttrlib_js::NdIn nd_;
  std::string err_;
  if (!tttrlib_js::nd_input<DATA_TYPE>($input, NAPI_TYPE, 1, &nd_, &err_)) {
    SWIG_exception_fail(SWIG_TypeError,
      ("in method '$symname', argument $argnum: expected a " JS_NAME " -- " + err_).c_str());
  }
  DATA_TYPE *p_ = static_cast<DATA_TYPE*>(nd_.data);
  $1 = new std::vector<DATA_TYPE>(p_, p_ + nd_.dims[0]);
  if (nd_.owned) std::free(nd_.data);
}
%typemap(freearg) const std::vector<DATA_TYPE>*, std::vector<DATA_TYPE>* {
  delete $1;
}
// The matching getter returns `std::vector<DATA_TYPE> *` pointing INTO the
// object, so it must be read, never freed or adopted.
%typemap(out) const std::vector<DATA_TYPE>*, std::vector<DATA_TYPE>* {
  if (!$1) { $result = env.Null(); }
  else {
    size_t n_ = $1->size();
    size_t dims_[1] = { n_ };
    void *buf_ = std::malloc(n_ * sizeof(DATA_TYPE) + 1);
    if (!buf_) SWIG_exception_fail(SWIG_MemoryError, "out of memory building a " JS_NAME);
    if (n_) std::memcpy(buf_, $1->data(), n_ * sizeof(DATA_TYPE));
    $result = tttrlib_js::make_view(env, buf_, n_, sizeof(DATA_TYPE), NAPI_TYPE, true, dims_, 1);
  }
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_DOUBLE_ARRAY)
             const std::vector<DATA_TYPE>*, std::vector<DATA_TYPE>* {
  $1 = tttrlib_js::could_be_array($input, NAPI_TYPE) ? 1 : 0;
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_DOUBLE_ARRAY)
             std::vector<DATA_TYPE>, const std::vector<DATA_TYPE>&, std::vector<DATA_TYPE>& {
  $1 = tttrlib_js::could_be_array($input, NAPI_TYPE) ? 1 : 0;
}

// Nested: std::vector<std::vector<T>> -> Array of TypedArrays (Python gives a
// list of lists; an Array of TypedArrays is the same shape in JavaScript).
%typemap(out) std::vector<std::vector<DATA_TYPE> > {
  Napi::Array outer_ = Napi::Array::New(env, $1.size());
  for (size_t i_ = 0; i_ < $1.size(); i_++) {
    size_t n_ = $1[i_].size();
    size_t dims_[1] = { n_ };
    void *buf_ = std::malloc(n_ * sizeof(DATA_TYPE) + 1);
    if (!buf_) SWIG_exception_fail(SWIG_MemoryError, "out of memory building a " JS_NAME);
    if (n_) std::memcpy(buf_, $1[i_].data(), n_ * sizeof(DATA_TYPE));
    outer_.Set(static_cast<uint32_t>(i_),
               tttrlib_js::make_view(env, buf_, n_, sizeof(DATA_TYPE), NAPI_TYPE, true, dims_, 1));
  }
  $result = outer_;
}

%enddef

// ===========================================================================
// Concrete instantiations -- every element type the interface actually uses.
// ===========================================================================
%js_numpy_typemaps(double,             napi_float64_array,   "Float64Array")
%js_numpy_typemaps(float,              napi_float32_array,   "Float32Array")
%js_numpy_typemaps(char,               napi_int8_array,      "Int8Array")
%js_numpy_typemaps(signed char,        napi_int8_array,      "Int8Array")
%js_numpy_typemaps(unsigned char,      napi_uint8_array,     "Uint8Array")
%js_numpy_typemaps(short,              napi_int16_array,     "Int16Array")
%js_numpy_typemaps(unsigned short,     napi_uint16_array,    "Uint16Array")
%js_numpy_typemaps(int,                napi_int32_array,     "Int32Array")
%js_numpy_typemaps(unsigned int,       napi_uint32_array,    "Uint32Array")
%js_numpy_typemaps(long long,          napi_bigint64_array,  "BigInt64Array")
%js_numpy_typemaps(unsigned long long, napi_biguint64_array, "BigUint64Array")
// LP64 Linux: long/unsigned long are 64-bit and distinct from long long.
%js_numpy_typemaps(long,               napi_bigint64_array,  "BigInt64Array")
%js_numpy_typemaps(unsigned long,      napi_biguint64_array, "BigUint64Array")
// bool travels as Uint8Array: JavaScript has no boolean TypedArray, and
// sizeof(bool) == 1 on every platform tttrlib builds for. Values are 0 / 1.
%js_numpy_typemaps(bool,               napi_uint8_array,     "Uint8Array")

// std::vector<T> conversions. `long`/`unsigned long` appear in the API on top
// of the fixed-width types (std::vector<unsigned long> is instantiated in
// misc_types.i), and on LP64 they are a distinct C++ type from `long long`.
%js_vector_typemaps(double,             napi_float64_array,   "Float64Array")
%js_vector_typemaps(float,              napi_float32_array,   "Float32Array")
%js_vector_typemaps(signed char,        napi_int8_array,      "Int8Array")
%js_vector_typemaps(unsigned char,      napi_uint8_array,     "Uint8Array")
%js_vector_typemaps(short,              napi_int16_array,     "Int16Array")
%js_vector_typemaps(unsigned short,     napi_uint16_array,    "Uint16Array")
%js_vector_typemaps(int,                napi_int32_array,     "Int32Array")
%js_vector_typemaps(unsigned int,       napi_uint32_array,    "Uint32Array")
%js_vector_typemaps(long long,          napi_bigint64_array,  "BigInt64Array")
%js_vector_typemaps(unsigned long long, napi_biguint64_array, "BigUint64Array")
// LP64 Linux: long/unsigned long are 64-bit and distinct from long long.
%js_vector_typemaps(long,               napi_bigint64_array,  "BigInt64Array")
%js_vector_typemaps(unsigned long,      napi_biguint64_array, "BigUint64Array")

// ===========================================================================
// std::map<K, V> -> a plain JavaScript object
//
// Python's std_map.i turns a map into a dict; the Node-API one wraps it as an
// opaque proxy with .get()/.set()/.size(), the way the Java backend does. So
// `extractor.get_burst_channel_photons()` came back as a handle a caller had to
// iterate by hand, where Python hands over a dict -- the one marshalling gap
// left after the vector conversions.
//
// Keys become strings, because JavaScript object keys are strings and because
// that is the shape either binding's JSON already produces. Values go through
// the js_from() overloads above, so a map of vectors yields TypedArrays.
//
// OUT only: no wrapped API takes a map as a parameter.
// ===========================================================================
%define %js_map_out(MAPTYPE...)
%typemap(out) MAPTYPE {
  Napi::Object obj_ = Napi::Object::New(env);
  // A by-value class return arrives as SwigValueWrapper<T>, which has no
  // begin()/end() but does convert to T&. Bind through that conversion before
  // iterating, or the wrapper does not compile.
  const $1_ltype& map_ = $1;
  for (const auto& kv_ : map_)
    obj_.Set(tttrlib_js::js_key(kv_.first), tttrlib_js::js_from(env, kv_.second));
  $result = obj_;
}
%typemap(out) const MAPTYPE&, MAPTYPE& {
  Napi::Object obj_ = Napi::Object::New(env);
  for (const auto& kv_ : *$1)
    obj_.Set(tttrlib_js::js_key(kv_.first), tttrlib_js::js_from(env, kv_.second));
  $result = obj_;
}
%enddef

// Every map the wrapped API returns. int64_t is spelled both ways because it is
// `long` on LP64 Linux and `long long` elsewhere, and SWIG matches on the
// written type, not the resolved one.
%js_map_out(std::map<std::string, int>)
%js_map_out(std::map<std::string, std::string>)
%js_map_out(std::map<std::string, double>)
%js_map_out(std::map<std::string, std::vector<double> >)
%js_map_out(std::map<std::string, std::vector<float> >)
%js_map_out(std::map<std::string, std::vector<int> >)
%js_map_out(std::map<std::string, std::vector<unsigned char> >)
%js_map_out(std::map<std::string, std::vector<long long> >)
%js_map_out(std::map<std::string, std::vector<long> >)
%js_map_out(std::map<std::string, std::vector<std::vector<long long> > >)
%js_map_out(std::map<std::string, std::vector<std::vector<long> > >)
%js_map_out(std::map<std::string, std::vector<std::string> >)
%js_map_out(std::map<int, int>)
%js_map_out(std::map<int, std::vector<float> >)
%js_map_out(std::map<int, std::vector<double> >)
%js_map_out(std::map<short, std::vector<double> >)
%js_map_out(std::map<signed char, int>)

// std::vector<std::string> -> Array of strings (Python gives a list of str).
%typemap(out) std::vector<std::string> {
  Napi::Array a_ = Napi::Array::New(env, $1.size());
  for (size_t i_ = 0; i_ < $1.size(); i_++)
    a_.Set(static_cast<uint32_t>(i_), Napi::String::New(env, $1[i_]));
  $result = a_;
}
%typemap(out) const std::vector<std::string>&, std::vector<std::string>& {
  Napi::Array a_ = Napi::Array::New(env, $1->size());
  for (size_t i_ = 0; i_ < $1->size(); i_++)
    a_.Set(static_cast<uint32_t>(i_), Napi::String::New(env, (*$1)[i_]));
  $result = a_;
}
%typemap(in) std::vector<std::string> {
  if (!$input.IsArray()) SWIG_exception_fail(SWIG_TypeError,
      "in method '$symname', argument $argnum: expected an Array of strings");
  Napi::Array a_ = $input.As<Napi::Array>();
  for (uint32_t i_ = 0; i_ < a_.Length(); i_++)
    $1.push_back(a_.Get(i_).ToString().Utf8Value());
}
%typemap(in) const std::vector<std::string>&, std::vector<std::string>& {
  if (!$input.IsArray()) SWIG_exception_fail(SWIG_TypeError,
      "in method '$symname', argument $argnum: expected an Array of strings");
  Napi::Array a_ = $input.As<Napi::Array>();
  $1 = new std::vector<std::string>();
  for (uint32_t i_ = 0; i_ < a_.Length(); i_++)
    $1->push_back(a_.Get(i_).ToString().Utf8Value());
}
%typemap(freearg) const std::vector<std::string>&, std::vector<std::string>& {
  delete $1;
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_STRING_ARRAY)
             std::vector<std::string>, const std::vector<std::string>&, std::vector<std::string>& {
  $1 = $input.IsArray() ? 1 : 0;
}
