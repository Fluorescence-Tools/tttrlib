# tttrlib performance

Single source of truth for tttrlib benchmark results: how it compares to other
open-source tools, and how speed and memory evolve across releases.

Every number here is **CPU-only** (no GPU on the tttrlib side), best-of-N wall
time, measured on identical input files on one machine. The harness that
produces them lives in [`benchmarks/`](benchmarks/); raw per-run records are in
`benchmarks/results/*.jsonl` and per-task charts in `benchmarks/plots/`.

## Reference environment

- **Apple M1 Pro** (6 performance + 2 efficiency cores), 16 GB, macOS 26.5.
- Python 3.10 (base) / 3.12 (FLIMKit); **tttrlib 0.27.0**, flimlib 2.2.5,
  ptufile 2025.5.10, FRETBursts 0.8.3, PyBroMo 0.8.1, FLIMKit git-main,
  pycorrelate 0.3, H2MM_C 1.0; birfi, BrightEyes-ISM and s2ISM from their
  upstream repositories on 2026-08-17 (birfi 0.1.1, brighteyes-ism 1.5.1,
  s2ism 0.2.0; torch 2.13 CPU); scikit-image 0.25.2, scikit-learn 1.7.2,
  filterpy 1.4.5, hmmlearn 0.3.3, phasorpy 0.4 (`sciref` venv).
- tttrlib built with its runtime-dispatched NEON kernels + OpenMP, no GPU.

Numbers are hardware-specific. Re-run on your machine for local figures; the
stable quantity is the **ratio**, not the absolute time.

## vs. other tools

Each competitor runs in its own isolated environment but consumes the *identical*
input that tttrlib does (shared inputs are written to `benchmarks/results/shared/`
by the tttrlib harness and loaded by every competitor), so wall-clock times are
directly comparable.

![tttrlib speedup vs competitors](benchmarks/plots/summary_speedup.png)

| Task | tttrlib | Best competitor | Result |
|------|--------:|----------------:|:-------|
| **Single-curve lifetime fit** (one detector) | **0.22 ms** | flimlib LMA 2.43 ms | **11×** |
| ↳ batched (`fit_many`, per fit) | **0.07 ms** | flimlib LMA batch 2.29 ms | **33×** |
| **H2MM** photon-by-photon HMM (Baum–Welch) | **116 ms** (SQUAREM) · 403 ms (plain EM) | H2MM_C 914 ms · chisurf-numba 491 ms | **7.9× vs C ref** (2.3× plain) |
| Correlation / FCS (multi-tau) | **176 ms** | pycorrelate 1801 ms (direct) | **10.2×** |
| Diffusion simulation (coasting) | **0.53 s** | PyBroMo 2.40 s | **4.5×** |
| Burst search | **2.6 ms** | FRETBursts 13.8 ms | **5.3×** |
| Per-pixel reconvolution MLE (CPU) | **140 ms** | FLIMKit **GPU** 1770 ms · CPU 1194 ms · flimlib LMA 211 s | **6.3× vs GPU**, 5.3× vs its CPU |
| **Single-molecule localization** (2D Gaussian PSF) | **0.19 ms** (1 emitter) · 0.62 ms (3) | scipy `least_squares` 1.23 ms · 2.39 ms | **6.5×** · **3.9×** |
| Fast lifetime (moments) map | **28.1 ms** | flimlib RLD 43.9 ms | **1.6×** |
| ↳ re-tune IRF on an already-built map | **0.11 ms** | flimlib RLD 43.9 ms (recomputes) | **~400×** |
| CLSM intensity image | **19.8 ms** | ptufile 22.8 ms | **1.15×** |
| TTTR file reading | **26.4 ms** | ptufile 25.8 ms | **≈1.0×** (I/O-bound) |
| ↳ PTU / HT3 / SPC-130 vs phconvert | **23.0 ms** / **90.8 ms** (15.6 M photons) / **1.55 ms** | phconvert 134 ms / 2238 ms / 6.8 ms | **5.8× / 25× / 4.4×** — photon-for-photon identical |
| ↳ SPC-630 (256 ch) / SPC-QC / `.sm` vs phconvert | **1.6 ms** / **2.1 ms** / **12.1 ms** | phconvert 6.3 ms / 7.8 ms / 18.1 ms | **4.0× / 3.6× / 1.5×** — identical (SPC-630 up to phconvert's own overflow-shift defect) |
| ↳ PicoHarp T3 PTU vs ptufile | 3.8 ms (723 k records) | ptufile 2.1 ms | 0.55× — identical; a 3 MB file where the fixed costs (header JSON, array hand-over) dominate; on the 19 MB HydraHarp file the two tie |
| **Blind IRF estimation** (BIRFI, 25 ch × 1024 bins, 500 RL it.) | **780 ms** | birfi (torch, CPU) 3039 ms | **3.9×** |
| **ISM adaptive pixel reassignment** (25 el. × 256², usf 10) | **40.4 ms** | BrightEyes-ISM APR 153 ms (`fourier`) · 204 ms (`interp`, default) | **3.8×** · 5.0× — identical output |
| **Focus-ISM** (25 el. × 64²) | **30.3 ms** | BrightEyes-ISM focusISM 8806 ms | **291×** |
| **s2ISM** (25 el. × 3 planes × 129², 30 it.) | **685 ms** | s2ISM (torch, CPU) 2479 ms | **3.6×** — identical output |
| **Watershed** (1024², 200 markers) | **124 ms** | scikit-image 0.25.2 174 ms | **1.4×** — identical labels |
| **Marching squares** (1024², one level) | **3.1 ms** | scikit-image segments 10.9 ms | **3.5×** — identical segments, in order |
| **Richardson–Lucy** (512², 15² PSF, 30 it.) | **203 ms** | scikit-image 358 ms | **1.8×** — identical (4e-15) |
| **k-means** (n=200 k, d=8, k=10; k-means++ + Lloyd) | **29.6 ms** (seed 21.4 + Lloyd 8.2) | scikit-learn 176 ms (k-means++ + Lloyd) · 24.0 ms (Lloyd only, given init) | **5.9×** same job · **2.9× on Lloyd** — identical centres/labels/inertia |
| **HDBSCAN** (n=20 k, d=4) | **72.7 ms** | scikit-learn HDBSCAN 1548 ms | **21×** — identical partition (ARI 1.0) |
| **Kalman filter** (50 k steps × 2 ch) | **2.2 ms** | filterpy 1140 ms | **510×** — identical (5e-16) |
| **HMM lattice** (T=200 k, K=4: forward + posteriors + Viterbi) | **60.9 ms** | hmmlearn `_hmmc` 113 ms | **1.8×** — identical (log-prob, posteriors 4e-16, paths) |
| **VB-HMM** to convergence (200 dense chains, 49 780 ticks, K=3) | **128 ms** | hmmlearn `VariationalCategoricalHMM` 1698 ms | **13×** — `elbo` = hmmlearn's bound at tttrlib's posterior (2e-10); posteriors 1e-4 |
| **Phasor** (100 k decays × 256 bins) | **7.3 ms** | phasorpy 25.2 ms | **3.4×** — identical (0.0) |
| **1-D max-tree** (component tree, 2 M samples, 1024 levels) | **38.5 ms** | scikit-image `max_tree` 428 ms | **11×** — identical component set (1.9 M components) |
| **PDA** S1/S2 histogram (nmax 180, 3 species) | **0.31 ms** | PAM `PDA_histogram.cpp` (native build) 1.43 ms | **4.7×** — identical (2e-18) |
| **BurstML** likelihood (187 bursts × 20 param. sets) | **265 ms** | original FRET_burstML MEX (native, GSL) 2194 ms | **8.3×** — identical (3e-13) |
| **FRET-2CDE** (Laplace KDE, 200 bursts × 120 ph.) | **2.15 ms** | FRETBursts `kde_laplace` + Tomov formula 9.0 ms | **4.2×** — identical (6e-15) |
| **2D-FDC** log matrices (4000 photons × 3 lags) | **2.9 ms** | `TK_Create2DFDC_04.m` in Octave 13.6 s | **~4700×** — identical counts |
| **CUSUM/SPRT burst search** (3.3 k photons) | **0.04 ms** | PAM `CUSUM_burstsearch` in Octave 61 ms | **~1400×** — behavioural (Jaccard ≥ 0.87) |

Run 2026-08-09. The per-pixel `fit_map` is now **140 ms** — a 3.3× improvement
over the 456 ms baseline — from three changes: (1) allocation-free `FitWorkspace`
inner loop in `DecayFitNExp.cpp`, (2) buffer-based `fit_batch_flat_buffers` SWIG
binding that eliminates the Python→`std::vector` element copy, and (3) a
stacked-moment cache that avoids allocating 41 MB of per-frame buffers per
mean-lifetime map (28.1 ms vs 39.6 ms). The CPU fits now beat FLIMKit's MLX GPU
by **6.3×** on per-pixel MLE.

Run 2026-08-17 (VicidominiLab rows). The four kernels ported from or validated
against VicidominiLab code — `blind_irf_estimate` (birfi), `shift_vectors` /
`apr_reconstruction` (BrightEyes-ISM `APR_lib`), `focus_reconstruction`
(`FocusISM_lib`), `s2ism_reconstruction` (s2ISM) — are benchmarked against the
upstream packages themselves in the `vicidomini` venv (`build_envs.sh`; torch
CPU), on identical inputs written by `bench_vicidomini.py`; see *The
VicidominiLab kernels* below for the output-identity checklist that goes with
the numbers. Getting APR faster than the reference took two changes: the
per-element work now runs in OpenMP, and the reassignment is the reference's
own circular Fourier shift (it had been on a canvas zero-padded to twice the
frame — 4× the FFT work — which is also why its output only matched the
reference away from the edges). Focus-ISM keeps a zero-padded margin (now a
few times the shift instead of half the frame) because `focusISM` reassigns
with the zero-filled `interp` mode before fitting; a wrapped border row would
move its background split at the frame edge.

Run 2026-08-17 (scientific-Python rows). The general kernels are benchmarked
against their upstream references in the `sciref` venv (scikit-image 0.25.2,
scikit-learn 1.7.2, filterpy 1.4.5, hmmlearn 0.3.3, phasorpy 0.4) with
`check_sciref.py` confirming identical outputs on the benchmark inputs — all
eight identical. Three changes came out of it: watershed follows current
upstream's marker seeding (skimage 0.25.1 reverted 0.25.0's `-inf`; 13 % of
the pixels of the benchmark image change basin between the two, and tttrlib
had pinned 0.25.0); k-means assigns points in parallel while every sum stays
serial and in order (bit-exact with ChiSurf) and the k-means++ seeding replaces
its per-trial linear scans with one prefix array + binary search (same
numbers); Richardson–Lucy threads its pocketfft transforms. A batched phasor
binding (`compute_phasor_bincounts_batch`) was added so a decay stack is one
call rather than a per-decay loop that measures the binding, not the kernel.

