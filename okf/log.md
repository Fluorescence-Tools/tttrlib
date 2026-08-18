# Bundle update log

## 2026-08-18 (48th entry)

* **One registry** (commits 508c1135a, cd9b85b1e; PRD-032 closed). The
  owner's ruling -- "there should be only one registry", "no per-module
  registry files" -- ended the two remaining hand-authored literals.
  `kFitRegistry` (5 fits, 2 setup blocks, 4 objectives) and
  `kOperationRegistry` (8 pipeline operations) are deleted; each entry is now
  a `register_algorithm_json` call next to the code it describes (the model
  TUs, `DecayStatistics.cpp`, `BVA.cpp`, `TwoCDE.cpp`, `RecurrenceAnalysis.cpp`,
  `Correlator.cpp`, ...), the plugin host registers every plugin capability
  into the same table as the plugin loads (a failed init unregisters through
  the journal) and the three text splices are gone; `Registry.cpp` primes and
  assembles, holds nothing. `AlgorithmDescriptor` gained `name` -- the
  registry key, distinct from the mmfdb `operation_type` (mle_green and mle_red
  are one operation type) -- and `extra_json` for capability-specific keys, so
  the emitter needed no per-capability fields. The layering worry recorded
  earlier in PRD-032 dissolved: `decay` depends on `algorithm` (below it) and
  reads its own registrations back; `registry` now depends on `decay` and
  `fcs`. Verified the way the burst-search migration was: 0 entries removed,
  0 changed, 145 generic keys added, `params_schema` property order identical
  to the literals (a test now pins it, since a sorting JSON type would
  silently reorder the flat parameter layout). Built-in correlation methods
  and prior kinds register too, so `registry("correlation_method")` and
  `registry("prior")` describe exactly what `set_correlation_method` and
  `DecayFitPrior.from_json_string` accept (test pins the sets equal); the
  conformance category case grew from 11 to 13.
  Then the second ruling, "registry must be in core" / "why is there still a
  split: algoreg and reg?": the `algorithm` (table) and `registry` (assembler)
  modules are folded into one `core/Registry.h` (1f1846038). That forced the
  priming question into the open -- core cannot call the modules above it to
  make them register -- and the answer is what the plan had avoided since
  `DecayFitModelRegistration.h`: modules register from static initialisers
  next to their code, and the archive-drop problem is solved at the link
  instead of in the code (`tttrlib_link_all_modules` links module objects for
  STATIC builds; the R recipe links `libtttrlib_static.a` whole). The plugin
  host, beneath core, records declarations and the registry pulls them; a
  failed init drops them with the journal before anyone can have pulled.
  fcs/hmm/pda descriptors moved from the central `BuiltinAlgorithms.cpp` into
  their modules. Java (STATIC) verified: the JNI library carries the
  registrations; R is CI's to confirm.

## 2026-08-18 (47th entry)

* **Modularization tickets T-03, T-05, T-06, T-07 closed** (commits 0e2d34d73,
  7ec9fe82c, e1c97644a, 70df77cf2, b9c6597a6). Every source now compiles once:
  each module owns an OBJECT library and the module `.so`, `libtttrlib.so` and
  `libtttrlib_static.a` are links over the same objects -- an extraction can no
  longer subtract from the aggregates (`MODULE-DEBT.md` §2). The static archive
  needed thought: GCC gets `-ffat-lto-objects` (bitcode + code in one object),
  Apple's ld64 reads bitcode archives natively (a Release+LTO consumer link was
  the check), any other LTO toolchain keeps the archive's own `-fno-lto` compile.
  Every module has a `WITH_<NAME>` switch, the four `tttrlib.i` and the split
  `mod_*.i` guard each fragment with `#ifndef TTTRLIB_WITHOUT_<NAME>`, and
  presets `dev-sim` / `dev-clsm` / `dev-hmm` build ~15 of 35 modules and import;
  a module whose dependency is off fails the configure naming both switches,
  checked at finalize because declaration order is not dependency order (decay
  is declared before registry). The last two dispatch chains became registries
  earlier in the day (Correlator methods, DecayFitPrior kinds) and both are now
  plugin capabilities in the C ABI (`tttrlib_correlation_method_v1`,
  `tttrlib_decay_prior_v1`), looked up by fcs/decay on a table miss so a
  rolled-back plugin leaves nothing dangling. The `friend` "cycle" of §6 turned
  out to be two dead lines. Left open: T-04 export macros (needs Windows CI to
  verify; no `__declspec` yet) and T-01 flipping the split to default (needs
  a CI wheel).
  Later the same day: JS (Node-API) and Java (`TTTRLIB_MODULE_TYPE=STATIC`, as
  CI builds it) both compile from the object-library layout with the guarded
  interfaces; `HmmLattice.i` offered to R/Java/JS and `hmm_forward_log` gives
  the Python value bit-for-bit from Node (`-2.3496767005278962`).

## 2026-08-18 (46th entry)

* **The Python bindings as six SWIG extensions** (`TTTRLIB_PYTHON_SPLIT`, preset
  `dev-split`; commit 4245cbd47): the plan's "N SWIG modules, not one" is real
  -- core / formats / kernels / spectroscopy / imaging / sim over one shared
  type table, flat re-export, opt-in until CI has shipped a wheel with it
  (T-20260818-01). Getting there cost a day of SWIG traps, all written down in
  `ext/python/split/README.md`: a split file that shares a fragment's name
  includes itself on a case-insensitive disk; %import carries types but not
  library fragments, `%{ #include %}` blocks or global `%exception` order;
  templates must be instantiated (namelessly) where used; `import *` is not a
  re-export; `io`/`math` cannot be submodule names. Measured: a leaf change
  rebuilds one module (~45 s); a core change still rebuilds all six in
  parallel, bounded by core (110k lines). Modularization TODO filed as
  T-20260818-01..07; T-02 (`formats`/`kernels` off core) done in the same pass.
* Also this session: `neyman_lsq`/`gehrels_lsq` were advertised objectives that
  ran the Poisson likelihood -- fixed; MT19937 engine name ran Philox -- fixed;
  BH `.set` TAC width; PicoHarp T3 markers (see entries 42-45).

## 2026-08-17 (45th entry)

* **The last IO paths get outside references.** TIFF I/O vs tifffile in both
  directions (dtypes, compressions, ImageJ hyperstacks: identical); `.photons`
  vs the public photonsfile decoder on our written files; CSV reader/writer
  rows for the existing pyarrow A/B; header marks on those files. The BH `.set`
  comparison against phconvert `load_set` found the SPC-130 path ignoring the
  sidecar and every path ignoring the TAC gain (6.1 ps read for a 3.05 ps FLIM
  file) — fixed, BUGS entry, pinned. HT3 header units and Photon-HDF5 units
  equal phconvert/h5py. Register: every header with a kernel or a decoder now
  carries a `// Validation:` block or a NO-REF/plumbing row.

## 2026-08-17 (44th entry)

* **MT19937 was a name without an engine** — `TTTR_RNG_ENGINE=mt19937` accepted
  and every streaming draw silently Philox. `Random` now streams `std::mt19937`
  (numpy `RandomState(int)` raw stream, bit for bit; `seek` by discard;
  `deterministic` keeps its documented Philox fallback). No default changed, no
  result depended on it. FLIM LABS: checked flim-labs and VicidominiLab on
  GitHub — no STT1/ITT1 sample exists; the vendor's own STT1 reader now runs on
  our writer's bytes as the reference (bounded PASS). PCH/ICS vs pysimfcs and
  Kolin/Wiseman STICS in the previous entry.

## 2026-08-17 (43rd entry)

* **PCH and ICS against an implementation that is not ours — pysimfcs (J.
  Unruh).** `pch_single_species` has Chen 1999 eq. 16's shape to 1e-5, and the
  open-system histogram equals pysimfcs' to 5e-5 once the particle number is
  converted: tttrlib references `avg_n` to V0 = 4π w0³, Chen/pysimfcs to
  V_PSF, so `avg_n` = 6.383 N_PSF. That was undocumented (ChiSurf's PCH module
  knew, the header did not) — the header now states the convention and the
  conversion; the number itself is left alone (ChiSurf's fitted N depends on
  it). `compute_ics` equals pysimfcs' `autocorr2d` exactly. Also this round:
  Photon-HDF5 vs h5py, HT3-v1 sample is SF-compressed, Kristine `.cor` gap.

## 2026-08-17 (42nd entry)

* **Vectorial PSF vs PyFocus; second reading round; three decoding fixes.**
  `vectorial_psf` (Richards-Wolf) vs BrightEyes-ISM/PyFocus's
  `VectorialCartesianPropagator`: 5e-5 of peak in focus, 5e-4 defocused, x/y/
  circular, after undoing PyFocus's `[x, y]` indexing and `fov/(Nx-1)` pixel
  (recorded fixture, vicidomini venv). Reading: SPC-630 / SPC-QC / `.sm` vs
  phconvert and PicoHarp T3 / TH260 / generic T3 vs ptufile — **PicoHarp T3
  was wrong** (markers by `dtime == 0`, channel-15 markers as photons; fixed
  in reader and writer, SP8 fixture regenerated by three photons), the
  SPC-6x0 header frame was ignored (resolution 1.0 s; now read/written per
  BH spec), and a from-scratch PTU lacked `Measurement_Mode` (ptufile could
  not decode it). phconvert's own SPC-6xx defects (2^12 shift on a 17-bit
  field, ADC vs 4095, path re-reads the header) recorded as bounded. Reading
  benchmark set extended (4 pairs, all identical; 1.5–4× vs phconvert, tie /
  0.55× on a 3 MB file vs ptufile — fixed costs). BrightEyes `.ttr` vs the
  vendor parser libttp (recorded, 4 M words): photons, macro/micro, marker
  edges identical. Downstream of the PHT3 fix the default CLSM routine now
  recognises PicoHarp T3 markers; `test_CLSM_single_frame_ptu.py` had pinned a
  garbage 652-line "salvaged" frame built from misread photons — now one clean
  256 × 256 frame with every photon. Register/PERF/BUGS/CHANGELOG/doc.
  Later: Photon-HDF5 read vs h5py identical; the HT3 v1 sample turns out
  SF-compressed (counted overflows) — tttrlib detects it, phconvert reads it
  2.2× short; the Kristine `.cor` references are a per-time-window estimator
  tttrlib does not have (feature gap, not a validation).

## 2026-08-17 (41st entry)

* **Two more kernels off the KNOWN-ANSWER list.** (1) The 1-D max-tree behind
  `burst_search_maxtree` vs scikit-image's `max_tree`: component set identical
  on six signals and on a 2 M-sample bench signal (1.9 M components), 11×
  faster; new `max_tree_1d` binding, `max_tree` pair in the sciref set. (2)
  `HMMBayes.h`: `rhat`/`ess` vs ArviZ — R-hat identical, **ESS was not** (it
  ignored chains disagreeing in mean; replaced by the Vehtari 2021 split-chain
  estimator, now bit-identical to ArviZ; BUGS FIXED entry); the blocked Gibbs
  posterior vs hmmlearn's VB posterior on the dense fixture (means within 3 sd,
  Gibbs sd 1.2–1.5× VB's — the mean-field direction); gamma/Dirichlet variates
  KS vs scipy (batch bindings, the scalar forms were uncallable). arviz added to
  the sciref venv. Register/PERF/CHANGELOG/headers updated.

## 2026-08-17 (40th entry)

* **HmmVB: `elbo` is Beal's bound now.** The recommended option of
  [`design/hmmvb-elbo-decision.md`](design/hmmvb-elbo-decision.md) implemented:
  one sub-stochastic forward pass at the returned posterior
  (`log_z_sub_stochastic`, ~one E-step) gives `elbo` = hmmlearn's lower bound
  to 2e-10; the iteration and its fixed point are untouched and its value is
  kept as `elbo_normalised` (+ `history`), with `loglik` / `loglik_beal` the two
  data terms. Header rewritten (bound vs iteration variable, K(K−1)/2, the
  "conservative" wording), `// Validation:` block added; register row PASS,
  BUGS entry FIXED, PERF/check text updated, CHANGELOG. Example
  `plot_hmm_variational_bayes.py` + notebook (simulated two-state stream,
  K = 1..4, ELBO vs BIC, posterior ± sd vs truth). hmm suite 173 green.

## 2026-08-17 (39th entry)

