# PRD-037 — Everything tttrlib needs so ChiSurf can drop numba entirely

**Status:** 🔵 Proposed
**Priority:** requested by the user (2026-08-11): *"make a bigger plan what needs
to be there in tttrlib … the piecewise approach is super annoying."* This
replaces the file-by-file round trips with one list.
**Consumer:** ChiSurf `okf/subsystems/numba-retirement.md`. **13 files and 56
kernels remain**; the work below unblocks **8 files / 30 kernels**. The other 5
are ChiSurf's own (see *Not in scope*).
**Depends on:** nothing new. Every item follows the NumPy-typemap pattern in
`ext/python/MaxEntTcspc.i` and the rule in
`okf/bindings/marshalling-cost.md`.

## Why one PRD

Each of the last three ChiSurf files took three round trips — build, test
against a recorded fixture, find a gap, ask, wait, rebuild. The gaps were never
big; they were *unknown*, because nobody had put ChiSurf's kernel list beside
tttrlib's surface and diffed them. This document is that diff, done once.

**Everything here was checked against the installed 0.27.0 rather than assumed.**
Six route labels have been corrected this way in the last day, two of them in
the course of writing this file — see *What is already here* for the two that
turned out not to exist.

## Part A — two existing surfaces are one change each from unblocking a file

These are the cheapest items in the document and each clears a ChiSurf file
outright. Do them first.

### A1. `GopichSzabo::viterbi` needs `offsets`

`viterbi(times, colors)` has no burst boundaries, so a concatenated multi-burst
array is decoded as **one burst** — the state at the end of burst 1 propagates
across the dark gap into burst 2. `log_likelihood(times, colors, offsets)`
already takes them, so the layout is understood; it just never reached
`viterbi`.

- **Add** `viterbi(times, colors, offsets)`, decoding each burst independently.
- **Unblocks** `chisurf/core/fluorescence/burst/gopich_szabo.py` (2 kernels),
  whose `_viterbi_burst` exists only for this.
- The *other* kernel there is already unblocked: the `set_scheme` defect that
  rejected disconnected schemes **is fixed**. Verified on four schemes
  (connected, `k = 0`, one-way, 3-state with an isolated state) — all accepted,
  and the likelihoods agree with ChiSurf's numba to 6.8e-13 … 2.8e-10. ChiSurf
  still carries a warned fallback for it; that can go the moment `viterbi` lands.

### A2. The 2D-FDC micro-time gate must be `t_imax`, not `t_max`

`TK_Create2DFDC_04.m:66` (in `chisurf/junk/2D-FLC-code/`):

```matlab
if (tauI <= 0) || (tauI >= t_Imax)
```

`t_Imax = lint_Imax * lint_BinFactor` is **≥ the span**, so the method admits
photons above `tMax` whenever the span is not a whole number of linear bins.
`fdc_scan_axis` / `fdc_scan_two_axes` gate at `t_max` and so count fewer pairs:
**432 against 972** on a gate of `[15, 25]` at factor 4, 63 of 400 cells
differing.

- **Take the upper bound explicitly** rather than hard-coding `t_imax`. `t_max`
  is a physically meaningful gate and this method's `t_Imax` is a binning
  artefact; conflating them would be the wrong default for callers not
  reproducing this paper. A caller passes `fdc_t_imax(span, factor)` when they
  want the reference's behaviour.
- **Unblocks** `chisurf/plugins/fcs/flc_2d/core.py` (5 kernels). Two earlier
  gaps on this file are already closed (`fdc_scan_axis`, `fdc_scan_two_axes`,
  `fdc_t_imax`).
- **Test it with data that reaches past `t_max`.** Three of four ChiSurf fixture
  cases agree *exactly* despite this bug, because their gate is `[1, 40]` with
  no micro-times above 40. A gate difference is invisible unless the data
  reaches into the excess range, and a suite that only uses
  `lint_bin_factor = 1` never creates one.

## Part B — new kernels

Grouped by ChiSurf file. Each is genuinely serial or genuinely hot; all were
measured or inspected, not guessed.

