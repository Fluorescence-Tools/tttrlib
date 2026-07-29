# Changelog

## [Unreleased]

### Changed
- **One interface for every decay fit.** A fit is now built by registry name and
  called the same way whatever the model: `DecayFit2("fit23", setup, irf)`, a
  `DecayFitProblem` holding the measurement, a `DecayFitConstraints` saying what
  may move, and an outcome carrying `parameters`, `results` and `objective`. The
  per-estimator classes, their per-model `fit_matrix`, and `DecayFitData` are
  gone; `DecayFitNExp` is registered as `fit_nexp` and is reachable the same way
  (it never appeared in the registry before).

  Parameters, setup values and results each cross as one flat `double` array
  whose slots the registry names, so nothing counts positions. `FitRegistry.cpp`
  now states the **flattening rule** normatively, gains JSON-Schema `array`
  properties with a `count_from` link for variable-length blocks, a symmetric
  `results_schema`, an `objective` category replacing the two boolean flags that
  used to ride inside the parameter vector, and `n_patterns`/`supports_*`
  capability flags. `decay_fit_setup_vector(name, json)` builds a setup block
  from named values in **every** binding, so R and Java are not left assembling
  it by hand.

  New capabilities that fell out of the unification: `fixed` becomes an integer
  **link vector** (negative fixed, `0` free, `k>0` shares one value across every
  slot in group `k`); a parameter may carry a **prior**, of which a hard bound is
  the degenerate uniform case, serialised in the same JSON form ChiSurf uses;
  one `fit_batch` serves every model instead of five near-copies; `model_curve`
  returns the model *independent of the data* (which `evaluate` cannot, since it
  scales to the observed counts); and every model now reports `converged` and
  `iterations`, which only the multi-exponential fit did before.

  **Linked fits.** `fit_linked` fits many measurements *together* against one
  parameter vector, with link groups resolved across the whole of it, so a
  parameter that belongs to the instrument (a rotational correlation time, a
  g-factor, a timeshift) is informed by every measurement at once while a
  per-sample parameter stays free. `fit_many` fits rows independently, where a
  group spanning rows can do nothing because the rows never meet.

  Migration: the former Python API (`Fit23`…`Fit26`, the `fit23`-style helpers,
  `DecayFitData`, `DecayFit23.modelf`/`fit_matrix`) is kept as a **Python-only**
  compatibility layer that warns on use and is **removed in 0.29**. It is rebuilt
  on the new interface and reproduces the same numbers. R and Java took the clean
  break. See `doc/fit-guide.rst` and the `plot_decay_fit_interface` example.

### Fixed
- **`fconv_per_cs` shifted its wrap-around tail one bin early.** The periodic
  convolution's tail loop applied a decay step *before* writing bin 0, but the
  value it held was already the continuation at bin `period_n` — which *is* bin 0
  of the next period. The three `_cs` kernels (scalar, NEON, and the two-channel
  NEON) all carried it; `fconv_per()` did not, because its main loop ends one bin
  earlier and compensates. The error is invisible whenever the decay completes
  within the excitation period and grows as it does not — 5.8e-5 of the peak at a
  lifetime of a fifth of the period, and larger for longer lifetimes or higher
  repetition rates. With the fix the recursion matches an exact circular
  convolution to 1.3e-15, which is what makes the machine-precision agreement
  between the two convolution backends a meaningful check rather than a tuned
  tolerance.
- **Priors set on a single-row fit were stored, serialised and then ignored.**
  `DecayFitConstraints::set_prior_json` reached the optimiser in `fit_linked` but
  not in `DecayFit2::fit` for `fit23`/`24`/`25`/`26`, because those models build
  their optimiser internally and never saw the constraints. A uniform prior — the
  documented way to express a hard bound — therefore did nothing on the primary
  path, silently, while the caller believed the bound held. Bounds now travel on
  `DecayFitContext` and are applied by every fit2x kernel, including `fit23`'s
  1-D Brent specialisation (where an unbounded fast path would have made a fit's
  answer depend on which internal route it took).