* **HmmVB: an independent reference at last — hmmlearn's VB-HMM.** The brief's
  numbers had all been checked against our own transcription. On dense streams
  (dt = 1, where the photon-stream VB-HMM *is* a categorical VB-HMM)
  `hmmlearn.vhmm.VariationalCategoricalHMM` 0.3.3 gives: its lower bound at
  tttrlib's converged posterior = the sub-stochastic (Beal) bound to 2e-10,
  converged posteriors equal to 1e-4, and tttrlib's reported `elbo` sits
  0.998 / 2.999 nat above the upstream bound at K = 2 / 3 — the K(K−1)/2
  prediction, now against an outside implementation. Fixture + generator
  (sciref venv), `TestVariationalBayesAgainstHmmlearn`, `hmm_vb` benchmark pair
  (13× faster, identical). Register/PERF/BUGS updated; brief gained a §5
  "Sources" separating literature (MacKay 1997, Beal 2003, Bishop 2006,
  vbFRET/ebFRET, H2MM, Beal & Ghahramani 2003) from what is ours (the tick-chain
  extension, the engine's E, the closed-form gap). Decision on the reported
  value still the user's.

## 2026-08-17 (38th entry)

* **HmmVB ELBO: decision brief with numbers, no kernel change.**
  [`design/hmmvb-elbo-decision.md`](design/hmmvb-elbo-decision.md) settles what
  the E (engine, rows normalised) vs H (header, sub-stochastic Ã^Δt = Beal's
  bound) difference *is*: exactly K(K−1)/2 nat — ½ nat per free transition
  parameter, data-independent (within 0.07 nat over 100 fits), derived from
  the Dirichlet row mass 1 − (K−1)/(2α_i) times the ticks spent in each state.
  Against an importance-sampled exact log evidence (VB-posterior proposal,
  engine forward pass) on the H2MM_C fixture and 24 simulated K_true = 2/3
  datasets, K = 1..4: H is a valid bound, E is not licensed by anything but
  never exceeded logZ either; both trail the evidence by 2–16 nat growing
  with K (ln K! + mean-field losses), both pick the same K on every seed and
  agree with BIC (ICL always under-counts at K_true = 3). Recommendation:
  report H as `elbo`, keep the iteration (posterior differs at 1e-4
  relative), keep E's data term as `loglik`. Scripts + raw output under
  `design/scripts/`; BUGS.md entry points at the brief. Uncommitted.

## 2026-08-17 (37th entry)

* **The "flaky" Kalman A/B was a real defect.** Its second appearance in a
  16-minute regression run prompted the look: `kalman_filter`'s general
  `K = P_pred·S⁻¹` branch was written for dim = 4 and read past its buffers
  for dim = 1 — UB that surfaced only when other shapes had run first (the
  same case repeated 60× never showed it; interleaved dims did within 30
  reps). Fixed (branch general, `dim ≤ 4` cap dropped, dims 1–6 at ~1e-15
  vs the textbook), regression test added, CHANGELOG/BUGS/register updated.
  Also added `doc/validation.rst` (linked from the doc index) pointing users
  at the registers and the identity checks. Uncommitted.

## 2026-08-17 (36th entry)

* **Reading vs phconvert in the suite.** `phconvert` added to the `read`
  venv; `competitors/bench_phconvert.py` + `check_reading.py`: PTU 5.8×,
  HT3 25×, SPC-130 4.4× faster, photon-for-photon identical (photons,
  markers, resolutions); ptufile stays the PTU parity reference. PERF.md,
  README, recipes, register updated. Uncommitted.

## 2026-08-17 (35th entry)

* **Standing rule applied: examples as `.py` + executed `.ipynb` on simulated
  data for every algorithm worked on today, and the code documented.** Ten
  new sphinx-gallery scripts (blind IRF, phasor of a decay stack, watershed +
  marching squares, Richardson–Lucy, burst-feature clustering with k-means
  and the HDBSCAN pipeline, Kalman burst detection, HMM lattice, BurstML
  profiles + fit, 2CDE, background rate) each with an executed notebook next
  to it, plus executed notebooks for the existing ISM/PDA/2D-FDC/burst-search
  examples (three ISM scripts made notebook-safe). `BurstML.h` class Doxygen
  written, `TwoCDE.h` usage paragraph, module READMEs gained "Examples"
  lines. One binding added from the writers' friction list:
  `blind_irf_estimate_array` (NumPy in/out, pinned equal to the flat form);
  three more friction items closed the same day — `burst_search_kalman(...,
  warmup_bins)` (the t≈0 artefact is `x0 = 0` making R vanish on the first
  update; default 0 stays ChiSurf-identical), `kmeans_n_uniforms` /
  `kmeans_uniforms`, and the scikit-image-shaped Python `watershed(image,
  markers, mask=None, connectivity=1)` — the rest is filed in BUGS.md.
  Uncommitted.

## 2026-08-17 (34th entry)

* **Third benchmark set: the FRET / burst kernels against their upstream
  code — identical, faster on all five.** `bench_fret.py` /
  `competitors/bench_fret.py` / `check_fret.py`, five plot categories,
  PERF.md rows + identity checklist. References are the code itself: PAM's
  `PDA_histogram.cpp` and the original FRET_burstML MEX compiled natively
  through the test shims (new timing drivers in
  `benchmarks/competitors/native/`, clock inside the process), FRETBursts'
  cython KDE in its venv, and Toru Kondo's `TK_Create2DFDC_04.m` and PAM's
  `CUSUM_burstsearch` in Octave with `tic`/`toc` around the call. Results:
  PDA 4.7× (2e-18), BurstML 8.3× (3e-13), FRET-2CDE 4.2× (6e-15), 2D-FDC
  ~4700× (0 of 389 185 pair counts differ), CUSUM ~1400× (behavioural,
  Jaccard ≥ 0.87 — PAM discretises the same search differently by
  construction). No kernel needed changing. Uncommitted.

## 2026-08-17 (33rd entry)

* **Next set in the benchmark suite: the scientific-Python kernels —
  identical outputs, faster on all eight.** New `sciref` venv (scikit-image
  0.25.2, scikit-learn 1.7.2, filterpy, hmmlearn, phasorpy),
  `bench_sciref.py` / `competitors/bench_sciref.py` / `check_sciref.py`,
  eight plot categories, PERF.md rows + identity checklist. Findings on the
  way: (1) **skimage 0.25.1 reverted 0.25.0's `-inf` watershed marker seed**
  (PR 7702) — the very divergence the port had settled in 0.25.0's favour
  over ChiSurf; on the 1024² benchmark image 13 % of pixels change basin
  between the two. Followed upstream: markers enter at their image value
  (ChiSurf's original), fixture re-recorded from 0.25.2, live sweeps skip on
  < 0.25.1 with the reason. (2) k-means was slower than sklearn's Lloyd: the
  sweep now assigns points in parallel with every sum serial and in point
  order (bit-exact with ChiSurf, pins green), and the greedy k-means++
  seeding — 40 full passes over X, bandwidth-bound, 70 % of the run — drops
  its per-trial linear scans for a prefix array + binary search (same
  numbers): 59 → 30 ms; sklearn's same job (its own k-means++) is 176 ms,
  its Lloyd alone 24 ms. (3) Richardson–Lucy threads pocketfft (264 → 203
  ms). (4) A batched phasor binding (`compute_phasor_bincounts_batch`),
  digit-for-digit the per-decay one, so a stack is one call. Uncommitted.

## 2026-08-17 (32nd entry)

* **VicidominiLab kernels in the benchmark suite — identical outputs, faster
  on all four.** New `vicidomini` venv (birfi, BrightEyes-ISM, s2ISM from
  upstream + torch CPU), `bench_vicidomini.py` / `competitors/bench_vicidomini.py`
  / `check_vicidomini.py`, four plot categories, PERF.md rows and a section
  with the identity checklist. APR was *slower* than BrightEyes (282 vs
  146 ms) and only matched it away from the edges: registration on a canvas
  padded to twice the frame (4× the FFT work) and serial per-element work.
  `apr_reconstruction` now uses the reference's circular Fourier shift
  (identical, 3e-16, on any image), the reference spectrum once, and OpenMP:
  40 ms, 3.8×. `blind_irf_estimate` now solves the reference model (shared
  k, per-channel A, C — variable projection + golden section) instead of a
  log-linear tail fit with `C = min(y)`, which had left a Poisson-floor
  pedestal for RL to smear (IRF–truth corr 0.945 → 0.996 on a 1024-bin
  decay; birfi 0.993): 3.9×. focus-ISM 291× (its registration keeps a small zero-padded margin, matching focusISM's zero-filled `interp`), s2ISM 3.6× identical.
  Documentation obligations done: PERF.md, CHANGELOG, superres/decay
  READMEs, plots regenerated, header marks, this log, and the register's
  checked list. Uncommitted.

## 2026-08-17 (31st entry)

* **The rest of the library A/B-validated — decay, FCS/PCH, burst search,
  HMM/kinetics/PDA, core/corrections/util/streaming, imaging, simulation —
  register created, four more defects fixed, the open ones filed.** Same
  contract as the math round below, applied everywhere an algorithm lives:
  an independent reference first (FRETBursts, pycorrelate, multipletau,
  H2MM_C, astropy `bayesian_blocks`, phasorpy, ptufile, phconvert, PyBroMo,
  hmmlearn/filterpy fixtures; the **original FRET_burstML MEX** and **PAM's
  `PDA_histogram.cpp`** compiled natively from `junk/` and `../chisurf/junk/`
  through small `mex.h` shims; PAM's CUSUM in Octave; NumPy/scipy
  transcriptions of Coates 1968, Li & Ma 1983, Zhang & Yang 2005, Adams &
  MacKay 2007, Scargle 2013, Gopich & Szabo 2009 (via `expm`), Antonik 2006,
  Hellenkamp 2018, Chen 1999 PCH, Torella BVA, Tomov 2CDE, Isenberg 1973,
  Marsaglia & Tsang 2000, mt19937ar.c, xoshiro256++), ChiSurf's Python as the
  second check where the kernel is a port. Fifteen new permanent suites
  (`test_ab_*_reference.py` per area, ~200 tests, four compiled harnesses,
  five recorded fixtures with inputs stored beside outputs); 100+ headers
  carry the `// Validation:` block. Results by area, with metric and verdict,
  in [`testing/algorithm-validation.md`](testing/algorithm-validation.md).
  **Fixed the same day** (each with the test that found it now asserting):
  `estimate_background_rate` divided by Σt instead of Σ(t − t_thr) — 0.59×
  the true rate at the default tail fraction (and returned Hz where the
  header said kHz — now kHz, typical background 0.2–3 kHz);
  the histogram 'search' axis never filled bin 0 or the last bin; the
  Laurence cross-correlation formed `t2 − t1` unsigned and returned zeros
  whenever the second stream started first; the `fconv_ref` Python wrapper
  dropped `dt`. **Filed in BUGS.md**, pinned by tests, headers left
  unmarked: `felekyan` block lags vs the shared axis (τ mislabelled ≤ 25 %);
  `HmmVB`'s ELBO data term ≠ its own header derivation (~1 nat, model
  selection); `blind_irf_estimate` recovers position not shape; the
  simulator's rotational diffusion is a single Gaussian kick (r(t) above the
  exponential beyond ~ρ); three PTU-writer header defects an independent
  reader trips on; plus a list of unbound-from-Python surfaces and doc/code
  mismatches. Reference libraries never entered the project env — scratch
  venvs and `benchmarks/.venvs/`; `multipletau` was added to the pycorrelate
  reference venv (`build_envs.sh`). **Follow-up the same day** worked the
  open list down: `estimate_background_rate` returns kHz (user: typical
  background 0.2–3 kHz — the header's original contract; the code multiplied
  to Hz), tests at 0.2/1/3 kHz; the `felekyan` correlator got its own
  contiguous lag axis (block k at 2^(k−1)) and wahl/felekyan now agree as
  functions of τ; the PTU writer's three header defects fixed (`Header_End`
  last and typed, record count patched after the stream) and ptufile now
  decodes every record; the simulator's excited-state rotation sub-stepped
  (r(2ρ) within 0.02 of the exponential); `fconv_cs_time_axis` bound to
  NumPy; eight doc/code mismatches corrected in the headers; the PyBroMo
  benchmark PSF width corrected (PERF re-run pending). A last pass bound the
  six exposed-but-uncallable surfaces (`DecayPhasor.compute_phasor` +
  `compute_phasor_selection`, `ProductPrior` via `VectorDecayFitPrior`,
  `bincount1D`, `SimRandom.init_by_array`, `dirichlet_kl`) with array
  typemaps and A/Bs, made `TTTR.set_mt_linearizer` copy instead of adopt
  (double free), and fixed `blind_irf_estimate` — three defects (SG
  derivative abscissa mismatch, circular FFT at n, RL back-projection off by
  one per iteration) plus a weighted log-linear tail fit; the IRF shape now
  comes back (99.5 % of the mass in place, was 25 %). On the user's
  direction the reference for it is **VicidominiLab's `birfi`** (cloned into
  `../chisurf/junk/birfi`, registered in its `clone.sh`; ChiSurf's
  `irf_estimation.py` is a port of it): run in a subprocess over four
  configurations × 30/500 iterations, aligned IRFs correlate 0.978–0.999 —
  after undoing birfi's n/2 `ifftshift` roll, which the A/B pins; birfi's own
  Adam lifetime fit is 5–38 % off and forgiven by RL. That check also
  corrected the record: the circular forward model is birfi's periodic
  model, not a defect (kept, made exact for any n); the port's own defects
  were the SG derivative and the centroid lifetime. The same direction applied
  to the other VicidominiLab-derived kernels: live A/Bs against BrightEyes-ISM
  `APR_lib.ShiftVectors` (bit-identical), `APR_lib.APR(mode='fourier')`
  (1e-9), `FocusISM_lib.focusISM` (corr > 0.98, bg fractions within 0.02) and
  the torch `s2ISM` package (1e-6, after pinning that its `max_iter=n` runs
  n+1 updates and that it crops even sizes) — where the suite had used
  transcriptions. Final run: **279 passed, 0 xfailed** (+ the birfi, ChiSurf,
  BrightEyes-ISM and s2ISM A/Bs since). The only finding left is the HmmVB ELBO question,
  filed as a modelling decision. Everything uncommitted.

## 2026-08-17 (30th entry)

* **`modules/math` A/B-validated end to end against independent references;
  register created; one RNG defect found and fixed.** Every kernel in the
  module — 19 headers — now has an A/B test against a library the code was
  not ported from (sklearn NearestNeighbors / KMeans / HDBSCAN, scipy
  minimum_spanning_tree / nnls / L-BFGS-B / Nelder-Mead / logsumexp /
  signal, scikit-image watershed / find_contours / richardson_lucy,
  numpy.linalg solve / inv / lstsq / eig / matrix_power, hmmlearn and filterpy
  through a recorded fixture, Random123 known-answer vectors, O'Neill's pcg32
  and the canonical SplitMix64), with ChiSurf's Python as the *second* check
  where the kernel is a port of it. Four permanent suites,
  `test/python/misc/test_math_ab_{clustering,imaging,probabilistic,numerics}.py`
  (72 tests, 209 subtests, ~17 s; reference libraries optional and skipped
  visibly), plus `test/cpp/ab_numerics_harness.cpp` for the unbound C++
  (NelderMead, i_lbfgs, Mat, QREigen, Random, Sampling, SimPcgRandom, Nnls)
  compiled once by the pytest. Results are in
  [`testing/math-kernel-validation.md`](testing/math-kernel-validation.md),
  and each header carries a `// Validation: A/B-TESTED 2026-08-17 -- ...`
  block after its include guard naming reference, metric and test. Headline
  numbers: core distances to 4e-16, MST weight multiset bit-identical to
  scipy, HDBSCAN partitions identical to sklearn from either side's tree
  (36/36; end-to-end differs only where sklearn's unstable argsort orders
  tied edges), k-means to 1e-14 of sklearn's Lloyd from the same seed,
  Kalman to 1e-14 of filterpy and a textbook filter, HMM forward/backward/
  Viterbi bit-identical to hmmlearn, watershed and marching squares exact vs
  skimage over > 1400 cases, Richardson-Lucy to 1e-15 of skimage across every
  PSF parity, Philox bit-exact to Random123, SimPcgRandom bit-exact to
  O'Neill. **Found:** `Random.h`'s PCG engine wrote the XSH-RR xorshift as
  `(state >> 18) ^ (state >> 27)` instead of `(state >> 18) ^ state` — a
  19-live-bit word, per-bit P(1) 0.22–0.37; fixed (one token), the two tests
  that pinned it as expected failures now assert. Reach was
  `TTTR_RNG_ENGINE=pcg` and the non-deterministic default, never the Philox
  default. Also recorded: `mt19937` is a name that falls through to Philox
  (pinned, unchanged); `skimage.restoration.wiener` and ChiSurf's `mem.py`
  are not valid references for `wiener_deconvolve` / `run_mem` (different
  estimator; value/gradient of two objectives) — documented rather than
  compared loosely; `quadpr_bound`'s non-KKT caveat pinned in shape. All
  uncommitted, like the rest of the bundle.

## 2026-08-16 (29th entry)

