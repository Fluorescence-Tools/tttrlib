# PRD-002 — Java N-dimensional / generic output-array marshalling

> **PRD #:** 002 · **Status:** Done · **Created:** 2026-07-03 · **Owner:** tpeulen

## Summary

Give the Java binding first-class, general N-dimensional array output — so native
C++ methods that return arrays via output pointers are usable from Java without a
hand-written `%extend` per method.

## Problem / motivation

`jarrays.i` deliberately implements **input** typemaps only. A Java method's
return is bound to the C++ return type, so a `void` "output-pointer" function has
no `jresult` to assign; `ARGOUTVIEW`/`ARGOUTVIEWM` output typemaps would not
compile and were left out. The workaround is per-method `%extend` wrappers in
`ext/java/helpers.i` (`get_intensity_into`, `get_mean_micro_time_into`,
`get_phasor_into`, `get_decay_of_pixels_masked`, …) using preallocated `INPLACE`
arrays or `std::vector` returns. This does not scale: every new array-returning
method needs a bespoke wrapper, and multi-dimensional shapes are flattened by
hand.

## Goals

- A reusable mechanism to return 1-D/2-D/3-D/4-D arrays from Java for the
  `ARGOUTVIEW*`/`ARGOUTVIEWM*` typemap names the shared fragments already use.
- No per-method `%extend` for the common cases.
- Preserve the existing input typemaps and the byte-identical Python wrapper.

## Non-goals

- Zero-copy for every getter (a `java.nio` direct-buffer fast path is optional,
  only for the hottest large getters).
- Changing the Python or R marshalling.

## Options

1. **Return a boxed result object** (`double[]` + `int[] shape`) from a generated
   wrapper — SWIG `out` typemap on a small struct. Idiomatic-ish; needs a carrier
   type.
2. **`java.nio` direct `ByteBuffer`** views over C++-owned memory with explicit
   free — zero-copy, but lifetime management is manual and error-prone.
3. **Flat primitive array + separate `getShape()`** — simplest; caller reshapes.
   Matches current `*_into` ergonomics but generalized so it is generated, not
   hand-written.
4. **Codegen helper macro** — a `%define` that stamps an `_into`/return wrapper
   for a given method signature, cutting the boilerplate without new runtime
   machinery.

Recommendation: prototype (3)+(4) first (lowest risk, matches current API), keep
(2) as an opt-in fast path.

## Milestones

- **M1** — decide carrier/shape convention; spike one 3-D getter (CLSM intensity).
- **M2** — `%define` macro to stamp the wrapper; migrate the existing `helpers.i`
  accessors onto it.
- **M3** — optional `nio` fast path for `get_macro_times` / large getters.

## Risks

- Java has no unsigned types; document the signed-carrier caveat (already noted in
  `jarrays.i`).
- Ownership/`free` correctness for `ARGOUTVIEWM` (transfer) vs `ARGOUTVIEW` (view).

## References

- `ext/java/jarrays.i` (input-only note), `ext/java/helpers.i` (current wrappers)
- Tracked as task “Follow-up: Java ND + output array marshalling”.