### B1. HDBSCAN — 4 kernels (`chisurf/core/ml/cluster/_hdbscan.py`, 7 total)

**Half of this is already here**: `core_distances` and
`mutual_reachability_mst` exist and cover `_core_distances_bruteforce`,
`_edge_less` and `_prim_mst`. Those three leave by making the compiled path
**required** — no new code.

What is missing is everything downstream of the MST:

| ChiSurf kernel | What it is |
|---|---|
| `_single_linkage` | union-find with path halving over the sorted MST edges |
| `_bfs_nodes` | breadth-first walk of the dendrogram |
| `_condense` | dynamic compaction of the tree at `min_cluster_size` |
| `_label_points` | stability-based label assignment |

Measured on the *compiled* path, these four are **43% of a run**: at n=20,000,
MST 22.4 ms against single-linkage 3.9 ms and condense+label 13.0 ms; at
n=100,000, 117.6 / 25.4 / 65.1 ms. None of them is expressible in NumPy —
union-find, a tree walk and a dynamic compaction are pointer-chasing.

**Suggested surface:** one `hdbscan_labels(mst_edges, min_cluster_size)` that
does linkage → condense → label in one call, rather than four entry points. The
intermediate trees have no independent consumer in ChiSurf, and one call keeps
the loop whole.

### B2. k-means — 3 kernels (`chisurf/core/ml/cluster/_kmeans.py`)

**`OptsCluster` does not cover this.** It is 2-D Gaussian peak fitting
(`fit2DGauss`, `maxNPeaks`, `background`, `elliptical_circular`), unrelated to
k-means. The ChiSurf tracker listed it as the route; that was wrong and is now
corrected there.

| ChiSurf kernel | What it is |
|---|---|
| `_squared_distances` | point-to-centre squared distances |
| `_kmeanspp_seed` | k-means++ seeding, serial by construction (each pick depends on the last) |
| `_kmeans_lloyd` | Lloyd iteration to convergence |

**Suggested surface:** `kmeans(X, n_clusters, seed_uniforms, max_iter, tol)`
returning labels and centres — the whole fit in one call. `_squared_distances`
alone is not worth a binding: it is O(n·d) with a small payload, exactly the
shape the marshalling concept warns about.

**Determinism matters here.** ChiSurf seeds k-means++ from a caller-supplied
array of uniforms so a fit is reproducible. Keep that: do not draw randomness
inside the kernel.

### B3. Kalman — 2 kernels (`chisurf/core/fluorescence/burst/kalman.py`)

Nothing upstream covers it — `_frc_smooth` is FRC curve smoothing, not a Kalman
filter.

| ChiSurf kernel | What it is |
|---|---|
| `_inv2x2` | closed-form 2×2 inverse |
| `_kalman_filter_loop` | the filter recursion over a burst |

**Suggested surface:** `kalman_filter(...)` for the whole trace; `_inv2x2`
should be inlined inside it rather than exposed — it is three arithmetic
operations and would be pure marshalling cost as a binding.

### B4. Region segmentation — 5 kernels (`chisurf/core/roi/segmentation.py`)

Nothing upstream (`load_store_region` / `pto_store_region` are I/O).

| ChiSurf kernel | What it is |
|---|---|
| `_flood` | watershed flood from markers, with a priority queue |
| `_grow` | the flood's per-pixel step |
| `_marching_squares` | iso-contour extraction |
| `_fraction`, `_emit` | contour helpers |

**Suggested surface:** `watershed(image, markers, mask)` and
`marching_squares(image, level, vertex_connect_high)`. The three helpers are
internals of those two and should not be exposed.

**These must match scikit-image exactly** — ChiSurf's `core/roi` is documented
as skimage-exact `regionprops`, and its tests compare against skimage. A port
that is merely *correct* will fail them.

### B5. DCD de-interleave — 1 kernel (`chisurf/core/fio/trajectory/dcd.py`)

