// SPDX-License-Identifier: BSD-3-Clause
//
// ptolib -- the PTO container and the DataStore -- wrapped ONCE for every
// binding. ptolib is one header (thirdparty/ptolib/ptolib.h), and SWIG wraps a
// header at the first %include and never again; so every typemap that used to
// sit in DataStore.i, StoreFile.i and Pto.i in front of "its" header has to
// sit here, in front of the one header that now holds all three. Those three
// files %include this one first (SWIG includes a file once, so the second and
// third are no-ops) and then wrap what is left in their tttrlib headers.
//
// Python names are unchanged: SWIG flattens namespaces, so pto::DataStore is
// tttrlib.DataStore exactly as tttrlib::data::DataStore was, and the tttrlib
// headers re-export the names under the old namespaces for C++.
%module tttrlib
%{
#include "ptolib/ptolib.h"
%}

%include "std_string.i"
%include "std_vector.i"

#ifdef TTTRLIB_CORE_IS_IMPORTED
// A split module that %imports core sees the types without wrapping them again.
%import "ptolib/ptolib.h"
#else

// ── DataStore ───────────────────────────────────────────────────────────────
// Distinctive parameter names throughout: %apply is global and keyed by name,
// so a generic (double* v, int n) here would redefine that pair for every other
// interface file in the project.
%apply (double* IN_ARRAY1, int DIM1)    { (const double* v, int n) }
%apply (float* IN_ARRAY1, int DIM1)     { (const float* v, int n) }
%apply (long long* IN_ARRAY1, int DIM1) { (const long long* v, int n) }
%apply (int* IN_ARRAY1, int DIM1)       { (const int* v, int n) }
%apply (short* IN_ARRAY1, int DIM1)     { (const short* v, int n) }
// Polygon vertices and a painted mask, for the drawn regions.
%apply (double* IN_ARRAY1, int DIM1) {
    (const double* xs, int n_xs),
    (const double* ys, int n_ys)
}
// DIM1 is the FIRST dimension of the array, which for a row-major image is the
// row count. The C++ parameters are named in that order for the same reason.
%apply (unsigned char* IN_ARRAY2, int DIM1, int DIM2) {
    (const unsigned char* image, int ny, int nx)
}
%apply (signed char* IN_ARRAY1, int DIM1) { (const signed char* v, int n) }
// DataStore::take_into. Named distinctly, like everything else here: %apply is
// global and keyed by the parameter names, so a generic (int*, int) pair would
// redefine that pair for every other interface file.
%apply (int* IN_ARRAY1, int DIM1) { (const int* take_rows, int n_take_rows) }
%apply (unsigned long long* IN_ARRAY1, int DIM1) { (const unsigned long long* v, int n) }
%apply (unsigned int* IN_ARRAY1, int DIM1) { (const unsigned int* v, int n) }
%apply (unsigned short* IN_ARRAY1, int DIM1) { (const unsigned short* v, int n) }
%apply (unsigned char* IN_ARRAY1, int DIM1) {
    (const unsigned char* v, int n),
    (const unsigned char* m, int n)
}
// BitMask::to_bytes writes into the caller's array.
%apply (unsigned char* INPLACE_ARRAY1, int DIM1) {
    (unsigned char* out_bytes, int n_out)
}

// Zero-copy views into the column buffers. Non-owning, like the histogram's --
// the Python layer attaches the owner so they cannot dangle.
%apply (double** ARGOUTVIEW_ARRAY1, int* DIM1)    { (double** view, int* n) }
%apply (float** ARGOUTVIEW_ARRAY1, int* DIM1)     { (float** view, int* n) }
%apply (long long** ARGOUTVIEW_ARRAY1, int* DIM1) { (long long** view, int* n) }
%apply (int** ARGOUTVIEW_ARRAY1, int* DIM1)       { (int** view, int* n) }
%apply (short** ARGOUTVIEW_ARRAY1, int* DIM1)     { (short** view, int* n) }
%apply (signed char** ARGOUTVIEW_ARRAY1, int* DIM1) { (signed char** view, int* n) }
%apply (unsigned long long** ARGOUTVIEW_ARRAY1, int* DIM1) { (unsigned long long** view, int* n) }
%apply (unsigned int** ARGOUTVIEW_ARRAY1, int* DIM1) { (unsigned int** view, int* n) }
%apply (unsigned short** ARGOUTVIEW_ARRAY1, int* DIM1) { (unsigned short** view, int* n) }
%apply (unsigned char** ARGOUTVIEW_ARRAY1, int* DIM1) { (unsigned char** view, int* n) }

