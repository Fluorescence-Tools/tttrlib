# Changelog

## [Unreleased]

### Added
- **s2ISM** (`CLSMSuperRes.s2ism_reconstruction`) -- joint super-resolution and
  optical sectioning by adaptive maximum-likelihood deconvolution over a stack
  of axial planes, after Zunino et al., *Nat. Photonics* (2025). A real port of
  the reference `s2ism/s2ism.py` (`amd_update_fft`, `amd_stop`,
  `max_likelihood_reconstruction`): the detector array is treated as Nch images
  of one object seen through Nch PSFs and inverted jointly by multi-image
  Richardson-Lucy, with the sectioning coming from giving the object several
  axial planes with their own PSFs. A line-by-line numpy transcription of the
  reference is the test oracle and the port matches it to round-off (3e-15
  relative). Two details the parity test pins down: the PSF flip is numpy's
  (about index (N-1)/2 -- conjugating the spectrum instead is a circular flip
  about 0 and lands one sample off per axis), and the update carries no extra
  normalization because each plane's PSF is already normalized over
  (channel, y, x).

- **SOFISM** (`CLSMSuperRes.sofism_reconstruction`) -- super-resolution optical
  fluctuation image scanning microscopy, after Sroda et al., *Optica* **7**, 1308
  (2020), in the formulation restated by Beck et al., arXiv:2606.16508. It
  multiplies the two independent resolution mechanisms of ISM and SOFI: for
  every pair of detector elements the temporal cross-correlation of the
  fluctuations `C_ij(r,tau) = 1/(N_t-tau) sum_t dI_i(r,t) dI_j(r,t+tau)` is
  formed, whose effective PSF is the *product* of the two elements' PSFs; the
  pair acts as one virtual detector midway between them and is reassigned by
  `v_ij = (v_i + v_j)/2`. Autocorrelation terms are excluded by default, since
  their shot noise does not cancel. On a simulated blinking sample the measured
  spot narrows 9.30 -> 6.11 -> 4.74 px going confocal -> APR-ISM -> SOFISM, a
  factor 1.96, matching the paper's stated ~2x before Fourier reweighting.
  `CLSMSuperRes.fourier_reweight` applies the Wiener-type filter
  `W(k) = 1/(OTF^2(k) + eps)` of the paper's Eq. 3.

### Fixed
- **eSRRF now reproduces NanoJ.** The RGC kernel deviated from
  `liveSRRF.cl` in four places, none of which showed at the image centre and
  all of which showed at the borders: the 2x gradient upsampling read a
  zero-padded buffer instead of clamping to the native width and height; `Gy`
  was fetched with `Gx`'s sub-pixel index shift (NanoJ shifts each gradient
  along its *own* axis); the `0 < vx < width` / `0 < vy < height` guards that
  keep out-of-frame samples out of both the numerator and `distanceWeightSum`
  were missing; and `getInterpolatedValue`'s bilinear extrapolation branch,
  which NanoJ takes wherever the bicubic 4x4 support does not fit, was not
  ported at all. The numpy oracle in `prototype/esrrf/esrrf_reference.py` had
  the same three last defects and so agreed with the C++ about being wrong; both
  are fixed, and the A/B test now demands a double-precision match over the
  whole field instead of a median with a 0.1 tolerance on the tail.

- **eSRRF photon reassignment.** The magnified flat position was accumulated in
  `int`, overflowing above ~2^31 (a 512x512 frame at magnification 8 wraps at
  128 frames). In `channel_mode="split"` the RGC field was picked with
  `event_index % n_channels`, which is not the photon's channel; it now comes
  from the frame block the photon belongs to, via the new
  `CLSMImage::get_channel_of_frame`. `CLSMSuperRes::write` discarded the
  container writer's return value and always reported success. `temporal_combine`
  divided by `n_frames - 1` for `TAC2` and produced infinities on a single frame.

- **`CLSMImage::get_photon_positions`** doubled the position along a line in its
  materialized-pixel path: the time offset is measured from the line start, so it
  already contains the pixel index that was being added to it.

