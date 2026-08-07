---
type: Reference
title: SWIG Node-API backend traps
description: Six behaviours of SWIG's -napi backend that fail silently or point the error somewhere else entirely, plus one testing lesson.
tags: [swig, node-api, javascript, debugging, gotchas]
status: stable
generated: { by: "claude-code/claude-opus-5", at: 2026-08-06T07:20:00Z }
sources:
  - id: swiglib
    resource: /Users/tpeulen/mambaforge/share/swig/4.2.1/javascript/napi
    title: SWIG 4.2.1 javascript/napi library (javascriptcode.swg, javascriptrun.swg)
  - id: session
    resource: ext/js/jsarrays.i
    title: Behaviour observed while writing ext/js/jsarrays.i and js_shared_ptr.i
    author: claude-code/claude-opus-5
    last_modified: 2026-08-06
---

# Why this exists

Each of the following cost real time during the port, and none of them announces
itself. Check this list before debugging a new symptom in `ext/js/*.i`.[^session]

# 1. Typemap locals are not emitted for template-generated classes

The Node-API backend generates **every** wrapped class as a C++ template
(`_exports_TTTR_templ<SWIG_OBJ_WRAP>`). Typemap locals declared for the methods
and constructors of such a class are silently not emitted, so the body refers to
variables that were never declared.

* **Symptom**: `use of undeclared identifier 'nd_'` in the generated wrapper.
* **Fix**: declare variables inside the typemap's own braces, or allocate with
  `new` and release in `freearg` — `$1` is always a real wrapper variable.

# 2. A multi-pattern typemap attaches locals to the LAST pattern only

```swig
%typemap(in, numinputs=0) (T** ARGOUTVIEW_ARRAY1, int* DIM1),
                          (T** ARGOUTVIEWM_ARRAY1, int* DIM1)
             (T* data_temp = 0, int dim_temp1 = 0) { ... }
```

The locals reach `ARGOUTVIEWM` only. Every `ARGOUTVIEW` wrapper referred to an
undeclared `data_temp`.

* **Fix**: one `%typemap` per pattern, even at the cost of duplication.

# 3. The preprocessor reads backticks and `%enddef` inside comments

Within a `%define` body, SWIG's preprocessor interprets:

* a **backtick**, its argument-quoting operator, and
* the macro-end directive **spelled out**, even in a `//` comment.

Either truncates the macro silently.

* **Symptom**: `Missing %enddef for macro starting here`, pointing hundreds of
  lines away at the macro's opening — nowhere near the actual comment.
* **Fix**: keep both out of comments inside macro bodies. Say "the macro-end
  directive", not the token.

# 4. `SWIG_exception_fail` must not appear in an `%extend` body

The raise macro expands to language-specific wrapper code. The Node-API spelling
needs the `env` that exists only inside the generated wrapper; an `%extend` body
is a free-standing `SWIGINTERN` function compiled before it.

* **Symptom**: `use of undeclared identifier 'env'`.
* **Fix**: throw, and translate in an `%exception` — which *is* wrapper code.
  This is portable and preserves the other languages' exception types.

# 5. `%feature("unref")` gets `arg1`, not `smartarg1`

Every other language's destructor wrapper declares a `smartarg1` local, so
upstream's shared_ptr code writes `delete smartarg1;`. The Node-API destructor
template (`js_dtoroverride`) declares only `arg1`, `reinterpret_cast` to `TYPE*`.

* **Fix**: write the unref action against `arg1`.

# 6. A by-value class return arrives as `SwigValueWrapper<T>`

It has no `begin()`/`end()`, so a range-for over `$1` in an `out` typemap does not
compile — which is how the first `std::map` conversion failed.

* **Symptom**: `invalid range expression of type 'SwigValueWrapper<std::map<...>>';
  no viable 'begin' function available`.
* **Fix**: bind through the wrapper's `operator T&` first:
  `const $1_ltype& map_ = $1;`

# A testing lesson, not a SWIG one

The first test for the map conversion asserted the result was **not** a SWIG
proxy. It passed on a binary with no map support at all: there the value is an
empty generic `SwigObject` with no `get()`/`size()` and no entries, so both halves
of the negative were vacuously true.

**A negative assertion about marshalling can pass because the feature is missing
entirely.** Assert what the value *is* — `counts.constructor === Object` — not
what it is not.

# Library facts worth knowing

| Fact | Consequence |
|---|---|
| `javascript/napi/` ships no `boost_shared_ptr.i`, `std_set.i`, `std_list.i`, `std_wstring.i` | `%shared_ptr` and those templates must be guarded or supplied — see [shared_ptr design](/bindings/shared-ptr-design.md) |
| `std_vector.i` / `std_map.i` wrap containers as opaque proxies | Conversions to TypedArray / plain object are hand-written in `ext/js/jsarrays.i` |
| `SWIG_Object` is `Napi::Value`; `env` is in scope in typemaps | `SWIG_exception_fail` works *inside* typemaps, unlike in `%extend` bodies |
| `SWIG_NAPI_AppendOutput` turns an Undefined result into a one-element Array | A single argout must be assigned directly, or `tttr.macroTimes()` returns `[array]` instead of `array` |
| The generated ObjectWrap constructor hard-codes the stored pointer and type | The reason shared_ptr needed a different design entirely |

[^session]: Behaviour observed while writing ext/js/jsarrays.i and js_shared_ptr.i
[^swiglib]: SWIG 4.2.1 javascript/napi library (javascriptcode.swg, javascriptrun.swg)