- **Cross-language `fit23` reference tests now start inside the basin they pin.**
  The 58-photon reference decay is sparse enough that the objective falls
  monotonically as the lifetime grows, so the historical answer is a *local*
  minimum with a basin of roughly 0.5–1.2; the old start of 2.1 sat close enough
  to the edge that a change of 1e-4 in the model flipped it to a converged,
  successful-looking fit reporting a lifetime in the tens of thousands. The
  reference values are unchanged. The runaway itself is now pinned by a test, and
  is documented as a failure mode in `doc/fit-guide.rst`.

### Added
- **Two convolution backends behind one call**, `dfa::convolve` /
  `dfa_convolve(rates, weights, irf, n_bins, shift_bins, method)`. `Recursive`
  (the default) is the SIMD time-domain recursion the library already used;
  `Spectral` multiplies the closed-form periodic spectrum by the response's.
  Benchmarked in `benchmarks/bench_convolution.py` and recorded in `PERF.md`:
  the recursion is **1.7× to 6.2× faster**, the gap widening with the number of
  rates, so the frequency domain is *not* the fast path — it is there for the
  three things the recursion cannot do (an arbitrary measured pattern, an
  independent cross-check, and a response that wraps around the period). A
  fractional `shift_bins` is applied to the *response* under either backend,
  costing one transform independent of the rate count.
- **`dfa::vv_vh_convolved`**, the polarisation-resolved decay in the form a fit
  compares against data: both rate products are convolved before the VV/VH
  projection, because the response acts on the photons and not on the anisotropy.
- **Bayesian Blocks burst search** (`TTTR::burst_search_bayesian_blocks`, also
  `burst_search(..., mode="bayesian_blocks")`). Rather than asking whether the
  rate near each photon clears a threshold, it finds by dynamic programming the
  single most probable partition of the photon stream into constant-rate
  intervals. There is no binning, no window duration and no phase, so burst edges
  are placed optimally instead of snapped to a window boundary, and the only
  detection parameter is `p0`, a false-alarm probability. The method is
  Scargle's, developed for time-tagged photon events from BATSE and Fermi — the
  same data model as a TTTR file.

  The segmentation is O(N²), so it runs behind a two-stage trigger in the manner
  of Fermi GBM and Swift BAT: a cheap loose sliding-window pass proposes
  candidate regions, and the exact segmentation runs only inside them. Cost is
  roughly `f · n̄ · N` for candidate fraction `f` and mean region size `n̄`, with
  `n̄` bounded by `max_region_photons`. See
  `include/BurstSearchBayesianBlocks.h`.

  Measured against the simulated ground truth in
  `examples/single_molecule/plot_burst_search_comparison.py`, pooled over 2688
  transits: it reaches 50% completeness at 18.4 photons, against 13.9 for
  `maxtree`, 21.6 for `cusum_sprt` and 22.7 for `sliding_window`. At the base
  condition its F1 is 0.918, against 0.961 for `cusum_sprt` and 0.952 for
  `maxtree`. It is the slowest of the five at roughly 1.2 Mphoton/s, against
  ~16 for `maxtree` and ~1080 for `sliding_window`.

  So it is **not** the best method on this benchmark — `maxtree` has the better
  detection limit and `cusum_sprt` the better F1 and purity. Bayesian Blocks
  looks strongest on isolated, sharp-edged, high-contrast bursts against a
  uniform background, which is the regime optimal segmentation is built for and
  which flatters it relative to real diffusion transits with soft edges. Its
  distinguishing property here is burst *extent*: because it places edges by
  likelihood rather than snapping them to a window boundary, it does not inherit
  the `m`-photon truncation the window-based searches do. Choose it when
  boundaries matter; choose `maxtree` for the lowest detection limit and
  `sliding_window` when throughput dominates.