Run 2026-08-17 (FRET / burst rows). The kernels whose upstream code is a
MEX source, a MATLAB file or FRETBursts are timed against exactly that:
PAM's `PDA_histogram.cpp` and the original FRET_burstML `mlhDiffNTRbkg_MT.cpp`
compiled natively (timing drivers in `benchmarks/competitors/native/`, clock
inside the process so startup does not count), Toru Kondo's
`TK_Create2DFDC_04.m` and PAM's `CUSUM_burstsearch` in Octave (`tic`/`toc`
around the call), FRETBursts' cython KDE in its venv. `check_fret.py` confirms
the outputs; the Octave ratios are what an interpreted double loop against a
compiled kernel looks like and are reported for completeness, not as a claim
about MATLAB.

Datasets: 3.5 M-photon HydraHarp T3 PTU (reading, burst search, correlation),
512×512 confocal PTU (CLSM intensity), 256×256 FLIM HT3 (lifetime maps),
synthetic 256-bin decay (curve fits), simulated 3-state 200 k-photon trace
(H2MM), 20 molecules / 1 s with matched D, box and PSF (simulation).

### The VicidominiLab kernels — speed AND identity, checked together

`benchmarks/check_vicidomini.py` runs after both sides and compares the outputs
on the benchmark inputs themselves (`results/shared/vicidomini/check.json`);
the same comparisons on smaller data are the permanent A/B tests
(`test_ab_decay_reference.py::TestBlindIrfAgainstBirfi`,
`test_clsm_superres_ism_arrays.py`, `test_clsm_superres_s2ism.py`).

| Kernel | Reference | tttrlib | Speedup | Output vs reference | Checked |
|---|---|--:|--:|---|:-:|
| `shift_vectors` | `APR_lib.ShiftVectors` | (part of APR) | — | **bit-identical** (max diff 0.0) | ✅ |
| `apr_reconstruction` | `APR_lib.APR(mode='fourier')` | 40.4 ms | 3.8× (5.0× vs default `interp`) | **identical**, 3e-16 relative | ✅ |
| `s2ism_reconstruction` | `s2ISM.max_likelihood_reconstruction` | 685 ms | 3.6× | **identical**, 2e-9 relative (reference is float32); its `max_iter=n` runs n+1 updates, so it is called with n−1 | ✅ |
| `blind_irf_estimate` | `birfi.Birfi.run` | 780 ms | 3.9× | same model (shared rate, per-channel A, C; RL) — birfi fits it with Adam (not converged), tttrlib solves it: IRFs correlate ≥ 0.9947 per channel after undoing birfi's n/2 `ifftshift` roll; against the **truth** min. corr tttrlib 0.9959 vs birfi 0.9932 | ✅ (at tolerance, ≥ reference accuracy) |
| `focus_reconstruction` | `FocusISM_lib.focusISM` | 30.3 ms | 291× | same split; the reference registers with its `interp` spline and fits every micro-image with `scipy.optimize.curve_fit`; against the **truth** the mean absolute background-fraction error is 0.084 (tttrlib) vs 0.083 (reference), the two disagree by 0.011 | ✅ (at tolerance, = reference accuracy) |

Why the two "at tolerance" rows are not identical: birfi's Adam fit of the
decay rate stops after 1000 steps wherever it is (5–38 % off on the A/B
fixtures) and Richardson–Lucy forgives it; matching that would mean copying a
non-converged optimiser. focusISM's per-pixel `curve_fit` and spline
registration are likewise not something to reproduce digit for digit; the
recovered physics (the background fraction map) is what is compared, and it is
the same to within noise. Both are documented in the tests and in
[`okf/testing/algorithm-validation.md`](okf/testing/algorithm-validation.md).

### Reading vs phconvert — identity checklist

`benchmarks/check_reading.py` (`results/shared/reading/check.json`), 2026-08-17,
against phconvert 0.10.1 in the `read` venv (the A/B in
`test/python/test_ab_core_reference.py` pins the same comparisons):

| File | Records | Output vs phconvert | Checked |
|---|--:|---|:-:|
| `pq_ptu_hh_t3.ptu` (HydraHarp T3) | 3 506 476 photons | macro/micro/channel identical | ✅ |
| `pq_ht3_clsm.ht3` (HydraHarp T3, CLSM) | 15 583 897 photons + 20 533 markers | photons and marker times identical | ✅ |
| `bh_spc132.spc` (Becker & Hickl SPC-130) | 183 657 photons | identical | ✅ |
| `bh_spc630_256.spc` (SPC-600/630, 32-bit records) | 294 884 photons | channels and ADC identical; macro times identical once phconvert's 2^12-per-overflow shift (a 17-bit field: 2^17) is undone — phconvert's timestamps run backwards 61× on this file | ✅ (bounded) |
| `bh_spcqc004.spc` (SPC-QC-104) | 32 644 photons | identical (pass phconvert an open file: a path re-reads the header word as an overflow) | ✅ |
| `data.sm` (Weiss-lab .sm) | 2 060 245 photons | identical | ✅ |
| `Example_PTU_PicoHarp.ptu` (PicoHarp T3) vs ptufile | 722 402 photons + 513 markers | identical (tttrlib keeps the 1-based channel field, ptufile 0-based) — **after fixing the PHT3 special-record logic 2026-08-17** | ✅ |

