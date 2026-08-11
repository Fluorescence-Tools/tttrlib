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
thing being measured. A `tcspc_shift_lamp` call on a 512-channel decay costs
26.5 µs, of which 24.4 µs remains when the shift is set to zero — 98% wrapper.
The same arithmetic through a NumPy in-place binding (`fconv`) costs 2.6 µs.

# The rule

**A loop stays whole in C++.** The seam is crossed once per *analysis* — never
once per iteration, per column, per component, or per frame.

This is a statement about where to put the boundary, not about how fast the C++
is. A binding that exposes a loop **body** is a performance regression by
construction however well the body is written: a caller who uses it as intended
runs slower than one who never linked the library. Three measured instances:

- Building the maximum-entropy design matrix column by column, reusing the
  exposed `tcspc_fconv_*` kernels, costs 1668 µs for 60 columns. One call to
  `tcspc_build_fi_lifetimes` builds the identical matrix in **58.5 µs**. The
  28.5× is entirely argument conversion.
- Driving the MEM iteration from Python and delegating only the inner QP costs
  146 µs per call at `n_tau = 60` — **29 ms of seam** over 200 iterations,
  before any arithmetic happens.
- An optimisation behind such a binding cannot be seen by the caller at all:
  `fconv_simd` measures 1.01× against `fconv` at n=4096.

# What to do instead

**NumPy typemaps on numeric entry points.** `%apply(double* IN_ARRAY1, int
DIM1)` for inputs, `ARGOUTVIEWM_ARRAY1` / `ARGOUTVIEWM_ARRAY2` for outputs — a
borrowed pointer in, an owned buffer out, no per-element boxing.
`ext/python/MaxEntTcspc.i` is the worked example. Two traps it records:

- `ARGOUTVIEWM` hands ownership to NumPy and frees with `free()`, so the buffer
  copied out of a `std::vector` must be `malloc`'d, never `new`'d.
- A C++ function returning results through `std::vector&` **out-parameters**
  becomes, under the default typemaps, a Python function with those as
  *required inputs* that no caller can satisfy. Such a function is compiled,
  exported, documented — and uncallable. `%ignore` the original, `%rename` an
  `%inline` wrapper over it.

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

1. **Give the MEM solvers a progress/cancel callback.** This is the one gap
   that is currently forcing a duplicate implementation downstream, so it buys
   more than any single typemap conversion. Re-derive the cost it removes with
   the `tcspc_quadpr_bound` timing above (146 µs/call at `n_tau = 60`); the
   trap is that the duplicate looks harmless because both copies pass their
   tests — what it costs is paid the next time either is edited.
2. **Convert the numeric hot paths to NumPy typemaps.** 68 headers under
   `modules/` mention `std::vector<double>`; that is the search space, not the
   worklist. Rank by call frequency from Python, not by count. The
   `fconv`/`rescale` family already uses in-place typemaps and is the shape to
   copy.
3. **Settle `fconv_simd`.** It measures 1.01× against `fconv` at n=4096, where
   call overhead cannot be hiding a real gain. Either the SIMD path is not
   selected in this build (a build bug) or the kernel is memory-bound (delete
   the second implementation). Time both in C++ — with the binding in the way
   the question cannot be answered.
4. **Benchmark through the bindings.** A benchmark that times C++ directly
   would have measured none of this. The suite proposed in `BUGS.md` must call
   the library the way a user does.