* **PRD-037 B2 `kmeans` — kernel, binding, tests, benchmarks, validated —
  and the FMA-contraction contract moved from the build into source.**
  Followed the PRD and the B1 precedent: Python prototype against chisurf's
  `_kmeans.py` first (pure-Python since the numba removal), then the C++
  port (`modules/math/include/KMeans.h`, `src/KMeans.cpp`), a NumPy-typemap
  binding in `Cluster.i`, and a committed fixture recorded from chisurf's
  implementation on a fixed stream
  (`test/data/reference/kmeans_chisurf_reference.npz`). Of the 7 parity
  configurations the first full run matched 6 bit-for-bit and the seventh --
  n=1000 d=5 k=8 n_init=4, restart 2 -- had centres and labels **identical**
  but inertia one ulp apart. Root cause: FMA contraction (`acc += diff*diff`
  fused into a multiply-add, one rounding instead of two; clang's default
  `-ffp-contract=on`). First response was to add `-ffp-contract=off` to
  `KMeans.cpp`, which made 7/7 bit-identical — and that was right to flag:
  a per-file CMake compile option is build configuration carrying a
  load-bearing contract, and `if(NOT MSVC)` skipped Windows entirely, so any
  contracting build silently re-opened the ulp drift. The fix is the
  standard `#pragma STDC FP_CONTRACT OFF` at the top of `KMeans.cpp` and
  `Cluster.cpp` (B1's file, same contract), with the CMake flag removed. A
  probe confirms the pragma beats clang's default `on` and gcc's default on
  a bare `-O2` build (both compiled one ulp off without it); `-ffp-contract=
  fast` still outranks it, which is an explicit library-wide IEEE opt-out
  and out of scope. Rebuilt with no per-file flag: 7/7 k-means configs
  bit-identical again, HDBSCAN + cluster + kmeans suites green (28 tests),
  and `TestAgainstTheRecordedReference` re-measures the returned centres in
  Python and asserts exact equality, so a future contracting build fails the
  suite instead of drifting silently. Also moved the uniforms-length
  validation *before* the degenerate branch so a wrong-shaped stream is
  rejected even when n_samples ≤ n_clusters. 8 tests land in
  `test/python/misc/test_kmeans.py` (known-answer blobs where membership is
  fixed before clustering; bit-for-bit determinism; a guard that different
  uniforms may differ; wrong-length rejection with the required count in the
  message; the degenerate case; the committed-fixture pin including the
  re-measurement check). One test had to be written around a real Lloyd
  behaviour shared with the reference: an empty cluster can survive as an
  orphan re-seeded centre, so the assertion runs over occupied clusters.
  Benchmarks: 919× at n=2k (4.3 ms vs 4.0 s), 418× at n=10k (41 ms vs 17 s),
  904× at n=50k (1.75 s vs 26 min). SWIG four-language guard clean. PRD-037
  B2 ticked, board T-20260816-03 ⟶ ✅ done (validated); all of it uncommitted
  like the rest of the bundle.

## 2026-08-16 (28th entry)

* **2D-FDC log-axis tick quantization fixed by proof against the original
  MATLAB, and the method papers cited.** The user demanded proof of PRD-036's
  parity claim, so the original `TK_Create2DFDC_04.m` itself was run (Octave,
  `junk/2D-FLC-code` in chisurf) against the library on a shared 3000-photon
  stream. Linear matrices were identical; log matrices were not — ~0.5% of
  pairs in different bins at both `lint_bin_factor` 1 and 8. Root cause: the
  .m keeps its log edges real-valued and compares the integer tick against
  them, so the effective integer edge is the floor, while the kernels (and
  chisurf's numba original before them) quantized to nearest. Prototype-first
  in Python (floored caller axis, 6/6 identical) before the one-line C++
  change (`build_log_ticks` floors, `Fdc2D.cpp`), re-proven through the
  production path (9/9 identical). The .m's outputs are now a committed
  fixture (`test/data/reference/fdc2d_matlab_tk_create2dfdc04.npz`) pinned by
  `TestAgainstTheOriginalMatlab` — the one fixture recorded from the
  authoritative implementation rather than the code under test; existing tests
  that had pinned the round convention were rewritten (edge test to floor;
  two-axes totals test to a hand-built asymmetric axis). Chisurf fallout
  handled: `flc_2d_fdc.npz` re-recorded through the delegated path (call-site
  pin, correctness anchored upstream), `test_fdc_parity.py` docstring updated,
  7/7 parity green.   Papers cited with Crossref-verified DOIs in `Fdc2D.h`, the
  fcs README, and chisurf `flc_2d/{__init__,api,core}.py` (Ishii & Tahara
  JPCB 2013 ×2; Kondo et al. PNAS 2019). 35 fcs tests + 61 subtests green;
  chisurf flc_2d 21 passed / 6 skipped (data-file skips, pre-existing).
  Follow-ups same day: (a) `kondo2019` added to chisurf's bibliography yaml
  and the Literature page regenerated through its own generator (the four
  deconvolution/PSF entries a concurrent agent had hand-added to the
  generated file were back-ported into the yaml first so the regen did not
  destroy them — generated files must be regenerated, never hand-edited);
  plugin README and gui help now carry the full citations. (b) A full audit
  of the 46-file MATLAB corpus recorded in PRD-036: the photon pass is C++
  and MATLAB-proven, the inversion/fit family is ported in chisurf `fit/`
  (each file marked "Port of TK_*" by name), two items are honestly
  unported (reproduct-from-parameters, split-data bootstrap). (c) Two
  gallery examples: `plot_fdc_2d.py` (walkthrough, 0.91 s recovered vs 1.00 s
  simulated) and `plot_fdc_2d_dynamics_resolution.py` (the benchmark: two-state
  relaxation resolved within 25% from 50 ms to 10 s, floor = the 80 ms lag
  window; 3-state two-timescale resolved on the seed-averaged curve at
  0.86/7.9 s vs 1/10 s true, slow mode noise-limited per-stream). Two
  plausible-wrong-number traps found while building it are recorded in the
  PRD (lag grid must straddle every claimed timescale; curve_fit's taus sit
  at popt positions 1 and 3, not 1:3). (d) The microsecond question, asked
  next ("can it recover 200 ns - 10 us on a single molecule? simulate fret"):
  `plot_fdc_2d_microsecond_fret.py` — T3 clock at the 25 ns laser period,
  immobilized FRET molecule, E = 0.2/0.8, 500 kcps — recovers every
  relaxation 200 ns–10 µs within 25% (3 seeds, ±3% spread, −15…−22%
  estimator bias). First attempt had flat D curves: the engine's state log
  proved kinetics perfect (dwell 1.9985 µs vs 1/k = 2 µs) and the defect was
  a caller unit slip — `SimIntegrator.dt`/rates are SECONDS,
  `microtime_resolution`/`laser_period` are NS, and a resolution passed in
  seconds flattens the decay pattern into uniform micro-times.
  `SimIntegrator.h` now documents the split; the diagnostic sequence
  (state log → pair covariance → D curve) is recorded in PRD-036.
  (e) The diffusion follow-up ("now do with diffusioning molecules 2 ms
  diffusion time"): `plot_fdc_2d_microsecond_fret_diffusion.py` — open
  volume, surface-flux injection (a closed box lets molecules walk away,
  76→3 kcps), τ_diff = 2 ms, ~0.15 focus occupancy. µs recovery survives
  dilution (200 ns–10 µs within 0.7–27%); a 5 ms relaxation is gated by
  diffusion (ceiling = τ_diff, mirroring the 75 ns window floor). Two
  measured findings en route: the TV coupling statistic has a √(K/4N)
  shot-noise pedestal (0.018 flat in single-lifetime and shuffled controls;
  invisible in the immobilized case) — the pair micro-time covariance from
  the same matrix is bias-free and replaces it there; and engine windows
  coarser than the T3 clock (2.5 µs, ticks = (w·dt+arrival)/25 ns) cut
  runtime 25× with nothing load-bearing lost. `plot_lifetime_fcs.py`'s unit
  comments (dt "ms", D "µm²/ms") mislabel the engine's actual units
  (seconds, µm²/s) — recorded in PRD-036, left for its owner.
  (f) PRD-037 B1 validated on pick-up (T-20260816-02): the kernels were
  already landed (`9e55b6b22`, `hdbscan_condensed_tree` +
  `hdbscan_label_points`, two calls split at the selection-policy boundary)
  so the session ran the validation instead of rewriting: 12/12 in-tree
  tests; condensed trees bit-identical to chisurf's implementation over 12
  dataset × min_cluster_size configs; end-to-end labels identical on 6
  ground-truth sets; sklearn's independent HDBSCAN 4/6 exact, rest ≥ 0.996
  purity; post-MST 1.4/9.1 ms at n=20k/100k vs 72/380 ms for the Python
  path chisurf runs today (42–51×, ~10× over the old numba). One input
  contract worth recording: the edge list must be (low, high)-normalized
  and weight-sorted exactly as chisurf's `single_linkage_tree` feeds its
  own linkage — raw endpoints give an equivalent tree with different node
  numbering. CHANGELOG gap filled (the landing commit had no entry); PRD-037
  B1 ticked; chisurf delegation still open under T-20260811-20.

## 2026-08-15 (27th entry)

* **PRD-003 (docs deploy via rattler) closed as superseded, and a
  `Superseded` status marker added to the PRD lifecycle.** Checking it
  against ground truth before starting work found its foundation deleted:
  the 2026-08-13 CI cost rework removed the `build_conda_docs` rattler job
  (`db7daa710`, `d48521f3f`), leaving a comment where it sat explaining why
  the M1 wiring was deliberately *not* finished — the job's `docs-html`
  artifact had never been downloaded by anything, and `build_docs` compiles
  the library for autodoc regardless, that compile being the expensive part
  rather than the Sphinx render, so an artifact round-trip would have added
  cost instead of removing the duplicate. The PRD's underlying goal (one doc
  build path in CI, not two) is true by construction now; `recipes/docs/`
  stays as the documented reproducible offline build (`BUILDING.md`), just
  without a CI consumer. Closed honestly rather than implemented against a
  deleted foundation; new `🚫 Superseded` lifecycle state ("overtaken by
  events; do not implement; kept for the record") since ⚫ Deferred means
  valid-but-parked, which this is not.

## 2026-08-15 (26th entry)

* **Historic-MaxEnt search redesigned after the bisection failed on a steep
  real case (PRD-039 rework).** The 1M-photon FRET figure (Gaussian distance
  distribution, 100-point R grid) exposed it: chisq(nu)'s transition there
  is steep, and the outer bisection — cold-starting the fixed-nu `run_mem`
  once per nu probe — exhausted 500 cold MEM solves (~143–195 s, replicated)
  stuck at chisq 0.98 against target 1.0, converged=false. The failure is
  structural: one full MEM convergence per sample, one sample per ~40× nu
  step. Replacement (prototyped in Python against that exact failure before
  porting): a joint (p, nu) Gull-Skilling controller — one bound-QP Newton
  step on the amplitudes at the current nu, then a secant move of log(nu) in
  (log nu, log chisq) space toward the target, warm-started throughout,
  moves clamped to ×30 per iterate. Converges to chisq 1.0000 in 157 warm
  QP steps (~4 s, all C++) where the bisection failed; A/B in the Python
  prototype: 300 QP solves/1.4 s vs 194 s failed. API simplification rode
  along: `nu_lo`/`nu_hi`/`max_outer_iter` plumbing dropped from all three
  call surfaces (it was all uncommitted), the caller's `nu` now seeds the
  controller, and the cap defaults to 1000 with callers flooring `max_iter`
  there — measured: fixed-nu run_mem converges in ~20 outer iterations but
  the joint interleaving needs 647 on the lifetime fixture, so a
  fixed-nu-appropriate cap silently under-runs the search. The killer
  fixture is pinned as
  `TestTcspcMemFret::test_target_chisq_converges_on_a_steep_fret_case`
  (recovery mean 44.2 Å / sd 5.1 Å vs truth 45/4, bands catching both
  classic MEM failure modes), and `mem_dgrad`/`mem_entropy` were extracted
  as shared helpers so `run_mem` and the controller cannot drift on what
  the TEST quantity and S mean. Figure regenerated with the converged
  solver: `doc/img/maxent_fret_distance_recovery.png`; its permanent
  generator is the gallery example
  `examples/fluorescence_decay/plot_maxent_fret_recovery.py` (fixed seeds,
  ~50 s end to end, prints the converged nu/chisq).

## 2026-08-13 (25th entry)

* **`DecayFit23`'s general (tau/gamma) fit branch converted to an exact
  forward-mode gradient (PRD-010 Phase 6).** `fconv_per_cs_ad`/`Wcm_ad`/
  `log_m_ext_ad`/`soft_floor_ad`/`clamp_value_ad` are templated siblings of the
  existing `double` kernels, not replacements; the fit's `set_bounds`-only
  bound mechanism (Phase 5c/5d) was the prerequisite that made this a
  templating exercise. Measured A/B: one `Fit23()` call 1.27× faster, `fit_many`
  on 8000 rows 1.68× faster wall clock (`tttrlib::parallel_for`, all cores),
  fitted values unchanged. `Wcm_p2s` stays on central differences (not
  templated). See PRD-010, `PERF.md` and `CHANGELOG.md` for the numbers.
* **One conformance case re-pinned as a direct, predicted consequence.**
  `decayfit.fit23_published_answer` runs at `1e-6` tolerance, tight enough that
  the exact gradient's more trustworthy `EpsG` termination (documented in
  `i_lbfgs.h`'s `set_gradient`) stops the fit at a measurably different point
  along the same flat tau valley: objective 23.79112398420848 →
  23.791082287859183, tau 0.721353235715964 → 0.7212727630506686. Cross-language
  answer key, so Python/R/Java/JS all move together from one JSON edit.
* **`DecayFit24` got the same AD conversion (PRD-010 Phase 7), built and
  verified -- then declined and removed, because the measured win was not
  clear.** `tau1`/`tau2` moved to `soft_floor` first (this part shipped and
  stays); `A2`/`gamma`/`offset` stay deliberately hard-clamped either way.
  `fconv_per_cs_ad`/`Wcm_ad` reused unchanged for the gradient itself, verified
  to the finite-difference floor (`test/cpp/test_ad_gradient.cpp`'s `decay24`
  section, kept as the record it was correct), and existing regression/
  conformance suites passed with no re-pin needed. Measured at 8000 rows: wall
  clock 1.08x-1.15x faster, but total CPU across worker threads -- the more
  repeatable metric -- roughly flat to 6% slower, against `DecayFit23`'s clean
  1.3x-1.7x. **Declined on that measurement** (same call already made for
  `DecayFit26`): the gradient callback was removed from `DecayFit24.cpp`, a
  note in the source says what was tried and why, and the two throwaway A/B
  benchmark scripts were removed with it -- the numbers live in PRD-010,
  `PERF.md`, `CHANGELOG.md` and here instead. Likely cause: `i_lbfgs`'s own
  per-iteration overhead absorbing the gradient-level saving (5.44x in
  isolation, Phase 4) against a comparatively cheap 128-bin model.
* **The central-difference step retune (PRD-010 Phase 8) landed, closing PRD-010
  entirely.** `bfgs` (`i_lbfgs.h`) was using `sqrt(eps)` -- the forward-difference
  optimum -- as its central-difference step, because it shared the `sqrt_eps`
  member with two unrelated convergence thresholds (`EpsG`, `EpsX`). Gave the
  step its own member, `fd_eps = eps^(1/3)`, set alongside `sqrt_eps` in
  `seteps()` without repurposing it. This is not a narrow fix: every `bfgs`
  consumer still on central differences benefits (`DecayFit24/25/26`,
  `DecayFit23`'s `p2s_twoIstar` branch, `fit_linked`, plugin fits), so before
  touching it I surfaced the actual blast radius to the user rather than
  quietly landing a change with that reach -- they chose "do it fully, re-pin
  whatever moves." **Nothing moved**: the full C++ (`ctest`) and Python suite
  (2698 passed, 74 subtests, two unrelated pre-existing failures) ran clean at
  zero re-pins. Also corrected PRD-010's own Correction 3, which had predicted
  a smaller-than-6x win from the retune: measured against the step actually
  replaced (not the never-used `eps` baseline Phase 4's original table
  compared against), the real gain is 62x-440x more accurate
  (`benchmarks/bench_ad_gradients.cpp`, updated to measure it) -- `sqrt_eps`
  being numerically closer to zero than `eps^(1/3)` is not the same as being
  the right size for a central difference, whose error is `O(h^2) + O(eps/h)`.
  PRD-010 flipped to Done.
* **Follow-up, same day: `EpsG`/`EpsX` don't share a variable either, and `EpsG`
  finally acts on a promise its own docstring made (PRD-010 Phase 9).** Asked
  to make the fix an architectural one -- if giving every conflated quantity
  its own identity also makes things more accurate, better -- so `sqrt_eps`'s
  remaining two roles (gradient-norm convergence `EpsG`, step-size convergence
  `EpsX`) got split into `epsg`/`epsx`, each with its own `set_epsg`/`set_epsx`
  setter, matching the class's existing per-concern API. `set_gradient()`'s
  docstring has said since Phase 6 that an exact gradient "makes the EpsG
  termination test trustworthy at tight tolerances"; the code never acted on
  it. Now it does: registering a gradient auto-tightens `epsg` to `eps`
  (`sqrt(eps)`'s ~1.49e-08 was a central difference's noise floor, not an
  exact gradient's, which is `eps`-scale, ~6.7e7x tighter), and un-registering
  restores `sqrt(eps)` -- both skipped if a caller has called `set_epsg`
  explicitly. Verified against both AD-gradient consumers specifically
  (`DecayFit23`, `ImageLocalization`) plus the full suite: zero regressions,
  same re-pinned `fit23` conformance value as before (that fixture's
  termination was not `EpsG`-bound -- a legitimate, data-dependent outcome,
  not evidence the fix does nothing elsewhere).
* **Follow-up, same day: `FitNExp` revisited and shipped a real improvement
  (PRD-010 Phase 10), without touching the reason its optimizer was chosen.**
  `DecayFitNExp.cpp` never used `bfgs`, deliberately -- amplitudes are
  profiled by EM (closed-form given fixed lifetimes) and lifetimes are
  searched one at a time by a multistart-aware Brent search, both real
  properties, not a stopgap. Investigated a handover note
  (`okf/handover/flim-performance-opt.md`) that had flagged AD as a possible
  future item and been closed as "not a candidate" without the narrower
  question actually being tested: not "does it use bfgs" but "does a joint
  step on top of the search recover anything coordinate-wise updates cannot
  see." Answer: yes, measurably. Added a joint `bfgs`+AD refinement pass,
  additive after the coordinate search converges -- amplitudes stay profiled
  by the same EM, the AD gradient holds them constant (exact by the envelope
  theorem: the EM optimum's own zero-gradient condition kills the term
  through weights' dependence on lifetime). Multistart is not reimplemented;
  a prototype (`benchmarks/bench_fitnexp_bfgs_ad.cpp`, kept) confirmed a cold
  joint start without it can land in a worse basin than Brent's grid scan,
  on data the same refinement improves when started from Brent's own answer.
  Measured before shipping, per instruction ("if cheap proceed check perf"):
  at realistic batched photon counts (`fit_batch_flat`, not a toy single
  case), 210 ms/row baseline, 1.02 ms/row refinement, **0.5% overhead**,
  **100/100** rows improved, **0** regressed. `bfgs`'s Armijo line search
  only accepts strictly decreasing steps, so this cannot make an answer
  worse by construction. **One real regression found and fixed before
  shipping**: N=1 (mono-exponential, the library's most benchmarked path)
  has no cross-lifetime correlation to recover, so the refinement there was
  pure overhead -- measured +35% per call against `bench_tttrlib.py`'s own
  published benchmark, for an unchanged answer. Gated to N>=2; N=1
  re-measured back to the unmodified baseline. The fixed-lifetime `fit_map`
  path (the actual `PERF.md` 140 ms headline number) is structurally
  unreachable by this change -- confirmed by reading the `any_free` guard,
  not by re-running that benchmark.
* **Two independent MaxEnt implementations found while investigating "pattern
  fit"; consolidated onto one, fixing a real sign bug on the way (PRD-038).**
  `MaxEntTcspc.cpp` (decay) had the real Skilling-Bryan (1984) algorithm,
  verified against simulated data; `MaxEnt.cpp` (corrections) had a separate
  projected-gradient implementation whose entropy term was measurably
  backwards -- `S = -3.0` at the uniform prior (should be its maximum, `0`)
  and `S = +12.9` for a spiky far-from-prior solution, so its regulariser
  rewarded moving away from the prior instead of penalising it. Both engines
  are byte-identical-algorithm now: `modules/math/include/MaxEntQp.h` /
  `src/MaxEntQp.cpp` (`quadpr_bound`, `run_mem`, plus a new
  `build_normal_equations`), with `MaxEntTcspc.cpp`'s two functions and
  `MaxEnt.cpp`'s `maxent_invert` both reduced to thin wrappers over it. Public
  signatures unchanged. Verified: all 15 `test_maxent_tcspc.py` cases pass
  unchanged (same algorithm, relocated); both `TestMaxEnt` cases in
  `test_corrections.py` still pass -- their tolerances were always loose
  enough to hold under either engine, so this is a real, previously
  uncaught behaviour change, not a no-op refactor.
* **A general N-arbitrary-pattern fit, requested by name ("general NNLS
  pattern, maybe with regu try thikonov and maxent") after "what about pattern
  fit?" turned up that `DecayFit26` only mixes a *fixed pair* with one
  constrained fraction, and `DecayFitProblem::patterns` -- built for the C-ABI
  plugin interface -- had no built-in consumer (PRD-038).**
  `modules/spectroscopy/decay/include/DecayPatternFit.h`: `decay_pattern_fit`,
  three modes sharing one design matrix (`build_normal_equations`) --
  `kNone` (plain NNLS), `kTikhonov` (L2, bound-constrained via
  `quadpr_bound`), `kMaxEnt` (the now-shared Skilling-Bryan engine, toward a
  uniform or caller-supplied prior). NNLS itself is new:
  `modules/math/include/Nnls.h`, the classical Lawson-Hanson (1974)
  algorithm -- deliberately not `quadpr_bound`'s active-set sweep at `nu=0`,
  since that solver's own docstring says it is not KKT-correct. Verified
  against `scipy.optimize.nnls` directly (max abs diff `<2e-10` across clean,
  noisy, and near-collinear pattern sets); Tikhonov and MaxEnt checked for
  non-negativity and monotonic shrinkage toward zero/prior with increasing
  regularisation strength. SWIG-bound in all four languages
  (`ext/python/DecayPatternFit.i`, following `MaxEnt.i`'s plain-`std::vector`
  pattern, included from all of `ext/{python,r,java,js}/tttrlib.i`).
  `test/python/decayfit/test_decay_pattern_fit.py`, 9 cases.
* **Historic MaxEnt (PRD-039): opt-in joint chi²+nu optimization, built
  test-first in Python and validated against simulator data before any C++
  existed** — both per explicit instruction ("before impl in cpp do tests in
  python"; "the maxent solver must be tested against sim data generated by
  sim"). A pure-Python prototype of the bisection-in-log(nu) search, running
  on the already-bound `tcspc_run_mem`, surfaced two facts that shaped the
  final algorithm and would otherwise have been C++ debugging: (a) run_mem
  at exactly nu=0 lands ABOVE the truly reachable chi-square floor (1.679 vs
  0.919 at nu=1e-8 — the unregularised QP on the near-singular lifetime-grid
  H is ill-conditioned; tiny nu>0 is an interior-point regulariser), so
  there is deliberately no analytic floor precheck, only downward bracket
  expansion; (b) the nu→∞ ceiling IS analytic (quadratic form at the prior,
  zero solves). Also caught in the prototype phase: `tcspc_build_fi_lifetimes`
  returns Fi already divided by sigma — double-weighting it moved the floor
  from 0.92 to 43, a mistake now warned about in the test helper. The sim
  gate (`TestTcspcMemOnSimulatedPhotons`) generates decays with `SimEngine`
  (background zeroed — the simulator's unconfigured background is a
  micro-time-0 spike; IRF dt exactly laser_period/n_channels), and pinned a
  real effect: the sim-vs-fconv discretisation residual scales with photon
  count (floor 0.92 at 0.3 ns IRF/1024 bins vs 1.38 at 0.1 ns/512), so the
  two-lifetime test probes the floor and targets floor×1.2 rather than
  betting 1.0 is reachable. C++ (`run_mem_target_chisq`, MaxEntQp.h;
  `target_chisq` trailing params on `solve_tcspc_mem_lifetime`/`_fret`;
  `kMaxEntTargetChisq` on `decay_pattern_fit`) agrees with the frozen
  prototype to 1e-6 on nu and 1e-9 on amplitudes — the prototype stays in
  `test_maxent_tcspc.py` as the reference. Opt-in, never default;
  `target_chisq=-1` is asserted byte-identical to the pre-change path. The
  PRD carries the honesty framing: chisurf deliberately rejects this
  technique ("the wrong answer fits better") and ships only an L-curve
  suggestion. Also answered on the way: the MEM axes were already fully
  general (caller-supplied arbitrary tau/R arrays, no spacing assumption) —
  nothing needed changing for log-spaced grids.

## 2026-08-12 (24th entry)

* **`autodiff` removed; `modules/math/include/Dual.h` replaces it.** The
  vendored package was 20 headers and ~10k lines — forward dual, forward real,
  reverse `var`, four Eigen bridges, Taylor series — and the whole library used
  one class template and two elementary functions from it, in one file. The
  replacement is ~200 lines, a third of it comment. Same story as Eigen before
  it: a third-party package every build had to find, for one struct member.
* **The dependency was really an undocumented hook, not a package.** The
  localization fit carries a whole `GradVec<N>` in the derivative slot of a
  `Dual`, which upstream documents as a scalar; it compiles only because
  `NumberTraits` can be specialized to say otherwise. `test/cpp/test_ad_gradient.cpp`
  existed *because* of that — an upstream bump could keep compiling and
  silently propagate wrong derivatives. Taking the code in-house deletes the
  hazard rather than watching it: `Dual<G>` takes the carrier as a template
  parameter, because that is the point of the class.
* **Converted as a measured A/B, before deleting the thing being compared
  against** — the only order in which the comparison is possible, and worth
  repeating for the next vendored-package removal. Three levels: gradient
  (400 random points, agreement to 2.3e-13, and *closer* to a long-double
  reference than autodiff in 2336 of 4800 components), fit (44 end-to-end
  `fit2DGaussian` cases — **bitwise identical minimised objective in every
  one**), and speed. The harness was throwaway and lived outside the tree; to
  re-run it, both sides come out of git — `git show <this commit>^:thirdparty/autodiff/...`
  and the old `ImageLocalization.cpp` — compiled against the current header.
* **The A/B answered a question that was on file as unexplained.** PERF.md
  recorded a reproducible dip in the AD scaling curve at N = 32 — gain 5.7×
  between neighbours at 13× and 8.7× — noted as "not noise, changes no
  decision" and left. It was autodiff's expression templates materialising
  temporaries: the same row is now an 18.4× gain, 0.72 → 0.15 ms. The whole
  tail moved with it. *"Reproducible, not noise, changes no decision"* is how a
  fixable 5× hides for a release.
* **The test changed character with the code.** It no longer guards a foreign
  contract; it checks an implementation — every `Dual` and `GradVec` operator
  against a hand-written derivative, then the objective differentiated four
  ways (vectorized dual, scalar dual, long-double dual, central differences).
  The long-double path is new and is what makes the tolerances mean something:
  it separates rounding from a wrong formula.

## 2026-08-11 (23rd entry)

* **The capability constants a binding reads were decided by SWIG, not by the
  compiler.** `TTTRLIB_COMPILE_NEON` / `TTTRLIB_COMPILE_AVX` are `#define`s
  gated on `__aarch64__` / `__x86_64__`; SWIG's own preprocessor evaluates
  them at wrap time and defines neither symbol, so both were frozen at `0` in
  every binding on every platform — on a machine where NEON is compiled in and
  running at 1.87×. Fixed by hiding the macros from SWIG (`#ifndef SWIG`) and
  answering through `get_neon_compiled()` / `get_avx_compiled()`, functions
  the library's own compiler evaluated. One guard, all four bindings.
* **The generalisable shape**, worth more than the instance: any `#define`
  gated on a *compiler-supplied predefined macro* that reaches a binding
  through `%include` is fabricated, and a fabricated constant sitting next to
  a truthful function is worse than no answer — the two disagree and the
  wrong one has the more obvious name. Audited the rest of `info.h`; nothing
  else escapes.
* **Found by running `tools/check_swig_multilang.sh`: three of four bindings
  had not generated for a day.** `%pythonappend` — added to the *shared*
  `ext/python/TTTR.i` and `CLSM.i` by the `TTTR.header` keep-alive fix — is an
  "Unknown directive" **error** in the R, Java and JavaScript backends, which
  parse those same files. Wrapper generation stopped at the first one while
  `pip install -e .` kept succeeding, so nothing local showed it. Guarded with
  `#ifdef SWIGPYTHON`. **The rule: run the four-language check after touching
  any `ext/python/*.i`** — a Python-only *directive* is a hard error
  elsewhere, though `%feature("pythonappend")` in feature form is portable.
  The underlying lifetime hole remains open in the other three bindings.
* **`fconv_simd` / `fconv_per_simd` deprecated**, closing the entry that filed
  them as suspiciously slow. They were never slow and never fast: each is a
  one-line forward to `fconv` / `fconv_per`, which already dispatch on CPU
  feature and problem size. The defect was the name promising a scalar/vector
  decision the caller does not have. Shim kept one release (both are
  exported); internal callers already moved off.
* **Filed rather than fixed**: a wall-clock assertion in the unit suite
  (`test_the_recursion_is_faster_at_every_rate_count`) inverts under machine
  load. The example's best-of-50 timing is sound; asserting a strict
  inequality at the smallest problem size is not. Left for the example's
  author — a timing assertion quietly weakened by whoever trips over it stops
  meaning anything.

## 2026-08-11 (22nd entry)

* **Update**: the 20th entry's regression is fixed for the MaxEnt family — all
  nine entry points in `ext/python/MaxEntTcspc.i` on NumPy typemaps, calls
  18–69× cheaper, behaviour unchanged (decayfit + GIL still 111/1).
  Numbers, the three new SWIG traps and the narrowed worklist are in
  [The Python seam costs ~50 ns per element](/bindings/marshalling-cost.md).
* **Still open, now top of that concept**: no progress/cancel callback on the
  MEM solvers, which is what forces ChiSurf's duplicate `_run_mem`.
* **Correction**: the concept cited `fconv_simd` at 1.01× as an optimisation the
  binding hid. It is a one-line alias for `fconv`; NEON is alive at 1.87×. Kept
  as a refuted inference — "the wrapper must be hiding it" is a hypothesis.

## 2026-08-11 (21st entry)

* **Amendment**: [PRD-034](/prds/PRD-034-pto-native-tttr-sink.md) gains
  *Writing a file that is still being measured* — a **checkpoint** operation
  on the writer, plus acceptance criterion 6 (`SIGKILL` a writer, keep the
  committed photons). Found while scoping chisurf PRD-98, which wants
  acquisition to write photons into a `.pto` as they arrive: 034 as first
  written described a **write-once** sink, and under `pto.rst`'s own rule
  that *bytes after the end of the `Segment` are not part of the file*, an
  hour of streamed-but-uncommitted `FileData` is not a short file — it is
  **no file**. The format already carries every mechanism the fix needs and
  says why each is shaped that way: the eight-octet `Segment` size
  "rewritten as the file grows", the 8 KiB `SeekHead` reserve that exists
  "because the commit protocol depends on being able to rewrite one where
  it lies", the over-wide `FileData` size VINT, and `PtoRowCount`. What was
  missing was the *obligation* to use them periodically and an API to do
  it. Checkpoint order is normative (FileData size → PtoRowCount → Segment
  size → SeekHead) so no intermediate state is a file a reader misreads,
  and cost is independent of data already written.
* **Gap found, no home yet**: there is **no streaming intensity trace**.
  `compute_intensity_trace` is a batch free function (`TTTR.h:131`) and
  `modules/streaming/include/` holds four headers (correlator, decay
  histogram, burst detector, CLSM image — `StreamingPhasor` lives inside
  the decay-histogram header). The MCS trace is the live count-rate display
  a single-molecule operator actually watches, so chisurf PRD-98's
  "append per-chunk bins" has nothing to delegate to. Recorded as a named
  requirement inside PRD-98 rather than a PRD of its own: one class,
  `push_photons` + fixed bin width + append-only counts, with the batch
  function as its oracle.

## 2026-08-11 (20th entry)

* **Creation**: Added [The Python seam costs ~50 ns per element](/bindings/marshalling-cost.md).
  `misc_types.i:61` declares `%template(VectorDouble) std::vector<double>` for the
  whole library, so every such binding marshals through the Python sequence
  protocol — one `PyFloat` per element, in and out, not a memcpy. Measured on
  arm64 / 0.27.0 against the NumPy in-place `fconv`: 9.1 vs 1.3 µs at n=64,
  26.5 vs 2.6 at n=512, 335 vs 10.8 at n=4096, 1360 vs 68 at n=16384. Per
  element: 50, 50, 66, 98 ns at n=1024/2048/4096/8192. `tcspc_shift_lamp` with a
  **zero** shift costs 24.4 µs of the 24.8 µs a real shift costs — 98% wrapper.
* **The rule the numbers imply**: a loop stays whole in C++; the seam is crossed
  once per *analysis*, never per iteration or per column. Building the MaxEnt
  design matrix column by column costs 1668 µs for 60 columns against 58.5 µs
  for the one-call `tcspc_build_fi_lifetimes`; driving the MEM iteration from
  Python and delegating only the inner QP costs 146 µs/call at `n_tau = 60`, so
  29 ms of pure seam over 200 iterations.
* **The cost being paid downstream, named**: `tcspc_run_mem` /
  `solve_tcspc_mem_lifetime` / `solve_tcspc_mem_fret` have no per-iteration
  callback, so ChiSurf's maximum-entropy plugin keeps a second NumPy copy of
  `_run_mem` and `_quadpr_bound` to drive its progress bar. Both entries filed
  in `BUGS.md` with reproductions.
* **Second entry filed**: `fconv_simd` measures 1.11× against `fconv` at n=512
  and 1.01× at n=4096, where call overhead cannot be hiding a gain — either the
  SIMD path is not selected in this build or the kernel is memory-bound. Not
  settled here; it needs timing in C++ with no binding in the way.

## 2026-08-10 (19th entry)

* **Correction to the 16th entry: the likelihood fix was not inert, and the
  check that said it was could not have caught it.** Running the Python suite —
  the step that had been deferred all session — moved four reference values:
  `fit23` 2I* 23.802337 -> 23.791124 (tau 0.74219 -> 0.721353) and `fit25`
  4.738831 -> 3.887975. `fit24` and `fit26` are untouched.

  **The evidence was real and the conclusion was still wrong.** Two checks were
  run and both did what they said: the new code is bitwise identical above the
  floor, and a 143,360-bin sweep of the clamped `DecayFit23` box never produced
  a bin below 1.86e-07. The sweep used a **flat non-zero background**. The
  reference decays do not have one — `fit23` runs on zero background with 58
  photons, so the tail underflows. A sweep proves nothing outside the inputs it
  sweeps, and "a representative parameter box" quietly meant "the inputs I
  thought of".

  `fit25` is the clean attribution and the largest move: its only other change
  removed an addend that was always zero, a bitwise no-op, so the entire 0.85
  comes from the likelihood. Most of that is `wcm_p2s`, which discarded the
  **pair** whenever *either* channel underflowed — a far more aggressive discard
  than `Wcm`'s per-bin skip, and the reason a fit with a 0.2 background still
  moved.

  The old numbers are not the more correct ones; they are the answer to a
  likelihood that silently dropped occupied bins. Re-pinned in all four
  conformance runners and both Python reference tests.

  Also: the 7 `test_maxent_tcspc` failures in the same run are **not** from this
  work — `ext/python/MaxEntTcspc.i` and that test file are uncommitted
  in-progress edits by a concurrent session, one of which currently raises
  `NameError: name 'importlib' is not defined`.

## 2026-08-10 (18th entry)

* **PRD-010 Phase 5e: the AD advantage peaks and then decays, and `FitNExp` was
  never a candidate.** Two results from asking a question none of the earlier
  benchmarks had: what happens at 200 exponentials?

  **`FitNExp` has no gradient to convert.** It appears in PRD-010's problem
  statement as an `i_lbfgs` consumer. It is not one — `DecayFitNExp.cpp` never
  constructs a `bfgs`. Lifetimes are optimised coordinate-wise by Brent and
  amplitudes are profiled out by EM. Struck from the open list; the premise had
  survived unexamined since the PRD was written.

  **Every AD decision so far was taken at N <= 18, and that is the good part of
  the curve.** Both methods are O(N) — central differences pay 2N objective
  evaluations, a vectorized forward pass makes every scalar carry an N-vector —
  so the ratio is a race between two O(N) costs settled by constants and memory
  traffic. Measured on a 1024-channel decay: the advantage peaks at **13.3x**
  around 8–16 exponentials and falls to **3.3x** at 200 (N = 400). Central
  differences stay near-linear (895x the objective against a theoretical 800x);
  AD goes superlinear, 257x where pure O(N) predicts ~160x. The reason is size:
  a `Dual<double, GradVec<400>>` is 3.2 kB, so one 1024-channel intermediate is
  3.13 MB, far outside cache, while the finite-difference path re-walks a plain
  8 kB array. AD still wins at 200 exponentials — but at that size both are the
  wrong tool, because for a sum of exponentials the analytic gradient is closed
  form and costs about one objective evaluation.

  The generalisable point: **"AD is 10x faster" is a statement about a
  parameter count, not about AD.** Quoting it without the N is how a
  benchmark becomes folklore.

  Also tidied `DecayFit25` and `DecayFit26` onto `set_bounds`. 25's hand-rolled
  penalty was dead; 26's was correctly signed, unlike 23's, so that one was a
  tidy rather than a bug fix — but it shared 23's other defect of being
  invisible to an analytic gradient. `gamma`'s hard clamp in fit23 is kept
  deliberately: unlike `tau` it has no arithmetic failure outside its range.

## 2026-08-10 (17th entry)

* **`_mmfdb_operation.algorithm` finished: every writer that knows its
  estimator now records it.** The item was half-done — ChiSurf's writers
  validated the term but no caller supplied one, so a container said *a burst
  search happened* without saying which, and two selections of the same
  measurement were indistinguishable except by a settings hash nobody can
  read back into a method.

  Four terms were added to mmfdb rather than approximated with existing ones,
  which is rule 4 of `okf/specs/mmfdb-is-the-vocabulary.md`: the IRF is three
  genuinely different instrument responses (`gaussian_prompt_fit`,
  `skew_normal_prompt_fit`, `measured_prompt`) and burst fusion is a
  `recurrence_probability` test. Wiring is a map per writer — `irf_model` in
  the MLE exporter, `used_filter` in burst selection, fixed strings for fusion
  and the MLE itself.

  **Two things had to be checked rather than assumed, and both would have
  produced a confidently wrong container.** `TttrlibSearchSettings.algorithm`
  holds `"maxtree"` whether or not that search ran, so reading it
  unconditionally stamps every container with a method it did not use; it is
  read only when `used_filter` names that mode. And `COUNT_RATE` and `BURST`
  look like two searches: `count_rate_filter` thresholds photons per time
  window, `burst_filter` is the L/m/T form of the same test. Both are
  `sliding_window`. Reading the two implementations is what settled it —
  the names suggest otherwise.

  `burst_gs` and `burst_ebfret` still record nothing, deliberately. **Absent
  means unrecorded; a guessed term is worse than none, because a missing
  provenance prompts a question and a wrong one is believed.** Pinned by
  `test_every_writer_that_knows_its_estimator_records_it`, which checks each
  mapping table against the live mmfdb enumeration instead of by eye, and by
  six end-to-end cases carrying `used_filter` through
  `selection -> write_container -> put_table` and reading the tag back out —
  the term crosses three functions, any of which could drop it silently while
  the settings hash still differed.

## 2026-08-10 (16th entry)

* **PRD-010 Phase 5c/5d: bounds are priors, and acting on that found two bugs.**
  Minimising `-log L + p(x)` is MAP estimation with `p = -log prior`, so a bound
  and a prior are one object. Both codebases already said so —
  `DecayFitContext.h:65`, *"A bound is a uniform prior in this interface"*, and
  ChiSurf folding a `UniformPrior` back onto its port bounds. It follows that
  `i_lbfgs`'s soft bound, `k(x-hi)^2` outside the box, is already a proper prior
  that nobody had named. Unifying the decay fits onto it turned up two defects
  that had been sitting behind the clamps.

  **The Poisson likelihood paid the optimiser to zero out a model bin.** `Wcm`
  and `wcm_p2s` skipped any bin at or below `1e-12`, under a comment reading
  "this is only for stability reasons". The term a near-zero bin contributes to
  the minimised objective is `-C*log(m)`, large and *positive*, so dropping it
  is a discontinuous improvement: measured, the objective falls **828.9** across
  the threshold and is identical for every negative model value. Now continued
  by the tangent to `log` — C1 across the floor, finite below, monotone.
  **Claimed inert; it is not** (corrected in the 19th entry). Bitwise identity
  above the floor holds; the 143,360-bin sweep that seemed to settle it used a
  flat *non-zero* background and so never tested the reference data, which has
  none.

  **`DecayFit23`'s hand-rolled tau penalty had its sign inverted.**
  `penalty = (x[0] < kMinTau) ? -x[0] : 0` is negative over the whole band
  `0 < tau < kMinTau`, so crossing *below* the bound improved the objective —
  measured 9e-4 better stepping from 1.1e-3 to 9e-4 — and it was discontinuous
  there. Replaced by `set_bounds`. Gamma's soft bound, meanwhile, was applied
  only inside the branch that frees gamma, so the pre-fit ran under different
  rules than the main fit; both are now set once, unconditionally.

  Three corrections to my own reasoning, each caught only by measuring:

  1. I claimed `gamma < 0` made the objective NaN. That was a transcription
     error in my probe, which guarded on `C > 0` where `Wcm` guards on
     `M > 1e-12`. `Wcm` never NaNs — it silently drops the bin, which is worse
     because it is invisible.
  2. I claimed clamps make L-BFGS stall here. Below `kMinTau` the gradient was
     `-1`, not zero — the hand-rolled penalty was supplying it. The flat region
     was real for *gamma*, not tau.
  3. "Delete the clamps" is not possible. Without the tau floor
     `exp(-dt/tau)` overflows for `tau` in `(-dt/709, 0)`: at `tau = -1e-6`
     every model bin is `inf` and the objective NaN, and no penalty rescues a
     NaN because the line search must be able to score what it proposes. The
     guards stay; only the constraint role moved.
  4. Making the floor *smooth* — `soft_floor`, identity above it bit-for-bit,
     `m0*exp((v-m0)/m0)` below — is right and is now in, but it does **not**
     buy back a gradient below `kMinTau`, which is what one hopes for. That
     flatness is physical: at `dt = 0.032`, `exp(-dt/tau)` is 1.3e-14 at
     `tau = 1e-3` and underflows below, and `d/dtau` is already exactly 0 at
     `tau = 1.1e-3`, *above* the floor. `tau_eff` keeps moving; the model stops
     caring. Tuning `kMinTau` cannot fix it — no parameter map manufactures
     information the likelihood does not contain.

  **ChiSurf's bound transforms are not the thing to copy.** `leastsqbound.py`
  uses MINUIT's `sin` (two-sided) and `sqrt(v^2+1)` (one-sided). Measured, both
  reintroduce the pathology they exist to avoid: the `sin` derivative is 3e-17
  at the bound and the map is periodic and non-monotonic; the `sqrt` form is
  *even* in `v`, with derivative exactly 0 at `v = 0`, which maps to the bound.
  The logistic/exponential pair used for the localization fit is monotonic and
  attains its bounds only asymptotically. What *is* worth taking from ChiSurf is
  `priors.py` — priors as extra residuals, imposing no bound at all.

  The AD payoff is the reason to care: `i_lbfgs` adds the bound penalty **and
  its gradient** to whatever a registered analytic callback returns
  (`i_lbfgs.h:313-320`), whereas a term added to the objective by hand is
  invisible to that callback. The old tau penalty would have made an AD gradient
  wrong by exactly `-1` below the bound.

## 2026-08-10 (15th entry)

* **PRD-034 registered: .pto as its own TTTR sink.** Scoping found the design
  pre-drawn and half-built: the spec's example object is already
  `PtoKind "photons"` + `PtoEncoding "dstore"`, and a loader exists that reads
  `macro_time`/`micro_time`/`routing_channel`/`event_type` columns into
  `append_events` (`io_pto.cpp:2583-2598`) — **but it applies no header at
  all**, so the TTTR comes back dimensionless; `build_cues` refuses native
  photons as "not a record stream" (`io_pto.cpp:2617`); and there is no writer
  (`can_write` is never true for pto). The PTU checklist is in the PRD: six tag
  groups from `io_pq.cpp`, of which clocks and `ImgHdr_*` (CLSM auto-config)
  are the ones a sink dies without, and `tyBinaryBlob` is the type the PTU
  reader currently drops with an error (`io_pq.cpp:114`) that `PtoType::Bytes`
  was designed to carry. Design: normative 4-column dstore table pinned to
  TTTR's in-memory dtypes, header as PtoTags on the object's UID (required
  closed set: both resolutions, bin count, source record type; everything else
  verbatim fidelity), ranges as dstore row slices so cues stay an
  embedded-stream concern. Acceptance includes a bit-identical PTU round trip
  with an imaging file whose CLSM reconstructs from the `.pto`.

## 2026-08-10 (14th entry)

* **PRD-010 Phase 5: Eigen left the project.** It had been a project-wide
  `FIND_PACKAGE(Eigen3 REQUIRED)` — an apt package on Linux CI, a Homebrew keg
  on macOS, a vcpkg port on Windows, a `dnf` package inside the manylinux wheel
  builder — for two things: the batched GEMMs in `NeuralNet`, already migrated
  to `Mat.h`, and **one struct member** in `ImageLocalization`, the fixed-size
  vector sitting in the derivative slot of the vectorized forward-mode dual
  number. That member is now `GradVec<N>` (`modules/math/include/GradVec.h`).

  The PRD's own decision line — *"Eigen is an acceptable dependency:
  header-only with no link step"* — is now marked reversed. Header-only
  understates the cost. A `REQUIRED` package is a package on every platform's
  installer whether or not it links anything, and the distinction that admitted
  Eigen while ruling out mlpack turned out not to be the one that mattered.

  Three findings worth keeping. **The cost of the swap is 0–16%** — parity at
  N=9, 13–16% slower at N=6 and N=12 — recorded as a number rather than rounded
  to "equivalent", and small against the 3.95–5.44× AD wins over central
  differences in the first place. **The gap closed by removing temporaries, not
  by alignment.** `alignas(32)` measured *slower* (it inflates every `Dual`, and
  169 are live in the inner loop) and padding N to the SIMD width did nothing;
  returning a proxy from `scalar * grad`, so the multiply fuses with the
  accumulate that always follows it in the product and quotient rules, is what
  worked — and made the vectorized gradient bitwise identical to autodiff's own
  scalar `dual`. **The guard test the source comment claimed had never been
  written.** `ImageLocalization.cpp` asserted the vectorized path was "guarded
  by a unit test that compares [it] against scalar `dual`"; there was no such
  test, and the `NumberTraits` specialization it depends on is undocumented
  upstream, so an autodiff bump would have compiled cleanly and produced wrong
  derivatives. `test/cpp/test_ad_gradient.cpp` now does what the comment said.

  A methodological note that cost real time and is recorded in
  `benchmarks/README.md` so the next person does not repeat it: the first
  harness used `steady_clock` and reported speedups from 0.22× to 4.77× for the
  same binary on consecutive runs. The machine was at load average 43 and wall
  clock keeps counting while the thread is descheduled.
  `CLOCK_THREAD_CPUTIME_ID` with interleaved implementations and min-of-nine
  made it repeatable to a few percent.

  Also closed `okf/MODULE-DEBT.md` item 3, and not by its stated exit — the
  plan was to extract `nn` so Eigen would be needed by two modules instead of
  all of them; what happened is that both consumers stopped needing it. The
  audit that came with it found `clsm` and `superres` declaring
  `EXTERNAL_DEPS tttrlib::eigen` while including neither, and `localization`
  declaring Eigen while using autodiff. Nothing caught that, because the
  top-level `INCLUDE_DIRECTORIES` for `thirdparty/` puts the vendored headers on
  every module's path regardless — so `EXTERNAL_DEPS` is documentation until a
  module compiles with only what it asked for. That is the item's new exit.

## 2026-08-10 (13th entry)

* **PRD-016 M5 closed: the JavaScript binding is packageable.** The milestone
  read as metadata work — "`prebuildify` binaries, `node-gyp-build`, npm
  release". Neither half of that survived contact.

  `prebuildify` drives `node-gyp`, and this addon is a CMake target linking 35
  module libraries; a second hand-maintained build description in
  `binding.gyp` is the drift this project avoids everywhere else. Only the
  *output* is a contract, so `scripts/prebuild.mjs` produces
  `prebuilds/<triple>/node.napi[.<libc>].node` from `cmake --install` and
  `node-gyp-build` reads it unchanged.

  **The actual work was that the addon had never been relocatable.** It found
  its 35 sibling libraries plus HDF5 and libomp through absolute build-tree
  RPATHs, and `ext/CMakeLists.txt` carried a comment asserting the modules "sit
  next to the addon in js-pkg/, the same layout the npm package ships" —
  describing a staging step that did not exist. Every test had run on the
  machine that built it, so nothing could have caught it. Three answers:
  `-DTTTRLIB_MODULE_TYPE=STATIC` for prebuilds (one 14 MB `.node`, two external
  dependencies), a `js` install component so CMake rewrites RPATHs to
  `@loader_path` / `$ORIGIN` — *install*, never a copy out of the build tree —
  and a verifier that refuses any prebuild whose dependencies reach outside its
  own directory.

  Two checks earn their keep. The STATIC smoke test counts **registry entries**
  rather than merely loading the binary, because a static archive can drop the
  translation units whose only purpose is a registration side effect and the
  result loads perfectly while advertising nothing. And `pack.mjs` refuses a
  package missing any of the five platforms: a partial one is
  indistinguishable from a complete one until somebody on the missing platform
  installs it.

* **PRD-032 broke the JavaScript binding, and its own verification could not
  have seen it.** `TTTR::burst_search_algorithms_json` parsed the migrated
  category into an `nlohmann::json` and dumped it again. That type is a
  `std::map`, so `params_schema.properties` came back alphabetically sorted —
  and that key order *is* the C++ argument order. JavaScript has no `**kwargs`,
  so `burstSearchByName` began passing `max_false_alarm_rate` where `p0`
  belongs. Python, R and Java all survived it.

  The before/after registry capture reported "0 entries changed" because it
  compared **parsed** JSON, and parsing is precisely the step that discards key
  order. That is the generalisable lesson: a structural diff of a
  consumer-visible blob is blind to ordering, so it cannot certify a
  serialisation change. The invariant that does catch it is now a test —
  `required` keeps declaration order, so it must be a subsequence of the
  property keys.

## 2026-08-10 (12th entry)

* **PRD-032: `kBurstSearchRegistry` migrated and deleted.** Seven searches
  described in a literal in one file and dispatched from a table in another —
  two lists with nothing keeping them in step, which is not a hypothetical:
  `bocpd` and `coincident` were advertised with a `method` the dispatcher had
  never heard of. One `register_burst_search(descriptor, fn)` call each now,
  descriptor registered first so "described but not runnable" is
  unrepresentable. `BurstSearchDispatch.cpp` no longer names a single search.

  **The check that matters is the before/after registry capture**: 0 removed,
  0 changed, 63 added. Additive-only is the whole contract for a
  consumer-visible blob, and it is cheap to verify and impossible to eyeball.

  Two failures only running it revealed. `algorithms_json("burst_search")`
  came back **empty** — it primes the algorithm module's built-ins, not the
  burst ones — so the category silently lost all seven entries instead of
  failing; watch for that in the remaining migrations. And
  `PluginHost::burst_searches_json()` returns a **brace-less fragment**, because
  it was written to be spliced into the middle of a literal; the splice it
  replaced found its insertion point with `find_last_of('}')`, a parser written
  in string search, which a description ending in a brace would have moved.

* **PRD-012 closed with a criterion waived, not met.** No FLIM LABS sample file
  is published anywhere and none is expected, so the `STT1` reader ships
  verified against the specification and synthetic fixtures only. The
  distinction is written into both the PRD and the index, because "green" here
  does not mean what it means on the other rows: the three facts the BrightEyes
  work had to *measure* (photons pair with the next laser word, the TDC counts
  backwards, only 27% of records carry their own laser word) are exactly the
  class of thing a specification does not state — and `STT1` has no equivalent
  measurement behind it. If a real file ever arrives, verify before trusting.

* **PRD-016: M6 was done and nobody had noticed.** The gap list said the
  conformance runner did not exist; `test/js/conformance.test.mjs` runs all 96
  cases across 19 areas with no `unsupported` declarations, which is this PRD's
  actual acceptance criterion. Two more of its six gaps were also already
  closed (HDF5 is wrapped in all four bindings). **A status note nobody reruns
  decays into fiction in about four days** — the note is now dated and its
  closed rows kept visible rather than deleted.

* **A test that described a property instead of exercising it.** The one test
  covering the zero-copy contract asserted that `arraysAreZeroCopy()` returns a
  *boolean*. That passes in a build that copies every array, in one that copies
  none, and in one where the function is a stub. This is the second time this
  binding has produced a vacuous test in the same way, so it is now written down
  as a rule rather than an anecdote. The replacement writes through an
  `ARGOUTVIEW` output (`Pda.get_amplitudes`, a view onto a live `std::vector`)
  and reads it back, and asserts **against the build mode** — so the zero-copy
  and copy builds must disagree. Both were built and run; they do. That also
  retires the "copy fallback has never executed" gap: the Electron/V8-sandbox
  path now has the whole suite behind it.

* **The leak that four bindings and a conformance suite did not see.**
  `get_routing_channel`, `get_event_type` and `get_used_routing_channels` are
  the three accessors on `(signed char** output, int* n_output)`. `signed char`
  was applied as `ARGOUTVIEW` — "C++ owns this" — while every sibling type on
  the identical signature used `ARGOUTVIEWM`. All allocate through
  `get_array<T>`, which `malloc`s. The promised owner did not exist, so nothing
  freed it: one byte per event per call, in Python, R, Java and JavaScript.
  39 MB over 200 calls, against 0.4 MB fixed.

  **The values were always correct**, which is the whole reason it survived:
  every test asks what the numbers are and none asks what the memory does. The
  regression test measures the footprint, and was confirmed to fail on a
  rebuilt-with-the-bug binary (36.8 MB against a predicted 36.7) before being
  kept — a leak test that has never seen the leak is the same vacuity as above.
  The JavaScript leak check read only `get_macro_times` and `get_micro_times`,
  both always `ARGOUTVIEWM`, so it could never have fired; it now reads the two
  affected accessors too.

* **ASAN is blocked on the platform, not on us.** PRD-016 asks for the "drop the
  owner, then read the view" case under a sanitiser. The instrumented addon
  builds, but preloading the ASAN runtime into Node on macOS arm64 hangs before
  any JavaScript runs — reproduced with **no addon loaded at all**. Run it from
  the Linux CI job; do not spend more time on it here.

## 2026-08-10 (11th entry)

* **PRD-026 closed.** The three items on its remaining-work list are done.

  **stdin (R2).** `tttr sm -`, `convert -` and `correlate -` read a pipe. It is
  spooled to a temp file, not streamed, and the note is the point: every
  container reader here seeks -- a PTU jumps to its record block, a PTO reads a
  directory at the end -- so streaming would mean a second decoder per format or
  a silent failure on the formats that jump. `InputPath` is a type so the
  temporary dies on every exit path. Two things only running it revealed: `-`
  must not reach the burst table's `First File` column or the container's object
  names (a piped input is `stdin`), and the source checksum must be taken over
  the bytes actually read, since `-` cannot be opened. Piped and direct output
  are now identical but for the source column.

  **Window column order.** `DetectorSetup::windows` was a `std::map` beside a
  `detectors` vector that already carried an "in file order" comment. A setup
  declaring prompt/delayed produced delayed/prompt: values right, positions
  wrong, invisible until something reads the table positionally -- which is what
  a `.bur` is for.

* **Two collisions with the other instance, both instructive.** It renamed the
  operation vocabulary (`bva` -> `burst_variance_analysis`, `kde_cde` ->
  `burst_2cde`, `mle_green`/`mle_red` -> `burst_lifetime_fitting`,
  `hmm_photon_by_photon` -> `photon_hmm`) across `OperationRegistry.cpp`,
  `BuiltinAlgorithms.cpp` and `mmfdb.dic` while I was working in the same files.
  The registry/dictionary conformance test written this morning reported the
  half-applied state precisely, in both directions -- which is what it is for --
  and everything agreed once rebuilt. I followed the rename rather than
  reverting it; descriptive mmfdb names are what PRD-027 asks for.
  Also: I "restored" a `data_format` enumeration that had been reduced to one
  row, and was wrong to. There is an upstream **mmfdb package**
  (`../mmfdb/src/mmfdb/data`) whose dictionaries are the authority, and
  `test_vocabulary_matches_mmfdb.py` fails on any term tttrlib invents. mmfdb's
  `data_format` is about *storage* -- bur, dstore, ptu, csv, hdf5 -- while the
  MFD companion suffixes (.bg4, .bv4, .2c4, .td4) name what a table *is*, which
  is `operation_type`'s job; and an object inside a container has no suffix at
  all, which PRD-026 already settled by writing every artifact as `dstore`. The
  reduction was right and my restore was not. Reverted to mmfdb's terms, with
  the reasoning written into the item so the next person does not re-add them.

* **The hand-copied `.so` got SIGKILLed by AMFI again** (rc=137) after a deploy
  that raced the other instance's build. `codesign -s - -f` on the extension and
  the module dylibs revives it. This is the failure the build note warns about;
  `pip install -e .` remains the supported path.

## 2026-08-10 (10th entry)

* **New rule, and it is a rule about API shape**: every algorithm added to this
  library must be usable on photons. Written up in
  [specs/photon-native-algorithms.md](specs/photon-native-algorithms.md) and
  registered in the index. Two entry points, never one -- a standard form on the
  binned array (numerically identical to whatever reference people will compare
  it against) and a `*_events` form that takes the detections with fractional
  coordinates and never builds the grid. Where an algorithm genuinely has no
  event-wise formulation, the fallback is `Jitter.h`, not binning.
* **`Jitter.h` / `Jitter.cpp`** (new, in `modules/math`): `jitter_coordinates`,
  `events_from_counts`, `counts_from_events`, with SWIG bindings and
  `test/python/misc/test_jitter.py`. The dither is uniform across the bin, drawn
  from the counter-based path of the central RNG per (photon, axis) so the
  result does not depend on thread count. The justification in the header is
  deliberately *not* "binning biases the mean" -- it does not; it is that
  binning creates ties, and 98.4% of nearest-neighbour distances collapse to
  exactly zero, which is what makes distance-based methods degenerate rather
  than merely noisy.
* **`richardson_lucy_events` gained `psf_oversampling`**, and it is not a tuning
  knob. Interpolating the kernel at a photon's fractional offset is itself a
  convolution of variance `t(1-t)` -- up to 0.25 px^2, *varying with sub-pixel
  position*, which is the exact quantity event mode exists to preserve. Flux,
  centroid and non-negativity were all exactly right while this was happening.
  Sampling `K` times finer divides it by `K^2`. It had also been flattering the
  benchmark, since an over-wide forward model over-sharpens.
* **Two smaller fixes in the same file.** The kernel is now normalised on the
  *comb* it is actually sampled at rather than on the fine array's Riemann sum
  (worth 1e-4 in flux and 8e-4 px in centroid), and `scan_blur_kernel`
  integrates its fine grid down by overlap rather than to the nearest output
  sample -- `refine` is even, so one fine sample per output sample sat exactly
  on a boundary and rounding sent every one of them the same way, leaving a
  symmetric kernel whose mean was 1/4096 px off centre.
* **Measured and recorded**: PSF *truncation*, not interpolation, sets how
  accurately a photon reconstructs to its own position -- 3.7 sigma of support
  gives 8e-4 px, 5 sigma gives 3e-6, 6.3 sigma gives 2e-9. Five sigma is the
  number to remember.

## 2026-08-10 (9th entry)

* **The .pto provenance vocabulary is now in mmfdb.dic, and a test keeps it
  there.** The ask was that the registry literals be documented in mmfdb and
  compatible with it; the gap turned out to be much wider than the four names I
  had flagged. `mmfdb.dic` had **no operation category at all** -- so every
  `_mmfdb_operation.*`, `_mmfdb_artifact.*` and `_mmfdb_edge.*` tag the .pto
  writer has been emitting all along, including the eight hand-authored pipeline
  operations, was an undefined name. A reader holding a .pto could not validate
  its `operation_type`, resolve a settings schema for it, or learn what one row
  of an artifact is.
  Added: three categories; `_mmfdb_operation.operation_type` with an enumeration
  that IS the controlled vocabulary (12 operations); controlled vocabularies for
  `row_grain` (burst / photon / curve_point / histogram_bin) and `data_format`
  (bur, bg4, br4, bv4, 2c4, fu4, td4, irf); definitions for `settings_json`,
  `parent_operation`, `relationship_type`, `source_node_id`; and a save block per
  operation with its label, grain, format and replayability.
  `test/python/test_registry_matches_mmfdb.py` enforces it **both ways** -- a
  registered operation absent from the dictionary fails, and a dictionary entry
  nobody registers fails too, so the dictionary cannot quietly describe an
  operation the library dropped. Both directions were verified to actually fire
  by perturbing the dictionary; a conformance check that cannot fail is
  decoration, and this file already had one category of names nobody was
  checking.
  Remaining for criterion 10: settings keys and output column names are not yet
  checked against the dictionary.

## 2026-08-10 (8th entry)

* **PRD-032, unblocking criterion 1**: the algorithm registry moved to its own
  module (`modules/algorithm`) depending on nothing but a JSON writer. It had
  to: `registry` already depends on `burst`, so an algorithm module that wanted
  to register itself could not depend on `registry` without closing a cycle --
  nothing could migrate while the mechanism sat *above* the algorithms.
  `AlgorithmDescriptor` also gained `dispatch_name` (emitted as `method`, and
  omitted when empty, since that absence is the signal consumers read) and
  `provider`, and every entry now carries `params_schema` beside
  `settings_schema`. So a category can migrate onto registrations without its
  entries changing shape -- verified against a pre-change capture of the whole
  registry: 16 differences, all additions, none removed or changed.

* **Two more registry-advertised searches that ran something else**: `bocpd` and
  `coincident` both carry a `method`, so a UI dispatching by name called them,
  and both returned sliding-window bursts through the unrecognised-name
  fallback. `bocpd` now dispatches; `coincident` cannot (its essential input is
  the channel grouping, and (L, m, T) has nowhere to put it) so it raises and
  names the call that works. This is the fourth instance today of the same
  shape: a plausible answer from the wrong code path. The test that should have
  caught it passed vacuously -- it asserted the result was well-formed, which
  the fallback always is. It now requires each name to produce what its own
  entry point produces.

## 2026-08-10 (7th entry)

* **PRD-032 criteria 3 and 4**: a burst search contributed by a plugin is now
  callable through `TTTR::burst_search(name, ...)`, the door every built-in
  uses. The interesting part is what the old code did instead: the `if/else`
  chain fell through to the sliding window for any name it did not recognise, so
  passing a plugin's name returned sliding-window bursts. The registry listed
  the plugin's search and the obvious call quietly ran something else -- a
  wrong answer that looks like a right one, which is the failure mode this
  library keeps finding.
  Also found: the whole plugin test suite had been skipping. `_plugin_binary()`
  globbed only scikit-build's `build/<tag>/` layout, so anyone configuring into
  `build_new/` got 24 skips and a message telling them to turn on the option
  they had already turned on. Widened; the fast lane went from 46 skipped to 23.
  Verified the registry blob is byte-identical to a capture taken before the
  work (0 differences) -- worth doing before touching anything consumer-visible.

  **Criterion 1 (delete the three literals) is blocked, not merely undone**, and
  the reasons are recorded in the PRD: (a) `burst_search` and `fit` have a
  consumer-visible entry shape (`method`, `params_schema`, `provider`) the
  generic descriptor does not produce, and PRD-027 requires the shape be kept --
  so the descriptor needs a dispatch-name field and aliases first; (b) retiring
  `kOperationRegistry` faithfully means each descriptor moving next to the code
  that performs it, and several of those operations have no C++ home yet
  because they are the PRD-026 pipeline steps. Moving eight entries into eight
  blocks in the same module would satisfy the wording and none of the purpose.

## 2026-08-10 (6th entry)

* **PRD-027 criterion 6**: `TTTR::burst_search` now dispatches on a name through
  a table (`BurstSearchDispatch.h`) rather than a chain of `if (mode == ...)`.
  The point is not tidiness: the chain sat inside the one function every burst
  search has to be reachable from, so adding a search meant editing that
  function, and a search contributed from anywhere else could not be reached by
  name at all. `register_burst_search(name, fn)` from the search's own
  translation unit is now the whole of it.
  Two behaviours were easy to lose in the move and are pinned by the new
  `test/python/burstfilter/test_burst_search_dispatch.py` (17 cases): the `T`
  reinterpretation the narrow `(L, m, T)` signature forces on `kalman`,
  `maxtree` and `bayesian_blocks` -- a property of the signature, not of the
  algorithms -- and the fallback that runs the sliding window for an
  unrecognised mode instead of raising, which callers rely on. Fast lane green:
  2325 passed.
  Found while scoping the rest of criterion 2: the `burst_search` and `fit`
  categories have a consumer-visible entry shape (`method`, `params_schema`)
  that the generic descriptor does not produce, and the PRD requires those
  categories keep their shape. So retiring those two literals needs a
  dispatch-name field and shape-compatible aliases first -- it is not a move.

## 2026-08-10 (5th entry)

* **PRD-026, the C++ burst-table half: done and verified against ChiSurf.**
  [PRD-026](prds/PRD-026-mfd-sim-to-ndx-pto-pipeline.md) updated with what
  landed. `tttr sm` now generates its columns from the `--setup` detector file
  rather than a hardcoded green/red split, and the result matches ChiSurf's
  `generate_burst_dataframe` **cell for cell** on simulated MFD data: 23/23
  columns on a two-detector setup, 37/37 on a four-detector one, 27/27 with PIE
  windows and micro-time gates. ndX opens the container, joins the companions
  and maps the provenance graph.

  Six things the port settled that were not in the design, all recorded in the
  PRD: `data_format` is `dstore` and not the legacy `bur`/`bg4`/`bv4`/`2c4`
  (which name a *file* layout); the `.bur` 2N+1 interleave and its blank column
  are deliberately **not** written, contradicting R5 as originally worded,
  because a container declares relations as keys and ChiSurf's own container
  writer deinterleaves first; a companion's run identity has to include its
  parent's `settings_hash` or the next search silently overwrites it; a burst
  search returns *inclusive* indices, so the old table was one photon short in
  every burst; `tttr sim` was encoding on a hardcoded micro-time axis, so a
  3.8 ns simulated decay read back as 1.9 ns; and the container was holding a
  **re-encoded, channel-filtered** copy of the measurement where the profile
  promises the instrument file verbatim — it now goes in unchanged with a
  SHA-256, round-trips byte for byte, and is smaller for it (1.9 MiB of `.spc`
  against 4.6 MiB of re-encoded `.sm`).

  Worth keeping from the verification: the two column tables agreed on 23/23,
  37/37 and 27/27 cells *including* `Confidence (sigma)`, which is the strongest
  evidence the port is a port and not a re-derivation. The one column that did
  differ on the first run was `First File` — ChiSurf writes `Path(f).name`, with
  the suffix, and `file_stem` drops it, so a merged table could not have told
  `run.spc` from `run.ptu`.

  The two placeholder MLE tables were **removed rather than kept** — they held
  the same five constants for every burst, which nothing downstream can
  distinguish from a measurement. BVA and FRET-2CDE are now real computations,
  and are omitted when their donor/acceptor streams cannot be named. Doing MLE
  for real is the remaining work, and is blocked on the simulator putting all
  its background photons in micro-time channel 0 (`BUGS.md`), which any
  lifetime fit on simulated data would read as a large scatter component.

* **The two chiSurf PRDs that touch the same files, handled with it.** `tttr sm`
  is a writer of `.mmfdb.pto` containers that lives outside chiSurf, so chiSurf
  PRD-84 (units) and PRD-88 (every provenance graph reconstructs) are its
  problems too, and neither was going to be caught from one side.

  - **PRD-88 reproduced its own root cause here.** Its finding was that a writer
    which *creates* a container never re-asks for the primary, so the defect only
    shows on **reopen**. The C++ writer had exactly that, in its own idiom: it
    looked the photon stream up by **file name**, so extending a container
    chiSurf had made added a *second* `tttr_photon_stream` whenever the name did
    not match — two roots, with the burst table on the one nothing else
    references. Now matched on `_mmfdb_artifact.checksum`. Verified by driving
    the real writers: chiSurf creates, the CLI extends, every artifact walks back
    to one primary — including when the CLI is pointed at a byte-identical copy
    under a different name.
  - **PRD-84's unit rule had three holes, and a second implementation is what
    found them.** `Number of Photons` was `photons` while
    `Number of Photons (green)` — same row, same table — was unitless, because
    the fallback table keys on the exact name. `S prompt green (kHz) | 0-2048`
    carries *both* in-band conventions at once and matched neither, since the bar
    branch gave up on `0-2048` and the suffix regex is anchored at the end. And
    `FRET 2CDE` had no entry at all. Fixed in chiSurf, ported to C++, and pinned
    by a test that asks both writers for the unit of every column and requires
    the same answer — 40/40 and 30/30. Worth repeating at the next boundary: two
    implementations turn "what should this column's unit be?" from taste into a
    question with one answer.
  - **PRD-88, widened: the container writer was emitting four words nothing can
    query.** The profile defines no vocabulary of its own, and ChiSurf's
    `put_table` checks every term against the dictionary before writing — so a
    ChiSurf writer *cannot* invent one, and every `operation_type` in that tree
    is declared. The C++ writer makes no such check (it does not link mmfdb and
    has no mmCIF parser) and had been emitting `bva`, `kde_cde`,
    `mle_<detector>` and `companion_of`, where the dictionary says
    `burst_variance_analysis`, `burst_2cde`, `burst_lifetime_fitting` and — for
    a companion — plain `derived_from`. Every container written so far carries
    them. The check now lives on the ChiSurf side, which is the only side that
    can read the dictionary.

    Two things fell out. The cross-writer test's own "the relation is a term"
    assertion had been *listing* `companion_of` among the accepted values, so it
    passed on precisely what it existed to catch — an allow-list copied from the
    code it is checking is not a check. And ndX's `_COMPANION_TYPES` was a set of
    informal spellings no writer produces; it kept working only because the
    parent edge is what actually links a companion, so it was a fallback that
    would have failed the first time it was needed.

    **Third instance today of one shape:** a term, a unit or a name that one side
    invents and the other never checks. Same fix each time — put the check where
    the vocabulary lives, and drive the other side through it.

  - **A build defect the OpenMP fix exposed, and the wrong fix for it first.**
    `BUILD_WITH_INSTALL_RPATH ON` gave the build-tree binary only the *install*
    rpath, and `@loader_path/../lib` does not resolve in a build tree — the
    library is at `<build>/libtttrlib.dylib`, not `<build>/lib/`. That was
    survivable as the single entry: `tttr` failed loudly and a
    `DYLD_LIBRARY_PATH` fixed it. Adding the OpenMP prefix as a second entry
    made it dangerous, because a conda prefix commonly carries a
    `libtttrlib.dylib` symlink into *another* build tree — so the tool started
    successfully on a months-old library, with the wrong subcommands and no
    error anywhere, and `tttr sm --output x.pto` overwrote an existing container
    with the old JSON burst list. **A loud failure is a bug; a quiet wrong
    answer is worse**, and the first attempt turned one into the other. Caught
    only because the cross-repo audit reopened a container the CLI had just
    written and found JSON where EBML should be — the tests all passed
    throughout, because each sets `DYLD_LIBRARY_PATH` for the subprocess and so
    never exercised the rpath at all.

* **PRD-026's MLE half, and four things that looked fine and were not.**
  `tttr sm --mle --irf <spec>` fits one lifetime per burst per detector through
  `fit23`. Ground truth recovered: 3.87 ns for the 3.8 ns species, 1.71 ns for
  the 1.6 ns one, as two resolved modes.

  What makes this worth writing down is that the *first* version recovered
  3.69 ns and looked like a success. It was luck. On a synthetic burst of the
  same size the same configuration returned **6.68 ns for a 3.8 ns truth**, and
  moved by a factor of two on a change of start value — with a 2I\* that still
  looked reasonable. Four corrections, in the order they were found:

  - **`DecayFitProblem(n_channels, n_bins, dt)`**, and it was being called
    `(n_bins, 1, dt)`. Every burst then failed `require_valid`, every exception
    was caught per burst, and the whole thing printed `0/3787 fitted` — which
    reads as "no burst had enough photons", not as an error. The catch now
    reports the first reason once.
  - **Only `tau` may be fitted.** A hundred photons do not determine an
    anisotropy. r0 = 0 with rho held is also the precondition for the kernel's
    own well-conditioned path.
  - **`gamma` is the background *fraction of the burst*** — the kernel
    renormalises the background pattern to the burst's own total, so the pattern
    gives a shape and gamma gives the magnitude. Measured from the photons no
    burst contains, times the burst's duration. Left at 0 it subtracts nothing
    and biases every lifetime up by ~50% on a 13% background. (Measured on a
    synthetic burst: 5.67 ns at gamma 0, 4.09 at the true fraction, for a
    3.8 ns truth.)
  - **Rebin before fitting** (`--mle-bins`, default 128): ~100 photons over 4096
    raw TAC channels is almost all zeros, and the convolution is 4096 long for
    no gain. 14 s → 0.26 s.

  `--irf` is **required** and has no default: a prompt is a claim about the
  instrument, and a lifetime fitted against the wrong one is wrong by roughly
  its width with nothing saying so. `--irf delta` exists and has to be typed.
  The synthetic prompts are `gauss:FWHM[,T0]` and `sgauss:FWHM[,T0[,SKEW]]` —
  the skew-normal `exp(-z²/2)·(1 + erf(αz/√2))`, default skew 1.5, positive
  tailing to later times as a real prompt does. `T0` is the skew-normal's
  *location* parameter, not its peak; that is how it is parameterised
  everywhere and what a fit to a measured IRF hands back, and it is the sort of
  thing that reads as a 0.1 ns calibration error if nobody writes it down.

  **The IRF is checked by physics, not by parsing.** A spec that parses and is
  then ignored passes every test about column names. So: the simulation
  convolves with nothing, therefore `delta` is the *correct* model and must
  recover the truth best (3.87 ns against 3.8); a wider prompt takes more out of
  the decay, so the recovered lifetime must fall monotonically with its width
  (3.87 → 3.67 → 3.03 ns for delta, 0.5 and 2.0 ns FWHM); and a skewed prompt of
  the same width must differ from the symmetric one. Three properties, each of
  which a disconnected IRF fails.

  The general lesson, which is the same one as the rpath: **an ill-conditioned
  fit does not announce itself.** It agreed with the truth on real data and was
  wrong by 75% on synthetic data of the same size. The only thing that
  distinguished them was a ground truth cheap enough to test against every time.

* **Two setup errors that change no total and every conclusion.** The shipped
  MFD setup had `green: [8, 0]` while the simulation emits green-*parallel* on
  routing 0 — `chs[::2]` is parallel, so every anisotropy from that file would
  have been inverted with every photon count correct. PRD-026's R3 contained the
  same contradiction in prose (it called 8/9 parallel while listing `green →
  [0, 8]`); the lists are what the code reads, so the prose was corrected.
  Separately, the setup's `g_factor`/`l1`/`l2` were parsed and thrown away — a
  g-factor left at 1 when the file says otherwise is a wrong number, not a
  missing one.

* **Naming, now normative:** `.pto` is *the container*; `.mmfdb.pto` is a `.pto`
  that also carries the PTO.MFDB profile. The tag goes on the stem, never the
  suffix — `.pto.mmfdb` would stop being a container to everything dispatching
  on the extension. Written into the profile spec (chisurf
  `okf/specs/pto-mfdb.md`, regenerated into `doc/formats/pto-mfdb.rst`), the
  container spec, both module READMEs and `tttr sm --help`.
  `modules/io/pto/README.md` had had it backwards as `.pto.mfdb`.

## 2026-08-10 (4th entry)

* **PRD-027 Parts 1/3**: `AlgorithmDescriptor` + `register_algorithm` — one
  registration path for built-ins and plugins — and `registry_json()` assembling
  `fcs`, `hmm` and `pda` from live registrations. This closes the PRD's *first*
  stated problem: those three families worked and were invisible, and the reason
  was not architectural but clerical — burst searches and decay fits had a
  hand-written JSON literal each, and these had none. Consequence of being
  invisible: no UI listing, no schema for `.pto` provenance to validate or
  replay against, and no name for a plugin to compete under.
  Each entry carries full prose `description` and a `references` array
  transcribed from the reference lists the sources already carried (`Correlator.h`,
  `HMM.h`); PDA carried none, so its citation comes from the literature and is
  marked as needing confirmation. Replayable registrations reach the `operation`
  category. Merging is additive by design — a live registration never displaces
  a hand-authored entry of the same name, because a name collision is a mistake
  to surface, not to settle by load order.
  30 new tests plus a conformance case (`registry.live_algorithm_categories`) so
  every binding has to serve the new categories; the conformance suite caught
  the category-list change itself, which is what it is for. Whole fast lane
  green: 2308 passed.
  Explicitly left, each its own piece: retiring the three literals (PRD-032),
  table dispatch on `TTTR::burst_search`, the `TTTRLIB_MODULAR_ALGORITHMS`
  split, `PtoFile.citations*`, and mmfdb dictionary entries for the four new
  operation types (criterion 10 cannot be claimed without them).

## 2026-08-10 (3rd entry)

* **Feature**: `StreamingCLSMImage` — scanned-image reconstruction from a live
  photon stream, with LIVE (the frame being scanned, filling in) and
  INTEGRATING (the sum over completed frames) and a switch between them that
  works mid-acquisition without discarding anything: the sum accumulates in
  either mode, so the mode is a view rather than a processing path. The design
  decision worth keeping: it buffers one frame and hands it to the ordinary
  `CLSMImage` constructor instead of reimplementing where a frame begins (which
  depends on the reading routine, a walk-back over markers sharing a macro-time
  tick, and a B&H first-frame correction). Memory is one frame rather than the
  acquisition -- the part that genuinely has to change for a stream -- and the
  pixel assignment is the batch's by construction. The partial frame is rebuilt
  per query, not per photon; a viewer redraws thousands of times slower than
  events arrive. Two traps found on the way: an *incomplete* frame must not be
  allowed to settle the image geometry (the first redraw pinned it at 26 lines),
  and `TTTR::append_events` left the used-routing-channel cache stale, so a
  CLSMImage built on an assembled TTTR returned the right shape with zero
  photons in it and no error anywhere. `cmc` has an equivalent decoder
  (`analysis/image/clsm_decoder.hpp`) that had to reimplement the marker
  handling because tttrlib offered nothing to stream against.

* **Environment**: the `build_new` tree links Homebrew HDF5 while the mambaforge
  interpreter loads conda's, so reading a Photon-HDF5 file from that build
  aborts with "HDF5 library version mismatched error". Not a code defect --
  `HDF5_DISABLE_VERSION_CHECK=1` runs the whole tttr suite green (633 passed).

## 2026-08-10 (2nd entry)

* **Correctness**: `StreamingCorrelator` fixed and
  [PRD-033](prds/PRD-033-streaming-correlator.md) closed. The class was
  documented as broken "at cascade >= 2, probably a normalization or
  border-handling issue"; it was neither. `get_correlation` read every level at
  a constant first coarse lag of `n_bins/2`, while the multi-tau axis it
  publishes needs level b to start at `x[b*n_bins] / 2^b` — 0, 8, 12, 14, 15,
  15, ... Only level 1 equals `n_bins/2`, which is why cascades 0 and 1 agreed
  with the batch correlator and nothing above did. Reporting a shorter lag than
  the label claims inflates a decaying G, and inflates it more the coarser the
  level: the recorded 1.19 / 1.25 / 1.12 / 2.39. Mean stream/batch ratio is now
  1.0000 on every cascade out to lag 65520. Also fixed: the coarse-bin width in
  the normalization was `2^(j/n_bins)` instead of the batch's
  `2^((j-1)/n_bins)`, so every block-boundary lag was off by two; and the
  two-channel entry point accumulated one channel's weight only, computing an
  autocorrelation and calling it a cross-correlation (replaced by
  `push_photon(mt, w, channel)` with a per-channel history). New
  `test/python/streaming/test_streaming_correlator.py` asserts agreement
  **per cascade**, because an aggregate tolerance is exactly what hid this.
  The lesson worth keeping: the ratio being non-constant (1.19, 1.25, 1.12,
  2.39) was read at the time as evidence of a subtle normalization problem. It
  was evidence of the opposite — a constant would have been a normalization
  problem; a drift means the lags themselves are wrong.

* **Performance**: the same correlator was also paying a full cascade step for
  every *empty* bin (~19 ns), and a real acquisition has hundreds to thousands
  of empty bins per photon. Empty runs are now skipped in closed form -- level b
  emits `n0 / 2^b` times, so a run covers a difference of two divisions and only
  the first emission can carry a non-zero accumulator. 80k photons over a 4.0M
  bin span: 0.136 -> 0.063 s; over a 200M bin span the old code would have spent
  about 4 s and it now takes 0.069 s. The cost no longer scales with the length
  of the acquisition. Numbers in PERF.md.

* **Correctness**: the three streaming consumers PRD-033 listed as "works"
  beside the broken correlator were checked against their batch equivalents
  rather than taken at their word. `StreamingDecayHistogram` and
  `StreamingPhasor` hold up (exact vs `np.bincount` and the batch microtime
  histogram; 1e-12 vs `DecayPhasor`, and on the universal semicircle).
  `StreamingBurstDetector` did not: every burst ended one photon late (the
  failing window ends the burst at the *preceding* window's last photon);
  a burst of coincident photons -- the highest rate the detector can see -- was
  reported as no burst, because the rate was `m/span` with a zero span guarded
  by `rate := 0`; it kept every photon's macro time in a consumer meant for an
  unbounded live acquisition (now a ring buffer, O(m)); and a non-positive
  macro-time resolution silently returned the whole stream as one burst, which
  is reachable by accident since an unread header reports -1.0. Boundaries are
  now compared index for index against the batch search.

* **Build**: the macOS OpenMP detection that landed alongside this work made
  every library target fail to link — the flags reach the compiler but nothing
  linked the runtime. `LINK_LIBRARIES(OpenMP::OpenMP_CXX)` after detection.

## 2026-08-10

* **Correctness**: The non-symmetric eigensolver (`QREigen.h`, used by
  `BurstML`, and duplicated inside `GopichSzabo`) returned eigenvectors with a
  relative residual `||Av - lambda v|| / ||A||` of order 1 — no information —
  on every non-trivial matrix. Three independent causes: the null vector was
  un-permuted by the elimination's row pivots (row pivots permute equations,
  not unknowns), the null direction was read off a pivot that is rounding-level
  noise when the shift is an eigenvalue, and the balancing back-transform
  scaled columns instead of rows and was applied to vectors that had never been
  balanced. Eigenvalues were always right, which is why nothing downstream
  noticed. Residuals are now 1e-15. Also fixed: the QR iteration stalled
  silently on matrices with a symmetric spectrum (no exceptional shift) and
  reported success with zeros; `mat_solve`/`mat_inverse_inplace` used an
  absolute 1e-300 singularity floor, which is not a rank test; `mat_lstsq_minnorm`
  used an absolute rather than relative `rcond`, zeroing the solution of any
  uniformly small system; and the MaxEnt TCSPC active set handed its
  least-squares fallback the matrix `mat_solve` had already destroyed. New
  `test/cpp/test_mat_linalg.cpp` and `test/cpp/test_qreigen.cpp` check these as
  properties, not stored numbers.

* **Performance**: Eigendecomposition 5.5x faster serial, 10x on eight threads
  (n=100: 45.3 -> 8.3 -> 4.5 ms) by moving inverse iteration onto the
  Hessenberg form (O(n^4) -> O(n^3)), dropping the unused Schur-vector
  accumulation, and parallelising the per-eigenvector loop.
  `mat_lstsq_minnorm` 2.5-7.3x faster (column-major working copy, carried
  column norms). `GopichSzabo` now shares the solver instead of its own O(n^4)
  path. New tracked benchmark `benchmarks/bench_linalg.cpp` with a recorded
  baseline and a `--check` mode that fails on a >1.30x regression; table and
  method in PERF.md. Module READMEs (`math`, `kinetics`) and CHANGELOG updated.
  The FLIM/competitor plots are unchanged — this change touches no FLIM path.

## 2026-08-08 (2nd entry)

* **Performance**: FLIM per-pixel reconvolution MLE (`fit_map`) improved from
  456 ms to 161 ms (2.8x) on a 256x256 image. Three changes: (1) allocation-free
  inner optimization loop via `FitWorkspace` scratch buffers in `DecayFitNExp.cpp`
  (the grid scan + Brent + EM loop previously allocated ~6 vectors per
  evaluation); (2) `fit_batch_flat_buffers` SWIG binding with `IN_ARRAY2`
  typemaps eliminating the Python->vector element copy that cost ~400 ms on the
  image path; (3) `FitNExp` Python wrapper (`FitNExpWrapper.py`) providing the
  `__call__`/`fit_many`/`fit_map` interface the benchmarks reference. CPU now
  beats FLIMKit GPU by 5.3x (was 1.25x). Documented in
  [testing/benchmarking.md](testing/benchmarking.md) with the full steps and
  the documentation obligations for future perf work. PERF.md, CHANGELOG.md,
  and the decay module README updated.

## 2026-08-08

* **Replacement**: Replaced the click-based `bin/tttrlib` Python runner with a
  compiled C++ CLI, `tttr`, living under `modules/cli` (cxxopts + cxxopts, built
  via `tttrlib_add_module`, installed flat to `bin/tttr` + `lib/libtttrlib`). The
  dispatch table in `cli_main.cpp` is a six-line switch; each subcommand owns its
  own cxxopts schema in `modules/cli/src/cmd_*.cpp`.

* **Progress, two layers**: A general progress ticker now lives in the library at
  `modules/util` (`ProgressTicker`, `ProgressEvent`, `ProgressSink`) — see
  [/design/tttr-cli-progress.md](/design/tttr-cli-progress.md). It does no I/O; the
  CLI (`cli_progress.cpp`) installs one sink that fans each event to a tty bar on
  stderr and to machine-readable JSONL (`--progress CHANNEL`), so a GUI can build
  `cli -> progress -> gui`. `tick` events are throttled to 60 ms in the library;
  `begin`/`finish` and the final tick are never dropped. The lifecycle bug in
  `cmd_convert` (no `set_total`/`finish`, so events printed `total=0` and never
  closed) is fixed.

* **Detector setups, chiSurf-compatible**: The instrument definition is pure JSON
  (no compiled-in detector knowledge — the binary only routes routing channels).
  `detector_setup.{h,cpp}` read/serialize the chiSurf `detector_setups.json`
  schema; unknown chiSurf keys are accepted and ignored. `sm` and `image export`
  share `--setup FILE --setup-name NAME --detector NAME`; a single detector's
  `chs` filters `sm`, and per-detector `chs` become per-TIFF groups in image
  export. Authoring is terminal-based: `tttr detectors FILE --add [--name N]`
  prompts for detectors/gates/windows and writes a chiSurf-shaped file, setting
  `last_used`. (If chiSurf has migrated setups to MMFDB, export a JSON file for
  the CLI.) Full contract in [/specs/tttr-runner.md](/specs/tttr-runner.md).

* **PTO CLI fixes**: `cmd_image` and `cmd_pto` both shipped `argc -= 2; argv += 2`
  after stripping the subcommand, which made cxxopts read the input file as the
  program name — the positional was silently dropped. Now `argc -= 1; argv += 1`.
  `cmd_pto extract` disambiguates `FILE OBJECT DIR` (one object) from `FILE DIR`
  (extract-all into a directory) by treating a stoull-parseable or file-known
  uid/name as OBJECT.

* **Install caveats (not code bugs)**: `cmake --install` is blocked by
  `include/tttrlib` being a symlink into the repo; the workaround is to copy
  `bin/tttr` and `lib/libtttrlib.dylib` into the prefix by hand. The binary's
  `@loader_path/../lib` RPATH resolves from the conda env without
  `DYLD_LIBRARY_PATH` (verified). `tttr convert` cannot write PTO — `TTTR::write`
  has no PTO writer and emits a corrupt file; build PTO fixtures via the Python
  bindings. The LSP (clangd) in this workspace reports phantom
  "pp_file_not_found"/"no member named value in std::" errors because the include
  path isn't fed to the language server — the real targets build clean.

## 2026-08-07

* **Fold-in**: Moved the photon-simulator implementation plan from the
  gitignored `PLANS/` directory into [design/](/design/plan-005-photon-simulator.md),
  next to its PRD. `PLANS/` is gone; the `.gitignore` line that excluded it is
  gone too, for the same reason as `PRDs/` before it.

* **Reorganization**: Sorted the bundle into subfolders and folded the PRDs in.
  The flat layout (ten files at the root) became thematic directories —
  `bindings/`, `testing/`, `design/`, `handover/`, `prds/` — with `index.md` and
  this log staying at the root. The 25 PRDs that lived as untracked,
  `.gitignore`d planning state under `PRDs/` are now `prds/` and version-
  controlled alongside the rest of the bundle, because a PRD that ships is
  knowledge that belongs with the source, not local scratch. The `PRDs/` line
  left `.gitignore` for the same reason. Every internal link was updated for the
  move.

## 2026-08-06

* **Creation**: Added [Test workload tiers and the fast lane](/testing/test-workload-tiers.md).
  The Python suite is now tiered by measured cost (`slow`, `heavy`, `smoke`) and
  selected with `--lane fast|standard|full`, cutting the working run from 33
  minutes to 1.7 at the cost of 2% of the tests. The note records the two things
  that are not guessable: cost is concentrated to an extreme degree — 29 of 1833
  tests are 85% of the runtime, and the biggest file is in `clsm`, not in the
  simulation groups everyone names — and a first, cold-cache measurement inflates
  I/O-bound tests up to 20x, which put 39 wrong markers in before the audit
  caught it. Also records that `MODULE_TEST_GROUPS` had drifted in both
  directions and is now derived from the module `CMakeLists.txt` instead, and
  that three `TEST_DIR` declarations pointed at directories that do not exist.

* **Initialization**: Established this bundle. The repository root already held a
  plain file named `okf` — a Fiji-plugin handover note from 2026-08-04. Creating
  the directory needed that name, so the note moved to
  [Fiji plugin — where to continue](/handover/fiji-plugin-handover.md) and gained the
  frontmatter OKF conformance requires. Its body is unchanged.
* **Creation**: Added [tttrlib for JavaScript (Node.js)](/bindings/javascript-binding.md),
  covering the fourth language binding committed as `b4e8e7d0` — a Node-API addon
  wrapping the full Python surface, generated by SWIG from the same interface
  fragments as Python, R and Java.
* **Creation**: Added [SWIG Node-API backend traps](/bindings/swig-node-api-traps.md).
  Five behaviours that each cost real time and none of which announces itself:
  typemap locals silently dropped for template-generated classes, multi-pattern
  typemaps attaching locals to the last pattern only, the preprocessor reading
  backticks and the macro-end directive inside comments, `SWIG_exception_fail`
  being unusable in an `%extend` body, and `%feature("unref")` receiving `arg1`
  rather than `smartarg1`.
* **Creation**: Added [shared_ptr for the Node-API backend](/bindings/shared-ptr-design.md).
  Records that transcribing SWIG's own `boost_shared_ptr.i` compiles cleanly and
  fails on every method call, because the backend's generated constructor
  hard-codes the stored pointer and its type — and what the holder-table design
  does instead, including the null-deleter limitation it inherits.
* **Creation**: Added [PTU web viewer](/bindings/ptu-webapp.md), the reference
  application, with the measured results on three file formats.
* **Update**: Recorded the verified state in
  [What is still open](/bindings/open-items.md) after the `std::map` conversion compiled
  and the suite went green (16 suites, ~60 tests, `--expose-gc`). Two failures on
  the way are kept there: `SwigValueWrapper<T>` has no `begin()`, and the first
  map test passed vacuously because a missing feature and a correct one both
  satisfy a negative assertion.
* **Creation**: Added [What is still open](/bindings/open-items.md). Test coverage is ~60
  functions against Python's 1507; only macOS arm64 has been built; the CI job
  has never run; there are no prebuilt binaries; the copy-fallback path has never
  executed; nothing has been run under a sanitiser.