- **Exact low-count detection statistics** (`include/BurstSignificance.h`,
  exposed in Python as `poisson_significance`, `li_ma_significance`,
  `log_poisson_upper_tail`, `log_p_to_sigma`, `sigma_for_false_alarm_rate`).
  The Gaussian z-score `(k − μ)/√μ` assumes the Poisson distribution is already
  normal, which is false at the counts this library operates at — a 20-photon
  transit against 2 expected background photons — so a "4 sigma" threshold did
  not deliver the false-positive rate it appeared to promise. Adds the exact
  Poisson upper tail (via the regularized incomplete gamma, computed in log space
  so it does not underflow where `scipy.stats.poisson.sf` returns 0) and the
  Li & Ma (1983) on/off statistic, which additionally propagates the uncertainty
  of a background that was *measured* rather than known — always the case here,
  since the baseline comes from a rolling-ball estimate.

  Selected by `significance_mode` on both `burst_search_maxtree` (default `0`,
  Gaussian, so existing results reproduce bit-exactly) and
  `burst_search_bayesian_blocks` (default `2`, Li & Ma).

  On the simulated benchmark, switching `maxtree` from Gaussian to Li & Ma is a
  modest, one-directional trade: purity rises from 0.904 to 0.916 and F1 from
  0.952 to 0.957, while completeness falls from 0.719 to 0.701 and the 50%
  detection limit moves from 13.9 to 14.6 photons. That is the expected
  behaviour — accounting for the background's own error makes the test strictly
  more conservative — and it is the right default only when false positives cost
  more than missed dim bursts. The effect is much larger on isolated bursts
  against a uniform background than on real diffusion transits, so do not expect
  the dramatic version of this result on experimental data.
- **Calibrated false-alarm thresholds** (`max_false_alarm_rate` on
  `burst_search_maxtree` and `burst_search_bayesian_blocks`). Expresses the
  detection threshold as expected spurious bursts per second instead of a bare
  sigma, correcting for the trials factor. Unlike a sigma, one such setting means
  the same thing on a 10 s and a 1 h acquisition. The trials correction is
  approximate — the exact factor for a multi-level search is not analytically
  available — so verify it against a background-only measurement before relying
  on the absolute number.
- **Burst-search algorithm table** in `doc/burst-analysis.rst`, comparing all
  five searches — how each decides, when to use it, and the primary literature
  each derives from, with resolvable DOI/arXiv links.
- **Max-tree burst search** (`TTTR::burst_search_maxtree`, also
  `burst_search(..., mode="maxtree")`). A threshold-free burst search: it builds
  the component tree of the local log count rate — every connected component at
  every level — and keeps components that are maximally stable (the MSER
  criterion) and whose photon count, duration, contrast and Poisson significance
  are plausible. Because each burst is detected at its own level, dim and bright
  bursts in the same trace are both found, which no single rate threshold can do,
  and overlapping transits are deblended by the tree structure rather than by a
  separate splitting step. Includes a rolling-ball (morphological opening)
  baseline that tracks drift without chunking. See
  `include/BurstSearchMaxTree.h`.

  Measured against a simulated ground truth at a dilute single-molecule
  condition (`examples/single_molecule/plot_burst_search_comparison.py`): recall
  0.98 / precision 0.93 / F1 0.95, versus 0.93 / 1.00 / 0.96 for `cusum_sprt` and
  0.84 / 0.69 / 0.76 for `sliding_window`. Its recall is flat across a 16x
  bright-to-dim brightness spread, where the threshold searches lose 10-15 points,
  and its merge rate is roughly half that of `cusum_sprt` as concentration rises.
  It is slower: ~15 Mphotons/s against ~110 for `cusum_sprt` and ~1000 for
  `sliding_window`.

  Note the method assumes bursts are a *minority* of the trace: its contrast and
  significance filters are measured against an estimated baseline. Above roughly
  half of photons belonging to bursts that assumption fails and those two filters
  should be disabled (`min_contrast=0`, `min_significance=0`).
