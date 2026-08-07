---
type: Design Decision
title: shared_ptr for the Node-API backend
description: Why ext/js/js_shared_ptr.i is not a transcription of SWIG's boost_shared_ptr.i, and what it does instead.
resource: /ext/js/js_shared_ptr.i
tags: [swig, node-api, shared-ptr, lifetime, design]
status: stable
generated: { by: "claude-code/claude-opus-5", at: 2026-08-06T07:20:00Z }
sources:
  - id: upstream
    resource: /Users/tpeulen/mambaforge/share/swig/4.2.1/r/boost_shared_ptr.i
    title: SWIG's own shared_ptr typemaps (r/ version, used as the starting point)
  - id: napi-ctor
    resource: /Users/tpeulen/mambaforge/share/swig/4.2.1/javascript/napi/javascriptcode.swg
    title: js_overloaded_ctor / js_dtoroverride templates
---

# The problem

`std::shared_ptr<TTTR>` is the currency of the tttrlib API — `TTTR::select`,
`get_tttr_by_channel`, `CLSMImage`'s constructor, `Correlator`'s setters. SWIG
4.2 ships `boost_shared_ptr.i` for `python/`, `r/` and `java/` but **not** for
`javascript/napi/`, so `%shared_ptr(TTTR)` expands to an undefined
`SWIG_SHARED_PTR_TYPEMAPS` and nothing builds.

# What was tried first, and why it failed

Transcribing the language-neutral core of upstream's version.[^upstream] It
**compiles cleanly and fails on every single method call**:

```
in method 'TTTR_get_macro_times', argument 1 of type 'TTTR *'
```

Every other language's shared_ptr support rests on one assumption: for a class
marked `%feature("smartptr")`, the proxy object stores a `shared_ptr<T>*`
registered under the shared_ptr's own `swig_type_info`, and all typemaps convert
through `$descriptor(shared_ptr<T>*)`.

The Node-API backend cannot satisfy that. Its generated ObjectWrap constructor is
fixed and does two things this must live with:[^napi-ctor]

```cpp
this->info = SWIGTYPE_p_TTTR;    // ALWAYS the raw class type
this->self = result;             // ALWAYS the raw `new TTTR(...)`
```

So `new tttrlib.TTTR(path)` stores a raw pointer under the raw type, and any
typemap asking for the shared_ptr descriptor misses. A faithful transcription is
useless at run time.

# What it does instead

**The proxy always holds the raw `T*`**, matching what the backend's constructor
produces. The C++ reference lives elsewhere: a process-wide, mutex-guarded,
refcounted holder table keyed by that raw pointer.

| Direction | Behaviour |
|---|---|
| `shared_ptr<T>` returned | Retain a reference in the table, hand out `sp.get()` with `SWIG_POINTER_OWN` |
| `shared_ptr<T>` argument | Look the raw pointer up in the table; if absent, wrap with a **null deleter** |
| Proxy destroyed | `sp_release(arg1)`; if the pointer was not in the table it came from `new` in JavaScript, so `delete` it |

The table also solves a second problem for free: it disambiguates destruction. A
pointer in it came from a shared_ptr and must be released; one that is not came
from a JavaScript constructor and must be deleted. One lookup answers both.

Why refcounted: SWIG hands out a fresh proxy per call, so the same C++ object can
be wrapped several times.

# The limitation, stated plainly

An object constructed in JavaScript and then handed to C++ that stores a
`shared_ptr` is passed with a **null deleter**. C++ holding it does not extend
its life: if the JavaScript object is collected first, the C++ side is left with
a dangling pointer.

```js
let t = new tttrlib.TTTR('x.ptu');
const img = new tttrlib.CLSMImage(t, ...);   // C++ stores a shared_ptr<TTTR>
t = null;                                     // do NOT do this while img lives
```

This is the same contract R and Java live under — neither has shared_ptr support
at all, so both pass raw pointers. Lifting it would need the proxy to hold a
`napi_ref` back-reference, which a typemap cannot reach.

Objects that came **from** C++ are unaffected: the holder table owns a real
reference, which is exactly what `test/js/lifetime.test.mjs` checks.

# Where it is verified

`test/js/lifetime.test.mjs` covers a `shared_ptr` result outliving the call that
produced it, surviving collection of the object it came from, and thousands of
proxies of the same object releasing cleanly. Nothing has been run under a
sanitiser — see [open items](/bindings/open-items.md).

[^upstream]: SWIG's own shared_ptr typemaps (r/ version, used as the starting point)
[^napi-ctor]: js_overloaded_ctor / js_dtoroverride templates
