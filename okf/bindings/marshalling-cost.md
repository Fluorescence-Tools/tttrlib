---
type: Design Constraint
title: The Python seam costs ~50 ns per element, so a loop stays whole in C++
description: Why `std::vector<double>` bindings make the wrapper, not the algorithm, set the runtime — and the granularity rule that follows for anything delegated to this library.
resource: /ext/python/misc_types.i
tags: [python, swig, typemaps, performance, numpy, binding, granularity]
status: stable
generated: { by: "claude-code/claude-opus-5", at: 2026-08-11T00:00:00Z }
sources:
  - id: measurement
    resource: BUGS.md
    title: Measured on arm64, tttrlib 0.27.0, 2026-08-11 — reproduction in the filed entry
    author: human:tpeulen
    last_modified: 2026-08-11
  - id: maxent
    resource: ext/python/MaxEntTcspc.i
    title: The NumPy-typemap pattern, and the granularity decision it encodes
    last_modified: 2026-08-11
---

# Summary

`ext/python/misc_types.i` declares `%template(VectorDouble) std::vector<double>`
for the whole library. Every exposed function taking or returning a
`std::vector<double>` therefore marshals through the Python **sequence
protocol** — one `PyFloat` boxed per element, in and out. It is not a memcpy,
and it is not free: **~50 ns per element**, degrading past ~4k elements.

For anything short of a large array this makes the *binding*, not the C++, the
thing being measured. A `tcspc_shift_lamp` call on a 512-channel decay cost
26.5 µs, of which 24.4 µs remained when the shift was set to zero — 98%
wrapper. The same arithmetic through a NumPy in-place binding (`fconv`) cost
2.6 µs.

**The MaxEnt family is converted** (`ext/python/MaxEntTcspc.i`, 2026-08-11) and
is the worked example for everything else. Nine entry points moved to
`IN_ARRAY1` in / `ARGOUTVIEWM_ARRAY1/2` out:

| n | before | after | speedup | after, per element |
|---|---|---|---|---|
| 64 | 9.1 µs | 0.50 µs | 18× | 7.9 ns |
| 512 | 26.5 µs | 0.81 µs | 33× | 1.6 ns |
| 4096 | 335 µs | 5.55 µs | 60× | 1.4 ns |
| 16384 | 1360 µs | 19.6 µs | 69× | 1.2 ns |

Per-element cost falls ~40× and the converted binding is now faster than the
in-place `fconv` above n=64. Signatures, keyword names and defaults are
unchanged; lists still work as input; `test/python/decayfit` +
`test_gil_release` stay at 111 passed / 1 skipped.

# The rule

**A loop stays whole in C++.** The seam is crossed once per *analysis* — never
once per iteration, per column, per component, or per frame.

Converting the typemaps does not repeal this, it only makes each crossing
cheaper: after the MaxEnt conversion a per-column design-matrix build still
costs 129 µs against 55.9 µs for the one whole-matrix call, and a
Python-driven 200-iteration MEM loop still burns 7.3 ms in the seam alone.
Better by 13× and 4×, and still the wrong shape.

This is a statement about where to put the boundary, not about how fast the C++
is. A binding that exposes a loop **body** is a performance regression by
construction however well the body is written: a caller who uses it as intended
runs slower than one who never linked the library. Three measured instances:

- Building the maximum-entropy design matrix column by column, reusing the
  exposed `tcspc_fconv_*` kernels, cost 1668 µs for 60 columns against **58.5
  µs** for the one call to `tcspc_build_fi_lifetimes` that builds the identical
  matrix. After conversion: 129 µs against 55.9 µs. The gap narrows from 28.5×
  to 2.3× and does not close.
- Driving the MEM iteration from Python and delegating only the inner QP cost
  146 µs per call at `n_tau = 60` — **29 ms of seam** over 200 iterations,
  before any arithmetic happened. After conversion, 36.3 µs and 7.3 ms.

A third instance was filed alongside these and **did not survive** — worth
keeping because the wrong inference is an easy one to repeat. `fconv_simd`
measures 1.01× against `fconv` at n=4096, which reads as an optimisation the
binding is hiding. It is not: `fconv_simd` *is* `fconv`, a one-line forwarding
alias, and the NEON dispatch inside `fconv` is alive at 1.87×. 1.00× is the
right answer for a function compared with itself. Marshalling cost explains a
great many things; it does not explain everything, and "the wrapper must be
hiding it" is the hypothesis to test rather than assume.