- **ISM reconstructions were quantitatively wrong.** The inverse FFT applied no
  `1/N` normalization (pocketfft never normalizes on its own, contrary to the
  comment claiming it did), so every shifted detector channel came back scaled
  by the number of pixels and APR did not conserve photon flux. Adaptive pixel
  reassignment also ignored both of its documented knobs -- `usf` (so shifts were
  integer-pixel only) and `filter_sigma` -- and omitted the Hann apodization the
  BrightEyes-ISM reference applies before correlating. The estimator now
  reproduces `skimage.registration.phase_cross_correlation(..., normalization=None)`
  on apodized, optionally denoised input, including the upsampled-DFT peak
  refinement, and is exposed as `CLSMSuperRes.shift_vectors`.

- **The ISM subpixel shift no longer wraps around the frame.** It is a Fourier
  shift, which is periodic, so content leaving one edge reappeared at the
  opposite one -- with a bright structure at the left of the field, the far
  right margin carried 3-18% of the peak where it should carry nothing. The
  shift is now applied on a zero-padded canvas and cropped back, which also
  confines the interpolation's ringing (far-margin leakage drops to ~0.2%).
  Photons registered off the edge of the frame are now dropped rather than
  wrapped, so an APR sum is no longer exactly equal to the input total; the
  focus-ISM planes still partition the reassigned total exactly. Note that the
  BrightEyes-ISM reference defaults to `mode='interp'` (a compact-support
  spline) rather than the Fourier shift ported here; the boundary behaviour now
  agrees, the interpolation kernel still differs.

- **Focus-ISM follows the published algorithm again.** The separable
  least-squares core (solve the mixing fraction *B* exactly at each trial
  background width) was sound and is kept, but everything around it was
  invented: a confidence weight and a ridge shrinkage that biased *B*, an
  edge-aware smoothing of the *B* map, detector coordinates inferred from
  measured shifts rather than the known lattice, an unused APR pass, and a
  `threshold` parameter that was accepted and ignored -- where the reference
  assigns sub-threshold pixels wholly to background. It now follows
  `FocusISM_lib.focusISM`/`pixel_fit_2`: APR first, the in-focus width fitted
  from the calibration-patch fingerprint, `sigma_bound` as the reference's
  `sigma_B_bound`, and the threshold honoured.

### Removed
- `CLSMISM` is gone; every ISM algorithm now lives on `CLSMSuperRes`, which had
  been forwarding to it. `apr_reconstruction` returns `(1, ny, nx)` and
  `focus_reconstruction` returns `(3, ny, nx)` -- in-focus signal, background,
  APR sum -- so the `nz` parameter, which only replicated the same plane, is
  gone with it.
- The old `CLSMSuperRes.s2ism_reconstruction`, `.deconv_reconstruction` and
  `.generate_ism_psf`. The first was a variance-weighted APR carrying the name of
  a different published method -- the real s2ISM is now implemented, see Added;
  the second a hand-rolled Richardson-Lucy against
  a hardcoded Gaussian, not ISM deconvolution; the third imported from the repo's
  `prototype/` directory at call time and otherwise silently fell back to a
  Gaussian approximation. The PSF simulator remains available to examples as
  `prototype/esrrf/simulate.generate_ism_psf`.
- `CLSMSuperRes.frc_resolution` returned an FRC *curve*, not a resolution, and
  was not a port of anything. It is now `frc_curve` plus `frc_resolution`,
  ported from BrightEyes-ISM `FRC_lib`: Hann apodization before the transform
  (without it the spectral leakage from the frame edges correlates perfectly
  between the two halves and holds the curve up at every frequency), the
  reference radial binning and frequency axis, LOWESS smoothing or a sigmoid
  fit, and the fixed 1/7, 3-sigma and 5-sigma threshold criteria. Verified
  against the reference: the raw curve to 2e-15, the frequency axis exactly,
  and the resolution to nine digits for both criteria. The LOWESS is
  reimplemented rather than taken from statsmodels, which meant matching its
  window choice -- the r *nearest* points, where a symmetric window twice as
  wide oversmooths and shifts the resolution by 0.25%.

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
- **A C++ throw from `TTTR`, `TTTRMask` or `H2MM` aborted the interpreter.**
  Those three `.i` files had no SWIG `%exception` handler, so an exception
  raised to report a bad argument (a mismatched array length, an unreadable
  file, a foreign msgpack payload) unwound through the wrapper and terminated
  the process — no traceback, and no way to catch it. They now raise
  `RuntimeError` like the rest of the library.
