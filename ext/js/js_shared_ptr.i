// SPDX-License-Identifier: BSD-3-Clause
//
// js_shared_ptr.i -- std::shared_ptr support for SWIG's Node-API backend.
//
// SWIG 4.2 ships boost_shared_ptr.i for python/, r/, java/ and a dozen other
// languages, but NOT for javascript/napi/ -- so `%shared_ptr(TTTR)` expands to
// an undefined SWIG_SHARED_PTR_TYPEMAPS and the wrapper never builds. tttrlib
// cannot route around it: shared_ptr<TTTR> is the currency of the API
// (TTTR::select, get_tttr_by_channel, CLSMImage's constructor, Correlator's
// setters), and without these typemaps every one of those is unusable.
//
// ---------------------------------------------------------------------------
// Why this is NOT a transcription of SWIG's own boost_shared_ptr.i
// ---------------------------------------------------------------------------
//
// Every other language's version rests on one assumption: for a class marked
// %feature("smartptr"), the pointer stored in the language's proxy object is a
// `shared_ptr<T>*`, registered under the shared_ptr's own swig_type_info. All
// its typemaps convert through `$descriptor(shared_ptr<T>*)`.
//
// The Node-API backend cannot satisfy that. Its generated ObjectWrap constructor
// is fixed, and it does two things this file has to live with:
//
//     this->info = SWIGTYPE_p_TTTR;        // ALWAYS the raw class type
//     ...
//     this->self = result;                 // ALWAYS the raw `new TTTR(...)`
//
// So a `new tttrlib.TTTR(path)` in JavaScript stores a raw `TTTR*` under the
// raw type, and any typemap asking `SWIG_ConvertPtr(this, …, shared_ptr<TTTR>*)`
// fails on every single method call. (It did: "in method 'TTTR_get_macro_times',
// argument 1 of type 'TTTR *'", on everything.) A transcription of the upstream
// file compiles perfectly and is useless at run time.
//
// This file therefore inverts the assumption: **the wrapped pointer is always
// the raw `T*`**, matching what the backend's constructor produces, and the
// C++-side reference is kept somewhere else.
//
// ---------------------------------------------------------------------------
// The holder table
// ---------------------------------------------------------------------------
//
// When C++ returns a `shared_ptr<T>`, something must keep that reference alive
// or the object dies the moment the temporary does -- `TTTR::select()` returns a
// shared_ptr whose only reference IS the return value. Since the proxy can only
// store a raw pointer, the reference lives in a process-wide table keyed by that
// raw pointer, refcounted because two JavaScript objects can wrap the same C++
// object. The proxy's destructor (%feature("unref")) drops its entry.
//
// The table also disambiguates destruction, which is the other thing it buys:
// a pointer IN the table came from a shared_ptr and must be released, not
// deleted; a pointer NOT in it came from `new` in a JavaScript constructor and
// must be deleted. One lookup answers both.
//
// ---------------------------------------------------------------------------
// The limitation, stated plainly
// ---------------------------------------------------------------------------
//
// An object constructed in JavaScript (`new tttrlib.TTTR(path)`) and then handed
// to C++ code that stores a shared_ptr (`new CLSMImage(tttr)`) is passed as a
// shared_ptr with a NULL DELETER. C++ holding it does not extend its life: if
// the JavaScript object is garbage-collected first, the C++ side is left with a
// dangling pointer. Keep the JavaScript variable alive for as long as anything
// C++-side refers to it.
//
// This is the same contract the R and Java bindings live under (neither has any
// shared_ptr support, so both pass raw pointers), and the same one the
// ARGOUTVIEW array views carry. Lifting it would need the proxy to hold a
// napi_ref back-reference, which a typemap cannot reach.

