# Known bugs

Found from outside the library, with a reproduction each. Anything fixed moves
to the changelog and leaves here.

## PARTLY FIXED — A file tttrlib wrote is not recognised by tttrlib, and opening it returns zero events instead of failing

> **`.sm` fixed 2026-08-11**, by the session that filed this. Three separate
> discrepancies against a real file, each enough on its own, and all of them
> in the writer:
>
> * the version defaulted to **1** where real files carry 2 and `isSMFile`
>   requires 2 — a source without its own `version` tag (any non-SM source)
>   therefore produced an unidentifiable file;
> * every counted string was written **a byte longer than its content**,
>   counting a terminating null the format does not have — a real `.sm` stores
>   `"Simple"` as a count of 6 and six bytes;
> * the `simple` field defaulted to **empty**, so that extra byte was an
>   unprintable `0x00` exactly where the detector checks for printable
>   characters.
>
> Fixing the writer exposed a **latent overread in the reader**: `read_string`
> allocated the count and handed the buffer to `add_tag`, which takes a
> `char*` and reads to the first null. Real files have no terminating null
> either, so this was always reading past the allocation — it only became a
> crash once the bytes that followed changed. The buffer is now one longer and
> terminated.
>
> Verified on each format **against itself**, which is the case that has to
> hold before any other: `.sm` → `.sm` gives back all 2,060,245 events with
> identical macro *and* micro times, and `.ht3` → `.ht3` its 11,605,946 — a
> `.ht3` and a `.sm` are different files with different record types, and
> proving one through the other proves neither. The transcode that exposed the
> defect (an HT3 source has no `version` tag, so the SM writer took its wrong
> default) is tested separately and labelled as a transcode, including that
> the result describes itself as SM rather than carrying the source's record
> type. `test/python/tttr/test_written_files_are_re_detected.py` also pins the
> three header facts as bytes, because each defect was invisible at every
> level above them — the events were always correct.
>
> **Still open from this entry**, and the more valuable half:
>
> 1. **SPC-600/256 and SPC-600/4096** are untouched. `.spc` is claimed by four
>    formats told apart by content, so the diagnosis is not the same as `.sm`'s.
> 2. **`TTTR(path)` still returns an empty object rather than raising** for any
>    unidentifiable path. That is not specific to these formats and is what
>    turns a detection failure into silent wrong data.

## The original entry

**2026-08-11.** Three formats — **SPC-600/256, SPC-600/4096 and `.sm`** — write
correctly and cannot be opened again by name. `write()` returns `True`, the
file has plausible size, and `TTTR(path)` hands back an **empty object with no
exception**:

```python
t = tttrlib.TTTR("some.ht3")                 # 11,605,946 events
t.write("out.spc", None, 3)                  # BH_SPC600_256 -> True, 46.5 MB
len(tttrlib.TTTR("out.spc"))                 # 0        <- and only a stderr line
```

**The bytes are correct.** Forcing the container type reads the whole thing
back:

```python
len(tttrlib.TTTR("out.spc", 3))              # 11,605,946
# first 1000 macro times identical to the source
```

So this is **not** a writer defect. The writer is fine and the payload is
faithful; what fails is *identification*, and then the failure is swallowed.

| Container | written | `TTTR(path)` | `TTTR(path, container)` |
|---|---|---|---|
| SPC-600/256 (3) | 46.5 MB | **0** | 11,605,946 |
| SPC-600/4096 (4) | 69.6 MB | **0** | 11,605,946 |
| `.sm` (7) | 139.3 MB | **0** | 11,605,946 |

Real files of these types open by name normally (`sample_c01.spc` → 607,866;
`data.sm` → 2,060,245), so the sniffers work on instrument files and not on
ours. For `.spc` the extension is claimed by four formats — SPC-130, both
SPC-600 flavours and SPC-QC — and they are told apart by content, so a written
SPC-600 is most likely being tried as SPC-130 and rejected, or matching nothing.
`.sm` has the extension to itself, which makes it the cleaner case to debug
first: nothing else can be shadowing it.

**Two defects, and the second is the dangerous one.**

1. **Detection does not recognise what this library writes.** Whatever the
   sniffer keys on, `write` is not producing it — a header field, a magic
   value, or a size that has to divide evenly. A round trip through our own
   writer is the one case detection should never miss.
2. **`TTTR(path)` returns an empty object rather than raising.** "File not
   supported" goes to `stderr` and the constructor succeeds. A caller in a
   script, a notebook or a GUI gets a TTTR with zero photons and no exception,
   and every downstream number — a count rate, a correlation, a lifetime — is
   computed from nothing. Anything that captured stderr, or simply was not
   watching it, sees a measurement that ran and produced no signal. **This
   half is not specific to these three formats**: it is what happens for any
   unidentifiable path, so it is the more valuable of the two to fix.

**What to do.** Fix (2) first and independently — an unidentifiable file must
raise, and the message must name the path and say detection failed. Then (1):
compare the first kilobyte of a written SPC-600 against `sample_c01.spc`, and a
written `.sm` against `data.sm`, and make the writer emit whatever the sniffer
requires. A conformance case per format that writes and reopens **by name**
would have caught this and would keep it caught; today's round-trip tests pass
the container type explicitly, which is exactly the path that works.

**Found while** adding streaming writers, where the same three formats stood out
(`examples/streaming/stream_to_any_format.py` compares every streamed file with
a whole-file `TTTR.write` and marks these "format cannot round-trip"). Worth
correcting the record: that example's wording, and my first reading of it, put
the blame on the writers. The forced-read column above shows the writers are
innocent.

## Sixteen to eighteen subsystems are Python-only, and every other binding's interface file says it is identical to Python's

**2026-08-11.** The four bindings do not share one `%include` list. Each master
interface keeps its own, and they have drifted:

```
python  60 includes
r       43   (17 missing)
java    44   (18 missing)
js      45   (16 missing)
```

Missing from all three: `Cluster.i` (the k-d tree and the HDBSCAN kernels),
`Deconvolution.i`, `MaxEnt.i`, `MaxEntTcspc.i`, `Pda3cCore.i`, `GopichSzabo.i`,
`PhotonCountingHistogram.i`, `Streaming.i`, `BurstML.i`, `Sampling.i`,
`Jitter.i`, `BlindIRF.i`, `RecurrenceAnalysis.i`, `SpectralCrosstalk.i`,
`BackgroundEstimation.i`; plus `BurstSignificance.i` (R, Java) and
`documentation.i` (Java).

Reproduce:

```python
import re, pathlib
inc = lambda p: [m.group(1) for m in
                 re.finditer(r'^%include\s+"([^"]+)"', pathlib.Path(p).read_text(), re.M)]
py = inc('ext/python/tttrlib.i')
for b in ('r', 'java', 'js'):
    missing = [i for i in py if i not in set(inc(f'ext/{b}/tttrlib.i'))]
    print(b, len(missing), missing)
```

Two things make this worse than a to-do list.

**Every one of those files asserted the opposite.** All three carried the line
`// Shared C++ core -- identical %include list to ext/python/tttrlib.i`, and the
JavaScript one repeated it further down. A reader checking whether R has the
clustering kernels finds a comment saying it must. Corrected 2026-08-11 to state
the drift and point at the board ticket; the code is unchanged.

**Nothing detected it — now something does.** `tools/check_swig_multilang.sh`
ran SWIG four times and passed if four wrappers generate; it never compared what
they expose, so a subsystem could be complete in Python and absent from the
other three indefinitely with a green check.

`tools/check_binding_parity.py` (2026-08-11, wired in as a fifth check) closes
that. It does not demand parity — it demands every gap be **declared**: an
interface missing from a binding must appear in
`tools/binding_parity_exceptions.txt` with a reason, and an exception for a gap
that has since been closed fails too, so the list cannot rot. The 51 current
gaps are seeded there, almost all reading *"unreviewed drift as of 2026-08-11,
not a decision"* — which is what they are. **The gaps themselves are still
open**; what changed is that they are now visible and cannot silently grow.

**Adding these back needs nothing clever, and I first thought it did.** The
plausible worry is that an interface which `%ignore`s its `std::vector`
overloads in favour of `IN_ARRAY1`/`ARGOUTVIEWM` shims — `MaxEntTcspc.i` does —
hands the other three languages a `SWIGTYPE_p_double` no caller can build. It
does not: `ext/r/rarrays.i`, `ext/java/jarrays.i` and `ext/js/jsarrays.i`
implement those same typemap names against R vectors, Java arrays and JS
TypedArrays, exactly so one `%apply` line serves all four languages. Verified by
generating the R wrapper from an interface with no per-language surface:
`dfa_convolve(rates, weights, irf, n_bins, shift_bins, method)` takes plain R
numeric vectors. For most of the missing files, adding the `%include` is the
whole job.

Tracked as `T-20260811-09` on the agent board.

**2026-08-11, five of the thirteen closed — the ones with no NumPy in them.**
By a different session than the one that filed this, so the entry stays open
for its author; 22 declared gaps remain, down from 51.

Closed by adding the `%include` to all three lists, positioned to preserve
Python's relative order: **`BurstSignificance.i`** (r, java),
**`BurstML.i`**, **`GopichSzabo.i`**, **`PhotonCountingHistogram.i`** and
**`Pda3cCore.i`** (r, java, js). These were chosen by the criterion this entry
supplies: each has **zero `IN_ARRAY`/`ARGOUTVIEW` typemaps and zero
`%ignore`s**, so none of them is the second mechanism above — there is no
NumPy shim hiding a `std::vector` overload, and nothing to convert first.

**Verified callable, not merely generated**, because "it generates" is exactly
the check this entry warns is insufficient. In the generated Java: `BurstML`
and `GopichSzabo` come out as classes taking `VectorDouble` / `VectorInt32`
(`set_burst_data`, `fit`, `set_scheme`, `log_likelihood`, `viterbi`), and the
five `Pda3cCore` free functions, three `BurstSignificance` statistics and five
`pch_*`/`fida_*` functions all reach the module class returning `VectorDouble`
or `double`. No `SWIGTYPE_p_double` anywhere in them. All four wrappers
generate, the Java proxies compile, and the Python wrapper is byte-for-byte
reproducible — Python's list was not touched.

**No linking gap, checked rather than assumed:** all four bindings call
`tttrlib_link_all_modules()`, so every module's implementation was already in
the R/Java/JS targets and only the interface was absent. That is why these
five could be closed by an `%include` alone.

**2026-08-11, later — and the paragraph I wrote above was wrong, so read this
instead.** Four more closed; **12 declared gaps remain, down from 51.**
`Cluster.i` and `Sampling.i` to all three, `Deconvolution.i` and `Jitter.i` to
**r and js only**.