- **Burst-search registry** (`TTTR::burst_search_algorithms_json()`, and in Python
  `TTTR.burst_search_algorithms()`, `TTTR.burst_search_defaults()`,
  `TTTR.burst_search_by_name()`). Advertises every burst search with its label,
  summary and a **JSON Schema** of its parameters — types, defaults, ranges and
  units — the way `container_names` advertises the readable file containers.
  Because the parameter description is standard JSON Schema, a tool that can
  already render one can build a burst-search user interface with no
  tttrlib-specific code, and picks up new algorithms on upgrade.
  See `src/BurstSearchRegistry.cpp`.
- **Burst-search benchmark example**
  (`examples/single_molecule/plot_burst_search_comparison.py`). Simulates a
  single-molecule sample with `SimEngine`, recovers the true transits from the
  simulator's per-photon molecule labels, and scores every burst search on
  precision, recall, and the split and merge rates, sweeping concentration and
  brightness heterogeneity.

### Fixed
- **The Python extension compiled with OpenMP but never linked it.** The
  top-level OpenMP flags reach `CMAKE_CXX_FLAGS` and `CMAKE_EXE_LINKER_FLAGS`,
  but a Python extension is a `MODULE` library rather than an executable, so it
  received the compile flags and none of the link ones. The R and Java modules
  have always linked `OpenMP::OpenMP_CXX` explicitly; the Python module now does
  too.

  The failure mode was worse than a plain link error. On macOS the Python module
  is linked with `-Wl,-flat_namespace,-undefined,dynamic_lookup` so that Python
  symbols resolve at load time, and that flag let the unresolved OpenMP symbols
  through as well. The build therefore succeeded and the module failed at
  **import**, with `symbol not found in flat namespace '___kmpc_barrier'` —
  which forced every downstream build to inject `-lomp` and an rpath by hand.
  `pip install .` in an environment where CMake can find OpenMP now produces an
  importable module with no extra flags.
- **`CLSMImage::compute_ics` returned an array longer than it allocated,
  segfaulting any caller that used frame lags.** The function allocates one
  correlation map per correlated frame *pair* — `calloc(pairs * pixels)` — but
  reported the number of input *frames* as the first output dimension. The two
  agree only for the default auto-correlation, where every frame is paired with
  itself. Any other pairing has fewer pairs than frames: correlating a stack of
  50 frames at a lag of 2 writes 48 maps but declares 50, so the returned NumPy
  array over-declares its own length and reading the tail — `arr.mean(axis=0)`
  is enough — walks off the allocation and crashes the interpreter. This made
  the whole spatiotemporal side of image correlation (STICS/TICS/iMSD, anything
  with a non-zero frame lag) unusable from Python.

  The first dimension is now the number of pairs actually correlated. Frame
  pairs are also bounds-checked against the ROI and dropped if out of range;
  the correlation loop indexes `roi[frame * pixel_in_roi]` directly, so an
  out-of-range frame number was a second out-of-bounds read. A pair list with
  no valid entries returns an empty result instead of correlating garbage. See
  `test/python/clsm/test_clsm_ics.py`.
- **MLE decay fits (`Fit23`/`Fit24`/`Fit25`/`Fit26`) could segfault on a decay
  whose length did not match the IRF.** The fit derives its channel count from
  the experimental `data`, but `DecayFitData::set_data()` grows only `data` (and
  `model`) to a new decay's length while leaving `irf`/`background` at their
  original size. A decay longer than the IRF therefore made the objective read
  past the end of `irf`/`background` and crash the process — in every binding
  (Python/Java/R), since they all reach native code through `DecayFit*::fit`. The
  single-decay path now validates array consistency the way the batch
  `fit_matrix` already did, via `DecayFitData::has_consistent_fit_arrays()`, and
  reports an invalid fit (`x[0] = -1`) instead of segfaulting. `targetf` is
  guarded too, as it is exposed directly. See
  `test/python/decayfit/test_DecayFit23.py` (`..._decay_longer_than_irf...`,
  `..._empty_data...`).