`_gather_frames` turns DCD's separate X/Y/Z blocks into interleaved
`(frame, atom, 3)`. NumPy is **7.4–21.5× slower** as a fancy-index gather and
still **2.5–9.4×** as a strided view — it is a parallel gather *with* a
transpose, the shape NumPy expresses worst.

**Scope question to settle before writing it:** this is the only item here with
no photon content, and imp-tricks has no trajectory reader either. Decide where
a molecular-trajectory reader belongs before adding a kernel for one. If the
answer is "not tttrlib", say so and ChiSurf will keep it or route it elsewhere —
that is a useful answer and costs nothing.

> **Answered 2026-08-11 by the tttrlib session (`opus-5/ac9f6757`): not
> tttrlib.**
>
> The test this library applies elsewhere is whether a thing knows what a
> photon is, and a DCD frame gather does not — no macro time, no micro time, no
> detector. `modules/math` holds kernels that fail that test too (`Cluster.h`,
> `Deconvolution.h`), but they are *numerical* kernels shared by several
> analyses in this library. A molecular-trajectory **file format** is different
> in kind: taking `_gather_frames` means owning DCD's endianness, its Fortran
> record padding, its CHARMM-vs-NAMD header variants and the next format after
> it, for one consumer, in a library whose I/O layer is otherwise entirely
> TTTR containers.
>
> That the kernel is 7.4–21.5× faster than NumPy argues for compiling it
> *somewhere*; it says nothing about where. B1–B4 all earn their place by
> subject (clustering and segmentation over the feature spaces this library
> already produces); B5 does not.
>
> Concretely, so ChiSurf is not left holding an unanswered question: keep it
> where it is, or route it to a trajectory library if one appears. This is a
> boundary answer and not a judgement about the measurement — the measurement
> is good and the kernel is worth having.

## What is already here (do not rebuild)

Verified present in 0.27.0 by import, not by memory:

`fconv`, `fconv_per_cs`, `sconv`, `fconv_ref`, `shift_lamp`, `rescale_w_bg`,
`rescale_w`, `add_pile_up_to_model`, `histogram1D_double`, `histogram1D_int`,
`decode_records`, `GopichSzabo`, `HMM` / `HmmModel` / `HmmVB`,
`hmm_forward_log` and the PRD-035 lattice, `maxent_invert`,
`solve_tcspc_mem_lifetime` and the PRD-035 MaxEnt surface, `core_distances`,
`mutual_reachability_mst`, and the PRD-036 `fdc_*` family.

**Two that are NOT what a label claimed** — both corrected while writing this:

- `OptsCluster` is Gaussian peak fitting, not k-means (B2).
- `_frc_smooth` is FRC smoothing, not a Kalman filter (B3).

## Cross-cutting requirements

These apply to every item and are not negotiable per-kernel:

1. **NumPy typemaps, never `std::vector`.** `misc_types.i`'s library-wide
   `%template(VectorDouble)` converts through the Python sequence protocol at
   ~50 ns/element. On a photon stream or an image that is the dominant cost.
   `MaxEntTcspc.i` is the worked example; `okf/bindings/marshalling-cost.md` has
   the numbers and four traps. **Note A1's `GopichSzabo` still uses
   `std::vector` and should be converted while it is open.**
2. **The loop stays whole in C++.** One call per analysis — never one per burst,
   per frame, per iteration or per column.
3. **Determinism is part of the contract** where ChiSurf currently has it: the
   k-means uniforms (B2), and the chunk-count invariance the 2D-FDC already
   guarantees (integer counts summed across chunks).
4. **`#ifdef SWIGPYTHON` any NumPy-only surface**, with an `#else` giving R,
   Java and JavaScript the plain surface. A `%ignore`/`%rename` pair outside
   that guard silently removes the function from three bindings — that happened
   to `MaxEntTcspc.i` and was caught only by a later reader.
5. **Never end an `.i` with a bare `%exception;`** — it clears the *global*
   handler and every interface included afterwards loses exception translation.
   `tools/check_exception_handlers.py` guards this.