- **`get_used_routing_channels` could return the channels the file had before
  you edited it.** `set_routing_channel_at` is public but
  `find_used_routing_channels`, which refreshes the cache it invalidates, was
  protected — so any code that rewrote channels (as the new H2MM state split
  does) left the accessor silently reporting stale values with no way to fix it.
  `find_used_routing_channels` is now public; the cache itself stays protected.
- **A response sized for one channel was accepted on a multi-channel fit, and
  read out of bounds.** `DecayFitProblem::validation_error` treated any
  `n_bins`-long `irf`/`background` as "shared", but a polarisation-resolved model
  reads `n_channels * n_bins` samples straight out of the array — so a
  half-length response was an out-of-bounds read, not a shorthand. It was silent:
  the memory past a heap vector is usually mapped, so the curve came back with
  plausible numbers and the process died later in unrelated code. Found by
  bisecting exactly that: a crash in a different test file, four runs out of
  four. A shared response is now accepted only when there is one channel to share
  it with, and the message names both sizes.
- **`model_curve` validated nothing.** `fit` reaches the sizing check through
  `bind`/`is_usable`; `model_curve` bypassed it and handed raw pointers to the
  kernel. It now calls `require_valid()` first, so a mis-sized problem raises
  instead of corrupting the heap.
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
- **`TTTR::set_routing_channel`** — the bulk companion to
  `set_routing_channel_at`. Relabelling a whole measurement through the
  per-event setter costs one binding call per photon, which at photon scale is
  the dominant cost of the operation. Refreshes `used_routing_channels`, so the
  accessor cannot go stale behind it.
- **`H2mmChannelMap::allocate` and `H2mmStateSidecar::set_arrays`** — the id
  allocation and the sidecar format, usable without an `H2MM` engine. A caller
  that assembled its photon streams some other way (several source files, a
  burst table, a nanotime-split stream set) can now write *this* layout and
  *this* file rather than a second, subtly different one; `H2MM::build_channel_map`
  is a thin wrapper over the former.
- **H2MM state decoding that reports a distribution, not a winner.** Viterbi
  answers "what is the single most likely state sequence"; most burst analysis
  instead asks "how do the photons distribute over the states", and the argmax
  answers that badly — photons at γ = (0.7, 0.3) all land in state 0, so
  well-separated states are inflated and ambiguous or short-lived ones erased.
  On simulated data with 75/25 occupancy and overlapping emission profiles the
  Viterbi occupancy error is **0.081** against a ground truth the two new
  decoders reproduce to **0.0006**.

  `H2MM::posterior` returns the per-photon posterior **γ** as a float32
  `(N, n_states)` matrix — the quantity the E-step already formed and threw
  away, and the same array the reference `H2MM_C` calls `gamma`.
  `H2MM::sample_states` draws each photon's state from its own γ row (faithful
  marginal, but the independent draws shatter dwells — 15103 where the truth has
  1244 — so it must not drive dwell or transition statistics), and
  `H2MM::sample_paths` does **FFBS** (forward filtering, backward sampling),
  drawing whole trajectories from `P(path | data)`, which reproduces the
  marginal *and* the dwell structure (1191 dwells against a true 1244).
  Averaging over draws is multiple imputation: the spread is the decoding
  uncertainty a single Viterbi path reports as zero. `viterbi` is untouched and
  stays the default.

  Both samplers use a **counter-based** RNG keyed by
  `(seed, draw, photon index)` rather than a shared stream, so output is
  bit-identical at any thread count. The forward filter is shared across FFBS
  draws, so extra draws are nearly free (8 draws over 200 k photons: 3.3 ms,
  against 1.6 ms for γ alone and 110 ms for the fit).

  A decode can be persisted two ways, which agree photon for photon:
  `split_routing_channels` rewrites each photon's routing channel so every
  `(stream, state)` pair has its own id and writes one PTU holding every photon
  — self-describing, so per-state decays and FCS become ordinary `Channel`
  selections; and `state_sidecar` writes a msgpack sidecar leaving the source
  file untouched, with no id budget. The split **compacts the whole id space**,
  not just the new ids: source channels are usually sparse (1, 12, 30 for three
  detectors is ordinary) and those gaps are dead weight in a field a few bits
  wide, so the used ids compress to `0..k-1` and the `(stream, state)` pairs are
  allocated immediately after, densely, one step apart. Everything then lies in
  one run from 0 — a file whose detectors sat at 1, 12 and 30 needs 3 bits
  instead of 5, so a split that would have required PTU round-trips through an
  SPC-600/256 record. Both directions are recorded in the map, which travels in
  the sidecar. Photons no decoder assigned move to the compressed form of the
  channel they were on, so afterwards those ids hold *only* unassigned photons —
  and a tool that hard-codes channel numbers will be wrong about a split file.

  `set_bursts_from_tttr` / `set_bursts_from_filter` now record the source photon
  index (`get_photon_index`), which both persistence paths need and which was
  previously discarded.