- **BVA and H2MM dropped the last photon of every burst.** Burst index ranges are
  inclusive `[start, stop]` everywhere they are produced — a 30-photon burst is
  reported as `[0, 29]`, and `BurstFilter` sizes it as `stop - start + 1` — but
  `BVA::compute` and `H2MM::set_bursts_from_tttr` indexed them half-open. Both
  therefore silently discarded each burst's final photon: a 5% count error on a
  20-photon burst, and a biased one, since the discarded photon is the photon
  that ended the burst. This changes the numeric output of existing BVA and H2MM
  analyses. The stale "half-open" wording in `BVA.h` and `H2MM.h` is corrected,
  and `test/python/burstfilter/test_burst_range_convention.py` now pins the
  convention across producers and consumers together — which is what the previous
  per-component tests could not do, since each agreed only with itself.

### Changed
- **Bayesian Blocks is ~9.6x faster** (119 ms -> 12 ms on 127k photons), by
  applying **PELT pruning** (Killick, Fearnhead & Eckley 2012,
  doi:10.1080/01621459.2012.737745) to the dynamic program. A candidate block
  start that has fallen behind the current optimum by more than the change-point
  penalty can never recover, so it is dropped. This is an *exact* optimisation:
  verified byte-identical output across 72 parameter combinations and 19,647
  bursts. The practical consequence is that cost no longer scales with region
  size — raising `max_region_photons` from 512 to 8192 now changes runtime by
  under 10%, against more than 12x before — so boundary quality can be bought
  without a quadratic bill.
- **Max-tree is ~1.3x faster** (8.1 ms -> 6.4 ms). The rolling-ball baseline was
  53% of its runtime and is now threaded, each chunk re-deriving its monotonic
  deque from a time-based halo so the result is identical to the serial sweep;
  the per-photon rate signal is threaded too. The component-tree sweep is
  inherently sequential, which caps this at about 1.5x parallel speed-up.
  Verified byte-identical output across 48 parameter combinations.
- The Bayesian Blocks candidate set is now a struct-of-arrays, carrying each
  candidate's `block_length[i]` and `best[i-1]` beside it so the inner loop reads
  sequentially instead of gathering. Worth a further ~5%, output unchanged.
- **Bayesian Blocks defaults retuned against the simulated ground truth**:
  `p0` 0.05 -> **0.005** and `trigger_contrast` 1.5 -> **2.5**. Together these
  are worth a further ~2x (12.0 ms -> 6.8 ms on 127k photons), bringing the
  method level with `maxtree` on throughput.

  This is not a speed-for-accuracy trade. Chosen by sweeping 60 parameter
  combinations against per-transit ground truth and taking the Pareto frontier,
  then **validated on held-out conditions** (different seeds, plus 0.4x and 3x
  crowding) to check the choice was not fitted to one simulation. On the
  held-out set the new defaults are 3.5x faster with **purity 0.903 vs 0.869**,
  completeness 0.678 vs 0.680 and the 50% detection limit unchanged at 19.1 —
  i.e. the only metric that moved beyond noise moved in the right direction.

  Two things the sweep exposed. `p0 = 0.05` was simply the wrong default:
  0.005 measured better on purity, completeness *and* detection limit
  simultaneously, because splitting a burst into spurious extra blocks costs
  more here than missing a marginal change point does. And `trigger_contrast`
  = 1.5 defeated the two-stage architecture entirely — at that setting the
  trigger fires on roughly a quarter of background positions, and once each
  firing is padded and merged the segmentation covers essentially the whole
  stream, so the cheap stage filtered nothing.

  `max_region_photons` was also swept and left at 4096: dropping it to 512 is
  faster but collapses completeness to ~0.59, and 2048 is indistinguishable
  from 4096.