%{
#include <map>
#include <memory>
#include <mutex>

namespace tttrlib_js {

// One entry per distinct C++ object that reached JavaScript through a
// shared_ptr. `n` counts the JavaScript proxies pointing at it -- SWIG hands out
// a fresh proxy per call, so the same object can be wrapped several times.
struct SpHolder {
  std::shared_ptr<void> sp;
  long n = 0;
};

inline std::map<void *, SpHolder> &sp_table() {
  static std::map<void *, SpHolder> t;
  return t;
}
// Node-API worker threads each get their own env but share one process address
// space, and this table is process-wide, so it is guarded.
inline std::mutex &sp_mutex() {
  static std::mutex m;
  return m;
}

/// Keep a C++ reference to `sp` alive on behalf of a new JavaScript proxy.
template <class T>
inline void sp_retain(const std::shared_ptr<T> &sp) {
  if (!sp) return;
  std::lock_guard<std::mutex> guard(sp_mutex());
  SpHolder &e = sp_table()[static_cast<void *>(sp.get())];
  if (e.n++ == 0) e.sp = std::static_pointer_cast<void>(sp);
}

/// Recover the shared_ptr for a raw pointer that came from C++, if there is one.
template <class T>
inline std::shared_ptr<T> sp_lookup(T *p) {
  if (!p) return std::shared_ptr<T>();
  std::lock_guard<std::mutex> guard(sp_mutex());
  auto it = sp_table().find(static_cast<void *>(p));
  if (it == sp_table().end()) return std::shared_ptr<T>();
  return std::static_pointer_cast<T>(it->second.sp);
}

/// Drop one proxy's reference. Returns false when the pointer was never in the
/// table -- meaning it came from `new` in a JavaScript constructor and the
/// caller must delete it instead.
inline bool sp_release(void *p) {
  if (!p) return true;
  std::lock_guard<std::mutex> guard(sp_mutex());
  auto it = sp_table().find(p);
  if (it == sp_table().end()) return false;
  if (--it->second.n <= 0) sp_table().erase(it);
  return true;
}

}  // namespace tttrlib_js
%}

// The type must be known to SWIG's parser so `std::shared_ptr<X>` in a header
// resolves, but it is never wrapped as a class -- it is converted away by the
// typemaps below.
namespace std {
  template <class T> class shared_ptr {};
}

// ===========================================================================
// %shared_ptr(TYPE) -- the user-facing macro, redefined for this backend.
//
// Deliberately WITHOUT %feature("smartptr"). That feature is what tells SWIG to
// store a shared_ptr<T>* as the proxy's pointer, which this backend's generated
// constructor cannot do (see the header comment). Setting it produces a wrapper
// that compiles and then fails on every method call.
// ===========================================================================
%define %shared_ptr(TYPE...)

// Destruction. A pointer in the holder table came from a shared_ptr and is
// released; one that is not came from `new` in a JavaScript constructor and is
// deleted. SWIG's other backends can write a plain `delete smartarg1;` because
// their proxy always holds a shared_ptr; here the two cases are genuinely
// different objects and must be told apart at run time.
%feature("unref") TYPE "if (!tttrlib_js::sp_release(arg1)) delete reinterpret_cast< TYPE * >(arg1);"

// --- shared_ptr<TYPE> as an argument ---------------------------------------
// The proxy holds a raw TYPE*, so convert through the RAW descriptor and rebuild
// a shared_ptr around it: the real one when the object came from C++, otherwise
// a non-owning alias (see the limitation in the header comment).
// No typemap LOCALS below. SWIG does not emit them for the methods of a class
// the Node-API backend generates as a template (which is every wrapped class),
// so a local named in the typemap body is simply undeclared in the wrapper. The
// by-value form declares its variables inside its own braces; the reference form
// allocates and lets freearg release, because `$1` must outlive the block.
%typemap(in) std::shared_ptr< TYPE >, std::shared_ptr< const TYPE > {
  void *argp_ = 0;
  int res_ = SWIG_ConvertPtr($input, &argp_, $descriptor(TYPE *), %convertptr_flags);
  if (!SWIG_IsOK(res_)) {
    %argument_fail(res_, "$type", $symname, $argnum);
  }
  TYPE *raw_ = %reinterpret_cast(argp_, TYPE *);
  $1 = tttrlib_js::sp_lookup(raw_);
  if (!$1 && raw_) $1 = std::shared_ptr< TYPE >(raw_, [](TYPE *) {});
}
%typemap(in) std::shared_ptr< TYPE > &, std::shared_ptr< const TYPE > & {
  void *argp_ = 0;
  int res_ = SWIG_ConvertPtr($input, &argp_, $descriptor(TYPE *), %convertptr_flags);
  if (!SWIG_IsOK(res_)) {
    %argument_fail(res_, "$type", $symname, $argnum);
  }
  TYPE *raw_ = %reinterpret_cast(argp_, TYPE *);
  std::shared_ptr< TYPE > sp_ = tttrlib_js::sp_lookup(raw_);
  if (!sp_ && raw_) sp_ = std::shared_ptr< TYPE >(raw_, [](TYPE *) {});
  $1 = new std::shared_ptr< TYPE >(sp_);
}
%typemap(freearg) std::shared_ptr< TYPE > &, std::shared_ptr< const TYPE > & {
  delete $1;
}