- **`TTTRMask::to_msgpack` / `from_msgpack` / `write_msgpack` / `read_msgpack`**
  (Python: `to_bytes` / `from_bytes`). `to_json` emits one JSON integer **per
  event**, so a 10 M-photon mask is ~20 MB of decimal text; the msgpack form
  carries the already bit-packed words verbatim as a `bin` field, at `size/8`
  bytes plus a header. msgpack is now the convention for tttrlib outputs that
  scale with the photon count — it costs no new dependency, since nlohmann/json
  is already vendored. `to_json` stays for small masks and compatibility.
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
- **Bayesian Blocks stage-1 trigger is now O(n).** The Fries/Eggeling trigger — a
  pure macro-time-difference test — marked all `m` photons of every firing
  window, which costs O(n*m) and costs it precisely inside bursts, where windows
  fire at every offset. Consecutive firing windows overlap by construction, so
  the candidate runs are now accumulated in a single pass and the intermediate
  per-photon flag array is gone. The candidate set is unchanged; only the
  bookkeeping is cheaper, which makes permissive trigger settings more affordable.

  Measured against the simulated ground truth, the trigger/segmentation trade is
  monotonic but shallow: `trigger_contrast` 1.5 gives F1 0.952 in 51 ms,
  2.5 (the default) 0.943 in 11 ms, and 4.0 0.941 in 4.5 ms for 200k photons. A
  permissive stage 1 is genuinely the most accurate setting, at roughly 5x the
  time for +0.01 F1.

- **Coincident (multi-detector) burst search** — `TTTR.burst_search_coincident`,
  registered as `coincident`. Keeps only bursts found *independently* in several
  groups of detectors, which is what rejects singly-labelled and photobleached
  molecules in ALEX/PIE; the classical dual-channel burst search is the
  two-group case. Any number of groups is accepted and `min_groups` sets how many
  must agree, so "2 of 3" is expressible as well as "all of 2". It is a
  composition rather than a new algorithm — the search named by `algorithm` runs
  once per group — so it works with every other registered search, including ones
  added later. Coincidence is decided in time, not per photon.

  On a simulation with 40 molecules emitting into all three detector groups plus
  40 *brighter* ones emitting into a single group, a pooled max-tree search finds
  all 80 and cannot separate them; the coincident search finds the 40 complete
  events and none of the 40 partial ones, at every quorum tested.

  Composite entries like this carry a `parameters_of: {category, selector}` link
  on the property holding the inner search's parameters, so a consumer can render
  them as a nested form rather than as raw JSON. chisurf does; the link is
  declarative, so any future composite entry nests the same way with no new code.

- **API registry and generated API index.** Two complementary ways to ask what
  tttrlib can do, both as JSON so every language binding reads the same bytes
  instead of re-declaring lists that drift.

  `tttrlib.registry()` (C++ `tttrlib::registry_json()`, `include/Registry.h`) is
  the *curated* layer: `{category: {name: entry}}`, currently `burst_search` and
  `file_container`. Entries carry what a signature cannot express — what a thing
  is, which parameters are meaningful, their units, ranges and defaults — as
  standard **JSON Schema**, so a consumer that can already render a JSON Schema
  can offer a tttrlib feature with no tttrlib-specific code and picks up new
  entries on upgrade. `registry("burst_search")` supersedes
  `TTTR.burst_search_algorithms()`, which still works.

  `tttrlib.api_index()` is the *generated* layer: every class, method, function,
  property and attribute the module exports — 111 classes, ~1450 methods, 73
  functions — with signatures, defaults and docstrings, so a scripting language
  can discover and call anything without parsing C++ headers. It is derived from
  the built module rather than authored, which is the only way coverage that wide
  stays correct; a hand-written index would be wrong within a release.
  `tools/generate_api_index.py` dumps either layer to a file.

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