// How a description is STORED, which no caller has any reason to see: the API
// face is Column::metadata(), a JSON string that crosses every binding with no
// typemap. Wrapping these would put a raw byte pointer and an opaque
// vector<unsigned char> on the module for no gain.
%ignore pto::metadata_to_msgpack;
%ignore pto::metadata_from_msgpack;

// %extend must come BEFORE the header it extends...
// %pythoncode is a Python-only directive. The other bindings (R, Java) never
// reached it because they wrap a subset; the JavaScript module wraps the whole
// Python surface, so every such block now needs the guard. The JavaScript
// equivalents of these conveniences live in ext/js/pkg/index.js.
#ifdef SWIGPYTHON
%extend pto::Column { %pythoncode "./ext/python/Column.py" }
#endif  // SWIGPYTHON
// Python-only; see the note above.
#ifdef SWIGPYTHON
%extend pto::DataStore { %pythoncode "./ext/python/DataStore.py" }

// A group proxy is a BORROWED reference into the ROOT's tree. The root owns
// every child, so the proxy has to hold the ROOT -- not the immediate parent --
// or a zero-copy view reached through a nested group points at freed memory the
// moment the root is collected. Same chain and same reason as Column._store;
// see DataStore.py::__getitem__.
//
// `getattr(self, "_store", None) or self` reaches the root in one step: a root
// has no _store and yields itself, a group proxy already carries the root. That
// flattens the chain rather than lengthening it.
%define TTTRLIB_DS_KEEP_ROOT(Method)
%feature("pythonappend") Method %{
    val._store = getattr(self, "_store", None) or self
%}
%enddef
TTTRLIB_DS_KEEP_ROOT(pto::DataStore::group)
TTTRLIB_DS_KEEP_ROOT(pto::DataStore::add_group)
TTTRLIB_DS_KEEP_ROOT(pto::DataStore::ensure_group)

// A Column is borrowed too, and every zero-copy array is reached through one,
// so the same rule has to hold for EVERY way of getting a column -- not just
// for the two that go through DataStore.py. `store.column(0).numpy()` and
// `store.column_by_name("x").numpy()` handed back arrays that outlived the
// store and read reused memory: a 1000-row CSV column came
// back with row 2 as 6.001000000000001e-05 instead of 6.0, and a burst table
// read 84 of 154 rows of `First Photon` as 3.3e-319.
TTTRLIB_DS_KEEP_ROOT(pto::DataStore::column)
TTTRLIB_DS_KEEP_ROOT(pto::DataStore::column_by_name)

// A SWIG VectorString is not a list and has no __eq__, so group_names() ==
// ['a', 'b'] would be False however right the answer was. Everything else on
// this surface hands back a real list -- `names` builds one by hand at
// DataStore.py -- and these should not be the exception.
%feature("pythonappend") pto::DataStore::group_names %{
    val = list(val)
%}
%feature("pythonappend") pto::DataStore::group_paths %{
    val = list(val)
%}
%feature("pythonappend") pto::DataStore::column_names %{
    val = list(val)
%}
#endif  // SWIGPYTHON

// The same keep-alive problem, in R, and it is not theoretical: a group taken
// from a store that then goes out of scope reads freed memory after the next
// gc() -- n_rows() answers with a stale number and column_by_name() says the
// column is not there.
//
// R has no __dict__ to hang the owner on, so the root rides along as an
// attribute of the returned S4 object. The receiver has to be captured BEFORE
// SWIG's default scoercein replaces it with its `ref` slot, which is why the
// input side is overridden too -- by the time the output typemap runs, `self`
// is a bare externalptr that has lost the attribute and the chain would break
// at the first nested group.
//
// Reading the root off the receiver rather than the receiver itself is what
// flattens the chain: a root has no attribute and becomes the root, a proxy
// already carries one and passes it on. Same rule as the Python side above.
#ifdef SWIGR
%typemap(scoercein) pto::DataStore *, pto::DataStore & %{
  .tttrlib_receiver <- $input;
  if (inherits($input, "ExternalReference")) $input = slot($input,"ref");
%}

%typemap(scoerceout) pto::DataStore & %{
  $result <- if (is.null($result)) $result
  else new("$R_class", ref=$result);
  if (!is.null($result) && exists(".tttrlib_receiver", inherits = FALSE)) {
    .tttrlib_root <- attr(.tttrlib_receiver, "tttrlib.root");
    if (is.null(.tttrlib_root)) .tttrlib_root <- .tttrlib_receiver;
    attr($result, "tttrlib.root") <- .tttrlib_root;
  }
%}
#endif  // SWIGR