**This entry's second mechanism is real but its stated cause is not.** The
entry says NumPy typemaps "exist only in the Python binding". They do not:
`ext/r/rarrays.i`, `ext/java/jarrays.i` and `ext/js/jsarrays.i` implement the
same `IN_ARRAY*` / `INPLACE_ARRAY*` / `ARGOUTVIEW(M)_ARRAY*` names against R
vectors, Java arrays and JS TypedArrays, exactly so one `%apply` serves all
four languages — `ext/python/DecayFit.i` says so at length and had verified it.
So an interface carrying NumPy typemaps is **not** disqualified, and the
`#ifdef SWIGPYTHON` / `#else` conversion the entry prescribes is usually
unnecessary.

**The actual discriminator is rank, and it is one binding.** Coverage,
measured:

| | IN/INPLACE 1,2,3 | ARGOUTVIEW(M) 1 | ARGOUTVIEW(M) 2,3 |
|---|---|---|---|
| r | yes | yes | **yes** |
| js | yes | yes | **yes** |
| java | yes | yes | **no** |

`jarrays.i` implements the argout views at rank 1 only. So an interface that
*returns* a 2-D or 3-D array is the only one that breaks, and only in Java.
`Cluster.i` (IN_ARRAY2 in, nothing out) and `Sampling.i` (rank-1 argout) are
therefore fine everywhere; `Deconvolution.i` and `Jitter.i` return through
`ARGOUTVIEWM_ARRAY2/3` and are fine in r and js.

**Demonstrated rather than reasoned, by generating the Java that was not
added.** With `Deconvolution.i` on Java's list, SWIG emits — without a warning
— `richardson_lucy_2d(double[][] input, double[][] psf, …,
SWIGTYPE_p_p_double output, SWIGTYPE_p_int n_output1, SWIGTYPE_p_int
n_output2)`: the *inputs* convert fine, the *output* is uncallable. That is
this entry's warning, reproduced on demand, and it is what the two Java
exceptions now record instead of "unreviewed drift".

**One correction to the exceptions list, same method.** `MaxEntTcspc.i` was
annotated "needs nothing but the %include — its IN_ARRAY typemaps work in all
four languages". True for r and js; **half true for Java**, which is worse
than false because it splits the subsystem: `solve_tcspc_mem_lifetime` comes
out fully callable (`double[]` in, `MemTcspcResult` out) while
`tcspc_build_fi_lifetimes` returns its four arrays through
`ARGOUTVIEWM_ARRAY2/1` and every out-parameter is an opaque pointer. A Java
caller would get a working solver and an unusable design-matrix builder.

**So what closing the rest actually needs**, which is smaller than this entry
assumed: `HmmLattice.i`, `Streaming.i` and `MaxEntTcspc.i` (r, js) are a
`%include` away — left here only because all three were being written by
another session this morning. Java's share of the remainder is **one job, not
several**: implement `ARGOUTVIEWM_ARRAY2` / `ARGOUTVIEW_ARRAY2` (and rank 3)
in `ext/java/jarrays.i`, and `Deconvolution.i`, `Jitter.i` and MaxEntTcspc's
builders all become addable at once. `documentation.i` (java) is separate,
being docstrings rather than API.

## FIXED — A wall-clock assertion in the unit suite fails when the machine is busy

> **Fixed 2026-08-11** (board ticket `T-20260811-01`). Took the **first** of the
> two options the entry named: the strict inequality now runs only where a
> clock can see the gap. `RATES_WITH_A_MEASURABLE_GAP = 4` splits the claim in
> two — `test_the_recursion_is_faster_wherever_the_gap_is_measurable` asserts
> `speedup > 1.0` from four rates up, where the lead is 2.6x and climbing, and
> `test_the_recursion_is_not_beaten_at_one_or_two_rates` asserts only
> `speedup > 0.5` at one and two rates, which is the claim those sizes can
> support: a few percent either way is the clock, twice the recursion is a
> regression. The example is unchanged, as the entry argued it should be.
> 7 passed. Tightening that 0.5 re-files this bug — that is said in the
> docstring, not just here.

**2026-08-11.** `test_convolution_methods_example.py::test_the_recursion_is_
faster_at_every_rate_count` asserts `np.all(speedup > 1.0)` on timings measured
during the test run. Observed failing while a compile was saturating the
machine, and passing three times in a row on the same build once it was quiet:

```
the text says the recursion wins everywhere:
[1.635 0.827 2.614 3.414 4.394 5.328 6.045]
                ^ the second rate count, under load
```

The example is not at fault — `plot_convolution_methods.py:timed()` already
takes a best-of-50, which is the right robust estimator. What fails is
asserting a *strict* inequality at the smallest problem size, where the true
gap is a few percent and one descheduled run in fifty is enough to invert it.
The neighbouring `test_the_gap_widens_with_the_rate_count` in the same file
already says "timings are noisy, so compare the ends" and does not have this
problem, so the file disagrees with itself about how much to trust a
stopwatch.

Worth deciding rather than patching blind: either drop the smallest rate
counts from the strict claim (the example's real argument is the trend, which
the sibling test already checks), or keep the claim and move it out of the
unit suite into the benchmark harness where a loaded machine is not a
correctness failure. A CI runner is a shared machine, so this will fire there.

Not fixed here because the answer is the example author's call, and a
wall-clock assertion weakened by whoever happens to trip over it is how a test
stops meaning anything.

## FIXED — SIGSEGV: `TTTR(path).header` on a temporary — the header outlives its owner

> **Fixed 2026-08-11** (removal = fix landed, not a concurrent-write loss).
> As the entry proposed, and audited together rather than patched alone: a
> `%pythonappend` keep-alive tags the owner onto every proxy returned by an
> accessor that hands out a pointer into its container — `TTTR::get_header`
> (the filed crash), `TTTR::get_mt_linearizer`, `CLSMImage::get_frames` /
> `get_frame_for_channel`, `CLSMFrame::get_lines` (SWIG returns the pointer
> vectors as tuples, so each ELEMENT carries the owner). The `.header`
> property routes through `__getattr__` → `get_header`, so it is covered.
> The entry's repro now survives a `gc.collect()`;
> `test/python/tttr/test_header_lifetime.py` pins it; tttr (633) and clsm
> (196) groups green.
>
> **2026-08-11, two corrections to this stub, from running
> `tools/check_swig_multilang.sh` rather than reading:**
>
> * **The fix is Python-only, and the entry did not say so.** `%pythonappend`
>   emits nothing for the other backends, so the keep-alive does not exist in
>   R, Java or JavaScript. Each needs its own equivalent (R: an attribute on
>   the returned S4 object; Java: a strong field on the proxy; JS: a Napi
>   reference) and **none of the three has a keep-alive idiom anywhere in its
>   interface files today** — this would be the first.
>
>   **How far that is established, stated precisely, because this entry's own
>   standard is a reproduction and there is not one for the other three.**
>   What is certain from the source: `TTTR::get_header` is declared
>   `TTTRHeader* get_header();` — a **raw, non-owning pointer** — and
>   `TTTRHeader` is not `%shared_ptr`'d, so no binding gets ownership from the
>   type system; only Python was given a keep-alive on top. R, Java and
>   JavaScript are all garbage-collected, so a header taken from a temporary
>   container is by construction a pointer into an object the collector is
>   free to reclaim. That is an argument, not a crash: what has **not** been
>   checked is whether each backend's proxy makes the pattern reachable with
>   the same ease as `TTTR(path).header` does in Python. Neither R nor the
>   JavaScript addon is built in this checkout, so settling it means building
>   them — worth doing before anyone writes three keep-alives, since the cost
>   of the fix is three mechanisms and the evidence so far is one language's.
>   Reopening is the author's call; recorded here so the stub does not read as
>   "closed everywhere".
> * **It broke wrapper generation for those three bindings for a day.**
>   `%pythonappend` is an *unknown directive*, not a no-op, in the R/Java/JS
>   backends: generation stopped at `TTTR.i:116` while `pip install -e .` went
>   on succeeding, so nothing local showed it. Both files are now
>   `#ifdef SWIGPYTHON`-guarded and all four backends generate again. The
>   lesson generalises past this entry: **`ext/python/*.i` is the SHARED SWIG
>   core** — run the four-language check after touching any of it.

## FIXED — TCSPC MaxEnt is half-landed: the lifetime axis is here, the FRET distance axis is not

> **Fixed 2026-08-10, entry moved to the changelog** (removal = fix landed,
> not a concurrent-write loss). `solve_tcspc_mem_fret` +
> `tcspc_build_fi_distances` now live beside the lifetime solver, sharing one
> `run_mem_from_design` engine — the two differ only in the design matrix,
> which was the entry's point. Parity with ChiSurf's `solve_fret_mem`:
> `max |dp| < 1e-8` on identical input
> (`test_maxent_tcspc.py::TestTcspcMemFret`), plus a no-reference
> ground-truth recovery test. ChiSurf's `solve_fret_mem` can now delegate the
> way `solve_lifetime_mem` does.
>
> **Still open from this entry:** FCS MaxEnt
> (`chisurf/core/models/fcs/maxent.py`) remains a third MEM implementation
> with no home here; closing it means exposing the engine with a pluggable
> design matrix, at which point all three are one solver and two matrix
> builders. This stub can be deleted once both sessions have seen it.

## Every `std::vector<double>` binding converts element by element, so the wrapper costs more than the algorithm — MaxEnt converted, the rest of the library open

**2026-08-11.** `misc_types.i` declares `%template(VectorDouble)
std::vector<double>` for the whole library, so every exposed function taking or
returning a `std::vector<double>` marshals through the Python **sequence
protocol** — one `PyFloat` per element, in and out. It is not a memcpy. The
result is that the binding, not the C++, sets the runtime of any small or
medium call, and the compiled implementation is invisible from Python.

The conversion measures **~50 ns per element** and degrades past ~4k (50, 50,
66, 98 ns/element at n=1024/2048/4096/8192). Calling `tcspc_shift_lamp` with a
shift of **zero** channels cost 24.4 µs at n=512 against 24.8 µs for a real
shift: 98% of the call was the wrapper. A 512-channel decay is a small TCSPC
histogram, so this is the normal case, not the tail.

**The MaxEnt family is converted** (`ext/python/MaxEntTcspc.i`, 2026-08-11).
All nine entry points now take `double* IN_ARRAY1, int DIM1` and return through
`ARGOUTVIEWM_ARRAY1/2`. Same machine, same arithmetic, before → after:

| n | before | after | speedup | after, per element |
|---|---|---|---|---|
| 64 | 9.1 µs | 0.50 µs | 18× | 7.9 ns |
| 512 | 26.5 µs | 0.81 µs | 33× | 1.6 ns |
| 4096 | 335 µs | 5.55 µs | 60× | 1.4 ns |
| 16384 | 1360 µs | 19.6 µs | 69× | 1.2 ns |