6. **Tested by simulation with a known answer**, not only against ChiSurf's
   output. A port checked only against what it replaces cannot tell a faithful
   port from a shared mistake — PRD-035 shipped a fixture that encoded a live
   bug for exactly this reason. Where ChiSurf has a recorded fixture it is a
   *second* check, not the primary one.

## Not in scope

Five of the thirteen ChiSurf files are ChiSurf's own work and need nothing here:

- `av/static.py`, `av/dynamic.py`, `potentials.py`, `protein.py` — re-expression
  against `IMP.bff.AV` / `IMP.cgmol`, scoped as ChiSurf PRD-100.
- `av/functions.py` — a WGSL compute shader, or moot if `IMP.bff.AV` computes
  the grid.
- `burst_h2mm/core/h2mm.py` — the tttrlib HMM backend already covers it; what
  remains is ChiSurf routing two call sites that bypass its own backend selector.

## Definition of Done

- [x] A1 `viterbi(times, colors, offsets)`, plus NumPy typemaps on `GopichSzabo`.
- [x] A2 explicit upper bound on the `fdc_scan_*` gate, tested with data above
      `t_max`.
      *(Part A verified complete 2026-08-16: `GopichSzabo.viterbi(times, colors,
      offsets)` is bound and takes offsets; `fdc_scan_axis`/`fdc_scan_two_axes`
      take the explicit `t_imax` bound with `t_max` as default, pinned by
      `TestTheGateIsTheReferenceGate` — and both consumer files delegated with
      chisurf's numba removal, `f1290e84b`. What remains of this PRD is
      Part B.)*
- [x] B1 `hdbscan_labels` (linkage + condense + label).
      *(Landed as `hdbscan_condensed_tree` + `hdbscan_label_points`,
      `9e55b6b22` — two calls instead of one, split at the policy boundary:
      cluster *selection* stays with the caller. **Validated 2026-08-16**:
      the 12 in-tree tests pass; condensed trees bit-identical to ChiSurf's
      implementation across 12 dataset × min_cluster_size configurations
      (fed the same normalized-edge MST list); end-to-end labels identical
      to ChiSurf's estimator on 6 ground-truth sets (3 blobs, blobs+noise,
      bridge, single blob, moons, pure noise), sklearn's independent HDBSCAN
      agreeing exactly on 4/6 and ≥ 0.996 purity on the rest; post-MST
      1.4 ms @ n=20k / 9.1 ms @ n=100k against 72/380 ms for the Python path
      ChiSurf runs today (42–51×, ~10× over the old numba numbers). What
      remains of B1 is the ChiSurf delegation, not the kernel.)*
- [x] B2 `kmeans` with caller-supplied seeding uniforms.
      *(Kernel, binding, fixture and tests landed — this check was unticked
      briefly while the FMA-contraction contract moved out of the build and
      into source: `KMeans.cpp` and `Cluster.cpp` open with `#pragma STDC
      FP_CONTRACT OFF`, replacing the `-ffp-contract=off` CMake flag that
      silently skipped MSVC. Re-validated 2026-08-16 on a rebuild with no
      per-file flag: 7/7 configurations bit-identical to ChiSurf's
      implementation (including k=1, k=12, d=32; the formerly one-ulp-off
      restart one stays bit-exact), the committed-fixture pin green, HDBSCAN
      + cluster suites green on the same pragma-carrying build. It landed as
      `kmeans(X, n_clusters, uniforms, n_init, max_iter, tol)` returning
      `(centres, labels, [inertia, n_iter])` — one call, the whole fit, the
      uniforms the caller's stream. Six of the eight in-tree tests pass
      (known-answer blobs, bit-for-bit determinism, a committed fixture
      recorded from ChiSurf's `_kmeans.py` on a fixed stream, wrong-length
      rejection, the degenerate n_samples ≤ k case); the exactness pin is
      **validated through the fixture** — a compiler that contracts under
      default flags now fails `TestAgainstTheRecordedReference` (which
      re-measures the returned centres in Python and asserts exact equality)
      rather than silently drifting. Benchmarked regardless: 919× at n=2k,
      418× at n=10k, 904× at n=50k (C++ 1.75 s vs 26 min for the pure-Python
      path ChiSurf runs today). What remains of B2 is the ChiSurf delegation,
      not the kernel.)*