- **Max-tree `delta` retuned, 0.5 -> 0.15**, which turns out to matter far more
  than any of the performance work. Against the ground truth this raises
  completeness from 0.719 to 0.799, purity from 0.904 to 0.964 and drops the 50%
  detection limit from 13.9 to 11.4 photons — while running slightly *faster*.
  At the base condition it reaches recall 1.000 / precision 0.975 / F1 0.988,
  against 0.976 / 0.929 / 0.952 before, and its split rate falls to zero.
  Confirmed on the held-out conditions (0.714 -> 0.800 completeness,
  0.897 -> 0.973 purity, 15.3 -> 12.0 limit).

  The reason is worth stating because the parameter reads backwards. A *smaller*
  `delta` probes a smaller level drop, so components grow less over it, so more
  of them survive `max_variation` — lower is the more *permissive* setting. It
  therefore shifts the work from the stability heuristic onto the contrast and
  Poisson-significance filters, and since those two are the statistically
  principled tests and stability is a shape heuristic, that is the better
  division of labour.

  This also changes what `significance_mode` is worth: with the new `delta`,
  Li & Ma reaches precision 0.997 and purity 0.994 against the Gaussian form's
  0.975 / 0.964, a much clearer margin than before.

  `background_window` measured better still at 0.2-0.5 s but was **left at
  0.05 s**: the simulation has a constant background, so a longer window is free
  there in a way it will not be on real data, where tracking drift is the whole
  point of the rolling ball. Documented in the header rather than adopted.
- **Defaults are no longer written down more than once.** `TTTR::burst_search`
  had the max-tree and Bayesian Blocks defaults spelled out as literals; it now
  reads them from the settings structs. `burst_search_sliding_window` and
  `burst_search_cusum_sprt` had no C++ defaults at all while the registry
  advertised some, so the two entry points disagreed — they now carry the same
  defaults, and every burst search is callable with no arguments. A new test
  asserts the registry defaults equal the C++ signature defaults for every
  algorithm and parameter, so the remaining duplication cannot drift silently.
  `plot_burst_search_comparison.py` no longer pins tuning parameters either; it
  was benchmarking a combination nobody runs.
- `parallel_for` was duplicated in `BVA.cpp` and the Bayesian Blocks source and
  was about to be copied a third time; it now lives in `include/ParallelFor.h`.

### Not done
- **Approaching `sliding_window`'s throughput is not achievable** for the exact
  searches, and the gap is inherent rather than an implementation defect:
  `sliding_window` does one comparison per photon (~1100 Mphotons/s), while
  `bayesian_blocks` (~10) and `maxtree` (~20) do strictly more work to get a
  lower detection limit and better boundaries. The remaining Bayesian Blocks
  cost is concentrated in flat background stretches, where PELT has no change
  point to prune against; `pad_photons` pads every region with exactly such
  stretches, which is why it is the dominant cost knob.
- Decoupling the local background estimate from the segmented region (a wider
  *counted* span rather than a wider *segmented* one) was implemented and then
  removed: measurement showed it changed completeness by less than the binomial
  error. Padding helps by giving the segmentation edge-placement context, not by
  improving the background estimate.
- A hand-rolled polynomial logarithm (1 ulp, 1.75x faster than `std::log` in
  isolation) was implemented and then removed: in the actual inner loop it made
  the search **23% slower**. Measured in a loop resembling the real one,
  `std::log` costs 3.51 ns/iteration against 4.4-4.6 ns for three different
  hand-rolled variants. The isolated microbenchmark was misleading — there the
  logarithm is the only work, whereas in the real loop its latency is already
  hidden by neighbouring loads and multiply-adds, and a 10-term series is one
  long dependency chain with no instruction-level parallelism to hide. The DP
  now carries a comment saying so, to stop the idea being retried.
