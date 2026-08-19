---
title: "Algorithm validation register: every algorithm in tttrlib A/B-tested against an independent reference"
status: living
last_verified: 2026-08-17
---

# Algorithm validation register

Every algorithm in tttrlib — not only `modules/math` — has been A/B-tested
against an independent reference implementation, or where none exists against
an analytic / simulated known answer, and the A/B is a **permanent test**. Each
validated header carries, right after its include guard:

```
// Validation: A/B-TESTED 2026-08-17 -- <reference(s), metric>. <test path>.
//   Register: okf/testing/algorithm-validation.md
```

(`KNOWN-ANSWER-TESTED` when the check is a simulation/analytic answer, not a
second implementation; `EQUIVALENCE-TESTED` for streaming-vs-batch.) A header
whose comparison **failed** is not marked; the failure is a `BUGS.md` entry
and a test that pins it, and this register says so.

Rule (user, 2026-08-11): a method reimplemented in-tree is A/B'd against the
reference and the A/B is kept as the acceptance test — the **upstream code
itself** where it exists (user, 2026-08-17: VicidominiLab's repos for the
blind IRF, ISM, s2ISM kernels — cloned into `../chisurf/junk`, run live or in
a subprocess), a transcription from the paper only when the code cannot run
here.

**ChiSurf is not a reference (user, 2026-08-19: "no chisurf as ref, not
stable").** It moves, and this library is its upstream, so agreement between
the two says only that two things which change together still agree; "bit-exact
with ChiSurf" pins a snapshot, not the mathematics. Every kernel that rested on
it now rests on an upstream package, a paper transcription, or -- best where it
applies -- **ground truth from a simulation**: the burst searches are checked
against the bursts that were injected, which no amount of agreement between two
implementations can substitute for. (Ground truth found a real defect the
agreement test had hidden: with the old settings both implementations returned
three detections covering forty injected bursts and agreed perfectly while
resolving nothing.)

The sweep was completed on 2026-08-19. Three places still rested on ChiSurf
after the first pass, each hiding differently:

* `BurstSearchBOCPD.h` credited ChiSurf's numba code in its validation comment
  long after the reference had been rewritten from Adams & MacKay 2007. The
  header was stale, not the test — but a header is what a reader checks.
* `test/python/pda/test_pda3c_core.py` compared **every** kernel against
  `chisurf.core.fluorescence.pda3c` behind
  `sys.path.insert(0, '/Users/tpeulen/dev/chisurf')`. Three of its five tests
  therefore skipped everywhere but one machine — a ChiSurf dependency and a
  coverage hole in the same line. Rewritten against moment exactness, the
  cascade formula and the defining composition; it now runs everywhere.
* `test_ab_pda_reference.py::test_channel_probabilities_against_chisurf`, same
  absolute path, same outcome.

A second lesson from the same sweep: an invalid reference is often also an
*absent* one. Grep for the skip as well as for the name — a test that skips is
indistinguishable from a test that passes in a summary line.

`modules/math` has its own register with the per-kernel rows:
[`math-kernel-validation.md`](math-kernel-validation.md). Everything else is
below.

## How to run

```bash
PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -m pytest \
  test/python/misc/test_math_ab_*.py \
  test/python/decayfit/test_ab_decay_reference.py \
  test/python/clsm/test_ab_*_reference.py \
  test/python/correlator/test_ab_correlator_reference.py \
  test/python/fluctuation/test_ab_pch_reference.py \
  test/python/bva/test_ab_bva_2cde_recurrence_reference.py \
  test/python/burstfilter/test_ab_burst_reference.py \
  test/python/hmm/test_ab_hmm_reference.py \
  test/python/kinetics/test_ab_kinetics_reference.py \
  test/python/pda/test_ab_pda_reference.py \
  test/python/test_ab_core_reference.py \
  test/python/corrections/test_ab_corrections_reference.py \
  test/python/streaming/test_ab_streaming_equivalence.py \
  test/python/simulation/test_ab_simulation_reference.py
```

Reference libraries are optional imports; a missing one skips its class
visibly. Several suites compile a small C++ harness at test time
(`test/cpp/ab_numerics_harness.cpp`, `ab_burst_harness.cpp`,
`ab_simulation_harness.cpp`, `ab_burstml_mex_driver.cpp` + `burstml_mex_shim/`)
and skip without a compiler. Reference libraries that must not enter the
project environment (hmmlearn, filterpy, phasorpy, astropy, H2MM_C, PyBroMo)
were run once in scratch/benchmark venvs by the `gen_*_reference.py` scripts
beside the tests, and their inputs **and** outputs are the recorded
`test/data/reference/*_reference.npz` fixtures. FRETBursts, pycorrelate and
multipletau are driven live from `benchmarks/.venvs/`.

## Register by area

Verdicts: **PASS** — agrees to the stated metric. **PASS (bounded)** — agrees
up to a documented, understood difference. **KNOWN-ANSWER** — validated
against an analytic or simulated truth, no second implementation exists.
**EQUIVALENCE** — streaming equals batch. **FAIL** — defect found; see the
BUGS.md entry. **NO-REF / not marked** — plumbing, or nothing to compare.

### Decay (`modules/spectroscopy/decay`, `imaging/clsm/DecayPhasor.h`) — `test/python/decayfit/test_ab_decay_reference.py`, `test/python/clsm/test_ab_phasor_reference.py`

| Header | Kernel | Reference | Metric / result | Verdict |
|---|---|---|---|---|
| DecayConvolution.h | `fconv` (scalar, SIMD) | NumPy trapezoid convolution | 1e-16 | PASS |
| DecayConvolution.h | `fconv_per`, `fconv_per_cs`, `fconv_per_cs_time_axis` | brute-force periodic sum (IRF tiled 40 periods, folded) | 1e-16 (bins ≥ 1); `fconv_per_cs` bin 0 carries an extra `dt/2·irf[0]·…` term, invisible when irf[0]=0 | PASS (bounded, pinned) |
| DecayConvolution.h | `sconv` | np.convolve, half endpoint weights | 1e-13 | PASS |
| DecayConvolution.h | `fconv_ref` | corrected-amplitude trapezoid sum | 5e-17; **the Python wrapper dropped `dt`** (C++ default 0.05 always) — fixed 2026-08-17 | PASS (after fix) |
| DecayConvolution.h | `shift_lamp` | np.interp | 1e-16 | PASS |
| DecayConvolution.h | `rescale`, `rescale_w`, `rescale_w_bg` | closed formulas | 1e-12 (`rescale_w_bg`'s 3rd arg is inverse-error e, weight e²+1e-12; header says 1/w²) | PASS |
| DecayConvolution.h | `add_pile_up_to_model` | Coates 1968 | exact with the *inclusive* cumsum; Coates' Σ_{j<i} differs 1.4e-4 rel | PASS (bounded) |
| DecayFit23.h, DecayStatistics.h, DecayFit.h | objective, optimum, r/rs | NumPy Poisson likelihood on an independent convolution; scipy Nelder-Mead; closed anisotropy formulas | 1e-13 rel objective, 1e-7 optimum, 1e-10 r/rs; 2.7 ns recovered | PASS |
| DecayFit24.h | objective, optimum | same | 1e-13 / 1e-7 | PASS |
| DecayFit23.h + DecayStatistics.h | `neyman_lsq` / `gehrels_lsq` objectives | NumPy χ² on the reference model; scipy Nelder–Mead on the same statistic | **advertised but ignored until 2026-08-17 (Poisson ran regardless)**; now evaluate 1e-9, optimum = scipy's, three objectives → three optima | PASS (after fix) |
| DecayFit25.h | `selected_index` | argmin of the NumPy Fit23 likelihood over candidates | 3/3 | PASS |
| DecayFit26.h | objective, optimum | NumPy mixture likelihood, scipy bounded Brent | 1e-13 / 1e-8 | PASS |
| DecayFitNExp.h | `FitNExp` | scipy Nelder-Mead on the same Poisson-mixture NLL | lifetimes 1e-7, NLL identical, model 4e-17; 0.7/3.5 ns 60/40 recovered | PASS |
| DecayFitDFA.h | `dfa_convolve` (spectral/recursive/shift), `dfa_vv_vh_convolved` | np.fft circular convolution of the closed-form periodic decay | 1e-16 / 1e-10 / 6e-11 | PASS |
| DecayFitPrior.h | 7 priors `lnpdf` | scipy.stats logpdf | 1e-10; TruncatedNormal = truncnorm up to the truncation constant | PASS / bounded |
| DecayPatternFit.h | `decay_pattern_fit` | scipy.optimize.nnls | pre-existing | PASS |
| MaxEntTcspc.h | `tcspc_*` | brute-force sum, known answer; scipy KKT (round 1) | pre-existing + round 1 | PASS |
| BlindIRF.h | `blind_irf_estimate` | **VicidominiLab `birfi`** (junk checkout, subprocess; its n/2 `ifftshift` roll pinned and undone), known answer | aligned IRFs corr 0.978–0.999 vs birfi over 4 configs × 30/500 iterations, peaks within 0.15 ns, never worse vs truth; > 95 % mass within ±0.5 ns — **was 25 %: the port's SG derivative abscissa mismatch and centroid-only lifetime, fixed 2026-08-17** (circular forward model is birfi's, kept, made exact for any n) | PASS (after fix) |
| DecayPhasor.h | `compute_phasor_bincounts`, `phasor_of_bincounts`, g/s | phasorpy 0.4 (recorded), complex division | 1e-16 raw/IRF/calibrated, harmonics 1–2 | PASS |
| StreamingDecayHistogram.h `StreamingPhasor` | raw phasor | phasorpy | 1e-11 | PASS |
| DecayFitModel/Problem/Context.h | plumbing | — | — | not marked |

All decay surfaces are reachable from Python since 2026-08-17: `fconv_cs_time_axis` (missing `%apply`), `DecayPhasor.compute_phasor` (+ `compute_phasor_selection`) and `ProductPrior` (`VectorDecayFitPrior`) were bound and each has an A/B row above.

### FCS, PCH, burst-shape statistics — `test/python/correlator/test_ab_correlator_reference.py`, `fluctuation/test_ab_pch_reference.py`, `bva/test_ab_bva_2cde_recurrence_reference.py`

| Header | Kernel | Reference | Metric / result | Verdict |
|---|---|---|---|---|
| Correlator.h | `wahl` unnormalised | NumPy multi-tau pair counter; pycorrelate `pcorrelate` per cascade | rounding-exact; identical | PASS |
| Correlator.h | `wahl` normalised | analytic G(τ) of a simulated blinking emitter; multipletau on the binned trace | 3σ amplitude, tail 1±0.02; 3 % on G−1 | PASS (bounded: the axis label sits at the upper end of the coarse bin by ≤ one coarse step) |
| Correlator.h | `laurence` ACF | exact pair counts on [x_k, x_{k+1}); pcorrelate | bins ≥ 1 exact | PASS |
| Correlator.h | `laurence` CCF | exact pair counts | **was all-zero / cumulative on the first bin — unsigned wrap of `t2 − t1`; fixed 2026-08-17**, now exact both orderings | PASS (after fix) |
| Correlator.h | `felekyan` unnormalised | NumPy pair counter of its block structure | rounding-exact as implemented | PASS (structure) |
| Correlator.h | `felekyan` normalised | blinking known answer; wahl as a function of τ on real data | **was labelled with the wahl axis (2^k) while counting at 2^(k−1) — τ off up to 25 %; fixed 2026-08-17** with its own contiguous axis; now brackets G(τ) and agrees with wahl in τ | PASS (after fix) |
| CorrelatorCurve.h / CorrelatorPhotonStream.h | multi-tau axis, `coarsen`, `make_fine` | NumPy construction | exact | PASS |
| Fdc2D.h | `fdc_*` | original MATLAB (Octave) fixture | identical counts | PASS (pre-existing) |
| StreamingCorrelator.h | streaming wahl | batch | 5 % on a blinking trace; batch equivalence pre-existing | EQUIVALENCE |
| PhotonCountingHistogram.h | `pch_single_species` | scipy.integrate.quad of the radial integral | 3e-4 (C++ is a 1000-pt Riemann sum) | PASS |
| PhotonCountingHistogram.h | `pch_open_system`, `pch_mixture` | compound-Poisson PGF/FFT; analytic moments; np.convolve | 1e-4 / 1e-9 / 1e-12 | PASS |
| PhotonCountingHistogram.h | `pch_single_species` / `pch_open_system` vs **pysimfcs** (J. Unruh's NumPy port of the Jay_Plugins PCH: Chen 1999 eq. 16, incomplete gamma) | independent implementation (junk/pysimfcs, live) | k ≥ 1 shape identical (ratio constant to 1e-5); open-system P(k) equal to 5e-5 once **`avg_n` = N_PSF·16/√(2π) = 6.38 N_PSF** — tttrlib references N to V0 = 4π w0³, Chen/pysimfcs to V_PSF; was undocumented, header now says so | PASS (bounded: N convention documented) |
| PhotonCountingHistogram.h | `fida_dvdx_gaussian`, `fida_pch` | analytic dV/dx; PGF inversion; moments | 1e-12 … 1e-9 | PASS |
| PhotonCountingHistogram.h | FIDA ≡ PCH | change of variables with a converged (65536-bin) profile | 5e-3; **the default 256-bin profile is an unconverged quadrature (N ~6.8× the converged N, shape same)** | PASS (bounded, caveat) |
| BVA.h | `compute` (photon slices, time windows), static line | NumPy from Torella's definition; binomial line | 1e-12 | PASS |
| TwoCDE.h | FRET-2CDE (Laplace/Gaussian), ALEX-2CDE | FRETBursts `kde_laplace`/`kde_gaussian` live + Tomov formulas | 1e-9 | PASS |
| RecurrenceAnalysis.h | `pair_statistics`, `same_molecule_probability`, `recurrence_efficiencies` | NumPy pair counting, Poisson expectation, planted recurrences | exact / 5 % / 2 % | PASS |

### Burst search — `test/python/burstfilter/test_ab_burst_reference.py`

| Header | Kernel | Reference | Metric / result | Verdict |
|---|---|---|---|---|
| TTTR.h / BurstSearchDispatch.h | sliding window | FRETBursts 0.8.3 `bsearch_c` + `bsearch_py` live | (istart, istop) identical, 6 streams × 5 settings | PASS |
| TTTR (Python) | `burst_search_coincident` (DCBS) | FRETBursts `and_gate` | identical after fusing FRETBursts' overlapping outputs | PASS (bounded) |
| TTTR.h | `burst_search_cusum_sprt` | Zhang & Yang 2005 in NumPy; PAM `CUSUM_burstsearch` in Octave | identical; Jaccard ≥ 0.85 vs PAM's discretised variant | PASS / behavioural |
| BurstSearchKalman.h | `burst_search_kalman` | **ground truth** (40 injected bursts per case) + **filterpy 1.4.5** running the filter with the detection in NumPy (recorded fixture) | precision 100 % (every burst found was injected), recall 73–98 %, and identical to the filterpy reference on 4 configurations | PASS |
| BurstSearchBOCPD.h | `burst_search_bocpd` | Adams & MacKay 2007 run-length recursion transcribed in NumPy from the paper (plug-in Poisson predictive) | identical | PASS (header says Negative-Binomial; the code is plug-in Poisson) |
| BurstSearchBayesianBlocks.h | `bayesian_blocks_events`, `ncp_prior_from_p0` | astropy `bayesian_blocks(fitness='events')` recorded; Scargle 2013 eq. 21 | change points identical on 7 sets; 1e-12 | PASS |
| BurstSearchBayesianBlocks.h | full two-stage search | — | injected bursts found, ≤ 6 detections; **ground truth** below (36–39 detections for 40 bursts, precision 100 %, recall 90–98 %) | KNOWN-ANSWER |
| BurstSignificance.h | Li & Ma, Poisson tails, σ conversions, trials | Li & Ma 1983 eq. 17; scipy.stats | 1e-10 / 1e-8 | PASS |
| BurstConfidence.h | `burst_confidence` (3 modes) | NumPy transcription | 1e-9 | PASS |
| BurstFilter.h / BurstFeatureExtractor.h | `find_bursts`, properties, filters, E | NumPy; FRETBursts size/width | exact / 1e-12 | PASS |
| BurstML.h | `neg_log_likelihood` | **the original FRET_burstML MEX compiled natively** (GSL, shims in `test/cpp/burstml_mex_shim/`) | ratio 1 ± 1e-12; 2–3 states, 2–3 colours | PASS |
| BurstSearchMaxTree.h | `build_max_tree_1d` (`max_tree_1d` binding) | **skimage 0.25.2 `morphology.max_tree`** (recorded, 6 signals; live on the 2 M-sample bench signal) | component set (level, lo, hi, parent) identical, 1.9 M components; 11× | PASS |
| BurstSearchMaxTree.h | `burst_search_maxtree` (attribute filter + MSER) | — | injected-burst recovery (`test_burst_search_maxtree.py`); **ground truth** below (recall 100 %, precision 95–100 %, 3 false positives in background-only) | KNOWN-ANSWER |
| StreamingBurstDetector.h | streaming search | batch / FRETBursts rule | identical | EQUIVALENCE |
| BurstFeature.h | `build_kde` (Laplace 5τ / Gaussian 3τ two-pointer window) | FRETBursts `kde_laplace`/`kde_gaussian` live, through TwoCDE (the only caller) | 1e-9 | PASS (via TwoCDE); stream builder / reduction is plumbing |

#### Every search against simulation ground truth — `test/python/burstfilter/test_burst_search_ground_truth.py`

The rows above ask whether a search agrees with somebody else's implementation.
This suite asks the question two agreeing implementations can both fail: the
stream is simulated, so the bursts in it are known — are those the ones that come
back? It is driven from the registry, so a search added later is measured the day
it registers, and it runs on three seeds because a floor that holds for one seed
is a floor somebody tuned.

Workload: 40 transits of 60–200 photons over 100–400 µs (0.25–1.5 MHz), 3–8 ms
apart, on a 50 kHz Poisson background — ~16 000 photons, ~30 % of them in a burst.

| Search | Detections (40 bursts) | Precision | Recall | Widest detection |
|---|---|---|---|---|
| `sliding_window` | 51–60 | 100 % | 95–100 % | 1.2 % of the stream |
| `cusum_sprt` | 41 | 100 % | 92–100 % | 1.2 % |
| `kalman` | 38–40 | 100 % | 95–100 % | 1.2 % |
| `bocpd` | 44–46 | 98–100 % | 95–100 % | 1.3 % |
| `maxtree` | 40–42 | 95–100 % | 100 % | 1.3 % |
| `bayesian_blocks` | 36–39 | 100 % | 90–98 % | 1.2 % |

Verdict: **PASS** (all six searches, three seeds). What the suite is for is the
three failures it produced before it passed, none of which an A/B could have
found:

* **The workload has to be the regime the method documents.** A first version
  left 77 % of photons inside a burst; the max-tree recovered 55 % of them and
  Bayesian blocks 57 % whatever it was asked. `BurstSearchMaxTree.h` states the
  assumption — its baseline is a median rate, so its contrast and significance
  filters are background filters only while bursts are a *minority* of the trace,
  and it records the inversion (F1 0.95 at ~24 % occupancy, 0.63 at ~69 %). At
  77 % both methods were measuring bursts against bursts. `simulate()` is now
  dilute and the suite asserts the occupancy so a later edit cannot quietly
  re-create the wrong regime.
* **`q` is not a universal constant.** The Kalman search's process noise is in
  (counts/s)² per bin — how fast the *background* may drift. At q = 1e9 (32 kHz
  per bin against a 50 kHz background) the filter simply follows the burst up and
  reports no innovation: recall 48 %. At q = 1e7 it is 100 %.
* **A per-component σ is not a false-alarm rate.** The max-tree at 3σ with a
  loosened window flagged 22 bursts in pure Poisson background — which is what an
  uncorrected 3σ over ~12 000 positions means (≈16 expected). At its registry
  defaults it flags 3. `max_false_alarm_rate` is the documented control and the
  suite verifies it the only way its own schema says it can be verified, against
  a background-only measurement: 10 → 1 → 0.1 per second gives monotonically
  fewer false alarms at no cost in recall. The absolute rate is optimistic by
  about an order of magnitude (0.1/s over 0.25 s predicts 0.025, delivers 1),
  which is why the test pins the direction and the schema calls the trials
  correction approximate.

A fourth is recorded but not asserted as a defect: `bocpd`'s hazard rate must be
read together with its bin width. At 20 µs bins, `changepoint_prob` 0.2 resolves
the transits and ≤ 0.02 returns the whole measurement as one burst — the
degenerate answer that scores 100 % recall by overlap, which is why
`test_a_detection_is_not_the_whole_measurement` exists as its own test.

### HMM, kinetics, PDA — `test/python/hmm/test_ab_hmm_reference.py`, `kinetics/test_ab_kinetics_reference.py`, `pda/test_ab_pda_reference.py`

| Header | Kernel | Reference | Metric / result | Verdict |
|---|---|---|---|---|
| HMM.h | `evaluate` loglik (total, per burst), `gamma`, `viterbi_path` | H2MM_C 2.2.1 `H2MM_arr`, `viterbi_path` (recorded, 5 cases) | ≤ 1e-9 rel; gamma 3e-8 (float32 engine); paths identical over 10 574 photons | PASS |
| HMM.h | one Baum-Welch step | H2MM_C `EM_H2MM_C(max_iter=1)` | prior/trans/obs ≤ 1e-13 | PASS |
| HMM.h | ICL term | exact NumPy path log-likelihood | 1e-8 (H2MM_C's per-burst "path ll" is a different quantity) | PASS (bounded) |
| HMMVB.h | `digamma`; `fit_vb` fixed point + KL terms | scipy.special; closed-form Dirichlet KL | 1e-11; 2e-4 / 1e-8 | PASS |
| HMMVB.h | `fit_vb` posterior + Beal bound | **hmmlearn 0.3.3 `VariationalCategoricalHMM`** (upstream VB-HMM; recorded fixture, dense dt = 1 streams, K = 2 and 3) | posterior α ≤ 2e-3 rel (1e-4 on the bench); hmmlearn's bound at our posterior = sub-stochastic forward − ΣKL to 2e-10 | PASS |
| HMMVB.h | reported `elbo` | hmmlearn's lower bound | **fixed 2026-08-17**: `elbo` is now the sub-stochastic (Beal) bound at the returned posterior = hmmlearn's to 2e-10; the iteration's value is `elbo_normalised` = `elbo` + K(K−1)/2 nat (pinned at K = 2, 3) | PASS (`TestVariationalBayesAgainstHmmlearn`; brief: okf/design/hmmvb-elbo-decision.md) |
| HMMSurrogate.h | features | NumPy transcription from the feature definitions | exact | PASS |
| HMMBayes.h | `HmmPosterior::rhat`, `ess` | **ArviZ 0.23.4** `rhat(method="split")`, `ess(method="mean")` (recorded synthetic chains: agreeing, offset, single) | R-hat 1e-10; **ESS ignored between-chain disagreement (reported ~N where ArviZ says 38) — replaced by the split-chain Vehtari 2021 estimator 2026-08-17**, now = ArviZ to 1e-9 | PASS (after fix) |
| HMMBayes.h | `HMM::sample` blocked Gibbs | hmmlearn VB posterior (dense fixture) | means within 3 combined sd; sd 1.2–1.5× VB's (mean-field under-dispersion, expected direction); R-hat < 1.05 | PASS (statistical) |
| HMMBayes.h | `gamma_variate`, `dirichlet` (batch bindings `gamma_variates` / `dirichlet_variates` — the scalar forms were uncallable: counter by reference) | scipy.stats gamma / beta marginals, KS on 20 000 | p > 1e-3, shapes 0.4–30 | PASS |
| HMMConstraints/Restraints/Emission.h | constraints, restraints, product alphabet | — | existing known-answer suites | KNOWN-ANSWER |
| CtmcKinetics.h | generator, equilibrium, round trips | NumPy, `scipy.linalg.null_space`, `expm` | 1e-14 / 1e-9 | PASS |
| GopichSzabo.h | `log_likelihood`, `viterbi`, `relaxation_times`, `emission_from_efficiencies` | direct `scipy.linalg.expm` evaluation of GS-2009 eq. 3; NumPy max-product; `eig(Q)` | 1e-9 rel on 4 schemes; 100 % path; 1e-8 | PASS |
| Pda.h | `s1s2` | **PAM `PDA_histogram.cpp` compiled from `../chisurf/junk/PAM`** (mex.h shim); Antonik 2006 NumPy (pre-existing) | ≤ 7e-18; 1e-14 | PASS |
| PdaBurstLikelihood.h | burst likelihood | defining nested sum | pre-existing | PASS |
| Pda3cCore.h | `transfer_matrix_3c` | NumPy competing-acceptor cascade (40 geometries); limits E → I at large R, E = ½ at R = R₀ | 1e-12 | PASS |
| Pda3cCore.h | `gauss_hermite_grid` | **moment exactness** — an n-node rule is exact to degree 2n-1, so the grid for N(µ, Σ) must return µ, Σ and zero third moments (3, 5, 7 nodes; correlated Σ) | 1e-9 | PASS (no reference implementation involved) |
| Pda3cCore.h | `channel_probabilities_3c` | `normalise(excitation @ transfer @ emission)` | 1e-12 | PASS |
| Pda3cCore.h | `species_forward_model` | the composition of the three above, assembled in NumPy | 1e-10 | PASS |
| PdaCallback.h | interface | — | — | not marked |

`dirichlet_kl(a, b)` is bound with two arrays (length mismatch raises) since 2026-08-17 and pinned to the scipy closed form.

### Core, corrections, util, streaming — `test/python/test_ab_core_reference.py`, `corrections/test_ab_corrections_reference.py`, `streaming/test_ab_streaming_equivalence.py`

| Header | Kernel | Reference | Metric / result | Verdict |
|---|---|---|---|---|
| Histogram.h | `histogram1D_double/int` lin, log10; `histogram1D_range_double`; `make_bin_edges_double` | np.histogram (one edge appended), linspace/geomspace | exact away from edges (±1 count on-edge FP) | PASS |
| Histogram.h / HistogramAxis.h | 'search' axis (arbitrary edges) | np.histogram | inner bins exact; **first and last bin were never filled** — fixed 2026-08-17, now all bins equal numpy incl. the closed right edge | PASS (after fix) |
| Histogram.h | `bincount1D` | np.bincount via harness and (bound 2026-08-17) from Python | exact | PASS |
| Histogram.h / HistogramNd.h | 2-D, N-D, all axes/storage/flow | np.histogram2d, boost-histogram | pre-existing, exhaustive | PASS |
| MicrotimeLinearization.h | LUT + shift | NumPy transcription | exact | PASS |
| TTTR.h | `compute_intensity_trace`, `selection_by_count_rate`, `ranges_by_time_window`, count rate, mean micro time, `get_microtime_histogram`, `mean_lifetime` | np.bincount / searchsorted transcriptions; Isenberg 1973 first moment | exact; 1e-9; τ 3.7 ns within 0.05 | PASS |
| TTTRRecordReader.h / io_pq.h / io_bh.h | HydraHarp T3, HT3, SPC-130 decoding | ptufile; phconvert `load_ht3`, `_read_spc1xx_8xx` | macro/micro/channel/markers exact (15.6 M events HT3) | PASS |
| TTTRRecordReader.h | **PicoHarp T3** (every Leica SP8 PTU), TimeHarp 260 PT3, MultiHarp generic T3 | ptufile 2025.5.10 (live) | **PHT3 tested `dtime == 0` for markers and passed channel-15 markers as photons — 0.1 % of photons lost, every marker miscounted; fixed 2026-08-17** (reader + writer; SP8 CLSM fixture regenerated, 3 bins +1). Now photons/markers/bits identical (channel field kept 1-based); TH260/generic identical as they were | PASS (after fix) |
| TTTRRecordReader.h / io_bh.h | SPC-600/630 256-ch, SPC-QC-104, `.sm` | phconvert `_read_spc6xx_32bit`, `_read_QCX04`, `smreader.load_sm` (junk checkout, live) | SPC-QC and .sm identical; SPC-630 channels/ADC identical, macro times identical up to phconvert's 2^12 overflow shift on a 17-bit field (its output is non-monotone). **tttrlib had ignored the SPC-6x0 header frame (macro clock stayed 1.0 s) — read and written per BH `SPC_data_file_structure.h` since 2026-08-17** | PASS (bounded by the reference's defect) |
| io_hdf5.h | Photon-HDF5 read | h5py datasets (`/photon_data/*`) | timestamps, nanotimes, detectors, both units identical (22 M photons) | PASS |
| TTTRRecordReader.h | HT3 v1 sample (`pq_ht3v1.0_hh_t3.ht3`) | phconvert `load_ht3` | channels/micro identical; **the sample is SF-compressed** (counted overflow records, no two in a row at ~100 kHz — a plain v1 stream would have thousands): tttrlib detects it, phconvert reads it as plain v1 and is 2.2× too short. Macro = phconvert + 1024·Σcounts pinned | PASS (reference cannot know the format) |
| Correlator (Kristine `.cor` references in tttr-data) | — | Seidel-lab Kristine per-time-window (TW) normalised MCS correlation | **not comparable**: tttrlib has no TW-normalised estimator (whole-trace G ≈ 1.008 vs Kristine's 17 on a dilute sample); the old Hausdorff test is loose by construction. A TW-normalised mode would be a new feature, not a validation | NO-REF (feature gap) |
| io_fl.h | FLIM LABS STT1 time-tagger decoding | **vendor reader** `spectroscopy_STT1_time_tagger.py` (flim-labs/spectroscopy-py) run on a file our writer produced — no real STT1 sample is published anywhere (flim-labs, VicidominiLab GitHub checked 2026-08-17; only histogram/intensity exports) | every record parsed; photons/markers, macro (laser pulses), micro (256-bin grid) agree after the vendor's own sort | PASS (bounded: our bytes, their reader) |
| io_bh_set.h / io_bh.cpp | BH `.set` sidecar → micro-time width, ADC channels, image geometry | phconvert `load_set` (live) | **SPC-130 ignored the sidecar and every path the TAC gain — 6.1 ps read for a 3.05 ps file; fixed 2026-08-17**; now = `SP_TAC_TC` on both samples | PASS (after fix) |
| PhotonscoreD7.h | `.photons` (D7) read/write | **photonsfile** (public reference decoder, live) on our writer's bytes | datasets and attributes identical, both writers | PASS (bounded: our bytes, their reader; no vendor sample) |
| io_be.h | BrightEyes-TTM `.ttr` decoding | **libttp 0.1.43** (vendor Cython parser, recorded, first 4 M words) | 326 835 photons: channel / macro (16-bit step) / micro (code − laser code) identical; markers = rising edges of the enable bits (A pixel, C line, B frame) | PASS |
| TTTR.h `write` (PTU) | header built from scratch | ptufile | **`Measurement_Mode` was missing, so ptufile could not decode a PTU written from a bare `TTTR()`; added 2026-08-17** (T2/T3 from the record type). PHT3 write→read round trip with markers and dtime-0 photons pinned | PASS (after fix) |
| SpectralCrosstalk.h | `correct_three_cube(_batch)`, `invert_mixing_ridge` | Hellenkamp 2018; FRETBursts `correct_E_gamma_leak_dir`; np.linalg.lstsq, closed-form ridge, sklearn Ridge | 1e-12 / 1e-10 / 1e-8 | PASS |
| BackgroundEstimation.h | `estimate_background_rate` | FRETBursts `expon_fit` tail MLE; true Poisson rate | **tail estimate was biased low by 1/(1−ln f) (0.59× at the default 0.5) — threshold not subtracted; fixed 2026-08-17**, now = tail MLE to 1e-9 at 0.2/1/3 kHz; **returns kHz** as the header promised (code returned Hz; unit fixed 2026-08-17) | PASS (after fix) |
| MaxEnt.h | `maxent_invert` | scipy L-BFGS-B | round 1 | PASS |
| Sha256.h / BitOps.h / ByteOrder.h / string_encoding.h | hashes, bit ops, byte swaps, latin1↔utf8 | hashlib, Python ints, `int.to_bytes`, codecs | exact | PASS |
| StreamingIntensityTrace.h / StreamingDecayHistogram.h / StreamingCLSMImage.h | streaming | numpy bincount at random cuts; batch CLSMImage on real HT3 | exact | EQUIVALENCE |
| io_csv.h / io_csv_writer.h | CSV read (type inference, dictionary text, masks) and write (shortest round-trip doubles) | **pyarrow.csv** (live) | values and inferred types equal; every double round-trips to the same double | PASS |
| TiffArrayIO.h | `imread` / `imwrite` (2-D, stacks, ImageJ hyperstacks; none/lzw/packbits) | **tifffile** (live, both directions) | pixel-identical, same dtype/axes/page order | PASS |
| TTTRRange/Selection/Mask, DataStore | containers/plumbing | — | — | NO-REF, not marked |

### Imaging — `test/python/clsm/test_ab_clsm_reference.py`, `clsm/test_ab_localization_superres_reference.py`

| Header | Kernel | Reference | Metric / result | Verdict |
|---|---|---|---|---|
| CLSMImage.h | frame/line/pixel reconstruction (default HT3 routine; SP5 routine) | independent NumPy from markers | exact, 3.36 M photons / 40 frames; 3.49 M / 230 frames | PASS |
| CLSMImage.h | `get_tttr_indices`, `get_fluorescence_decay`, `get_decay_of_pixels`, `get_mean_micro_time`, `get_mean_lifetime`, `crop`, `rebin` | reference photon set; np.add.at / bincount; m1/m0; moment lifetime formula; slices/block sums | exact / 1e-12 / 1e-10 (`get_fluorescence_decay` is uint8 and saturates at 255) | PASS (bounded) |
| CLSMImage.h | `compute_ics` / `get_fcs_image` | numpy FFT correlation; **pysimfcs `autocorr2d`** (J. Unruh, live); **Kolin & Wiseman `stics.m` / `corrfunc.m`** (MATLAB 2003, run in Octave, recorded) | pre-existing; pysimfcs identical (0.0) after its N·mean²−1 normalisation on even shapes (its `irfft2` drops a column on odd widths); STICS at lags 0–3 (frame-pair average) and the per-frame normalised ACF bit-identical to the MATLAB code | PASS |
| ImageLocalization.h | `fit2DGaussian` (Poisson-MLE, fixed/free ellipticity) | scipy L-BFGS-B on the identical deviance from the same start | same minimum 1e-9 rel; position 1e-2 px | PASS |
| CLSMSuperRes.h | `airy_psf`; `apr_reconstruction`; `temporal_combine` | scipy.special.j1; scipy.ndimage.fourier_shift; numpy | 1e-9 / 1e-9 / 1e-12 | PASS |
| CLSMSuperRes.h | `shift_vectors`, `apr_reconstruction`, `focus_reconstruction`, `s2ism_reconstruction`, FRC | **VicidominiLab code live** from `../chisurf/junk`: BrightEyes-ISM `APR_lib.ShiftVectors` (bit-identical), `APR_lib.APR(mode='fourier')` (1e-9; default `interp` is 7e-4 away, not offered), `FocusISM_lib.focusISM` (corr > 0.98, bg fractions within 0.02), s2ISM `max_likelihood_reconstruction` (torch subprocess, 1e-6; its `max_iter+1` update count and even-size cropping pinned), `FRC_lib` | on the user's direction (2026-08-17): the upstream code, not transcriptions | PASS |
| CLSMSuperRes.h | `rgc_map` (eSRRF), SOFISM, detector grid | NanoJ-eSRRF numpy transcription, SOFISM transcription, BrightEyes lattices | pre-existing | PASS |
| CLSMSuperRes.i | `vectorial_psf` (Richards–Wolf) | **BrightEyes-ISM `PSF_sim.singlePSF` → PyFocus `VectorialCartesianPropagator`** (recorded; NA 1.4 oil, 520 nm, x / y / circular, z = 0 and 400 nm) | 5e-5 of peak in focus (0.1 % relative where I > 1 %), 5e-4 defocused; residual halves with PyFocus's pupil sampling. Two reference conventions undone: `[x, y]` indexing, pixel = fov/(Nx−1). Scalar limit vs `airy_psf` pre-existing | PASS |
| CLSMSuperRes.h | `reassign_photons`, `focus_reconstruction` | — | photon conservation, background fraction | KNOWN-ANSWER |
| CLSMFrame/Line/Pixel.h | containers | — | — | not marked |

### Simulation — `test/python/simulation/test_ab_simulation_reference.py`

| Header | Kernel | Reference | Metric / result | Verdict |
|---|---|---|---|---|
| SimRandom.h | MT19937 stream, `init_by_array`, `random_res53`, uniform mappings, state snapshot | numpy `MT19937._legacy_seeding`; mt19937ar.c test vector; `RandomState.random_sample` | bit-exact | PASS |
| SimXoshiroRandom.h | xoshiro256++ incl. seed hash | Blackman & Vigna reference | bit-exact | PASS |
| SimCounterRandom.h | Philox stream via `Random::seek` | Philox4x32-10 | bit-exact incl. counter 2³³+1 | PASS |
| SimZiggurat.h | `sim_randn` tables + stream | Marsaglia & Tsang 2000 zigset/RNOR on the same MT stream | tables and outputs bit-exact | PASS |
| SimSimd.h | `SimRandomV.normals` | N(0,1) KS | pass | KNOWN-ANSWER |
| SimInjection.h | `qnorm`, `influx_weight`, `random_erfc`, `random_entry_depth` | scipy.stats.norm.sf, closed form + quad, integrated densities | 1e-9 / 1e-12 / KS | PASS |
| SimDecay.h | pattern builders, `convolve`, `pdf`, alias sampling | numpy; chi-square | 1e-15 / 1e-12 | PASS |
| SimKinetics.h | `sim_occupation_fractions`, `sim_state_at_times` | null space of Q; `scipy.linalg.expm` | 0.01; chi-square | KNOWN-ANSWER |
| SimEngine.h | dwell times, equilibrium, MSD = 6Dt, Poisson window counts, background, micro-time histogram, r(0) and Perrin slope | analytic | 5–6 % / 2 % / τ within 0.05 | KNOWN-ANSWER |
| SimEngine.h | r(t) at t ≈ 2ρ | r0·exp(−t/ρ) | **was one Gaussian kick per emission (r(2ρ) ≈ 0.10–0.12 vs 0.054); sub-stepped 2026-08-17**, now within 0.02 | KNOWN-ANSWER (after fix) |
| SimEngine.h | FCS G(τ) | PyBroMo 0.8.1 (recorded, same D/PSF/concentration) + analytic 3-D curve | both recover D and G0·N within 25–30 %; RMS < 15 % | PASS (statistical A/B) |
| SimEngine.h + io/pq writer | HHT3v2 records + PTU header | tttrlib read-back; ptufile 2025.5.10 | identical macro/micro/channel; ptufile decodes every record (**three PTU header defects found and fixed 2026-08-17**: uninitialised `Header_End`, `Header_End` before the mandatory tags, `NumberOfRecords` = events) | PASS (after fix) |
| SimMicrotimeEncoder.h | SPC-132 export | photon/channel counts, micro-time multiset; pre-existing byte identity vs legacy `data2spc_tac` | exact | PASS |
| SimGrid/VectorGrid/Scanner/System/Species/Integrator/ThreadPool.h | config/plumbing, or exercised only through SimEngine | — | — | not marked |

## VicidominiLab kernels — identity and speed, checked (2026-08-17)

The user's direction: the reference is the upstream code, results must be
identical, and tttrlib must be faster. Checked on the benchmark inputs
(`benchmarks/check_vicidomini.py`, `results/shared/vicidomini/check.json`) and
in the permanent tests:

| Kernel | Reference | Identical? | Faster? | ✓ |
|---|---|---|---|:-:|
| `shift_vectors` | BrightEyes-ISM `APR_lib.ShiftVectors` | bit-identical (0.0) | (inside APR) | ✅ |
| `apr_reconstruction` | `APR_lib.APR(mode='fourier')` | identical, 3e-16 | 3.8× (5.0× vs default `interp`) — was 0.5× before the circular-shift + OpenMP change | ✅ |
| `s2ism_reconstruction` | `s2ISM.max_likelihood_reconstruction` | identical, 2e-9 (float32 ref); `max_iter+1` convention pinned | 3.6× | ✅ |
| `blind_irf_estimate` | `birfi` | same model; birfi's Adam fit not converged → compared by IRF correlation (≥ 0.9947 per channel after undoing its n/2 roll) and against the truth (tttrlib ≥ birfi: 0.9959 vs 0.9932) | 3.9× | ✅ (tolerance; ≥ reference accuracy) |
| `focus_reconstruction` | `FocusISM_lib.focusISM` | same background-fraction map to within noise (error vs truth 0.084 vs 0.083) | 291× | ✅ (tolerance; = reference accuracy) |

Not identical by construction, and why: birfi stops Adam after 1000 steps
wherever it is; focusISM registers with a spline and fits per pixel with
`curve_fit`. Both are documented in the tests; neither is something to copy.

## Scientific-Python kernels — identity and speed, checked (2026-08-17)

Same treatment for the kernels whose upstream is scikit-image / scikit-learn /
filterpy / hmmlearn / phasorpy (`benchmarks/check_sciref.py`,
`results/shared/sciref/check.json`):

| Kernel | Reference | Identical? | Faster? | ✓ |
|---|---|---|---|:-:|
| `watershed` | skimage 0.25.2 `watershed` | 0 of 1 048 576 pixels differ — after following upstream's 0.25.1 marker-seed revert (the port had pinned 0.25.0's `-inf`; 13 % of pixels differ between the two) | 1.4× | ✅ |
| `marching_squares` | skimage `_get_contour_segments` | 31 464 segments equal, in order | 3.5× | ✅ |
| `richardson_lucy_2d` | skimage `richardson_lucy` | 3.5e-15 | 1.8× (pocketfft threaded) | ✅ |
| `kmeans` | sklearn `KMeans` | centres 9e-15, labels equal, inertia 2e-15 (from the same k-means++ centres) | 5.9× same job, 2.9× Lloyd-only (parallel assignment, serial sums; prefix + binary search seeding) | ✅ |
| HDBSCAN pipeline | sklearn `HDBSCAN` | same partition, ARI 1.0 | 21× | ✅ |
| `kalman_filter` | filterpy | ≤ 5e-16 | 510× | ✅ |
| HMM lattice | hmmlearn `_hmmc` | log-prob 0, posteriors 4e-16, paths equal | 1.8× | ✅ |
| `max_tree_1d` | skimage `morphology.max_tree` | component set identical (1.9 M components) | 11× | ✅ |
| `fit_vb` (dense stream) | hmmlearn `VariationalCategoricalHMM` | `elbo` = hmmlearn's bound at our posterior to 2e-10; α 1e-4; `elbo_normalised` = `elbo` + K(K−1)/2 | 13× | ✅ |
| `compute_phasor_bincounts_batch` (new) | phasorpy `phasor_from_signal` | 0.0 | 3.4× | ✅ |

## FRET / burst kernels — identity and speed, checked (2026-08-17)

Upstream code itself as reference (`benchmarks/check_fret.py`,
`results/shared/fret/check.json`):

| Kernel | Reference | Identical? | Faster? | ✓ |
|---|---|---|---|:-:|
| `Pda.s1s2` | PAM `PDA_histogram.cpp`, compiled natively | 2e-18 | 4.7× | ✅ |
| `BurstML.neg_log_likelihood` | FRET_burstML MEX `mlhDiffNTRbkg_MT.cpp`, compiled natively (GSL) | ratio 1 ± 3e-13 | 8.3× | ✅ |
| `TwoCDE` (FRET-2CDE, Laplace) | FRETBursts `phrates.kde_laplace` + Tomov formula | 6e-15 | 4.2× | ✅ |
| `fdc_scan_log` | `TK_Create2DFDC_04.m` (Kondo) in Octave | 0 of 389 185 pair counts differ | ~4700× (interpreted double loop) | ✅ |
| `burst_search_cusum_sprt` | PAM `CUSUM_burstsearch` in Octave | behavioural: min Jaccard 0.87, same burst count (PAM's discretisation, α = 1/N, offsets) | ~1400× | ✅ (behavioural) |

## Record decoding — identity and speed, checked (2026-08-17)

`benchmarks/check_reading.py`: PTU (3.5 M photons), HT3 (15.6 M photons + 20 k markers), SPC-130 (184 k photons) decoded by tttrlib vs phconvert 0.10.1 — **identical**, 5.8× / 25× / 4.4× faster; PTU vs ptufile at parity (I/O-bound). ✅ ✅ ✅ Second round: SPC-630 / SPC-QC / `.sm` vs phconvert identical (SPC-630 up to phconvert's overflow-shift defect), 4.0× / 3.6× / 1.5×; PicoHarp T3 vs ptufile identical after the PHT3 fix, 0.55× on a 3 MB file (fixed costs). ✅ ✅ ✅ ✅

## What the A/B found — fixed the same day

* `Random.h` PCG output function was not PCG (round 1; see the math register).
* `estimate_background_rate` tail MLE did not subtract the threshold — 0.59× the true rate at the default `tail_fraction` — and returned Hz where the header said kHz. Now `N / Σ(t_i − t_thr)` in kHz (typical background 0.2–3 kHz).
* Histogram 'search' axis never filled bin 0 or the last bin (`idx > 0` in `bin_of`, `n_bins−2` bound in `search_bin_idx`). Now `[e_i, e_{i+1})` with numpy's closed right edge.
* `Correlator::ccf_laurence` cross-correlation formed `t2 − t1` unsigned; a second stream starting earlier gave all-zero bins. Edges now move onto the partner's axis.
* `DecayConvolution.i`'s `fconv_ref` wrapper dropped `dt` (C++ default 0.05 regardless). Forwarded now, default 1.0 like its siblings.
* `blind_irf_estimate` recovered the IRF position but not its shape — the port's SG derivative abscissa mismatch and centroid-only lifetime, fixed; A/B'd against VicidominiLab's birfi (corr 0.978–0.999 after undoing its n/2 roll).
* Simulator rotational diffusion was a single Gaussian kick per emission (r(t) above r0·e^{−t/ρ} beyond ~ρ); now sub-stepped, r(2ρ) within 0.02 of the exponential.
* PTU writer: the auto `Header_End` tag was uninitialised (ptufile: "invalid tag type"), a caller-supplied `Header_End` was written before the mandatory tags, and `TTResult_NumberOfRecords` counted events not records (strict readers truncated). All three fixed; ptufile now decodes every record.
* `felekyan` correlator was labelled with the wahl lag axis (spacing 2^k) while its blocks count at 2^(k−1) — τ off by up to 25 %. It now has its own contiguous axis; wahl and felekyan agree as functions of τ.

## What the A/B found — open (BUGS.md, tests pin the shape)

* ~~`HMMVB` ELBO data term ≠ its own header derivation~~ — fixed 2026-08-17: `elbo` is Beal's bound (hmmlearn A/B); the old value is `elbo_normalised`.
* `add_pile_up_to_model` uses the inclusive cumulative count (Coates: Σ_{j<i}); 1e-4-level.
* FIDA default 256-bin profile is an unconverged quadrature (N ~6.8× off; shape right).
* Unbound-from-Python surfaces — all bound 2026-08-17 (`DecayPhasor.compute_phasor`, `ProductPrior`, `bincount1D`, `SimRandom.init_by_array`, `dirichlet_kl`, `fconv_cs_time_axis`); `TTTR.set_mt_linearizer` now copies instead of adopting (was a double free).
* Doc/code mismatches — all corrected in the headers 2026-08-17: `get_mean_micro_time` (−1 sentinel, not zeros), `get_phasor` ((−1, −1)), `get_selection_by_count_rate` window in seconds, `TTTRHeader::get_macro_time_resolution` in seconds, `BurstSearchBOCPD.h` plug-in Poisson predictive, `compute_intensity_trace` has no sliding mode, `rescale_w_bg` takes inverse errors, Fit23 `r_scatter`/`r_experimental` legacy naming explained.
* Not references, documented: `skimage.restoration.wiener`, ChiSurf `mem.py`, ebFRET/MASH-FRET (Gaussian-trace VB), PAM PCH (GUI), H2MM_C per-burst path ll (different quantity). PyBroMo squares its PSF: matching tttrlib's exp(−2r²/w0²) needs `sx = w0/√2` — `benchmarks/competitors/bench_pybromo.py` uses `w0/2` (timing-only workload, but not the same physics).

## Keeping this current

A new algorithm anywhere in the library gets a row here, a `// Validation:`
block in its header, and an A/B test in the area's `test_ab_*_reference.py`
before it is called done. If a row's metric stops holding, the test fails —
record what changed and why or fix the kernel; never loosen a tolerance to
make it pass.

## Run record

2026-08-17, all A/B suites on the fixed build: **279 passed, 0 xfailed** (plus the
birfi/ChiSurf/BrightEyes-ISM/s2ISM/phconvert additions since). The
`test_math_ab_probabilistic.py::TestKalmanAgainstTheTextbook` failure first
recorded as a one-off flake showed a second time in a 16-minute run and turned
out to be a real defect: `kalman_filter`'s general `K = P_pred S^-1` branch was
written for dim == 4 and read past its buffers for dim == 1 (undefined
behaviour meeting stale heap only when other shapes had run before). Fixed;
`test_one_channel_is_deterministic_and_dims_beyond_four_work` pins it. Lesson
kept: a "flake" in a deterministic A/B is a defect until proven otherwise.