// And once more for Java, where the hazard is the same and the mechanism is a
// field: the proxy returned for a group holds a raw pointer into the root, and
// nothing else references the root, so the collector is free to run the root's
// finalizer -- which deletes the C++ object the proxy is still pointing at.
//
// Same flattening rule as Python and R: take the receiver's root if it has one,
// otherwise the receiver is the root. The field is package-private rather than
// private so the typemap can assign it from the proxy of another instance;
// SWIG writes this code inside the DataStore class either way.
#ifdef SWIGJAVA
// BitMask(std::size_t, bool) erases to (long, boolean) in Java, which is
// exactly the signature of the protected (long cPtr, boolean cMemOwn)
// constructor SWIG puts on every proxy -- so the generated class will not
// compile. Java therefore does without that overload. Nothing is lost: the
// parameter defaults to true, so BitMask(n) still builds a filled mask, and
// assign(n, false) is the other half.
%ignore pto::BitMask::BitMask(std::size_t, bool);

%typemap(javacode) pto::DataStore %{
  /**
   * The store that owns this one's memory, or null when this IS the root.
   * A group is a borrowed reference into its root's tree; holding the root
   * here is what stops the collector from freeing the tree underneath it.
   */
  DataStore tttrlibRoot;
%}

%typemap(javaout) pto::DataStore & {
    DataStore proxy = new DataStore($jnicall, $owner);
    proxy.tttrlibRoot = (this.tttrlibRoot != null) ? this.tttrlibRoot : this;
    return proxy;
  }
#endif  // SWIGJAVA


// ── .dstore ────────────────────────────────────────────────────────────────
//
// ptolib is spelled in <cstdint> types throughout, and only the Python backend
// resolves `std::uint64_t` on its own. Everywhere else it stays an unknown type
// and every offset, size and UID comes out as an opaque SWIGTYPE proxy -- so a
// Java caller could open a container and then do nothing with what it told
// them. `stdint.i` would fix it and cannot be included here: see the note at
// the top of misc_types.i about int64_t on glibc. This says the one thing that
// is needed instead.
//
// Not for R, which gives std::uint64_t its own typemaps in ext/r/tttrlib.i:
// `unsigned long long` there goes through as.integer(), which is 32-bit and
// silently NA above 2^31, and a %apply here would overwrite the fix with the
// very thing it corrects.
#ifndef SWIGR
%apply unsigned long long { std::uint64_t };
#endif

// A SWIG VectorString is not a list and has no __eq__, so
// store_groups(f) == ['a', 'b'] would be False however right the answer was.
// group_names/group_paths on DataStore get the same treatment above.
#ifdef SWIGPYTHON
%feature("pythonappend") pto::store_groups %{
    val = list(val)
%}
%feature("pythonappend") pto::store_columns %{
    val = list(val)
%}
#endif  // SWIGPYTHON

// ── PTO ────────────────────────────────────────────────────────────────────
// A payload is bytes on both sides. Without these, `add` wants a SWIG pointer
// and `read` hands back an opaque vector proxy -- neither of which is what a
// caller has or wants when the thing in question is a photon stream.
#ifdef SWIGPYTHON
%typemap(in) (const unsigned char* data, std::size_t n) {
    char* buffer = 0;
    Py_ssize_t length = 0;
    if (PyBytes_AsStringAndSize($input, &buffer, &length) < 0) SWIG_fail;
    // $1_ltype rather than the written type: SWIG strips the const from the
    // parameter it declares, so a plain cast to `const unsigned char*` fails.
    $1 = ($1_ltype) buffer;
    $2 = ($2_ltype) length;
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_STRING)
        (const unsigned char* data, std::size_t n) {
    $1 = PyBytes_Check($input) ? 1 : 0;
}
%typemap(out) std::vector<unsigned char> {
    $result = PyBytes_FromStringAndSize(
            $1.empty() ? "" : reinterpret_cast<const char*>($1.data()),
            static_cast<Py_ssize_t>($1.size()));
}
// PtoTag::bytes is a member, and SWIG's generated getter and setter deal in a
// pointer to the member rather than a value, so the pair above is not enough.
//
// Matched on the member's NAME as well as its type. An unqualified
// `std::vector<unsigned char>*` typemap is global from here on and lands on the
// `self` argument of every method of VectorUint8 -- which Sim.i instantiates
// further down tttrlib.i, so `len(sidecar.states)` came back as
// "expected bytes, VectorUint8 found" from a file that has nothing to do with
// PTO. Do not widen these two.
%typemap(out) std::vector<unsigned char> * bytes {
    $result = PyBytes_FromStringAndSize(
            $1->empty() ? "" : reinterpret_cast<const char*>($1->data()),
            static_cast<Py_ssize_t>($1->size()));
}
%typemap(in) std::vector<unsigned char> * bytes (std::vector<unsigned char> tmp) {
    char* buffer = 0;
    Py_ssize_t length = 0;
    if (PyBytes_AsStringAndSize($input, &buffer, &length) < 0) SWIG_fail;
    tmp.assign(buffer, buffer + length);
    $1 = &tmp;
}
#endif  // SWIGPYTHON