Per-element cost falls ~40×, from 50 ns to 1.2–1.6 ns, and the converted
binding is now *faster* than the in-place `fconv` at every size above 64. The
knock-on numbers: `tcspc_quadpr_bound` at `n_tau = 60` goes 146 → 36.3 µs, so a
Python-driven 200-iteration MEM loop drops from 29 ms of seam to 7.3 ms;
building the design matrix per column goes 1668 → 129 µs, cutting the penalty
over the single whole-matrix call from 28.5× to 2.3×. Behaviour is unchanged —
`test/python/decayfit` + `test_gil_release` are 111 passed / 1 skipped exactly
as before, and ChiSurf's design-matrix parity guard still matches its numba
fixture bit for bit.

**Still open: the rest of the library.** 68 headers under `modules/` mention
`std::vector<double>`; that is the search space, not the worklist. Rank by how
often each is called from Python, not by count — the `fconv`/`rescale` family
already uses in-place typemaps and is the shape to copy. The pattern and its
two traps are in `MaxEntTcspc.i`:

- `ARGOUTVIEWM` hands ownership to NumPy and frees with `free()`, so the buffer
  copied out of a `std::vector` must be `malloc`'d, never `new`'d.
- A function returning results through `std::vector&` **out-parameters**
  becomes, under the default typemaps, a Python function with those as
  *required inputs* that no caller can supply — compiled, exported, documented
  and uncallable. That is how `tcspc_build_fi_lifetimes` / `_distances`
  shipped. `%ignore` the original and `%rename` an `%inline` wrapper over it.
- Keep the wrapper's argument **names**: they are the public keyword names, and
  callers pass `nu=`, `prior=`, `max_iter=`.

**Also still open: a progress callback on the long-running solvers.**
`tcspc_run_mem`, `solve_tcspc_mem_lifetime` and `solve_tcspc_mem_fret` have no
per-iteration hook, so a caller cannot let the loop run in C++ *and* drive a
progress bar. ChiSurf's plugin therefore keeps a second, NumPy implementation
of `_run_mem` and `_quadpr_bound` — two copies of one algorithm, kept in step
by hand, which is the cost of the missing hook.
`std::function<bool(int, double, double)>` returning "keep going" would close
it and also give the GUI a cancel. Converting the typemaps did not remove this:
7.3 ms of seam over a fit is cheap, but the duplicate implementation is not.

**The rule, which the conversion does not repeal: a loop stays whole in C++.**
The seam is crossed once per *analysis*, never once per iteration, per column,
or per component. A binding that exposes a loop *body* is a performance
regression by construction however fast its C++ and its typemaps are.
`tcspc_shift_lamp`, `tcspc_fconv_single_shot`, `tcspc_fconv_periodic` and
`tcspc_quadpr_bound` are exposed so the port can be verified piece by piece;
`MaxEntTcspc.i` now says so, their docstrings still do not.

**Reproduction** (the ratio is ~1 after conversion, ~10 before):

```python
import timeit, numpy as np, tttrlib
for n in (512, 4096):
    lamp, fit, x = np.ones(n), np.zeros(n), np.array([1.0, 2.0])
    v = timeit.timeit(lambda: tttrlib.tcspc_shift_lamp(lamp, 0.0), number=500) / 500
    i = timeit.timeit(lambda: tttrlib.fconv(fit, lamp, x, 0, n, 0.064), number=500) / 500
    print(n, f"{v*1e6:.1f} us vs {i*1e6:.1f} us -> {v/i:.1f}x")
```

Related: the benchmark-suite enhancement below is what would have caught this
in CI. A benchmark that calls the library the way a user does — through the
Python bindings — measures the wrapper; one that times C++ directly does not.

## FIXED — `fconv_simd` is not measurably faster than `fconv`