- **Functional pruning (FPOP, Maidstone et al. 2017) was implemented and
  removed: it pruned nothing.** The candidate set stayed at 114.1 live starts and
  the cell count came out bit-identical to PELT's (14,480,250), while the root
  solving it requires made the search ~52x slower.

  Two findings are worth recording so this is not retried blind. First, the
  algebra collapses: "candidate i is beaten outright by the newest candidate" is
  exactly `val + ncp_prior <= best_val`, i.e. PELT's rule is the single-rival
  special case of functional pruning. Any gain must therefore come from the
  *intersection* over several rivals. Second, that intersection is empty far less
  often than the literature suggests, because a dilute photon stream is mostly
  homogeneous background: every candidate start inside a long background stretch
  implies almost the same rate, so their viable rate ranges sit on top of one
  another and none is squeezed out. Functional pruning needs candidates that
  disagree about the parameter, and background does not supply that.

  Note also that a fully correct FPOP needs per-candidate interval *lists*, not a
  single interval: a candidate beats newer rivals on an interval but older ones
  on the complement of an interval. The version tried here tracked one interval
  and so pruned strictly less than true FPOP — but since it pruned nothing at
  all, the extra machinery would have had to overcome a 52x cost deficit to break
  even.


## [0.27.0] - 2026-07-17

A **performance and memory** release. Confocal (CLSM/FLIM) reconstruction is
faster and much lighter on memory, single-detector lifetime fitting and
dynamic-FRET analysis are new, and a cross-version benchmark now tracks speed and
peak memory across releases. Measured numbers: [`PERF.md`](PERF.md).

### Performance / memory
- **CLSM lazy fill.** `CLSMImage.fill()` now stores a one-bit-per-event
  acceptance stream-mask instead of eagerly materializing a per-pixel
  `std::vector<int>` of photon indices; per-pixel containers are built only when
  a pixel handle is actually requested. Intensity, lifetime, phasor and
  tttr-index queries run straight off the mask.
- **Virtual fill.** New `CLSMImage(..., build_pixels=False)` skips per-pixel
  allocation entirely; `get_intensity_masked()` does a single-pass scatter into
  the image. Byte-identical intensity to a full fill.
- **Cached moments/phasor.** Mean-lifetime, fast-lifetime and phasor images share
  cached per-pixel raw moments/phasor sums, so re-tuning the IRF/background/
  modulation frequency is an O(pixels) correction rather than an O(photons)
  rescan.
- Measured against 0.26.2 on the same machine (Apple M1 Pro, CPU only; task
  memory = peak RSS minus the post-import baseline): CLSM fill+structure
  −63% time / −12% memory; 2.6 M-pixel HT3 fill −81% time / −40% memory;
  correlation −36% time.
- Lower-level trims (identical results, unchanged API): int32 instead of int64
  for within-burst count / Viterbi back-pointer buffers (BVA, H2MM); skip the
  unused macro-time buffer in BVA photon-count mode; reserve H2MM CSR/Δt and
  `write_ps_file` dataset buffers up front. `FitNExp` buffer-based overloads pass
  NumPy arrays with a single copy instead of boxing through Python lists.
  Deliberately kept: the `fit_buffers` owning-vector copy (required by `fit()`'s
  signature) and the ARGOUTVIEWM malloc handoffs (required by NumPy ownership).

### Added
- **H2MM and BVA** C++ modules for dynamic FRET: photon-by-photon hidden Markov
  modelling (Baum-Welch EM, SQUAREM acceleration, Viterbi) and burst variance
  analysis, with NumPy-array burst inputs/outputs.
- **FitNExp**: native single- and multi-exponential Poisson reconvolution fitter
  for one decay curve, with batched `fit_many` and per-pixel `fit_map` variants
  that thread across cores.
- **Photonscore `.photons` (D7)** reader/writer and **TIFF** 2D/3D array I/O.
- NumPy-native burst API: `TTTR.burst_search` and the BurstFilter/BVA/H2MM
  accessors return NumPy arrays and accept NumPy inputs, no list conversion.