- [x] B3 `kalman_filter`.*(2026-08-17, `opencode/glm-5.3`: whole-trace
      `kalman_filter` landed in `modules/math` (`Kalman.h`/`Kalman.cpp`), the
      closed-form `_inv2x2` ported as-is so the two-channel case is bit-exact;
      ARGOUTVIEWM_ARRAY3 binding in `ext/python`, `ext/r`, `ext/js`, Java
      excluded with the parity exception (jarrays.i marshals no argout of any
      rank); `test/python/misc/test_kalman.py` with a known-answer simulation, a
      bit-for-bit fixture recorded from ChiSurf's `kalman.py`, and shape/error
      cases; validated bit-identical (fixture passes at -O0/-O1/-O2/-O3 and
      over 50 randomised traces against ChiSurf's loop). The dim==2 BLAS
      insight: numpy's `@` for 2×2 forms `a0*b0 + a1*b1` as
      `std::fma(a1, b1, a0*b0)` — plain left-to-right disagrees ~44% of the
      time, and the port reproduces the fused-second-product form. Benchmark:
      **195× across T = 5k/20k/50k** (37.6 ms → 0.19 ms at 5k; 372.6 ms →
      2.0 ms at 50k) vs the pure-Python path ChiSurf runs today. dim>2 is
      deliberately not bit-parity (own GE inverse vs ChiSurf's LAPACK); the
      divergence is documented in the header and README, tests are dim==2.)*
- [x] B4 `watershed` + `marching_squares`, matching scikit-image exactly.
      *(2026-08-17, `opencode/deepseek-v4-flash-free`: both kernels landed in
      `modules/math` (`Watershed.h`/`Watershed.cpp`), binding in
      `ext/python/Watershed.i` with the NumPy IN_ARRAY2/ARGOUTVIEWM_ARRAY2
      typemaps, `ext/r`/`ext/js` include it, Java excluded with the parity
      exception (jarrays.i marshals no argout of any rank and there is no
      `_into` helper shape). The contract is **skimage 0.25.0, not ChiSurf** —
      ChiSurf's `_flood` seeds its queue at `image[marker]` where skimage
      pushes `-inf`, and its marching-squares case bits swap the lower row and
      invert the ambiguous squares; both divergences were measured against the
      installed skimage and settled in skimage's favour (0 diffs vs skimage on
      watershed across seeds 0–9 and connectivity 1/2 after the `-inf` change;
      the case table matches skimage's cython bit for bit in order). The
      committed fixture `test/data/reference/watershed_skimage_reference.npz`
      is recorded from skimage 0.25.0 for that reason — a "reference" from
      ChiSurf would fail its own pin. `test/python/misc/test_watershed.py`:
      18 tests, known-answer simulations, fixture bit-exactness (label image
      for both connectivities, with and without mask; marching-squares
      segments in raster order for 2 levels × 2 vertex_connect_high, plus the
      NaN-corner skip case), a live skimage sweep over 6 seeds × connectivities
      / levels × vch that skips when skimage is absent, and error cases
      (shape mismatch, connectivity out of range, <2×2 marching-squares
      input). Benchmark vs the pure-Python path ChiSurf runs today:
      watershed **97–108×** (6.9 ms vs 745 ms at 256²; 35.9 ms vs 3475 ms at
      512²), marching squares **219–240×** (0.9 ms vs 208 ms at 256²; 3.8 ms
      vs 832 ms at 512²). What remains is the ChiSurf delegation
      (T-20260811-20's roi/segmentation.py item), not the kernels.)*
- [x] B5 decided — declared out of scope 2026-08-11: not tttrlib. *[ticked
      2026-08-17 — the decision block above is the answer.]*
- [ ] Every item: NumPy typemaps, one call per analysis, a simulation test with
      a known answer, and the four-language guard.
- [ ] ChiSurf's allow-list drops to **5** (the *Not in scope* files).