> **Fixed 2026-08-11**, as the entry's own disposition prescribed: deprecate
> the alias, keep a shim for one release because both names are exported and
> ChiSurf may call them. `fconv_simd` / `fconv_per_simd` are now marked
> `@deprecated` in the header, their docstrings say they are aliases rather
> than repeating the old "AVX optimized, four lifetimes at once" claim (which
> described `fconv`'s internals, not theirs), and the Python bindings raise a
> `DeprecationWarning` naming the replacement. The two internal callers
> (`fconv_cs_time_axis`, `fconv_per_cs_time_axis`) and the two SWIG wrapper
> bodies now call `fconv` / `fconv_per` directly, so the shim has no callers
> left inside the library and can be deleted outright next release.
>
> Nothing about the measurement changed and nothing needed to: the entry had
> already settled that 1.00× is the correct answer for a function compared
> with itself, and that the NEON path is alive at 1.87×. The defect was the
> *name*, and that is what was removed.
> `test/python/misc/test_capability_report.py::TestSimdAliasDeprecation`
> pins both aliases warning and returning bit-identical results to the
> functions they forward to. This stub can be deleted once both sessions have
> seen it.

<details><summary>Original entry</summary>

**2026-08-11.** Both are exposed, the name promises a vectorised inner loop, and
from Python the two measure the same: **1.73 µs vs 1.56 µs at n=512 (1.11×) and
10.63 µs vs 10.57 µs at n=4096 (1.01×)**. At n=512 call overhead could hide a
real gain, but at n=4096 the calls are 10 µs of mostly arithmetic and there is
nothing to hide behind — 1% is noise.

Either the SIMD path is not being selected in this build (the conda arm64 build
does not enable it, or the runtime dispatch falls through to scalar), or the
kernel is memory-bound and vectorising the multiply-add buys nothing. Both are
worth knowing and the answer changes what to do: the first is a build bug, the
second means `fconv_simd` should be deleted rather than maintained as a second
implementation of `fconv`.

To settle it, time the two in C++ (no binding in the way) and check whether the
SIMD translation unit is compiled with the arch flags it needs. Same
reproduction as the entry above, substituting `fconv_simd`.

**2026-08-11, settled — and it is neither of the two answers above.**
`fconv_simd` **is** `fconv`, one line of it
(`DecayConvolution.cpp:209`): `void fconv_simd(...) { fconv(...); }`. The
runtime dispatch lives inside `fconv` itself, which already picks the AVX or
NEON kernel by CPU feature *and* problem size. So the measurement compares a
function with itself and 1.00× is the correct result, not a symptom. Confirmed
out to n=65536 (167.6 µs vs 168.8 µs), where 167 µs of arithmetic leaves call
overhead nothing to hide behind.

The SIMD path is alive and worth its keep. Timed across two processes through
the documented opt-out, `numexp=4`, n=4096:

```
NEON on                  21.21 µs
TTTRLIB_USE_NEON=0       39.56 µs      -> 1.87x
```

which sits inside the 1.65–1.85× the kernel's own comment claims. Note
`kSimdMinNumexp = 2`: at `numexp=1` the dispatcher deliberately stays scalar
because NEON there measured 0.89× for `fconv_per` — a regression. A benchmark
using one lifetime therefore measures the scalar kernel by design.

**So the disposition is the entry's second option, for the first option's
reason inverted:** delete `fconv_simd` and `fconv_per_simd`, not because they
are a slow second implementation but because they are an *alias* that promises
a choice the caller does not have. `fconv` already picks the best kernel; a
separate `_simd` name tells every reader there is a scalar/vector decision to
make at the call site, and there is not. Whoever removes them should keep a
deprecating shim for one release — both names are exported and ChiSurf may
call them.

</details>

## FIXED — An exposed capability constant says NEON is not compiled in, on a build where it is

> **Fixed 2026-08-11**, taking the entry's *second* option — `%ignore` rather
> than a parallel function — and its first as well, because a caller still
> needs the answer. `TTTRLIB_COMPILE_NEON`, `TTTRLIB_COMPILE_AVX` and
> `TTTRLIB_X86_FEATURES` are now inside `#ifndef SWIG` in `info.h`, so no
> wrapper generator can see them and no binding can read a fabricated value;
> `get_neon_compiled()` and `get_avx_compiled()` answer beside the existing
> `get_neon_enabled()` / `get_avx_enabled()`, evaluated by the compiler that
> built the library. One guard covers all four bindings, since Python, R,
> Java and JavaScript each `%include "info.h"`. `get_avx_enabled()` and
> `get_neon_enabled()` now route through the new predicates rather than
> repeating the macro test, so compiled-vs-enabled cannot drift apart.
> Verified on this arm64 build: the constants are gone,
> `get_neon_compiled()` is `True`, `get_avx_compiled()` is `False`.
> `test/python/misc/test_capability_report.py` pins that no fabricated
> constant is exported, that enabled implies compiled, and that the answer
> matches `platform.machine()`.
>
> **The entry's audit suggestion was taken and found nothing else:** no other
> `#define` gated on a compiler-supplied predefined macro reaches a binding.
> `TTTRLIB_TARGET_AVX` is inside the same guarded block. This stub can be
> deleted once both sessions have seen it.

<details><summary>Original entry</summary>

**2026-08-11.** Found while settling the entry above, and it is the reason that
entry guessed wrong. `tttrlib.TTTRLIB_COMPILE_NEON` is **`0` on this arm64
machine**, where NEON is compiled in, selected at runtime, and measurably
running at 1.87×:

```python
>>> tttrlib.TTTRLIB_COMPILE_NEON     # 0   <- false
>>> tttrlib.get_neon_enabled()       # True
>>> tttrlib.sim_simd_backend()       # 'neon'
```

`TTTRLIB_COMPILE_AVX` is `0` too — correct here by luck, and wrong the same way
on x86.

**Mechanism.** `ext/python/tttrlib.i:134` does `%include "info.h"`, and
`info.h:82` gates the macro on `#if (defined(__aarch64__) || defined(_M_ARM64))`.
SWIG's *own* preprocessor evaluates that when it generates the wrapper, and
SWIG defines neither symbol regardless of the host, so it takes the `#else`
branch and emits the constant as a literal `0`. The value never comes from the
compiler that built the library. **Both constants are `0` on every platform.**

Why it matters more than a wrong number: the two spellings disagree and the
wrong one is the one a person reaches for. `get_neon_enabled()` and
`sim_simd_backend()` are *functions*, compiled into the library, and they tell
the truth. `TTTRLIB_COMPILE_NEON` is a *constant*, frozen at wrap time, and it
lies. Anyone checking "was this built with SIMD?" — exactly what the
`fconv_simd` entry above needed — reads the constant, concludes the kernels
were never compiled, and goes looking for a build bug that does not exist.

**Fix**, either way round: expose the value through a function evaluated in the
compiled library (`get_neon_compiled()` beside `get_neon_enabled()`), or
`%ignore` the two macros so nothing can read a fabricated answer. Do not leave
a constant whose value is decided by the wrapper generator. Worth an audit for
the same shape elsewhere: any `#define` guarded by a compiler-supplied
predefined macro that reaches a binding through `%include` has this defect —
`TTTRLIB_TARGET_AVX`, and anything gated on `__x86_64__`, `_OPENMP` or
`__APPLE__`, are the candidates.

</details>

## Enhancement: automated performance measurement via GitHub Actions with docs auto-update

**2026-08-08.** tttrlib needs a **continuous performance measurement** pipeline:

1. **Benchmark suite.** A set of benchmarks covering the hot paths — file I/O
   (PTO/PTU/HDF5 read), burst search, FCS correlation, decay fitting, CLSM
   assembly, DataStore read/write. These run as a dedicated benchmark target,
   not part of the unit test suite.

2. **GitHub Actions runner.** A workflow (triggered on push to `dev`/`main`
   and on PRs) runs the benchmark suite on a fixed runner environment and
   captures timing metrics. The runner must be consistent (same OS, same
   hardware class) so numbers are comparable across runs.

3. **Auto-update docs.** The performance metrics are written into the
   documentation automatically — a performance table or page (e.g.
   `doc/performance.rst` or `okf/specs/performance-baseline.md`) is
   regenerated with the latest numbers on every push. The update happens
   through a **push hook**: the workflow commits the updated metrics back to
   the branch (or opens a PR with the updated numbers), so the docs never
   drift from measured reality.

4. **Regression detection.** A benchmark that regresses beyond a threshold
   (e.g. >10% slower than the baseline) fails the CI check and blocks the
   PR. The baseline is versioned alongside the benchmarks.

5. **Relates to the GIL/non-blocking rule.** The benchmark suite should also
   verify the "logging does not degrade performance" constraint (from the ndx
   verbosity issue) and the GIL-release requirement — a test that runs a
   long tttrlib call from a thread and asserts the main thread's heartbeat
   did not stall.

### Concrete deliverables

- `.github/workflows/benchmark.yml` — runs on push/PR, executes benchmarks,
  writes metrics, detects regressions.
- `test/benchmarks/` — the benchmark scripts (Python, using `pytest-benchmark`
  or a standalone timing harness).
- `doc/performance.rst` — auto-generated performance table, updated by the
  workflow.
- A baseline file (`test/benchmarks/baseline.json`) with the reference
  numbers; updated only when a benchmark change is intentional.

## Enhancement: stabilise the ABI so development gets faster (modular compile, incremental rebuild)

**2026-08-08.** Related to PRD-027 (modular algorithm registry) and PRD-018
(ABI stability). The goal is a development loop where editing one algorithm
does not recompile the world.

Today every C++ source file compiles into one aggregate library. Touching one
function in `decay` recompiles `burst`, `fcs`, `hmm`, `pda`, `clsm`, and the
SWIG wrapper that links them all — even though nothing they depend on changed.
ccache helps but does not solve it: a header change in `core` still invalidates
every translation unit that includes it.

What to do:

1. **Stable ABI boundaries (PRD-018 completion).** Finish the work PRD-018
   started: `TTTRLIB_API` visibility markers on the aggregate library, frozen
   public headers, `SOVERSION` on the shared lib. Once the ABI is stable,
   modules can link against a prebuilt `libtttrlib_core` instead of recompiling
   it every time. The plugin C ABI (`tttrlib_plugin_init_v1`) is already
   designed for this — the gap is the intra-library C++ boundary.

2. **Modular compilation (PRD-027 Part 4).** When
   `TTTRLIB_MODULAR_ALGORITHMS=ON`, each algorithm family compiles into its own
   shared library (`libtttrlib_fcs`, `libtttrlib_decay`, etc.) that links
   against `libtttrlib_core`. Editing a decay fit recompiles only
   `libtttrlib_decay`; the core, FCS, HMM, and PDA binaries are untouched.

3. **Unity / precompiled headers.** The heaviest headers (`TTTR.h`,
   `DataStore.h`, `DecayFitModel.h`) are included by dozens of TUs. A PCH or
   unity build for the aggregate target would cut compile time significantly
   for full rebuilds (the ones ccache can't help with).

4. **Dependency isolation.** Audit the module dependency graph
   (`modules/CMakeLists.txt`). If `fcs` transitively pulls in `decay` headers
   through a chain it doesn't actually need, sever it. Fewer header deps = less
   recompilation on any change.

5. **Verify with a benchmark.** Measure: time to rebuild after touching (a)
   one `.cpp` in `decay`, (b) one header in `core`, (c) clean build. Record
   before and after. Target: (a) drops from "relink everything" to "recompile
   one TU + one module lib"; (b) does not recompile modules that don't include
   the changed header.

This is the build-system half of the modular algorithm story. Without it,
PRD-027's `TTTRLIB_MODULAR_ALGORITHMS=ON` compiles correctly but the
development loop is no faster than today.

## DONE — tttrlib naming must align with mmfdb / flrCIF

> **Closed 2026-08-10.** The rule and the reasoning are now normative in
> [`okf/specs/mmfdb-is-the-vocabulary.md`](okf/specs/mmfdb-is-the-vocabulary.md);
> what follows is the original entry with the outcome against each point.

**2026-08-08.** The names used throughout tttrlib — class names, methods,
parameters, object kinds, tag keys, file format identifiers — must be
consistent with the vocabulary used in **mmfdb** (`/Users/tpeulen/dev/mmfdb`)
and the **flrCIF** dictionary standard that mmfdb defines and exports.

What was found and done:

* **Audit the public API surface against mmfdb** — done for everything that
  reaches a *file*. tttrlib was emitting **eighteen terms mmfdb does not
  declare**: `bva`, `kde_cde`, `mle_green`, `mle_red`, `burst_fcs`,
  `hmm_photon_by_photon`, `tcspc_calibration`, `pda_histogram`,
  `companion_of`, `histogram_bin`, and seven `…4` data formats. All reconciled.
* **Align object kinds and tag keys** — done. Every `operation_type`,
  `row_grain`, `data_format` and `relationship_type` the writer or the registry
  publishes is now an mmfdb term, checked in CI from both sides.
* **Rename, don't alias** — done, and the distinction that made it tractable is
  worth keeping: a registry entry's **`name`** is tttrlib's own identifier and
  was *not* renamed; its **`operation_type`** is the controlled term and was.
  A conformance test that compares the key against the vocabulary forces the
  two to be equal, which is how the local names got into the dictionary.
* **Document the canonical vocabulary in one place** — done, and the one place
  is **mmfdb**, not here. `okf/nomenclature/mmfdb.dic` is deleted; its 113
  genuinely-new items were migrated into `mmfdb_workflow_ext.dic`. A test
  asserts this repository contains no `.dic` at all.

Not covered by this entry and still open: the *Python/C++ identifier* surface
(class and method names) was not audited against flrCIF — only the names that
cross into a file. That is a larger and much lower-risk piece of work, since an
identifier is not a term.

## Enhancement: all tttrlib functions must be non-blocking and release the GIL

**2026-08-08.** Any long-running tttrlib function — file I/O, photon
decoding, burst search, CLSM assembly, convolution, fitting — should be
**non-blocking** and **release the GIL** in the Python bindings so
concurrency isn't killed. Same principle applies to other language bindings
(Julia, R, JS) wherever they have an equivalent global lock or event loop.

What to do:

* **Python (SWIG bindings).** Ensure every C++ function that may take more
  than a trivial amount of time is wrapped with `Py_BEGIN_ALLOW_THREADS` /
  `Py_END_ALLOW_THREADS` — either via `%inline`/`%template` directors or by
  annotating the SWIG interface files (`%feature("allowthread")` /
  `PYTHON_THREAD_BEGIN` / `PYTHON_THREAD_END`). Audit every binding entry
  point, not just the obvious ones.
* **Other bindings.** Julia (`ccall` is fine, but long calls should yield),
  R (release the R eval lock for lengthy C++), JavaScript (Web Workers /
  async for wasm). Same goal: the host runtime stays responsive while the
  C++ runs.
* **Verification.** Write a test that runs a long tttrlib call from a thread
  while the main thread keeps a heartbeat alive, and asserts the heartbeat
  did not stall. This guards against regressions.

**2026-08-10, the Python half is DONE.** The wrapper is generated with SWIG
`-threads`: all ~4,600 wrapped calls release the GIL around the C++ action
(typemap code keeps it; the RAII guard reacquires during exception unwinding
before the `%exception` handlers touch the Python C-API — verified in the
generated code). The two `%extend` methods whose C++ bodies call the Python
C-API (`localization.fit2DGaussian_array` / `model2DGaussian_array`) are
`%feature("nothread")`. **Composition warning for whoever maintains
`TTTRLIB_NOGIL`:** SWIG inserts its BEGIN/END_ALLOW inside `$action` even in
a custom `%exception`, so a guard of one's own there releases an
already-released GIL — a FATAL Python error, not a no-op; the macro now
carries only the exception translation and says so, and `tttrlib_gil_release`
is `PyGILState_Check()`-tolerant. Verified: the entry's heartbeat test
(`test/python/test_gil_release.py`) passes, and the full fast suite is green
under `-threads` (2501 passed). **Still open:** the R / Java / JS bindings,
and the non-blocking (async) surface beyond GIL release.

## [chisurf] The built-in games appear to have disappeared

**2026-08-08.** **Repo:** `../chisurf`. The Games hub
(`chisurf/plugins/misc/games/`) still ships Pong, Tetris, Breakout,
Minesweeper, and Number Quest — but they are **hidden from the default
menus** by the `plugins.show_demo` flag (`demo: true` in the manifest). To a
user who does not know the flag exists, the games have simply vanished.

The games are not deleted; they are gated. But the gate is invisible and the
default is "hidden," which reads as "gone."

Fix: surface the games in a **ribbon** inside the Games hub tool — a row of
game cards shown regardless of `plugins.show_demo`, since a user who opens
the Games hub has explicitly chosen to play. The flag should keep the games
out of the production/analysis menus but not out of the dedicated gamespace.
The Doc Review Quest (PRD-91) should be a first-class entry in that ribbon.

**2026-08-10, verified resolved in substance — not by this session, so the
entry stays for its author to close.** The hub's manifest is `demo: false`,
`menu_hidden: false` (discovery confirms it lands in
`Tools:Miscellaneous:Games` with `show_demo_plugins` OFF); the hub lists all
six games unconditionally in a navigation panel — a side list of icon+name
cards rather than the literal ribbon, which reads as a design preference,
not a gap; the five arcade games are `demo: true` and hub-only, exactly the
menu split asked for; and Doc Review Quest became **Lumis Quest** (PRD-91
renamed it) and is a first-class hub entry. Pinned so it cannot silently
regress: `games/test/test_manifest.py::test_the_gamespace_survives_the_demo_flag`
fails if the hub is ever demo-gated or menu-hidden, a game leaks into the
production menus, or a game leaves the hub list.

## FIXED — [chisurf] License tracker: enumerate dependencies and compare to most permissive possible

> **Fixed 2026-08-10** (removal = fix landed, not a concurrent-write loss).
> `build_tools/license_tracker.py` generates `doc/licenses.md` (matrix on
> top) + `doc/licenses.json` (machine-readable) from `pyproject.toml`
> resolved against installed metadata, with overrides for packages shipping
> none, plus tttrlib's bundled compiled components; **no JS/wasm asset is
> bundled anywhere in the tree** (the entry's assumption, checked). The
> answer: most permissive possible is **GPL**, binding constraint
> **PyQt5/sip** — and those are GPL **v3**, so the stated `GPL-2.0` needs a
> deliberate call (2.0-or-later or 3.0 resolves it; 2.0-only cannot combine
> with v3-only deps). `--check` mode + `test/test_license_tracker.py` fail
> when a new dependency arrives unclassifiable, which is the CI regression
> guard the entry asked for. This stub can be deleted once both sessions
> have seen it.

## FIXED — ndx UI/UX: show filename, not full path, in title/header

> **Fixed 2026-08-10** (removal = fix landed, not a concurrent-write loss).
> ndX never titled its window from the opened file at all — the path on
> screen was the header's working-path field. `open_files` now titles the
> window `ndX - <filename>` (` (+N)` for multi-selection, unchanged on
> append), matching the convention the four external launchers already used,
> and the full path(s) land in the header Path field's tooltip.
> `ndxplorer/tests/test_window_title.py`, 5 cases.

## FIXED — ndx UI/UX: buttons are hard to recognize

> **Fixed 2026-08-10** (removal = fix landed, not a concurrent-write loss).
> Every icon kept its glyph and gained a one-word label: the seven emoji-only
> buttons of the main window (`📁 Browse`, `📊 Data`, `🎨 Contrast`,
> `🔄 Update`, `🧹 Clear`, `📷 Screenshot`, `💾 Save`) and the six
> single-LETTER buttons of the axis rows — `u`/`r` are now `Set`/`Auto`, and
> the three `Auto` buttons, which had no tooltip at all, say what they
> auto-range. Buttons that already paired an icon with a word were left
> alone. Sizes are unchanged (expanding rows absorb the short labels); the
> full UI test set passes with the new texts.

## FIXED — ndx UI/UX: clean up slider position display

> **Fixed 2026-08-10** (removal = fix landed, not a concurrent-write loss).
> (1) was already true of the AutoForm playback panel — the Step slider's
> value box renders to the right of the slider. (2) the persistent
> `slice X/Y · N points` info row is removed from `playback.view.json`; the
> same live readout now answers on hover of the Step row (the row, the
> slider and the value box all carry it, refreshed on every step and after
> panel rebuilds). (3) the panel loses its one always-on secondary label,
> which was the noise. Test:
> `test_playback_panel.py::test_the_readout_is_a_hover_not_a_row`. This stub
> can be deleted once both sessions have seen it.

## ndx (ndxplorer) must be a fully autoform application

**2026-08-08.** **Repo:** `chisurf/modules/ndxplorer` (handled here because
ndx lives in the ChiSurf tree). Motivation: ndx should be portable to a webapp
later. For that to be possible it must not depend on imperative GUI wiring —
every form, table, and control should be **driven by a schema/declarative
description** (an "autoform") rather than hand-built widget code.

What blocks the port today:

* Widget construction is likely hand-coded against the desktop framework
  (Qt/enaml). A webapp cannot reuse that; it needs a schema it can render.
* Data binding is probably imperative (signals/slots). A webapp needs the form
  state to be a serialisable object the frontend can read and write.
* Layout and field definitions are embedded in Python GUI code rather than in
  a description layer.

To unblock: extract a declarative form definition (field names, types,
constraints, layout) that a desktop **and** a web renderer can consume, and
have ndx render its UI from that definition. No business logic in widget
code.

## ChiSurf issues

**2026-08-08.** Issues reported against `../chisurf` are also tracked here
until the project has its own tracker. Tag the report with `[chisurf]` and
note the affected path under `chisurf/`.

## FIXED — ndx is too chatty via logging; reduce verbosity

> **Fixed 2026-08-10 in two passes** (removal = fix landed, not a
> concurrent-write loss).
>
> **The level, everywhere it was set:** ndX standalone defaults to `WARNING`
> (`-v` for INFO, `--debug` unchanged) — but embedded ndX was re-raised to
> INFO by chisurf itself, from three places, all now `WARNING`:
> `chisurf/__init__.py`'s import-time default, its settings-fallback, and
> the shipped `settings_chisurf.yaml` (`log_level: 20` → `30`; the comment
> says how to get INFO back). The stale `log_level: 20` in
> `~/.chisurf/settings_chisurf.yaml` on this machine was updated too — it
> was a copy of the old default, not a choice. A settings file that states
> a level is still honoured.
>
> **The performance constraint, measured:** the plot-update hot path makes
> 13 log calls; the f-string sites among them now use lazy `%` args.
> Interleaved-median benchmark on a 50k-point `update_plots`: **2 µs (0.8%)
> logging overhead** with logging enabled vs removed — the hot path is
> unchanged, which is what the constraint demanded. This stub can be
> deleted once both sessions have seen it.

## FIXED — ndx DataFrame Editor is slow to open `tes_chisurf_mfd.pto`

> **Fixed 2026-08-10** (removal = fix landed, not a concurrent-write loss).
> The profile said the entry's suspects were innocent: container enumeration
> and table decode take 0.12 s cold. The seconds were the EDITOR — chitable
> had retired `DataFrameSource`, ndX's import of it raised, the ImportError
> was swallowed by the standalone-fallback machinery, and every ChiSurf user
> silently got the per-cell `QTableWidget` editor (~5 s at burst-table size,
> scalar `df.iloc[i, j]` per cell). The chitable branch now adapts the frame
> through `ArraySource` with edit write-back (~0.16 s for 4.6k x 20); the
> fallback hoists the per-cell frame access and bounds the resize scan for
> standalone installs; and a guard test fails loudly if the chitable branch
> ever rots into the fallback again
> (`test_dataframe_editor.py::test_chisurf_branch_is_alive_when_chisurf_is_importable`).
> This stub can be deleted once both sessions have seen it.

## `disassemble` does not create the directories an object's name implies

**2026-08-07.** An object name is written out as a *relative path* — which is
useful, and is what ChiSurf now relies on to address a container like a folder
(`m000.pto/countrate_All 0.2000#60/bursts`). But the writer does not create the
directories the name implies, so the first name containing a separator fails:

```
PtoMfdbError: could not disassemble into /tmp/unpack:
    cannot create /tmp/unpack/countrate_All 0.2000#30/bursts
```

Worked around by walking `objects()` and `mkdir(parents=True)`-ing each name's
parent before the call. Either the writer should do that, or it should say that
a name is a flat identifier and reject a separator — the present behaviour
accepts the name and then fails on it, which is the one option that teaches
nothing.

**2026-08-11: the directory half is fixed, and it took the first of the two
options.** Verified — an object named `countrate_All 0.2000#30/bursts`
disassembles into a created subdirectory, and `a/b/c/d/deep` nests four deep.
The downstream `mkdir(parents=True)` workaround can go.

**Choosing that option is what opened the entry below.** The other option —
"reject a separator" — would have closed both. Read the two together: a name is
now a path, and nothing constrains where it points.

## FIXED — `disassemble` writes outside the directory it is given

> **Fixed 2026-08-11, same session that filed it** (removal = fix landed, not
> a concurrent-write loss). Gated at both ends as the entry prescribed, and
> the audit it asked for found a hole the fix would otherwise have left:
> `pto_add_store` lays down its own object header instead of going through
> `emit_object`, so a single writer-side check would have missed the call
> every burst table in the ecosystem is written with. Both now share one
> refusal.
>
> **And the gate alone was not enough: `tttr pto extract FILE DIR` bypassed
> it.** The CLI carried its own copy of `disassemble`'s naming and loop —
> kept, its comment said, "so the progress count matches the object list" —
> so the library was fixed and the command a recipient actually unpacks with
> still wrote outside `DIR`. Found by running the built binary against the
> hostile file rather than trusting the library test. Fixed by deleting the
> copy: `disassemble` took an optional per-path callback, the CLI passes its
> progress tick through it, and there is one implementation again. Two
> lessons worth keeping — a duplicated loop is a second place every future
> fix must reach, and a security check has to be tested at the surface a user
> touches, not only at the API beneath it.
>
> Two more things the entry did not anticipate, both found by the tests:
>
> * **The reader's check has to be a pre-pass.** Checking inside the loop
>   refused the hostile object correctly and still left the objects *before*
>   it on disk — a directory neither empty nor complete, which is the failure
>   the entry's own "refuse, don't skip" wording was aiming at. Every name is
>   now verified before anything is written.
> * **A hostile container cannot be produced by this library any more**, so
>   the reader test byte-patches a written one (equal-length name, every
>   offset stays valid) — the writer's refusal proves nothing about a file
>   somebody else wrote, which is the entire threat.
>
> `\` counts as a separator on every platform, so a Windows-shaped traversal
> is refused on POSIX too. Legitimate nested names are unaffected, verified
> against `countrate_All 0.2000#30/bursts` and four levels of nesting. Now
> normative in `doc/formats/pto.rst` (`_pto_object_names`), including that a
> name is rejected rather than sanitised. `test/python/test_pto_names.py`,
> 22 cases. **Not audited, and still worth it:** `extract` takes its path
> from the caller rather than the container so it is not exposed, but
> `add_file` / `add_sidecar_file` / `attach` were not examined. This stub can
> be deleted once both sessions have seen it.

<details>
<summary>Original entry</summary>

**2026-08-11.** **Severity: arbitrary file overwrite from an untrusted input
file.** Found while checking whether the entry above was fixed; it is the fix's
direct consequence, because creating the parent directories is what makes a
traversing name *succeed* where it used to fail.

An object name is used as a relative path and is never checked, so `..` in a
name escapes the target directory:

```python
# an object named "../victim/keep.txt", disassembled into base/unpack/
g.disassemble(str(base / "unpack"))

>>> (victim / "keep.txt").read_text()
UnicodeDecodeError: 'utf-8' codec can't decode byte 0xa0 in position 32
```

The file outside the target held ASCII before the call and holds dstore binary
after it. It was overwritten, and `disassemble` returned success with an empty
`error()`. Enough `../` reaches any path the process can write.

Measured on the same run, so the shape of the hole is on record:

| Object name | Lands at | |
|---|---|---|
| `one/two` | `unpack/one/two` | intended |
| `a/b/c/d/deep` | `unpack/a/b/c/d/deep` | intended |
| `../escape` | `unpack/../escape` | **outside the target** |
| `/abs/rooted` | `unpack//abs/rooted` | inside, by luck |

The absolute-path row is not a defence, it is a coincidence of naive
concatenation: the leading `/` collapses into a double separator instead of
resetting the root. A join that follows POSIX semantics — `os.path.join`,
`std::filesystem::path::operator/` — resets to the root and writes to
`/abs/rooted` for real. So the same names behave differently depending on how
the path is assembled, and the safe-looking row is the fragile one.

**Why this is not theoretical.** A `.pto` is an interchange container — being
passed between people is the whole point of the format, and ChiSurf addresses
containers like folders on the strength of exactly this name-as-path feature.
`disassemble` is what a recipient runs on a file they were sent. Nothing in the
writer stops a name from being written, so a container can be built with any
name at all; the entry above records that names with separators arrive from
normal analysis runs, not just crafted ones.

**Fix.** Resolve each destination and require it to stay under the target —
`weakly_canonical(out / name)` compared against `weakly_canonical(out)` — and
refuse the object naming both the file and the target rather than skipping it
quietly. Reject, do not sanitise: silently rewriting `../x` to `x` puts an
object somewhere the container did not ask for and the caller cannot predict.
A `..` component, an absolute name, and (on Windows) a drive letter or UNC
prefix should all be refused at the same gate, and the gate belongs in the
writer too, so an unwritable name cannot enter a container in the first place.

Worth a look wherever else a container-supplied name becomes a path: `extract`,
`add_file` / `add_sidecar_file` and the `attach` family take or produce names
the same way, and were not tested here.

</details>

## MOSTLY FIXED — A container's objects have no identity beyond `(kind, name)`, so a reader cannot tell two runs apart

> **Fixed 2026-08-11** for the part that caused the wrong plot, by the lighter
> of the two routes the entry proposes. No `superseded_by` edge and no
> `current` flag — those are a format change, and the entry's actual
> complaint is answered without one:
>
> * **`objects()` promising write order** is exactly what the entry asked for,
>   and is now normative in `doc/formats/pto.rst` (`_pto_object_identity`)
>   rather than a property readers were quietly leaning on. A writer appends
>   and may not reorder.
> * **`find(name)` returned the *oldest* match**, which the entry did not know
>   — it describes the workaround as "take the last object", and the library
>   call gave the first. So the most natural spelling handed back the stalest
>   analysis in the container. It now returns the newest, and "most recently
>   written wins" is normative, which is what stops two readers disagreeing
>   about what a container shows.
> * **New `find_all(name)`** returns every match in write order, so a reader
>   can see the history, or notice there is more than one at all, without
>   re-deriving newest-wins for itself.
>
> `test/python/test_pto_names.py::TestANameIsNotAnIdentity`, 5 cases,
> including one that distinguishes the runs by row count so a `find` resolving
> to the wrong object fails even if the uids line up.
>
> **Still open from this entry:** `Measurement.metadata()` returning `""` for a
> container written by `Measurement.create()` (the "related and smaller" note
> below) is untouched. And the deliberate decision worth revisiting if this
> proves insufficient: an explicit edge or flag still buys something write
> order cannot — an object that supersedes one written *before* the container
> was last compacted, or a "current" that is not the newest.

<details>
<summary>Original entry</summary>

Found driving ChiSurf's burst pipeline end to end over a `.pto` built from ten
`.spc` files: search, change one setting, search again. Each run writes an
object of kind `burst_table` named `bursts` — correctly, because the older
result is meant to stay reachable. But `PtoFile.objects()` gives a reader
nothing to *choose* between them with except tags it has to know to look up
(`_mmfdb_operation.settings_hash`), and `find(name)` resolves a name that is
not unique.

The consequence in a reader that does the obvious thing: ndX read every
`bursts` object and concatenated them side by side, lining up three unrelated
analyses of 4621, 2318 and 1099 rows against each other and padding the short
ones — no error, a plot of a mixture of three searches.

```python
import tttrlib
f = tttrlib.PtoFile(); f.open("m000.pto")
names = [(o.uid, o.kind, o.name) for o in f.objects()]
# [(…, 'burst_table', 'bursts'), (…, 'burst_table', 'bursts'), (…, 'burst_table', 'bursts')]
f.find("bursts")   # one uid, and nothing says which
```

Worked around on the reader side (take the last object of the right
`operation_type`, then only tables whose parent is that one). Two things would
make that unnecessary, and the second matters more:

* **`objects()` should promise write order.** The workaround leans on it and
  the header does not say it holds.
* **A container should be able to say which object is current for a given
  `(kind, name)`** — a `superseded_by` edge, or a `current` flag the writer
  moves. Every reader otherwise re-implements "newest wins" and they will not
  agree; a reader that guesses wrong shows old numbers with no sign of it.

Related and smaller: `Measurement.metadata()` returns `""` for a container
written by `Measurement.create()`, so nothing at the file level says what the
measurement *is* while every object below it is richly tagged.

**2026-08-11, still open, re-verified — and the workaround is on thinner ice
than the entry says.** Three stores written as `('burst_table', 'bursts')` come
back as three objects with no distinguishing field; there is no `current`,
`is_current` or `superseded_by` anywhere on `PtoObject`.

Two corrections to the notes above, from measuring rather than reading:

* **`objects()` does return write order** — the three come back in the order
  they were added. So the workaround's assumption holds today. It is still
  undocumented, which is the entry's point and remains the thing to fix.
* **`find(name)` returns the *first* match, not the last.** The entry describes
  the workaround as "take the last object of the right `operation_type`", i.e.
  newest-wins — and `find` gives the **oldest**. A reader that reaches for the
  obvious call gets the stalest analysis in the container, which is precisely
  the failure mode the entry warns about, reachable in one line and with the
  most natural spelling. Whatever resolves the identity question should settle
  `find`'s tie-break explicitly; leaving it as "first in file order" is a
  defensible answer but an undocumented and surprising one.

</details>

## FIXED — Tags are appended, never replaced, and nothing dedupes an edge

> **Fixed 2026-08-10, entry moved to the changelog** (removal = fix landed,
> not a concurrent-write loss). As the entry proposed, with the semantics the
> two call sites agreed on: `add_tag` now skips a tag identical in every field
> (two *different* parents both still land); new `PtoFile::set_tag(tag)`
> replaces by `(target, name, index)`; new `clear_tags(target, name)` removes
> one name from one object. `cmd_sm.cpp`'s local implementation now delegates
> to the API; ChiSurf's read-and-skip workaround can be deleted once it pins a
> tttrlib with this. Tests:
> `test_pto.py::test_adding_the_same_fact_twice_records_it_once`,
> `::test_set_tag_replaces_what_was_stated_before`,
> `::test_clear_tags_for_one_name_leaves_the_rest`. This stub can be deleted
> once both sessions have seen it.

## Concurrent agents silently lose each other's documentation

**2026-08-10.** Two instances working the same checkout produced three
observable kinds of damage in the shared prose files, none of which any test
catches because none of it is code:

1. **`okf/log.md` had six byte-identical duplicated sections** and five colliding
   headings (three `## 2026-08-10 (9th entry)`, two `(8th entry)`, …). A
   read-modify-write of a whole file by two writers appends both copies; the
   ordinal in the heading is chosen by counting existing entries, so two writers
   counting concurrently pick the same number. Cleaned by dropping only the
   *exact* duplicates and renumbering the day; both are safe because neither
   changes a byte of anyone's content.
2. **A `BUGS.md` entry was lost entirely** — the `pto_update_store` /
   `PtoRowCount` finding, written earlier the same day, was absent a few hours
   later. Restored from the session that wrote it. Nothing would have noticed:
   a missing bug report has no failing test.
3. **A source-of-truth decision was reverted and re-applied**, recorded in the
   log by the instance that did it: a `data_format` enumeration reduced to one
   row by one instance was "restored" by the other before it found the upstream
   mmfdb package and reverted itself.

**2026-08-10, later: the same hazard in the index.** Committing the mmfdb
vocabulary work found two more shapes of it, both of which `git commit -a` would
have swallowed:

4. **The index carried a staged change from another session** — a revert of
   `region_table` and the `spot`/`region` row-grain distinction, which that
   session had already undone in the working tree. Staged but stale, and
   invisible unless you run `git diff --cached` before committing.
5. **One file held two sessions' work, interleaved and unsplittable.**
   `mmfdb_flr_ext.dic` carried this session's five additions and another's
   396-line `_mmfdb_object` category. Hand-splicing a dictionary somebody is
   actively editing risks corrupting it, so the commit names both rather than
   quietly claiming one.

   Also: **do not branch off the default branch under a live collaborator.**
   The usual advice is to branch before committing; moving the branch pointer
   while another session works in the same checkout is worse than a local commit
   that is easy to reset.

Worth stating because the mitigation is not "be careful": (2) is invisible, (1)
is only visible if somebody reads the whole file, and (4) is only visible if you
look at the index rather than the diff you expect. What would actually help:

* **`okf/log.md` entries keyed by something not counted** — a timestamp or a
  session id rather than an ordinal, so two writers cannot collide by
  construction.
* **Append-only writes for the log**, never read-modify-write of the whole file.
* A guardrail test that fails on a duplicated `##` heading or a duplicated
  section body in `okf/log.md` — trivial, and it turns (1) from invisible to
  loud.
* **`git reset` then stage explicit paths, always, in a shared checkout.**
  Never `git commit -a`, and read `git diff --cached` before every commit
  rather than trusting that the index holds what you put there.

The code side is already handled: the registry/dictionary conformance tests
reported the half-applied vocabulary rename precisely, in both directions, which
is exactly what they are for.

## FIXED — `pto_update_store` leaves `PtoRowCount` at the old value

> **Fixed 2026-08-10, entry moved to the changelog.** Not lost to a concurrent
> write this time: the removal *was* the fix landing. Implemented as the entry
> proposed — `uint_elem_fixed` at 8 octets, the offset recorded in the slot on
> write **and on parse** (the `tttr sm` re-run is two processes, so the patch
> has to work on a reopened file), patched in `pto_update_store`. Two adjacent
> holes closed with it: a relocating `update` and `compact` both rewrote object
> headers without `PtoRowCount` at all. Tests:
> `test_pto.py::test_an_update_corrects_the_row_count_the_header_claims`,
> `::test_compact_keeps_the_row_count`. This stub can be deleted once both
> sessions have seen it.

## FIXED — Local macOS builds compile against conda's HDF5 headers and link Homebrew's library

> **Fixed 2026-08-10, entry moved to the changelog** (removal = fix landed, not
> a concurrent-write loss). As the entry proposed: with a conda env active and
> no explicit `HDF5_ROOT`, the configure now pins `HDF5_ROOT` to
> `$CONDA_PREFIX` whenever that prefix ships `H5public.h` and disables the
> config-package path, so headers and library come from the same prefix. An
> explicit `HDF5_ROOT` still wins. Verified: fresh configure resolves both
> halves to the conda prefix at 1.12.2, `build_new` relinks without the
> Homebrew dylib warning, and the two named tests pass without
> `HDF5_DISABLE_VERSION_CHECK`.
>
> **Same day, the mirror image bit chisurf:** an install into `envs/arm64`
> built with `HDF5_ROOT` hardcoded to *base* mambaforge linked base's
> `libhdf5.200` — an install name the env cannot resolve, so `import tttrlib`
> died at chisurf startup. A cross-prefix HDF5 inside a conda env is now a
> configure `FATAL_ERROR` (override: `TTTRLIB_ALLOW_HDF5_PREFIX_MISMATCH=ON`;
> conda-build exempt). This stub can be deleted once both sessions have seen
> it.

## FIXED — `pch_mixture` indexes `avg_numbers` by the length of `brightnesses`, and reads past the end

> **Fixed 2026-08-10** (removal = fix landed, not a concurrent-write loss).
> As the entry prescribed: mismatched lengths now throw
> `std::invalid_argument` naming both sizes, surfacing as `ValueError` — the
> same shape `sample_from_cdf` uses. The entry's repro raises instead of
> returning the stable-but-wrong one-species histogram.
> `test_pch_fida.py::test_mixture_rejects_mismatched_species` pins both
> directions of the mismatch and that equal lengths are untouched. ChiSurf's
> `strict=True` zip in front of the delegation can now be dropped. This stub
> can be deleted once both sessions have seen it.

## A `background` with no `background_decay` puts every background photon in micro-time channel 0

**2026-08-10.** Not an engine defect — the mechanism exists and this is its
*default*, which is the part worth arguing about.

`SimEngine` draws a background photon's micro time from the optional
`background_decay` pattern, and writes **channel 0** when the config does not
declare one (`SimEngine.cpp`, `uint16_t micro = 0`). So a config with
`"background": [0.02, ...]` and no `background_decay` produces:

```
$ tttr sim <config without background_decay> --channels 4 -o s.spc
>>> np.bincount(micro[rout == 0], minlength=4096)[:4]
array([19557,   330,   319,   302])      # 13.6% of the channel, all in bin 0
```

Uncorrelated background in TCSPC is **flat** in micro time; a spike at zero is
what *scatter* looks like. So the default silently models the wrong thing, and a
lifetime fit on such a file fits a large fake scatter component — the channel's
mean micro time comes out 2.91 ns where the emission is 3.37 ns.

Declaring the distribution fixes it, and is what
`examples/simulation/configs/mfd_2col_2pol.json` now does:

```json
"background_decay": {"pattern": [1.0, 1.0, ...], "dt": 0.008}
```
```
>>> np.bincount(micro[rout == 0], minlength=4096)[:4]
array([328, 338, 322, 306])              # flat, as uncorrelated background is
```

**The open question is the default**, and it is a decision rather than an
investigation: an unconfigured background is more honestly *flat over the laser
period* than a delta at zero, because flat is what the physics is and zero is a
different physical claim. Changing it would move every existing result that used
a background without a decay, so it needs a deliberate call — but until then,
every config that sets `background` should set `background_decay` too, and
nothing warns when one is given without the other.

**2026-08-10, the warning half is done** (the default is untouched — that
decision stays open). `SimEngine::from_json` now prints a `WARNING:` to
`std::clog` when `background` has any nonzero rate and `background_decay` is
absent, naming the consequence (a lifetime fit reads the spike as scatter) and
the flat-pattern cure. Verified through `tttr sim` in both directions;
`test_config_trajectory.py::test_background_without_decay_warns` pins it.

## Not a bug, recorded so it is not re-derived: `.pto` round-trips CLSM markers exactly

`Leica_SP5.ptu` packed into a `.pto` and read back through the container gives
byte-identical event types, marker routing counts and `CLSMImage` geometry:

```
event_types  {0: 6596261, 1: 118288}   markers {1: 59133, 2: 58924, 4: 21, 6: 210}
CLSMImage    n_frames=1 n_lines=7921 n_pixel=256 counts=443139     # both
```

The `no complete frames; salvaging 1 frame(s) with 7921 line(s)` warning that
comes with it is **not** a container problem — the raw `.ptu` produces it too.
It is a marker-configuration question in `CLSMImage` (this file's frame marker
appears 21 times and is not being used), and it makes the intensity image of a
standard fixture a 256×7921 stripe instead of 31 frames of 256×256.

---

## FIXED — `Column.numpy()` hands out a view that does not keep the `DataStore` alive

> **Fixed 2026-08-07.** Both halves of the suggested fix, because the first
> alone does not close it:
>
> * The owner is now an object that is **not** an ndarray
>   (`_DsBuffer` in `ext/python/datastore_support.py`), so numpy's chain
>   collapse stops at something that owns the buffer. The report is right that
>   the collapse then works in the library's favour: the root of every derived
>   view is the owning object.
> * **Every** way of getting a column carries the store, not just the two that
>   went through `DataStore.py`. `store.column(0)` and
>   `store.column_by_name("x")` are wrapped C++ with no link back at all, and
>   still dangled with the first half in place — they now get the same
>   `TTTRLIB_DS_KEEP_ROOT` append as `group`/`add_group`/`ensure_group`.
>
> The owner is the `Column` proxy rather than the `DataStore` the report
> suggests, and reaches the store through `Column._store`; with the second half
> above that link now always exists, so the chain
> `array → _DsBuffer → Column → DataStore` holds for every accessor.
>
> The reproduction below is a test —
> `test/python/test_datastore_paths.py::test_a_csv_column_survives_the_store_it
> _was_read_from`, run eight times as filed — beside one per accessor and one
> asserting the root of a collapsed chain still owns the buffer.
>
> Zero-copy is unchanged: writing through `np.asarray(col)` still reaches the
> C++ buffer.
>
> Kept here rather than deleted because the analysis is the useful part, and
> the same trap is one `ARGOUTVIEW` away in any other binding.

**Found:** 2026-08-07 · **Severity:** silent wrong data · **Affects:** `DataStore`
(any store, `.dstore` and CSV-read alike) · **tttrlib 0.27.0, macOS arm64,
Python 3.12, numpy 2.x**

`Column.numpy()` returns a zero-copy view into the column's buffer — which is
the point of a store, and is documented as such. What is missing is the
ownership link: **no object in the returned array's base chain owns the memory
or references the store**, so the array is a dangling pointer the moment the
`DataStore` is collected.

It does not raise. It returns plausible numbers with occasional wrong ones.

### Reproduction

```python
import gc, tempfile, pathlib, numpy as np, tttrlib

d = pathlib.Path(tempfile.mkdtemp()); p = d / "t.csv"
expected = np.arange(1000, dtype=float) * 3.0
p.write_text("a\tb\n" + "".join(f"{v}\t{v * 2}\n" for v in expected))

def read():
    store = tttrlib.read_csv(str(p), delimiter="\t")
    # np.asarray, not np.array: with a matching dtype this does NOT copy.
    return {store[i].name(): np.asarray(store[i].numpy(), dtype=float)
            for i in range(store.n_columns())}          # <- store dies here

got = read()
gc.collect()
print((~np.isclose(got["a"], expected)).sum(), "values wrong")
```

Eight runs of exactly that: `1, 1, 0, 0, 1, 1, 1, 1` values wrong. Always at
**row 2**, which the file gives as `6.0`, read back as either `0.0` or
`6.001000000000001e-05` — a partially overwritten double, i.e. reused memory.

Downstream, the same defect read **84 of 154 rows** of a burst table's
`First Photon` column as `3.3e-319` instead of `2755`.

### What the base chain shows

```python
x = store[0].numpy()
y = np.asarray(x, dtype=float)      # matching dtype

x.flags["OWNDATA"]        # False
x.base.flags["OWNDATA"]   # False   <- nothing in the chain owns the buffer
x.base.base               # None
np.shares_memory(x, y)    # True
y.base is x               # False
y.base is x.base          # True    <- numpy COLLAPSES the chain
```

The last line is why the bug is intermittent rather than constant. numpy
shortcuts a view-of-a-view to the root, so a derived array does not even keep
the array it was derived from alive — and since the root does not own the
memory either, there is nothing anywhere holding the store. Whether a given
expression corrupts is then down to allocator timing, which is the worst
possible failure mode: `store[i].numpy()` alone looked correct in every trial,
and `np.asarray(store[i].numpy(), dtype=float)` — the same memory, one extra
temporary — corrupted in six trials of eight.

### Ruled out

* **Not a parser race.** With `threads=1`, and with the array copied
  immediately, 25 threaded reads and 10 single-threaded reads of the same file
  gave zero wrong values. The data written into the store is correct.
* **Not specific to the CSV reader.** It is a property of `Column.numpy()`; the
  reader only makes it easy to hit, because the store is usually a temporary.
* **Not the documented `Column`-proxy invalidation.** That is about a *proxy*
  going stale across a structural change, and is worked around by re-fetching.
  This is the *array*, after the store is gone, with no structural change at
  all.

### Suggested fix

Give the returned array an owner: set its `base` to the Python object that keeps
the store alive (the SWIG proxy for the `DataStore`, not for the `Column` —
the column is itself borrowed), so the buffer cannot outlive its owner. numpy's
chain-collapsing then works in the library's favour rather than against it,
because the root of every derived view is the owning object.

Until then the contract is "copy or keep the store", and it has to be *said* —
the current docstring advertises the zero-copy view without the lifetime that
makes it safe.

### Workaround in use downstream

`np.array(..., copy=True)` for anything that outlives the store, plus a test
asserting the arrays survive their store. Note that `np.asarray(x, dtype=...)`
is **not** a copy when the dtype already matches, which is exactly how this got
into shipped code.

**No longer needed.** `chisurf/core/datastore.py:207` carries the warning and
the copy rule; both can go once the downstream pins a tttrlib with the fix.
The copy is not free — it is the one on the largest array in the process.

---

# Coverage gaps

Not defects. Places where something works and is verified in **one** language,
recorded because "it compiles" is not "it passes" — and the R runner proved
that distinction on 2026-08-07, failing six of eight new conformance cases that
Python had green.

## CSV options are not in the conformance suite, in any language

`test/conformance/cases/csvfile.json` has three cases and all three go through
default options:

```python
tttrlib._write_csv_native(path, store, tttrlib.CsvWriteOptions())
tttrlib.read_csv_into(store, path, tttrlib.CsvOptions())
```

So the round trip, the digits and the column order are pinned in four
languages, and **every knob is pinned in Python only**:

| Not covered cross-language | Added |
|---|---|
| `nan_rep` — what a `NaN` is written as | 2026-08-07 |
| `metadata` / `comment` — the JSON Lines block | 2026-08-07 |
| `na_rep`, `true_string`, `false_string` quoting | earlier |
| `quoting`, `float_precision`, `float_decimals` | earlier |
| `na_values`, `text_columns`, `use_float32` | earlier |

Why it matters here specifically: the metadata block is the one CSV feature
whose *point* is that another program reads the file. A binding that built the
options struct wrongly would write a file this library reads back perfectly and
nothing else does — which is exactly the failure that has no local symptom.

**What closing it looks like.** The op signatures are the work, not the cases:
`csvfile.write` and `csvfile.read` take no options today, so they need an
options argument that four runners each build. Once they do, one case per knob
is cheap. Worth doing when the next CSV option lands rather than as its own
task — the ops only need generalising once.

Nothing is known to be wrong. This records that nothing is known to be right
either, outside Python.

---

# Enhancements

Not defects — things a downstream migration needs and cannot express today.
**Rewritten 2026-08-07 from evidence rather than prediction**: the first version
of this list was written before migrating any consumers, and the migration
disagreed with it. What follows is what actually cost time.

## Landed since this list was first written

`concat` / `append_rows` / `append_columns`, `take` / `compact`, the column
lifetime fix, `container_read_records` / `container_read_events` /
`decode_records`, na-ranges and column descriptions.

**And, since this list was rewritten: the blocker below is closed.** A `Column`
now supports `col[i]`, `col[a:b]`, `col[mask]`, `list(col)`, `col[i] = x` and
all six comparisons; `DataStore.copy()` exists in C++, so all four bindings have
it. Two things came out of implementing it that the proposal did not have:

* **`column == value` was not a missing feature, it was a wrong answer.** It
  returned SWIG's identity `False` rather than raising, so a selection built
  from it matched nothing and said nothing. `>` and the other three orderings
  raised a `TypeError` and were never dangerous. That reordered the work.
* **The proposed `self.numpy()[key]` cannot be implemented literally.** For a
  text or bool column `numpy()` is a *copy*, so an integer index would decode
  the whole column to read one row — measured at 38 s per thousand accesses on
  200 000 rows against 0.6 ms for the routed form. An integer key goes through
  `string_at` / `value_at`; only a slice or an index array goes through
  `numpy()`.

What remains from the list below: a readable spelling for row selection,
group-by over a dictionary column, `argsort` / `sort_by`, and
`rename_column` / `insert_column(position)`.

**The prediction was half right.** `concat` *was* the item that changed the
shape of the migration — but only for the **file layer**. With it, one downstream
plugin (burst fusion: core, driver and view-model) went from frames to stores
end to end, and the burst reader now returns a store. That half is done and it
worked as argued.

**It was wrong about the consumer layer**, which is where the remaining cost
actually is, and the blocker there was not on the list at all.

## ~~The blocker that matters now: a `Column` is not array-like~~ — CLOSED

*Kept for the record; this is what the migration hit.* At the time,
`np.asarray(column)` and `len(column)` worked and nothing else did:

```python
column[0]          # TypeError: 'Column' object is not subscriptable
column[1:3]        # TypeError
column > 1         # TypeError: '>' not supported
list(column)       # TypeError: not iterable
```

So every consumer that touched `frame[name]` as a value has to be rewritten to
take `np.asarray` first — not because the arithmetic changes, but because the
*handle* does not behave like the thing it replaced. Counted across the files
still holding frames in the downstream package:

| Idiom that needs a column to behave like an array | calls | files |
|---|---|---|
| `col.to_numpy(...)` | 31 | 10 |
| `col[i]` / `col[a:b]` | 17 | 4 |
| `col > x`, `col == x` (building a mask) | 5 | 2 |
| `col.map(fn)` | 3 | 2 |

That is ~56 mechanical edits whose only purpose is to insert a conversion. Every
one of them is a place a reader will later ask "why is this wrapped?".

**Element access and comparison would remove almost all of it.** A column that
supports `__getitem__`, `__len__`, `__iter__` and rich comparison returning a
bool array is the difference between "swap the reader" and "rewrite every
consumer". `map` is not needed — `np.asarray(col)` plus a comprehension is
honest — but indexing and comparison are used everywhere and have no readable
substitute.

## After that, in the order they were hit

| Operation | calls | files | Note |
|---|---|---|---|
| ~~**`DataStore.copy()`**~~ | 28 | 13 | **DONE.** The copy constructor did this already and nobody could find it; it is now a named method in C++, so all four bindings have it. |
| **row selection returning a store** (`loc`/`iloc` shaped) | 36 | 8 | `take`/`compact` cover it; what is missing is a *readable* spelling at the call site. |
| **group-by over a dictionary column** | 6 | 5 | Unchanged from the first list. |
| **`argsort` / `sort_by`** | 4 | 2 | |
| **`rename_column`, `insert_column(position)`** | — | — | `insert(0, "source", …)` prepends a provenance column before writing; `add` appends only. |
| **`to_numeric(column)`** setting the mask rather than raising | — | — | Mostly evaporated: the CSV reader already types columns, so what is left is coercing text that arrived from elsewhere. |

## What the migration confirmed about the file layer

Worth recording because it was the argument for all of this, and it held:

* an `int32` column survives an **outer join** where a frame must widen to
  `float64` to hold the `NaN` and cannot recover the dtype;
* columns line up **by name**, which is what a burst folder needs — two runs
  need not have written them in the same order;
* a dtype conflict is **refused and named** rather than promoted silently;
* ranged reads compose: `container_read_records` + `decode_records` from record
  0 with a carried state gave macro times **identical** to a whole-file read on
  a 174 438-event SPC-130 file.

## `.dstore` specifically

Nothing missing for the migration: `save_store` / `load_store` already keep
column order, dtypes, dictionary-encoded text, validity masks, labels, the group
tree and the row selection. Two notes from using it:

* **It is the right default for anything only this ecosystem reads** — measured
  downstream at a wash against uncompressed HDF5 on bulk I/O and dramatically
  faster than compressed. What keeps HDF5 in the picture downstream is that the
  burst and imaging files are *interchange* formats read by other programs.
* **The column-lifetime defect above is fixed**, and `.dstore` was where it
  would have bitten hardest: `load_store` is exactly the call whose result a
  caller lets go of after pulling arrays out of it.

---

# Proposal — give `Column` the array protocol

A concrete form of the blocker above, because "make it array-like" is not
actionable on its own and the interesting part is what it should do about
masks and text.

## The change

Four dunders and rich comparison, all delegating to the buffer the column
already exposes:

```python
class Column:
    def __getitem__(self, key):        # scalar for an int, ndarray otherwise
        return self.numpy()[key]

    def __setitem__(self, key, value): # numeric only -- see below
        ...

    def __iter__(self):
        return iter(self.numpy())

    # __len__ already exists; __array__ already works.
    # __eq__ __ne__ __lt__ __le__ __gt__ __ge__ -> np.asarray(self) OP other
```

Nothing new is computed: `numpy()` is a zero-copy view for a numeric column and
already materialises a text one. This is a *handle* change, not a data change.

## Why it is worth more than it looks

It is the difference between "swap the reader" and "rewrite every consumer".
Measured on the package migrating onto `DataStore`: **~56 call sites** exist
purely to insert a conversion — 31 `to_numpy`, 17 element accesses, 5 mask
comparisons, 3 `map`s — and every one is a place a later reader asks why the
wrapping is there. The arithmetic around them does not change at all.

## Three decisions worth making deliberately

**1. Should element access honour the validity mask?** Today `numpy()` ignores
it: a masked row still returns its stored value. Measured — a column with
`set_mask([1,0,1])` returns `[1., 2., 3.]`, and the `2.` is not a measurement.

The consistent answer is that the *array protocol* returns what is stored and
says nothing about validity, exactly as `numpy()` does, and that "value or
missing" stays an explicit question (`valid(i)` / `mask_numpy()`). The
alternative — `col[i]` returning `NaN` where masked — cannot work for an
integer or text column without changing its dtype, which is the whole reason
the mask exists. **Recommend: no masking, and say so in the docstring**, since
the silent-wrong-answer risk is real and one sentence removes it.

**2. Should `col[i] = x` write through?** The numeric view is writable, so
delegation works for numeric columns and *silently loses the write* for boolean
and text ones, which decode through a copy. A write that vanishes is worse than
one that refuses. **Recommend: implement `__setitem__` for numeric dtypes and
raise `TypeError` naming the dtype for boolean and text**, pointing at the
dictionary/`set_bool` route.

**3. What does comparison return for a text column?** `np.asarray` on a
dictionary column gives an object array of Python strings, so `col == "m000.spc"`
gives an elementwise bool array — which is what a caller wants and what the
frame did. It also decodes the whole column, so it is O(n) in Python. That is
acceptable for a comparison, and worth a note: a caller filtering a large text
column repeatedly should compare `codes()` against a dictionary index instead.

## What is deliberately *not* asked for

`map`, `isin`, `groupby` on the column. `np.asarray(col)` plus a comprehension
or `np.isin` is honest, reads fine, and does not grow a second table API inside
the column. The gap being closed here is the *protocol* a numpy user already
expects, not a dataframe surface.

---

# ~~Proposal — `write_csv` should say what a `NaN` is written as~~ — DONE

**Shipped as `nan_rep`.** Two defects were found while adding it and fixed in
the same change: the null/true/false texts were written unquoted, so an
`na_rep` containing a delimiter produced a file that did not read back; and
`quoting="never"` raised a bare `KeyError`. One thing the proposal got wrong:
it assumed `nan` round-trips as a NaN value. It does not — `nan` is one of the
reader's default `na_values`, so both spellings come back masked, and the
option is about what *other* programs read.

A second concrete request, from the same migration. Smaller than the array
protocol and it removes a whole class of workaround.

## The gap

`na_rep` controls what an **invalid** (masked) value is written as. It says
nothing about a float `NaN`, which is a *value*, so it goes out as the text
`nan`:

```python
store_from_arrays({"x": np.array([1.0, np.nan, 3.0])})
write_csv(None, s, na_rep="")      # -> "1\nnan\n3\n"
```

A frame's writer produces the empty field for both, and these files are read by
programs that were written against that. So a caller wanting the old text has to
**mask every non-finite float before writing**.

## Why that workaround is worse than it looks

It is not the cost — masking 12 columns of 500 000 rows is 19.5 ms against a
732 ms write, 3%. It is that **the mask is part of the table**, so doing it in
place means *writing a table changes it*:

```
before write: has_mask = False,  valid(1) = True
after  write: has_mask = True,   valid(1) = False
```

That shipped in the downstream package and was found only by measuring this. It
is fixed there by copying the store before masking — which is a whole-table copy
on every CSV write, to express one formatting choice.

## The change

```python
write_csv(..., nan_rep=None)   # None: as now, the shortest text ("nan")
                               # "":   the empty field, what a frame writes
                               # any:  that text
```

Independent of `na_rep`, because the two are genuinely different questions: a
masked cell says *not measured*, a `NaN` says *the number is not a number* —
a fit that diverged, a ratio with no denominator. The store keeps them apart on
purpose, and CSV has one blank field for both, so the writer is exactly the
place the caller has to be able to choose.

Suggested default `None` (unchanged), so no existing file changes.

## Why not solve it downstream

It is solved downstream, and the fix is a full copy of the table per write. The
information needed — "this float is NaN" — is already in the writer's hands as
it formats each value.