// --- shared_ptr<TYPE> as a result ------------------------------------------
// Retain a reference on the JavaScript proxy's behalf, then hand out the raw
// pointer -- which is the only thing this backend's proxy can hold.
// SWIG_POINTER_OWN makes the proxy's destructor run the unref above.
%typemap(out) std::shared_ptr< TYPE >, std::shared_ptr< const TYPE > {
  tttrlib_js::sp_retain($1);
  %set_output(SWIG_NewPointerObj(%as_voidptr(%const_cast($1.get(), TYPE *)),
                                 $descriptor(TYPE *), SWIG_POINTER_OWN));
}
%typemap(out) std::shared_ptr< TYPE > &, std::shared_ptr< const TYPE > & {
  tttrlib_js::sp_retain(*$1);
  %set_output(SWIG_NewPointerObj(%as_voidptr(%const_cast($1->get(), TYPE *)),
                                 $descriptor(TYPE *), SWIG_POINTER_OWN));
}
%typemap(out) std::shared_ptr< TYPE > *, std::shared_ptr< const TYPE > * {
  if ($1) tttrlib_js::sp_retain(*$1);
  %set_output(SWIG_NewPointerObj($1 ? %as_voidptr(%const_cast($1->get(), TYPE *)) : 0,
                                 $descriptor(TYPE *), SWIG_POINTER_OWN));
}

// --- shared_ptr<TYPE> as a data member --------------------------------------
// Member accessors go through varin/varout, not in/out. std::pair<shared_ptr<TTTR>,
// shared_ptr<TTTR>> (Correlator::get_tttr) reaches its elements this way, and
// without these the members come back as opaque handles.
%typemap(varin) std::shared_ptr< TYPE >, std::shared_ptr< const TYPE > {
  void *argp_ = 0;
  int res_ = SWIG_ConvertPtr($input, &argp_, $descriptor(TYPE *), %convertptr_flags);
  if (!SWIG_IsOK(res_)) {
    %variable_fail(res_, "$type", "$name");
  }
  TYPE *raw_ = %reinterpret_cast(argp_, TYPE *);
  std::shared_ptr< TYPE > sp_ = tttrlib_js::sp_lookup(raw_);
  if (!sp_ && raw_) sp_ = std::shared_ptr< TYPE >(raw_, [](TYPE *) {});
  $1 = sp_;
}
%typemap(varout) std::shared_ptr< TYPE >, std::shared_ptr< const TYPE > {
  tttrlib_js::sp_retain($1);
  %set_varoutput(SWIG_NewPointerObj(%as_voidptr(%const_cast($1.get(), TYPE *)),
                                    $descriptor(TYPE *), SWIG_POINTER_OWN));
}

// --- overload resolution ----------------------------------------------------
// A shared_ptr argument accepts exactly what a TYPE* argument accepts, so it
// must score the same, or an overload set mixing the two dispatches by accident.
%typemap(typecheck, precedence=SWIG_TYPECHECK_POINTER, noblock=1)
         std::shared_ptr< TYPE >, std::shared_ptr< const TYPE >,
         std::shared_ptr< TYPE > &, std::shared_ptr< const TYPE > & {
  void *vptr_ = 0;
  int res_ = SWIG_ConvertPtr($input, &vptr_, $descriptor(TYPE *), 0);
  $1 = SWIG_CheckState(res_);
}

// Never wrap shared_ptr<TYPE> as a class of its own.
%template() std::shared_ptr< TYPE >;

%enddef