- Cross-version performance + peak-memory monitor (`benchmarks/bench_versions.py`,
  `make_version_plots.py`), a benchmark-backed performance guide, and
  [`PERF.md`](PERF.md) as the single place where benchmark results are recorded.
- **Photon-simulation subsystem** (`SimEngine`, `SimSystem`, `SimSpecies`,
  `SimIntegrator`, `SimGrid`): single-molecule diffusion + photon simulation with
  an OpenMM-style API, PSF fillers, per-molecule coasting for throughput, and
  faithful `to_tttr` export.
- Photon simulator: **ALEX (alternating laser excitation)**. The engine now takes one
  excitation grid per laser (`excitation` may be an array) and each species a per-laser
  brightness matrix `SimSpecies.q_alex`; `SimIntegrator.alex_period` alternates the active laser
  per macro-window (equal-duty, exact integer-window schedule). A doubly-labelled FRET molecule
  emits DD+DA under the green laser and AA under the red laser; the alternation is encoded in the
  macro-time and recovered with `TTTR.alex_to_microtime`. Optional laser-switch markers
  (`alex_markers`) give explicit ground truth. New `SimEngine.alex_period()` / `n_lasers()`,
  config `alex.json`, example `alex_smfret.py`, notebooks `alex_01_simulation_basics.ipynb` /
  `alex_02_smfret_es.ipynb`, and `test/python/simulation/test_alex.py`.

### Fixed
- `SimEngine.to_tttr` no longer segfaults when the stream contains marker events (CLSM scan or
  ALEX) and no longer mangles the micro-time: the SPC encoder now carries the simulated micro-time
  (FLIM) axis faithfully (a `to_tttr` round-trip preserves `micro_times` exactly) and skips marker
  events instead of indexing out of bounds. Micro-time filters can now be used after `to_tttr`.
- Photon-count stop condition (`n_ph_max`) now counts photons only; marker events no longer consume
  the photon budget.
- T2 decoding: HydraHarp/MultiHarp `special` records with channel 0 are the sync
  input and now decode as photons on channel 0 (validated bit-exact against the
  independent `ptufile` decoder), not as markers.
- CLSM marker/dimension header auto-configuration ported from the Python wrapper
  into C++ so the R/Java/native bindings reconstruct images correctly.
- Cross-platform SWIG correctness on LP64 Linux: burst-array parameters use the
  NumPy `IN_ARRAY1` convention (a `std::vector<int64_t>` argument silently
  rejects Python lists/arrays there), FitNExp buffer overloads use unique
  parameter names (a re-`%apply` left a stale argout typemap in the R wrapper),
  and burst structured-property helpers avoid NumPy-2 array truthiness.

### Changed
- Faster used-channel scan and by-const-ref tag lookup in `TTTR`; the GIL is
  released around the CPU-only decay fits.
- Portable SIMD: AVX/NEON kernels are compiled in and selected at runtime, so a
  single binary stays fast across CPUs.

## [0.26.0] - 2026-03-08

### Added
- Becker & Hickl SPCM support (PR #48 by @cqian89)
  - Pixel-marker binning for BH SPC-130/140/150 detectors
  - Automatic `.set` file parsing and dimension inference
  - Frame 1 adjustment for BH SPC data
  - Truncated recording recovery

### Fixed
- CLSM `get_fluorescence_decay` stack_frames bug (PR #49 by @cqian89)
  - Fixed bug where only the last frame was processed when stack_frames=True
- CLSM `get_phasor` precision loss (PR #49 by @cqian89)
  - Fixed precision loss by using float instead of int calculation

### Changed
- Updated test data to include BH SPCM FocalCheck sample data
- Added new integration tests for BH pixel marker binning

## [0.25.1] - 2025-02-20
### Fixed
- Various bug fixes and improvements

## [0.25.0] - 2024-12-15
### Added
- Support for Photon-HDF5
- Transparency in-memory compression
- Linearity correction for micro times

For older releases, please refer to the documentation.