# What to do instead

**NumPy typemaps on numeric entry points.** `%apply(double* IN_ARRAY1, int
DIM1)` for inputs, `ARGOUTVIEWM_ARRAY1` / `ARGOUTVIEWM_ARRAY2` for outputs — a
borrowed pointer in, an owned buffer out, no per-element boxing.
`ext/python/MaxEntTcspc.i` is the worked example — all nine of its entry points
are converted. Four traps it records:

- `ARGOUTVIEWM` hands ownership to NumPy and frees with `free()`, so the buffer
  copied out of a `std::vector` must be `malloc`'d, never `new`'d.
- A C++ function returning results through `std::vector&` **out-parameters**
  becomes, under the default typemaps, a Python function with those as
  *required inputs* that no caller can satisfy. Such a function is compiled,
  exported, documented — and uncallable. `%ignore` the original, `%rename` an
  `%inline` wrapper over it.
- **The wrapper's argument names are the public keyword names.** Renaming
  `decay` to `mem_decay` to keep `%apply` patterns from colliding silently
  breaks every caller passing `nu=` or `prior=`. Use the real names and
  `%clear` them at the end of the file.
- **`%include` the header *before* the `%inline` block** when a wrapper returns
  a struct by value. SWIG parses top to bottom; with the include after, the
  return type is unknown and the caller gets an opaque pointer whose `.p`
  attribute does not exist.
- An optional array argument (`prior = {}`) cannot simply be omitted from a
  NumPy typemap pair. Give the wrapper's pair a C++ default (`double* prior =
  nullptr, int n_prior = 0`) and SWIG generates the overload; the Python
  signature keeps `prior=None`.

**A progress callback, not a Python-driven loop.** When a long solver has to
report to a GUI, the answer is a callback parameter on the C++ loop
(`std::function<bool(int, double, double)>`, returning "keep going", which also
gives the caller a cancel) — not handing the iteration back to Python.
`tcspc_run_mem`, `solve_tcspc_mem_lifetime` and `solve_tcspc_mem_fret` have no
such hook today, and the cost is concrete: ChiSurf's maximum-entropy plugin
keeps a **second** NumPy implementation of `_run_mem` and `_quadpr_bound`
because it cannot otherwise drive its progress bar. Two copies of one
algorithm, kept in step by hand.

**Do not export loop bodies**, or say so in the docstring where they exist for
verification. `tcspc_shift_lamp`, `tcspc_fconv_single_shot`,
`tcspc_fconv_periodic` and `tcspc_quadpr_bound` were exposed so a port could be
checked kernel by kernel; nothing in their documentation warns that building on
them from Python is slower than not using the library.

# Where to pick this up

1. **Give the MEM solvers a progress/cancel callback.** Now the largest item,
   and the conversion is why: the seam it would remove is down to 7.3 ms over a
   fit, so the argument for it is no longer speed but the **duplicate
   implementation** it is forcing downstream — ChiSurf cannot let the loop run
   in C++ and still drive a progress bar. The trap is that the duplicate looks
   harmless because both copies pass their tests; what it costs is paid the
   next time either is edited.
2. **Convert the remaining numeric hot paths to NumPy typemaps.** 68 headers
   under `modules/` mention `std::vector<double>`; that is the search space,
   not the worklist. Rank by call frequency from Python, not by count. The
   `fconv`/`rescale` family already uses in-place typemaps, and `MaxEntTcspc.i`
   is now a converted file to copy from. Measure before and after with the
   reproduction in `BUGS.md` — the ratio against `fconv` is ~10 before and ~1
   after, which is a clearer signal than an absolute time on any one machine.
3. **Benchmark through the bindings.** A benchmark that times C++ directly
   would have measured none of this. The suite proposed in `BUGS.md` must call
   the library the way a user does.

(`fconv_simd` was on this list and is closed: it was an alias, now deprecated
with a shim and a test pinning both names to bit-identical results.)