### The scientific-Python kernels — identity checklist

`benchmarks/check_sciref.py` (`results/shared/sciref/check.json`), 2026-08-17:

| Kernel | Reference | Output vs reference | Checked |
|---|---|---|:-:|
| `watershed` | `skimage.segmentation.watershed` 0.25.2 | 0 differing pixels of 1 048 576 | ✅ |
| `marching_squares` | `skimage.measure._find_contours_cy._get_contour_segments` | 31 464 segments equal, in raster order | ✅ |
| `richardson_lucy_2d` | `skimage.restoration.richardson_lucy` | 3.5e-15 relative | ✅ |
| `kmeans` | `sklearn.cluster.KMeans(lloyd)` from the same k-means++ centres | centres 9e-15, labels equal, inertia 2e-15 | ✅ |
| HDBSCAN pipeline | `sklearn.cluster.HDBSCAN` | same partition, ARI 1.0, 11 clusters both | ✅ |
| `kalman_filter` | `filterpy.kalman.KalmanFilter` (Joseph-form update) | x/P/D ≤ 5e-16 | ✅ |
| `hmm_forward_log` / `hmm_backward_posteriors_xi` / `hmm_viterbi_log` | `hmmlearn._hmmc` | log-prob 0.0, posteriors 4e-16, xi 5e-12, Viterbi paths equal | ✅ |
| `compute_phasor_bincounts_batch` | `phasorpy.phasor.phasor_from_signal` | g, s 0.0 | ✅ |
| `max_tree_1d` | `skimage.morphology.max_tree` (component set derived from its parent array) | (level, lo, hi, parent level, parent lo) identical, 1 913 790 components | ✅ |
| `fit_vb` (dense stream, dt = 1 → categorical VB-HMM) | `hmmlearn.vhmm.VariationalCategoricalHMM` 0.3.3, Dir(1) priors, same posterior seed | hmmlearn's lower bound at tttrlib's converged posterior = the sub-stochastic (Beal) bound to 2e-10; posterior α to 1.1e-4 rel (engine iterates on the row-normalised Ã, hmmlearn on Ã; hmmlearn's own optimum bound is 1e-5 nat higher); the iteration's `elbo_normalised` − hmmlearn bound = 2.99906 = K(K−1)/2; `elbo` is the bound | ✅ |

### The FRET / burst kernels — identity checklist

`benchmarks/check_fret.py` (`results/shared/fret/check.json`), 2026-08-17:

| Kernel | Reference | Output vs reference | Checked |
|---|---|---|:-:|
| `Pda.s1s2` | PAM `PDA_histogram.cpp` (native) | 2e-18 abs on the 181² matrix | ✅ |
| `BurstML.neg_log_likelihood` | FRET_burstML MEX (native, GSL) | ratio 1 ± 3e-13 over 20 parameter sets | ✅ |
| `TwoCDE` FRET-2CDE | FRETBursts `kde_laplace` + Tomov | 6e-15 relative, 200 bursts | ✅ |
| `fdc_scan_log` | `TK_Create2DFDC_04.m` (Octave) | 389 185 pair counts, 0 cells differ | ✅ |
| `burst_search_cusum_sprt` | PAM `CUSUM_burstsearch` (Octave) | 3 bursts each, min Jaccard 0.87 — behavioural by construction (PAM's discretisation, α = 1/N, offset heuristics) | ✅ (behavioural) |

### Steps taken to improve FLIM performance (0.27 → working tree)

The per-pixel reconvolution MLE path (`fit_map`) went from 456 ms to 161 ms
(2.8×) through three changes, listed in the order they were found and applied.

**1. Allocation-free MLE inner loop** (`DecayFitNExp.cpp`)

The `fit()` function optimises free lifetimes by coordinate-wise Brent
minimization. Each Brent trial calls `evaluate_profile`, which allocated ~6
`std::vector<double>` objects (convolved components, EM probability, EM
weights, next/prev buffers, ProfileResult members) on every call. A single
mono-exponential fit with a 24-point grid scan runs ~26 `evaluate_profile`
calls per outer iteration × 20 outer iterations = **~520 vector allocations
per fit**, each costing a malloc/free pair.

The fix adds a `FitWorkspace` struct that pre-allocates all scratch buffers
once per `fit()` call. Three new functions use it:

- `fill_component` — writes a convolved, normalized exponential component
  directly into pre-allocated workspace memory (replaces `convolved_component`)
- `profile_amplitudes_ws` / `compute_nll_only` — EM amplitude profiling with
  workspace buffers; the `_nll_only` variant skips the `ProfileResult.weights`
  and `.probability` vector copies entirely when the caller only needs the NLL
  (which is the case inside the Brent objective)
- `evaluate_profile_ws` / `evaluate_nll_ws` — wire the workspace into the
  component-fill + EM pipeline; the `_nll` variant returns a bare `double`

After this change the inner loop does **zero** heap allocation. The
`evaluate_profile_ws` path (full ProfileResult) is still used for the initial
and final evaluations where amplitudes/model are needed.

**2. Buffer-based batch SWIG binding** (`fit_batch_flat_buffers`)

`fit_batch_flat` takes `const std::vector<double>& data_matrix`. SWIG converts
a NumPy array to this by iterating element-by-element in Python — for a
256×256×256 image (46k valid pixels × 256 bins = 11.8 M doubles) this copy
alone cost ~400 ms, **dwarfing** the C++ compute.

The fix adds `fit_batch_flat_buffers` with `IN_ARRAY2` SWIG typemaps on the
`(const double* bfdata, int n_bfrows, int n_bfcols)` parameter triplet. NumPy
arrays now pass as raw pointers with a single pointer acquisition and no
element copy. The Python `FitNExp.fit_many` calls this path instead.

This was the single biggest win: `fit_map` went from ~600 ms (dominated by
SWIG marshalling) to ~200 ms (dominated by C++ compute).

**3. `FitNExp` Python wrapper** (`ext/python/FitNExpWrapper.py`)

The benchmark harness and examples reference `tttrlib.FitNExp(dt=..., irf=..., 
...)` with `__call__`, `fit_many`, and `fit_map` methods. No such class
existed — the C++ `DecayFitNExp` has only static methods. The wrapper holds
the instrument description (IRF, dt, period, bounds) as instance state and
delegates to the optimized C++ API, using `fit_buffers` for single curves and
`fit_batch_flat_buffers` for batch/image paths.

**What was NOT changed**

- The convolution kernels (`fconv_per_cs` and its NEON/AVX variants) are
  already SIMD-optimised with runtime CPU dispatch; no further gains there.
- The CLSM image paths (`fill`, `get_intensity_masked`, `get_mean_lifetime`,
  `get_fluorescence_decay`) were already optimised in 0.27 (lazy stream masks,
  cached moments, fused mask scans); no regression was found.
- The EM algorithm itself is unchanged — the same iterations, same convergence
  criteria, bit-identical results (102/102 decay-fit tests pass).

### Exact gradients in the localization fit

`fit2DGaussian` is the only consumer of `i_lbfgs` that uses analytic gradients:
one forward-mode automatic-differentiation pass replaces 2N central differences,
and the bounds were reparameterised smoothly at the same time (the previous
handling *teleported* an out-of-range parameter to the middle of its range,
which made the gradient meaningless near a bound). Measured before → after, on
the same noise-free image:

| model | central differences | AD + smooth bounds | |
|---|--:|--:|--:|
| 1 Gaussian, 1 emitter | 0.517 ms | 0.196 ms | **2.6×** |
| 2 Gaussians, 1 emitter | 1.788 ms | 0.416 ms | **4.3×** |
| 3 Gaussians, 3 emitters | 3.279 ms | 0.352 ms | **9.3×** |

The AD gradient agrees with central differences to 2.5e-08 relative — the
finite-difference error floor — for all three models.

**`DecayFit24/25/26` and `FitNExp` still use central differences.** The
gradient hook defaults to null, so the curve-fit and per-pixel-MLE rows above are
unaffected by this work; converting `DecayFit24` and `DecayFit26` was measured
and declined in both cases -- `DecayFit24` (N=5) at no clear net win, `DecayFit26`
(N=1) at 1.57x, the templating costing more than the gradient saves. See PRD-010.

### Exact gradient in DecayFit23's general (tau/gamma) branch

`DecayFit23`'s bounds were already reworked onto a single `set_bounds` mechanism
(PRD-010 Phase 5c/5d) specifically so an analytic gradient could be registered
without missing the bound's own contribution -- that groundwork is what made this
conversion a templating exercise rather than a redesign. `fconv_per_cs_ad<T>`
(`DecayConvolution.h`) is a second, `template<T>` scalar body for the periodic
convolution, alongside (not replacing) the runtime-dispatched NEON/scalar kernel;
`Wcm_ad`/`log_m_ext_ad` (`DecayStatistics.h`) and `soft_floor_ad`/`clamp_value_ad`
(`DecayFit.h`) are templated the same way. `Wcm_p2s`'s series expansion is not
templated, so the gradient is only registered when `fit_settings.p2s_twoIstar` is
off; central differences remain the fallback there.

Measured A/B (`benchmarks/bench_decayfit23_ad.py`, `benchmarks/bench_decayfit23_batch_ad.py` --
build once with the gradient registered and once with that call commented out,
`pip install -e .` between the two):

| path | central differences | AD | |
|---|--:|--:|--:|
| one `Fit23(...)` call through Python/SWIG, best of 3000 | 0.187 ms | 0.147 ms | **1.27×** |
| `fit_many`, 8000 rows, wall clock (parallel_for, all cores) | 2595 ms | 1540 ms | **1.68×** |
| `fit_many`, 8000 rows, total CPU across worker threads | 3721 ms | 2603 ms | **1.43×** |

The single-call number is diluted by SWIG list marshalling that has nothing to do
with the gradient; `fit_many` crosses into C++ once per batch and lets every row
pay only the fit cost, which is why its speedup is larger. Fitted values (median
tau, mean objective) were identical before and after at 8000 rows -- the speedup
is not bought with accuracy, and the existing `test/python/decayfit/` suite (109
tests, including the pinned `test_fit23` reference) still passes.

`fit_many` (`DecayFitModel.cpp`) parallelises over rows once a batch reaches 1024
rows with a hand-rolled thread pool (`tttrlib::parallel_for`, not OpenMP), and
`DecayFit23.cpp`'s `thread_local` `fit_signals`/`fit_corrections`/`fit_settings` --
which the new gradient callback reads the same way `targetf` always did -- give
each worker its own copy, so this is safe under that parallelism with no new
locking. `fit23` is documented as both a burst fit and a low-photon per-pixel FLIM
tool, but the FLIM competitor benchmark above exercises `FitNExp`, not `fit23`, so
this conversion does not move any number already published in the table above.

### DecayFit24: an exact gradient was tried, tested, and declined

`DecayFit24` (two-lifetime model, N=5: tau1/gamma/tau2/A2/offset) got a small,
kept fix -- `tau1`/`tau2` moved to `soft_floor`, matching `DecayFit23`'s `tau` --
and then a full AD conversion the same shape as `DecayFit23`'s, reusing
`fconv_per_cs_ad`/`Wcm_ad` unchanged. It was verified correct (value to machine
precision, gradient to 2.8e-8 relative against central differences,
`test/cpp/test_ad_gradient.cpp`), and then measured:

| metric | central differences | AD (tried), two runs | ratio |
|---|--:|--:|--:|
| wall clock (`parallel_for`, all cores) | 14861 ms | 12955 ms / 13783 ms | 1.08×-1.15× faster |
| total CPU across worker threads | 26463 ms | 28286 ms / 28147 ms | 0.94× (6% slower) |

Total CPU time -- the more reliable metric on a shared machine (repeats to <1%
across the two AD runs, versus ~6% run-to-run noise in wall clock) -- says AD
was marginally *more* expensive here, even though `bench_ad_gradients.cpp`'s
isolated measurement puts `DecayFit24`'s gradient at 5.44× cheaper (Phase 4).
The likely reason is `i_lbfgs`'s own per-iteration overhead (line search,
history update, bound penalty) plus `Dual<GradVec<5>>`'s larger memory
footprint absorbing the saving against a comparatively cheap 128-bin model —
not chased further. **No clear win, so it was not shipped**: the gradient
callback was removed from `DecayFit24.cpp` (a note in the source says what was
tried and why), the `tau1`/`tau2` fix stayed, and `test_ad_gradient.cpp`'s
`decay24` section stays too, as the record that the removed approach was
correct rather than merely attempted. Same call already made for `DecayFit26`.

### The central-difference step, retuned: 62×-440× more accurate

`bfgs` (`modules/math/include/i_lbfgs.h`) computed its central-difference gradient step as
`sqrt(eps)*|x|` -- the optimum for a *forward* difference, reused here because the step shared a
`sqrt_eps` member with two unrelated convergence thresholds (`EpsG`, `EpsX`). A central difference
wants `eps^(1/3)` instead: its error is `O(h^2) + O(eps/h)`, and `sqrt_eps` is too small for that
trade-off despite looking more precise. Landed as its own member, `fd_eps`, set alongside `sqrt_eps`
in `seteps()` without repurposing it -- so `EpsG`/`EpsX` are untouched and only the FD step moved.

This benefits every consumer still on central differences: `DecayFit24/25/26`, `DecayFit23`'s
`p2s_twoIstar` branch, `DecayFitModel.cpp`'s `fit_linked` joint-fit path, and plugin fits.

Measured against the step actually replaced (not the never-used `eps` baseline an earlier version of
`bench_ad_gradients.cpp` compared against -- see PRD-010's Correction 3, which predicted a smaller win
from exactly that mistake):

| N | error, old step (`sqrt(eps)`, ~1.49e-08) | error, new step (`eps^(1/3)`, ~6.06e-06) | improvement |
|---|--:|--:|--:|
| 1 (`DecayFit26`) | 2.2e-07 | 3.5e-09 | ~63× |
| 4 (`DecayFit23`'s `p2s_twoIstar` branch) | 1.6e-05 | 5.1e-08 | ~314× |
| 5 (`DecayFit24`) | 1.6e-05 | 5.1e-08 | ~314× |
| 8 | 3.7e-05 | 8.4e-08 | ~440× |
| 18 | 2.0e-04 | 7.8e-07 | ~256× |

Full C++ and Python suite (2698 passed, 74 subtests, two unrelated pre-existing failures) ran clean
with **zero conformance cases needing a re-pin** -- the precision gain lands inside every existing
tolerance rather than moving a converged fit's reported answer.

`EpsG` and `EpsX` (gradient-norm and step-size convergence) were sharing `sqrt_eps` too, the same
accidental-coupling smell that hid the FD-step bug. Split into their own members (`epsg`/`epsx`) with
their own setters, and `EpsG` now auto-tightens to `eps` when an exact gradient is registered — a
central difference's own noise floor is `sqrt(eps)`-scale, which is why the threshold lived there, but
an exact gradient's is `eps`-scale, ~6.7e7x tighter, and `set_gradient`'s docstring has claimed this
was "trustworthy at tight tolerances" since Phase 6 without the code ever acting on it. Checked
against both fits that register a gradient (`DecayFit23`, `ImageLocalization`) and the full suite:
zero regressions, same re-pinned `fit23` conformance value as before — this fixture's termination was
not `EpsG`-bound, which is a legitimate outcome given how convergence-criterion binding is
data-dependent, not evidence the fix does nothing.

### FitNExp: a joint AD refinement pass, additive to the shipped Brent+EM search

`DecayFitNExp.cpp` deliberately never used `bfgs`: amplitudes are profiled by EM (closed-form given
fixed lifetimes, since they enter the model linearly) and lifetimes are searched one at a time by a
multistart-aware Brent search. Both are real properties worth keeping, not stopgaps -- so this adds a
joint gradient step *after* that search converges rather than replacing any of it. The amplitudes stay
profiled by the same EM (plain `double`, re-run at every trial lifetime vector); the AD gradient
(`Dual<GradVec<N>>`, reusing `fconv_per_cs_ad`) is taken holding those amplitudes constant, which is
exact by the envelope theorem (`d(NLL)/d(weight) = 0` at the EM optimum, so the term through weights'
own dependence on lifetime vanishes). `bfgs`'s Armijo line search only accepts strictly decreasing
steps, so this cannot make a fit's answer worse by construction.

Measured at the scale that matters -- a batched fit at realistic per-curve photon counts
(`DecayFitNExp::fit_batch_flat`, not a single cold call):

| metric | value |
|---|--:|
| Brent+EM (shipped, unchanged) | 210 ms/row |
| + joint AD refinement | 1.02 ms/row |
| **overhead** | **0.5%** |
| rows improved | **100/100** |
| rows regressed | **0/100** |

**One real regression was found and fixed before shipping.** `N=1` (mono-exponential) has no
cross-lifetime correlation for a joint step to recover, so the refinement there is pure overhead --
measured directly against `bench_tttrlib.py`'s `bench_fit_curve`, the library's single most benchmarked
path: single-curve 0.25 ms → 0.34 ms (+35%), batched 0.06 ms → 0.08 ms (+35%), for an unchanged
answer. Gated to `N >= 2`; re-measured N=1 back to the unmodified baseline with zero change to N≥2's
improvement.

**The headline "Per-pixel reconvolution MLE" 140 ms number is unaffected, structurally.** That
benchmark fits at a *fixed* reference lifetime (`fixed=[1]`, `ext/python/FitNExpWrapper.py:91`), so no
lifetime is ever free there and the refinement's `any_free` guard makes that code path unreachable
regardless of `N` -- confirmed by reading the guard, not inferred from not having re-run the benchmark.

#### The derivative carrier: `GradVec` replaced Eigen

The vectorized forward pass needs a fixed-size vector in the dual number's
derivative slot. That was `Eigen::Array<double, N, 1>` and is now
`GradVec<N>` (`modules/math/include/GradVec.h`), which removed the last use of
Eigen in tttrlib — and with it a `FIND_PACKAGE(Eigen3 REQUIRED)` on the whole
build, an apt/brew/dnf package on four CI platforms, and a vcpkg port on
Windows, all for one struct member in one file.

Head to head on the localization objective at its real free-parameter counts
(N = 6/9/12 for one/two/three Gaussians — **not** 18; entries 12..17 of `vars`
are flags and outputs), cost of one full gradient:

| N | Eigen | GradVec | ratio |
|---|--:|--:|--:|
| 6 | 0.00098 ms | 0.00112 ms | 0.87× |
| 9 | 0.00290 ms | 0.00287 ms | 1.01× |
| 12 | 0.00498 ms | 0.00592 ms | 0.84× |

So: parity at N=9, 13–16% slower at N=6 and N=12. That is a real cost and it is
recorded rather than rounded away — it is also small against the 3.95–5.44× AD
wins over tuned central differences to begin with, which is the comparison that
decides whether the AD path is worth having at all.

Two things were tried and rejected on measurement: `alignas(32)` on the storage
(slower — it inflates every `Dual`, and there are 169 of them live in the inner
loop) and padding N up to a multiple of the SIMD width (no better, and worse at
N=12, which is already a multiple of 4). What *did* help was returning a proxy
from `scalar * grad` so the multiply fuses with the accumulate that always
follows it, rather than materialising an N-double temporary.

```bash
c++ -std=c++17 -O3 -I modules/math/include \
    -DHAVE_EIGEN -I "$CONDA_PREFIX/include/eigen3" \
    benchmarks/bench_gradvec.cpp -o /tmp/bench_gradvec && /tmp/bench_gradvec
```

**On measuring this at all.** The first attempt used `steady_clock` and reported
speedups from 0.22× to 4.77× for the same binary across consecutive runs — the
development machine was at load average 43, and wall clock keeps counting while
the thread is descheduled. The harness uses `CLOCK_THREAD_CPUTIME_ID`, counts
only cycles the thread was given, interleaves the two carriers so they see the
same load, and takes the minimum over nine trials. The numbers above are the
mean of eight such runs and are repeatable to a few percent. A benchmark that
cannot distinguish a 15% kernel difference from the scheduler is not measuring
the kernel.

### The FFT is the slow way to convolve a decay

"Convolution is a multiplication in frequency space, so use an FFT" is sound
advice for convolving a response with an arbitrary signal, and wrong for a decay
made of exponentials. Both paths are `O(n_bins × n_rates)`: the recursion
(`fconv_per_cs`) does one multiply–add per rate and bin, while the closed-form
periodic spectrum does one complex *division* per rate and frequency. The
transform is not what dominates — evaluating the closed form is — so the
frequency domain buys no better scaling, only worse constants, and the recursion
is SIMD-optimised on top of that.

| bins | rates | recursion | spectral | spectral is |
|-----:|------:|----------:|---------:|:------------|
| 1024 | 1 | **28 µs** | 47 µs | 1.7× slower |
| 1024 | 16 | **55 µs** | 253 µs | 4.6× slower |
| 1024 | 64 | **147 µs** | 911 µs | 6.2× slower |
| 4096 | 64 | **581 µs** | 3569 µs | 6.1× slower |

The gap *widens* with the rate count — the regime a donor ⊗ FRET ⊗ anisotropy
outer product lives in — so the frequency domain is at its worst where a
rate-spectrum model would need it most. The spectral path is kept for the three
things the recursion cannot do: convolving an arbitrary measured pattern, an
independent cross-check, and a response broad enough to wrap around the period.
A sub-bin timeshift is not one of them — the recursion borrows a single
transform of the *response* for that, costing 24% (53 → 66 µs at 1024 bins and
16 rates) and not growing with the rate count.

The two agree to **machine precision** (≤1.4e-14 relative across the table),
which required correcting a rate-dependent factor rather than tolerating it: the
recursion's trapezoid rule leaves the kernel `exp(-k L)` at every lag except
`L = 0`, where it leaves one half, and left uncorrected the two backends differ
by `(1 + exp(-k))/2` — 0.5% at `k = 0.01` against 5% at `k = 0.1`. That does not
divide out of a rate spectrum, it reweights it.

Reproduce with `python benchmarks/bench_convolution.py`.

#### The measurement itself was 70–90% wrapper until 2026-08-11

Worth recording because it changed the published figure, not just the runtime.
The `dfa_*` entry points marshalled every array through the Python sequence
protocol — one boxed float per element, each way — so a caller paid ~50 ns per
element on top of the algorithm. Converted to NumPy typemaps
(`double* IN_ARRAY1` in, `ARGOUTVIEWM_ARRAY1` out), same arithmetic, arm64,
best of 200, recursive backend:

| n_bins | rates | ndarray in, before | after | speedup |
|-------:|------:|-------------------:|------:|--------:|
| 64 | 1 | 5.58 µs | **1.00 µs** | 5.6× |
| 512 | 1 | 26.42 µs | **3.50 µs** | 7.5× |
| 4096 | 1 | 192.92 µs | **24.83 µs** | 7.8× |
| 16384 | 1 | 761.96 µs | **90.75 µs** | 8.4× |
| 16384 | 16 | 1171.87 µs | **508.46 µs** | 2.3× |

Two things in that table are worth more than the speedup.

**The natural call was the slow one.** Before the change, passing a NumPy array
cost about *twice* what passing a list cost (761.96 vs 326.00 µs at 16384),
because unboxing a NumPy scalar per element is more work than unboxing a float.
Every caller in this repository passes arrays. After the change a list is
marginally slower than it was (326.00 → 396.87 µs — NumPy now has to build an
array from it) and an array is 8× faster; lists still work, and nothing about
the call changed.

**It moved the published comparison above.** With both backends paying the same
wrapper, the ratio between them was pulled toward 1 at small rate counts, which
is where the wrapper share is largest. The example's own figure went from
1.6× → 6.0× across 1–64 rates to **4.1× → 7.2×**. The 1-rate point had been
close enough to 1.0 that a busy machine could invert it, which is how this was
found: as a flaky strict inequality in
`test_convolution_methods_example.py` split into a
two-tier assertion — and then made a single strict assertion again once the
real cause was gone.

### You do not need a GPU

FLIMKit ships a GPU backend (MLX / CUDA / MPS / ROCm); on this machine its GPU
path runs on the M1 Pro GPU via MLX. For per-pixel reconvolution FLIM fitting,
tttrlib's **CPU** `fit_map` (140 ms) beats it by **6.3×** (1770 ms), and also
beats FLIMKit's own CPU path (1194 ms) by **5.3×**. Per-pixel fitting is
a swarm of tiny independent fits with branching, which GPUs handle poorly, so
the GPU brings little benefit in this regime.

Caveat: this is a laptop integrated GPU running a discrete-exponential
per-pixel model. A datacenter GPU running a batched continuous-distribution fit
is a different regime that this suite does not test.

### Two wins come from optional fast paths

- **CLSM intensity** uses `CLSMImage(..., build_pixels=False)` — a single-pass
  "virtual fill" that skips eager allocation of per-pixel photon-index
  containers and scatter-adds accepted photons straight into the image. It is
  **byte-identical** to the classic `fill()` output (verified on PTU and HT3,
  single- and multi-frame). Per-pixel containers are materialized *lazily* on
  the first `fill()`, so `build_pixels=False` stays correct for the full FLIM
  workflow. The default `build_pixels=True` keeps the classic eager behaviour.
- **Simulation** uses the coasting integrator
  (`SimIntegrator.per_molecule_skip`), which stops stepping molecules far
  outside the detection volume. Same photon statistics, ~3× less wall time than
  the fixed-dt baseline (1.46 s → 0.51 s).

## Streaming correlator — cost per photon, not per bin

`StreamingCorrelator` bins photons onto a uniform macro-time grid, so its
natural cost is one cascade step per *bin*. At a native macro-time resolution
there are hundreds to thousands of empty bins between photons, and each one used
to cost a full cascade step: ~19 ns, measured. A 100 s acquisition at 10 ns
resolution is 10^10 bins, which is minutes of doing nothing.

An empty run is now skipped in closed form. Level *b* emits `n0 / 2^b` times, so
the number of emissions a run of `k` empty samples covers is a difference of two
divisions, and only the first of them can be non-zero — it carries the
accumulator left from before the run. The rest move history and nothing else, at
most one history depth of it.

80k photons, `n_bins=16`, `n_casc=25`, driven one at a time from Python:

| Acquisition span | Before | After |
|------------------|-------:|------:|
| 0.1 M bins | 0.061 s | 0.060 s |
| 4.0 M bins | 0.136 s | 0.063 s |
| 200 M bins | ~3.9 s (extrapolated at 19 ns/bin) | **0.069 s** |

The point is the shape, not the ratio: the cost no longer grows with the length
of the acquisition. What is left is dominated by the per-photon Python call
(~0.011 s of the 0.060 s here).

## Dense linear algebra — the shared math kernels

Every ported spectroscopy algorithm sits on two headers: `modules/math/Mat.h`
(solvers, GEMM) and `modules/math/QREigen.h` (non-symmetric eigendecomposition).
They have their own benchmark, their own recorded baseline, and a regression
check, because a change here moves MaxEnt, the Kalman burst search, the HMM
surrogate, Gopich–Szabo and BurstML at once and none of those benchmarks would
say which kernel did it.

### The tracked baseline — recorded 2026-08-10

`benchmarks/results/linalg_baseline.tsv` is the file the regression check reads.
It was recorded on arm64 macOS (M-series), AppleClang `-O3`, NEON (2 doubles
wide), OpenMP on, as the median of five trials:

| Case | ms |
|------|---:|
| `mat_solve` n=64 | 0.024 |
| `mat_solve` n=256 | 1.408 |
| `mat_lstsq_minnorm` 256×64 | 3.63 |
| `mat_lstsq_minnorm` 512×128 | 28.75 |
| `mat_inverse` n=2, ×1000 | 0.027 |
| `mat_inverse` n=4, ×1000 | 0.072 |
| `mat_power` n=5, p=64, ×100 | 0.116 |
| `gemm_nn` / `gemm_nt` / `gemm_tn` 128³ | 0.161 / 0.107 / 0.112 |
| `gemm_nn` / `gemm_nt` / `gemm_tn` 256³ | 0.825 / 0.844 / 0.853 |
| `qr_eigendecompose` n=25 | 0.196 |
| `qr_eigendecompose` n=100 | 4.47 |
| `qr_eigendecompose` n=200 | 36.44 |

### What the 2026-08-10 rewrite changed

Before/after for the solvers is from an A/B harness that runs both
implementations in one binary, so the two columns are directly comparable; the
eigensolver rows are the same benchmark before and after, single-threaded
except where noted.

| Case | Before | After | Speedup |
|------|-------:|------:|:-------:|
| `mat_solve` n=64 | 0.019 ms | 0.024 ms | 0.81× |
| `mat_solve` n=256 | 1.282 ms | 1.349 ms | 0.95× |
| `mat_lstsq_minnorm` 128×32 | 1.121 ms | 0.455 ms | **2.5×** |
| `mat_lstsq_minnorm` 256×64 | 14.33 ms | 3.41 ms | **4.2×** |
| `mat_lstsq_minnorm` 512×128 | 201.2 ms | 27.7 ms | **7.3×** |
| `mat_inverse` n=2, ×200k | 11.61 ms | 5.41 ms | **2.1×** |
| `mat_inverse` n=4, ×200k | 19.57 ms | 13.97 ms | **1.4×** |
| `qr_eigendecompose` n=25 | 0.348 ms | 0.196 ms | **1.8×** |
| `qr_eigendecompose` n=100 | 45.26 ms | 8.26 ms | **5.5×** |
| `qr_eigendecompose` n=100, 8 threads | 45.26 ms | 4.47 ms | **10.1×** |
| `qr_eigendecompose` n=150 | 208.1 ms | 26.5 ms | **7.9×** |
| `qr_eigendecompose` n=150, 8 threads | 208.1 ms | 13.0 ms | **16.0×** |

`mat_solve` is the one case that got slower, and it stays that way on purpose:
it now scans the matrix once, O(n²), so its singularity test can be relative to
the matrix scale rather than an absolute 1e-300 floor. Without that scan a
rank-deficient matrix with large entries is called regular and the caller gets
components of size 1e24 — measured, not hypothetical, on a rank-1 outer product
with entries ~1e8. Against the O(n³) factorisation the scan is 19% at n=64 and
5% at n=256.

`mat_lstsq_minnorm` got faster by working on a column-major copy — one-sided
Jacobi only ever touches whole columns, which are strided in a row-major matrix
and contiguous here — and by carrying the column norms through each rotation in
closed form instead of recomputing them, which removes two of the three
length-m passes per index pair.

The small-`n` OpenMP entries in the baseline (`mat_power`, `gemm_nn` 128³) are
slower with threads than without: fork/join on work too small to split. The
eigensolver's parallel loops are gated at n ≥ 32 for the same reason.

### Where the eigensolver time went

`qr_eigendecompose` spent 94% of its time on eigenvectors, and that part scaled
as n⁴: it ran a dense LU of `A - lambda*I` for every eigenvalue. Inverse
iteration on the **Hessenberg** form instead costs O(n²) per eigenvalue — a
Hessenberg column has exactly one entry to eliminate — so the whole basis is
O(n³), the same order as the QR iteration that produced the eigenvalues. The
Schur-vector accumulation inside the QR iteration was then switched off because
nothing consumes it, and the per-eigenvector loop was parallelised (the vectors
are independent).

This is why the two `test_burstml.py` cases no longer qualify as `slow`.

### Running it

```bash
c++ -std=c++17 -O3 -Xpreprocessor -fopenmp -I modules/math/include \
    -I$(brew --prefix libomp)/include -L$(brew --prefix libomp)/lib -lomp \
    benchmarks/bench_linalg.cpp -o benchmarks/bench_linalg

./benchmarks/bench_linalg                                          # table
./benchmarks/bench_linalg --check benchmarks/results/linalg_baseline.tsv
./benchmarks/bench_linalg --write benchmarks/results/linalg_baseline.tsv
```

`--check` exits non-zero when a case runs more than `--tol` (default 1.30)
times its recorded baseline. Run-to-run spread on the reference machine is
under 11%, so 1.30 flags a real regression rather than noise. A baseline is
only meaningful against the machine that recorded it — re-record with
`--write` on new hardware, and note in the commit that the numbers moved
machines.

## Across releases — 0.27.0 vs 0.26.2

0.27.0 is a performance **and** memory release. `fill()` moved to a lazy
per-event stream-mask (one bit per event) instead of eagerly materializing a
per-pixel photon-index vector; intensity, lifetime, phasor and tttr-index
queries run straight off the mask. `CLSMImage(..., build_pixels=False)` skips
per-pixel allocation entirely for intensity-only work.

Memory below is the **task footprint = peak process RSS − post-import
baseline**, measured with `getrusage` rather than `tracemalloc` because the
wins live in the C++ heap. Each task runs in its own subprocess so the
high-water mark isolates. Negative Δ is an improvement.

![Cross-version time and memory](benchmarks/plots/versions/summary_versions.png)

| Task | 0.26.2 time | 0.27.0 time | Δ time | 0.26.2 mem | 0.27.0 mem | Δ mem |
|------|------------:|------------:|:------:|-----------:|-----------:|:-----:|
| CLSM fill + structure (512×512 PTU) | 125.19 ms | 46.27 ms | **−63%** | 107 MB | 93 MB | **−12%** |
| CLSM fill, 2.6 M-pixel FLIM image (40×256×256 HT3) | 1483.78 ms | 279.39 ms | **−81%** | 515 MB | 307 MB | **−40%** |
| Correlation / FCS (3.5 M photons) | 437.97 ms | 279.06 ms | −36% | 350 MB | 325 MB | −7% |
| TTTR file reading (3.5 M-photon PTU) | 31.05 ms | 29.74 ms | −4% | 138 MB | 144 MB | +5% |
| Burst search (3.5 M photons) | 2.74 ms | 2.77 ms | +1% | 73 MB | 74 MB | ≈0 |
| CLSM intensity — virtual fill (512×512 PTU) | — | 21.94 ms | *new* | — | 87 MB | *new* |
| Per-pixel reconvolution-MLE map (256×256) | — | 900.05 ms | *new* | — | 934 MB | *new* |

The cross-version times are higher than the competitor-table times for the same
task because the version harness runs one task per subprocess with
`OMP_NUM_THREADS` pinned, and takes fewer repeats. Compare versions to versions,
not across tables.

## Caveats and honest notes

- **Fast reading is a close race** (~1.07×) — I/O- and memory-bandwidth-bound,
  so no tool pulls far ahead. tttrlib is ahead, but the margin is small and can
  flip on other hardware.
- **Derived FLIM maps share one photon pass and cache their moments.** The first
  mean-lifetime / mean-micro-time / phasor map costs one pass; re-tuning the
  IRF, background or resolution afterwards is an O(pixels) correction (~0.09 ms,
  ~510× vs a tool that recomputes). Real interactive FLIM builds once and
  re-tunes many times, so this is the dominant cost in practice.
- **The warm caches cost a little memory.** A few tens of bytes per pixel —
  the opposite direction from the fill wins above, and only paid once a cache is
  warm.
- **Correlation is multi-tau vs. direct.** tttrlib's `Correlator` uses the
  standard multi-tau algorithm (logarithmic photon coarsening); pycorrelate does
  an exact direct per-bin correlation. Both yield the same FCS curve (verified:
  both peak at G = 2.28 over the same lag range), but multi-tau is
  algorithmically cheaper, which is most of the 13×.
- **Use the batch fit API.** A single-curve `FitNExp` call is ~4× slower per fit
  than the multithreaded `fit_many` / `fit_map` path; for many curves or an
  image, batch them.
- **flimlib's per-pixel LMA map (196 s) is an outlier**, not a like-for-like
  contest — it is a single-threaded per-pixel Levenberg–Marquardt over the full
  image. The meaningful MLE comparison is against FLIMKit.

## Build time

Run-time speed is what the rest of this file measures. Build time is the other
number that matters, because it is what a change to tttrlib costs to try. All
figures below are the same machine as above (M1 Pro, 8 cores, clang/libc++,
`-std=gnu++17`), C++ library only (`-DBUILD_PYTHON_INTERFACE=OFF`), `ninja -j8`.

### Where the time went

The cost was never the size of the sources — it was what the *public headers*
dragged in. Measured as preprocessed line counts, with `#include <vector>`
alone (53,438 lines) as the floor any translation unit pays:

| header, alone | before | after | over the `<vector>` floor |
|---|---:|---:|---|
| `<nlohmann/json.hpp>` | 95,070 | — | +41,632 |
| `<nlohmann/json_fwd.hpp>` | — | 62,466 | +9,028 |
| `"TTTR.h"` | 133,889 | **99,196** | −26% |
| `"TTTRHeader.h"` | 126,512 | **91,899** | −27% |
| `"CLSMImage.h"` | 142,221 | **104,960** | −26% |
| `"TTTRRange.h"` | 135,139 | **100,456** | −26% |
| `"BurstFilter.h"` | 134,286 | **99,829** | −26% |
| `"HMM.h"` | 135,468 | **100,697** | −26% |
| `"Channel.h"` | 95,071 | **62,438** | −34% |
| `"DecayFitModel.h"` | 96,749 | **69,460** | −28% |

`src/Pda.cpp` is the extreme case: 460 source lines that used to preprocess to
138,564, now 104,966. `src/Correlator.cpp` went 142,892 → 105,875.

Three things did it, in order of payoff:

1. **`nlohmann/json.hpp` is out of every public header.** It cost ~41.6k
   preprocessed lines and appeared in 14 of them. Headers that only name `json`
   in a signature use `json_fwd.hpp`; headers that defined `to_json` inline
   (`DecayFit.h`, `DecayFitModel.h`, `DecayFitProblem.h`, `DecayFitPrior.h`)
   have those bodies in `.cpp` now; `TTTRHeader` holds its `json_data` behind a
   `unique_ptr` so the type may stay incomplete. `TTTRRange.h` and
   `TTTRSelection.h` did not use `nlohmann::json` at all — their serialisation
   has always been `std::string`.
2. **HighFive is out of `TTTR.h` and `TTTRHeader.h`.** `TTTR.h` needed it only
   for a private `hid_t` handle that was opened and closed inside a single
   function, so it is a local there now; `TTTRHeader.h` names `HighFive::Group`
   in one private declaration and uses HighFive's own forward-declaration header
   (44 preprocessed lines) for it. HDF5 no longer reaches any downstream
   consumer's include path.
3. **`pocketfft` is out of `CLSMImage.h`.** A vendored 71k-line header was in a
   public *installed* header for an FFT used only in the `.cpp`.

### What that buys

| clean build, C++ library only | wall | CPU (user) |
|---|---:|---:|
| before | 48.6 s | 212 s |
| after | **40.5 s** | **180 s** |
| after, `-DTTTRLIB_LTO=OFF` | **38.1 s** | 192 s |

(The LTO-off row spends *more* user CPU and less wall time: with LTO the
compiles emit bitcode cheaply and the real work happens in a single-threaded
link, which parallelizes across cores not at all.)

−17% wall on a clean build, while compiling two *more* translation units than
before (the moved-out `.cpp` bodies).

The number that governs daily iteration is the incremental one. Touching
`include/TTTR.h` and rebuilding:

| `touch include/TTTR.h && ninja` | wall |
|---|---:|
| LTO on (the `Release` default, and what `pip install -e .` configures) | 26.5 s |
| `-DTTTRLIB_LTO=OFF` | **18.2 s** |

LTO is a whole-program link and buys nothing while iterating, so
`TTTRLIB_LTO=OFF` is the default in the `dev` preset. Release artefacts — wheels
and conda packages — keep it on.

### Configuring for iteration

`CMakePresets.json` carries the developer configurations, so none of this is
archaeology:

```bash
cmake --preset dev        # Release without LTO -- the default for iteration
cmake --preset lib-only   # no SWIG wrappers at all: the fastest "does it compile"
cmake --preset no-hdf5    # drops HighFive/HDF5 from the compile line entirely
cmake --preset debug      # -Wall -Wextra -pedantic, verbose logging
cmake --preset release    # what ships; benchmark numbers above are taken here
```

`ccache` is used automatically when it is on `PATH` (`TTTRLIB_CCACHE=OFF` to
stop looking). It is worth installing: `pip install -e .` reconfigures into a
fresh build directory often enough that the cache is what makes a rebuild cheap.

**Still outstanding.** `tttrlibPYTHON_wrap.cxx` is ~182k lines in a *single*
translation unit and is fully serial, so touching any `.i` file recompiles all of
it — it is the longest pole in any build that includes the bindings, and none of
the above touches it. Splitting it into one SWIG module per subsystem is
tracked with the modularization work, not here.

## Reproducing

```bash
cd benchmarks
./build_envs.sh                        # one uv venv per competitor (base env untouched)
python bench_tttrlib.py                # tttrlib side + writes results/shared/ inputs
python bench_h2mm.py                   # tttrlib H2MM (plain EM + SQUAREM + Viterbi)
python bench_vicidomini.py             # blind IRF / APR / focus-ISM / s2ISM (tttrlib side)
.venvs/flimlib/bin/python      competitors/bench_flimlib.py
.venvs/read/bin/python         competitors/bench_ptufile.py
.venvs/read/bin/python         competitors/bench_phconvert.py   # PTU / HT3 / SPC-130 readers
python check_reading.py                # photon-for-photon identity of the reading pairs
.venvs/fretbursts/bin/python   competitors/bench_fretbursts.py
.venvs/pybromo/bin/python      competitors/bench_pybromo.py
.venvs/flimkit/bin/python      competitors/bench_flimkit.py
.venvs/h2mm_c/bin/python       competitors/bench_h2mm_c.py
.venvs/h2mm_numba/bin/python   competitors/bench_h2mm_numba.py
KMP_DUPLICATE_LIB_OK=TRUE .venvs/vicidomini/bin/python competitors/bench_vicidomini.py   # birfi / BrightEyes-ISM / s2ISM
python check_vicidomini.py             # output identity of the VicidominiLab pairs
python bench_sciref.py                 # watershed / marching squares / RL / k-means / HDBSCAN / Kalman / HMM / phasor (tttrlib)
.venvs/sciref/bin/python competitors/bench_sciref.py     # scikit-image / scikit-learn / filterpy / hmmlearn / phasorpy
python check_sciref.py                 # output identity of those pairs
python bench_fret.py                   # PDA / BurstML / 2CDE / 2D-FDC / CUSUM (tttrlib)
python competitors/bench_fret.py       # PAM + FRET_burstML natively, FRETBursts venv, Octave (base env; skips what is missing)
python check_fret.py                   # output identity of those pairs
python make_plots.py                   # -> plots/*.png
```

Cross-version tracking (time + peak memory):

```bash
cd benchmarks
python bench_versions.py --versions 0.26.2 0.27.0=local   # LABEL=local -> base env
python make_version_plots.py           # -> plots/versions/*.png + summary.md
```

## See also

- [`benchmarks/README.md`](benchmarks/README.md) — harness design, what is
  compared against what, how to add a task or a version.
- [Performance guide](doc/performance_guide.rst) — how to *use* tttrlib fast
  (selections, slicing, caching, memory, diagnostics).
- [`CHANGELOG.md`](CHANGELOG.md) — per-release performance notes.

### How far does the AD advantage go? Not as far as the small-N numbers suggest

Every conversion decision above was taken at N ≤ 18, where AD beats tuned
central differences by 4–10×. That is a statement about a parameter count, not
about AD. A multi-exponential decay is not in that regime: 200 exponentials is
N = 400 free parameters. Both methods are O(N) — central differences pay 2N
objective evaluations, and a vectorized forward pass makes every scalar carry an
N-vector — so the ratio is a race between two O(N) costs, decided by constants
and by memory traffic.

Measured on a 1024-channel multi-exponential decay
(`benchmarks/bench_ad_scaling.cpp`):

| n_exp | N | CD (× obj) | AD (× obj) | **AD gain** | bytes/dual | MB per model intermediate |
|---|---|--:|--:|--:|--:|--:|
| 2 | 4 | 7.9× | 1.5× | 5.2× | 40 | 0.04 |
| 4 | 8 | 16.0× | 1.5× | 10.5× | 72 | 0.07 |
| 8 | 16 | 31.8× | 2.0× | 15.6× | 136 | 0.13 |
| 16 | 32 | 63.9× | 3.5× | **18.4×** | 264 | 0.26 |
| 32 | 64 | 132× | 9.2× | 14.4× | 520 | 0.51 |
| 64 | 128 | 260× | 25.5× | 11.1× | 1032 | 1.01 |
| 128 | 256 | 532× | 157× | 3.4× | 2056 | 2.01 |
| 200 | 400 | 847× | 249× | **3.4×** | 3208 | 3.13 |

**The advantage peaks around 16–32 free parameters and then decays.** Central
differences stay near-linear (847× against the theoretical 2N = 800×), while AD
goes *superlinear*: 249× where pure O(N) predicts ~160×. The last column is why.
A `Dual<GradVec<400>>` is 3.2 kB, so one 1024-channel intermediate is 3.13 MB —
far outside any cache — while the finite-difference path re-walks a plain 8 kB
array 2N times.

Absolute cost matters as much as the ratio: one gradient at N = 400 is 135 ms by
AD against 455 ms by central differences. At ~100 iterations that is 13 s versus
45 s. AD wins, and neither is cheap.

**At that size both are the wrong tool.** For a sum of exponentials the analytic
gradient is closed-form — ∂/∂amplitude *is* the convolved exponential already
computed, and ∂/∂τ is a related recursion — so a hand-written gradient costs
about one objective evaluation and would beat the AD column by roughly its 249×.

The table above is measured with `Dual.h`; the earlier one, measured with
autodiff, had a reproducible **dip at N = 32** (gain 5.7×, AD at 11.6× the
objective) that was recorded as unexplained and left alone because it changed no
decision. It was autodiff: the same row is 3.5× the objective and a gain of
18.4× once the expression templates stop materialising temporaries. The whole
tail moved with it — N = 128 from 8.0× to 11.1×, N = 400 from 3.3× to 3.4× — so
the shape of the curve was partly the AD library and not only the memory
traffic. Left as a caution: "reproducible, not noise, changes no decision" is
how a fixable 5× hides.

Note this is a scaling study of the *method*, not a to-do for `FitNExp`, which
has no N-dimensional gradient to convert: it optimises lifetimes coordinate-wise
with Brent and profiles amplitudes out by EM, and never constructs a `bfgs`.