// ── A uid crosses to R as a STRING ─────────────────────────────────────────
//
// R has no 64-bit integer type: `integer` is 32-bit and `numeric` is an IEEE
// double, so a uid is stored with 53 bits of mantissa and comes back wrong --
//
//     written   14523661926200792394
//     read back 14523661926200793088     (every 2048th integer, at this size)
//
// and the lookup then fails with "no object with that uid". A 64-bit float is
// the widest numeric R has, and it is still not wide enough; three consecutive
// uids collapse onto the same double.
//
// A character string is the only thing base R holds exactly without a new
// dependency (bit64 would be the other answer). So in R a uid IS a string:
// produced as one, accepted as one, never arithmetic. Every other language
// keeps its integer -- JavaScript gained BigInt scalars, and Python and Java
// were always exact.
//
// Scoped to the IDENTIFIERS only. rows, offset, size and capacity are
// magnitudes that stay far below 2^53, and a double carries them fine; turning
// those into strings would make the API awkward for no gain -- and would break
// the conformance cases, which compare them as numbers.
#ifdef SWIGR
%typemap(out) std::uint64_t pto_add_store, std::uint64_t pto::File::add_file,
              std::uint64_t pto::File::attach,
              std::uint64_t PtoObject::uid, std::uint64_t PtoAnnotation::target,
              std::uint64_t PtoExtent::target %{
  {
    char buf_[24];
    std::snprintf(buf_, sizeof(buf_), "%llu", (unsigned long long) $1);
    $result = Rf_mkString(buf_);
  }
%}

// Accepts the string it produced, and a number too -- a small uid typed by
// hand should not need quoting, and anything a double can hold exactly is
// unambiguous.
%typemap(in) std::uint64_t uid, std::uint64_t primary, std::uint64_t target %{
  if (TYPEOF($input) == STRSXP && Rf_length($input) == 1) {
    $1 = (std::uint64_t) strtoull(CHAR(STRING_ELT($input, 0)), NULL, 10);
  } else {
    $1 = (std::uint64_t) Rf_asReal($input);
  }
%}
%typemap(scoercein) std::uint64_t uid, std::uint64_t primary,
                    std::uint64_t target %{ %}

// The overload dispatcher has to accept the string too, or every candidate
// fails its is.numeric() test and R reports "cannot find overloaded function".
%typemap("rtypecheck") std::uint64_t uid, std::uint64_t primary,
                       std::uint64_t target
  %{ (is.character($arg) || is.numeric($arg)) && length($arg) == 1 %}
#endif  // SWIGR

// The container class is pto::File; tttrlib's PtoFile derives from it and adds
// the photon-aware members (io_pto.h). Both are wrapped -- the base must be, or
// SWIG drops every inherited method from the derived class -- and the base is
// named so it cannot collide with the class callers actually use.
%rename(PtoFileBase) pto::File;

%include "ptolib/ptolib.h"

%template(DataStoreInfoVector) std::vector<pto::DataStoreInfo>;
// The runs a column records as never measured. A vector of a nested struct
// needs the template by name or it crosses as an opaque proxy with no length
// and no iteration.
%template(NaRangeVector) std::vector<pto::NaRange>;
%template(PtoObjectVector) std::vector<pto::PtoObject>;
%template(PtoTagVector) std::vector<pto::PtoTag>;
%template(PtoAnnotationVector) std::vector<pto::PtoAnnotation>;
%template(PtoExtentVector) std::vector<pto::PtoExtent>;
%template(PtoCueVector) std::vector<pto::PtoCue>;
%template(PtoElementVector) std::vector<pto::Element>;
%template(PtoProblemVector) std::vector<pto::Problem>;

#endif  // TTTRLIB_CORE_IS_IMPORTED
