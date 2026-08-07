// SPDX-License-Identifier: BSD-3-Clause
%module tttrlib
%{
#include "DataStore.h"
%}

%include "std_string.i"
%include "std_vector.i"

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
%ignore tttrlib::data::metadata_to_msgpack;
%ignore tttrlib::data::metadata_from_msgpack;

// %extend must come BEFORE the header it extends...
// %pythoncode is a Python-only directive. The other bindings (R, Java) never
// reached it because they wrap a subset; the JavaScript module wraps the whole
// Python surface, so every such block now needs the guard. The JavaScript
// equivalents of these conveniences live in ext/js/pkg/index.js.
#ifdef SWIGPYTHON
%extend tttrlib::data::Column { %pythoncode "./ext/python/Column.py" }
#endif  // SWIGPYTHON
// Python-only; see the note above.
#ifdef SWIGPYTHON
%extend tttrlib::data::DataStore { %pythoncode "./ext/python/DataStore.py" }

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
TTTRLIB_DS_KEEP_ROOT(tttrlib::data::DataStore::group)
TTTRLIB_DS_KEEP_ROOT(tttrlib::data::DataStore::add_group)
TTTRLIB_DS_KEEP_ROOT(tttrlib::data::DataStore::ensure_group)

// A Column is borrowed too, and every zero-copy array is reached through one,
// so the same rule has to hold for EVERY way of getting a column -- not just
// for the two that go through DataStore.py. `store.column(0).numpy()` and
// `store.column_by_name("x").numpy()` handed back arrays that outlived the
// store and read reused memory: see BUGS.md, where a 1000-row CSV column came
// back with row 2 as 6.001000000000001e-05 instead of 6.0, and a burst table
// read 84 of 154 rows of `First Photon` as 3.3e-319.
TTTRLIB_DS_KEEP_ROOT(tttrlib::data::DataStore::column)
TTTRLIB_DS_KEEP_ROOT(tttrlib::data::DataStore::column_by_name)

// A SWIG VectorString is not a list and has no __eq__, so group_names() ==
// ['a', 'b'] would be False however right the answer was. Everything else on
// this surface hands back a real list -- `names` builds one by hand at
// DataStore.py -- and these should not be the exception.
%feature("pythonappend") tttrlib::data::DataStore::group_names %{
    val = list(val)
%}
%feature("pythonappend") tttrlib::data::DataStore::group_paths %{
    val = list(val)
%}
%feature("pythonappend") tttrlib::data::DataStore::column_names %{
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
%typemap(scoercein) tttrlib::data::DataStore *, tttrlib::data::DataStore & %{
  .tttrlib_receiver <- $input;
  if (inherits($input, "ExternalReference")) $input = slot($input,"ref");
%}

%typemap(scoerceout) tttrlib::data::DataStore & %{
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
%ignore tttrlib::data::BitMask::BitMask(std::size_t, bool);

%typemap(javacode) tttrlib::data::DataStore %{
  /**
   * The store that owns this one's memory, or null when this IS the root.
   * A group is a borrowed reference into its root's tree; holding the root
   * here is what stops the collector from freeing the tree underneath it.
   */
  DataStore tttrlibRoot;
%}

%typemap(javaout) tttrlib::data::DataStore & {
    DataStore proxy = new DataStore($jnicall, $owner);
    proxy.tttrlibRoot = (this.tttrlibRoot != null) ? this.tttrlibRoot : this;
    return proxy;
  }
#endif  // SWIGJAVA

%include "DataStore.h"

%template(DataStoreInfoVector) std::vector<tttrlib::data::DataStoreInfo>;

// ...and the module-level support AFTER it, because it names the generated
// enum constants at import time and they do not exist until the header has
// been wrapped. (The methods above only name them when called, so their order
// does not matter.)
// Python-only; see the note above.
#ifdef SWIGPYTHON
%pythoncode "./ext/python/datastore_support.py"
#endif  // SWIGPYTHON

// std::vector<int> and std::vector<std::string> are already templated in
// misc_types.i as VectorInt32 / VectorString. Declaring them again is silently
// skipped by SWIG, so the second name never exists -- use the first ones.

%clear (const double* v, int n);
%clear (const float* v, int n);
%clear (const long long* v, int n);
%clear (const int* v, int n);
%clear (const unsigned char* v, int n);
%clear (const unsigned char* m, int n);
%clear (unsigned char* out_bytes, int n_out);
%clear (double** view, int* n);
%clear (float** view, int* n);
%clear (long long** view, int* n);
%clear (int** view, int* n);
%clear (short** view, int* n);
%clear (signed char** view, int* n);
%clear (unsigned long long** view, int* n);
%clear (unsigned int** view, int* n);
%clear (unsigned short** view, int* n);
%clear (unsigned char** view, int* n);
