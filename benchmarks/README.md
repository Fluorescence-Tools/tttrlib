# tttrlib benchmarks

Reproducible, apples-to-apples speed comparisons of **tttrlib** against the
common open-source FLIM / single-molecule / TCSPC packages — **on CPU, no GPU**.

Every competitor runs in its own isolated environment, but each one fits, reads,
searches or simulates the **identical input** that tttrlib does (the shared
inputs are written to `results/shared/` by the tttrlib harness and loaded by
every competitor), so the wall-clock numbers are directly comparable.

> **Results live in [`../PERF.md`](../PERF.md)** — the comparison tables,
> cross-version time/memory tracking, caveats and reference environment. This
> file documents the *harness*: what is compared against what, and how to run,
> extend or re-generate it. Keep measured numbers in `PERF.md` only, so there is
> one place to update after a re-run.

![tttrlib speedup summary](plots/summary_speedup.png)

## What is compared

| Task | tttrlib | Competitor(s) | Data |
|------|---------|---------------|------|
| TTTR file reading | `tttrlib.TTTR` | [ptufile](https://pypi.org/project/ptufile/) (Gohlke) | HydraHarp T3 PTU, 3.5 M photons |
| CLSM intensity image | `CLSMImage` | ptufile `decode_image` | 512×512 confocal PTU |
| Fast lifetime map | `CLSMImage.get_mean_lifetime` (moments) | [flimlib](https://pypi.org/project/flimlib/) RLD & phasor | 256×256 FLIM HT3 |
| Per-pixel reconvolution MLE | `FitNExp.fit_map` (CPU) | flimlib LMA, [FLIMKit](https://github.com/alex1075/FLIMKit) per-pixel **CPU and GPU** (MLX) | 256×256 FLIM HT3 |
| Single-curve lifetime fit | `FitNExp` (single detector) | flimlib LMA, FLIMKit summed (scipy DE) | synthetic 256-bin decay |
| Burst search | `TTTR.burst_search` | [FRETBursts](https://github.com/OpenSMFS/FRETBursts) | 3.5 M-photon confocal PTU |
| Correlation / FCS | `Correlator` (multi-tau) | [pycorrelate](https://github.com/OpenSMFS/pycorrelate) (direct) | 3.5 M-photon confocal PTU |
| Diffusion simulation | `SimEngine` | [PyBroMo](https://github.com/OpenSMFS/PyBroMo) | 20 molecules, 1 s, matched D/box/PSF |
| Single-molecule localization | `localization.fit2DGaussian` (Poisson MLE, AD gradients) | scipy `least_squares` | synthetic 15×15 PSF patch, 1–3 emitters |
| H2MM (photon-by-photon HMM) | `H2MM.optimize` (SQUAREM) | [H2MM_C](https://github.com/harripd/H2MMpythonlib) (Harris/Pirchi C ref), chisurf numba engine | simulated 3-state, 200k photons |
| Blind IRF (BIRFI) | `blind_irf_estimate` | [birfi](https://github.com/VicidominiLab/birfi) (torch, CPU) | 25 channels × 1024 bins, 500 RL iterations |
| ISM pixel reassignment (APR) | `CLSMSuperRes.apr_reconstruction` | [BrightEyes-ISM](https://github.com/VicidominiLab/BrightEyes-ISM) `APR_lib.APR` (`fourier` and default `interp`) | 25 elements × 256×256 — **identical output** |
| Focus-ISM | `CLSMSuperRes.focus_reconstruction` | BrightEyes-ISM `FocusISM_lib.focusISM` | 25 elements × 64×64 |
| s2ISM | `CLSMSuperRes.s2ism_reconstruction` | [s2ISM](https://github.com/VicidominiLab/s2ISM) (torch, CPU) | 25 elements × 3 planes × 129×129, 30 iterations — **identical output** |
| Watershed / marching squares | `watershed`, `marching_squares` | scikit-image | 1024×1024 — **identical** |
| Richardson–Lucy | `richardson_lucy_2d` | scikit-image | 512×512, 15×15 PSF, 30 iterations — **identical** |
| k-means | `kmeans` (k-means++ + Lloyd) | scikit-learn `KMeans` (same job, and Lloyd-only from the same centres) | n=200k, d=8, k=10 — **identical** |
| HDBSCAN | `core_distances` + `mutual_reachability_mst` + `hdbscan_condensed_tree` + `hdbscan_label_points` | scikit-learn `HDBSCAN` | n=20k, d=4 — **identical partition** |
| Kalman filter | `kalman_filter` | filterpy | 50k steps × 2 channels — **identical** |
| HMM lattice | `hmm_forward_log` / posteriors / Viterbi | hmmlearn `_hmmc` | T=200k, K=4 — **identical** |
| Phasor | `DecayPhasor.compute_phasor_bincounts_batch` | phasorpy | 100k decays × 256 bins — **identical** |
| File reading (PTU / HT3 / SPC-130) | `TTTR(...)` | [phconvert](https://github.com/Photon-HDF5/phconvert) readers (and ptufile for PTU) | real files, 0.2–15.6 M photons — **photon-for-photon identical** |
| PDA histogram | `Pda.s1s2` | PAM `PDA_histogram.cpp` (Schrimpf 2018), compiled natively | nmax 180, 3 species — **identical** |
| BurstML likelihood | `BurstML.neg_log_likelihood` | the original FRET_burstML MEX (Hoffmann et al.), compiled natively with GSL | 187 bursts × 20 parameter sets — **identical** |
| FRET-2CDE | `TwoCDE` | FRETBursts `phrates.kde_laplace` + Tomov's formula | 200 bursts × 120 photons — **identical** |
| 2D-FDC | `fdc_scan_log` | Toru Kondo's `TK_Create2DFDC_04.m` in Octave | 4000 photons × 3 lags — **identical** |
| CUSUM burst search | `TTTR.burst_search_cusum_sprt` | PAM `CUSUM_burstsearch` in Octave | 3.3k photons — behavioural (Jaccard ≥ 0.85) |


## Layout

| directory | what it holds |
|---|---|
| `competitors/` | one script per competing package, each run in its own uv venv (`.venvs/`, built by `build_envs.sh`) |
| `results/` | the raw records, JSONL, one line per measurement (`results/shared/` holds the inputs every side reads) |
| `plots/` | the figures `make_plots.py` regenerates from `results/` |
| `hist/` | the histogram micro-benchmarks (C++), built separately |
| `logs/` | stdout of the long competitor runs, kept so a number can be traced back to the run that produced it |

## Results

Measured numbers — the comparison table, cross-version time/memory tracking, the
fast-path notes (`build_pixels=False`, coasting integrator) and the caveats —
live in **[`../PERF.md`](../PERF.md)**.

Per-task charts: [`plots/`](plots/). Raw per-run records: `results/*.jsonl`
(one JSON object per run: `tool`, `task`, `dataset`, `best_s`, `times_s`,
`throughput`, `extra`). After a re-run, regenerate the plots and update the
tables in `PERF.md` from the new records.

## Reproducing

```bash
cd benchmarks
./build_envs.sh                       # one uv venv per competitor (base env untouched)
python bench_tttrlib.py               # tttrlib side + writes results/shared/ inputs
python bench_h2mm.py                   # tttrlib H2MM (plain EM + SQUAREM + Viterbi)
python bench_localization.py          # 2D Gaussian PSF fit vs scipy
.venvs/flimlib/bin/python     competitors/bench_flimlib.py
.venvs/read/bin/python        competitors/bench_ptufile.py
.venvs/read/bin/python competitors/bench_phconvert.py   # phconvert PTU/HT3/SPC-130 readers
python check_reading.py                            # photon-for-photon identity
.venvs/fretbursts/bin/python  competitors/bench_fretbursts.py
.venvs/pybromo/bin/python     competitors/bench_pybromo.py
.venvs/flimkit/bin/python     competitors/bench_flimkit.py
.venvs/h2mm_c/bin/python       competitors/bench_h2mm_c.py
.venvs/h2mm_numba/bin/python   competitors/bench_h2mm_numba.py
python bench_vicidomini.py            # blind IRF / APR / focus-ISM / s2ISM (tttrlib side)
KMP_DUPLICATE_LIB_OK=TRUE .venvs/vicidomini/bin/python competitors/bench_vicidomini.py
python check_vicidomini.py            # are the VicidominiLab pairs computing the same thing? (writes check.json)
python bench_sciref.py                # watershed / marching squares / RL / k-means / HDBSCAN / Kalman / HMM lattice / phasor (tttrlib side)
.venvs/sciref/bin/python competitors/bench_sciref.py
python check_sciref.py                # output identity of those pairs (writes check.json)
python bench_fret.py                  # PDA / BurstML / FRET-2CDE / 2D-FDC / CUSUM (tttrlib side)
python competitors/bench_fret.py      # PAM + FRET_burstML compiled natively, FRETBursts venv, Octave (base env)
python check_fret.py                  # output identity of those pairs (writes check.json)
python make_plots.py                  # -> plots/*.png
```

`check_vicidomini.py` exists because "faster" is only meaningful next to "the
same answer": it recomputes tttrlib's outputs on the benchmark inputs and
compares them with the reference outputs the competitor script saves — APR and
s2ISM identical, blind IRF and focus-ISM at tolerance with the accuracy against
the simulated truth reported for both sides (see PERF.md).

### The C++ kernel benchmarks

These are not part of the competitor suite: they need no venv, no test data and
no Python, and they compare a tttrlib kernel against the third-party library it
replaced or against another tttrlib kernel. Build them by hand from the
repository root; each file's header comment carries its own command line.

| file | question |
|---|---|
| `bench_mat.cpp` | `Mat.h` vs Eigen on the GEMM shapes tttrlib actually uses |
| `bench_linalg.cpp` | the shared solvers, with a tracked baseline (`--check`) |
| `bench_gradvec.cpp` | `GradVec<N>` vs `Eigen::Array<double,N,1>` as the AD derivative carrier |
| `bench_ad_gradients.cpp` | AD vs central differences, per parameter count |
| `bench_ad_vectorized.cpp` | the same, with central differences keeping the SIMD kernel |
| `bench_sim_propagation_simd.cpp` | the simulator's propagation step |

`bench_mat.cpp` and `bench_gradvec.cpp` are the only two files in the repository
that include Eigen, and they do it to measure against it — Eigen is not a
dependency of tttrlib. `bench_gradvec.cpp` builds and runs without it
(drop `-DHAVE_EIGEN`), reporting the GradVec column alone.

**If you are timing a kernel, do not use wall clock.** `bench_gradvec.cpp` uses
`CLOCK_THREAD_CPUTIME_ID`, interleaves the two implementations so they see the
same load, and takes the minimum over nine trials. The first version used
`steady_clock` and reported speedups between 0.22× and 4.77× for the same binary
on consecutive runs, because the machine was loaded and wall clock keeps counting
while the thread is descheduled. Copy that pattern rather than rediscovering it.

## Cross-version tracking (perf + peak memory)

`bench_versions.py` measures the same workloads across tttrlib releases, recording
wall time **and** peak resident memory so regressions/improvements show up over
time. Peak memory is process RSS (`getrusage`), not `tracemalloc`, because the
wins live in the C++ heap; each task runs in its own subprocess so the high-water
mark isolates. The working-tree build runs in the base env; each released version
runs in its own `uv` venv.

```bash
cd benchmarks
python bench_versions.py --versions 0.26.2 0.27.0=local   # LABEL=local -> base env
python make_version_plots.py          # -> plots/versions/*.png + summary.md
```

Result rows gain `version`, `peak_rss_mb`, `rss_baseline_mb` and `status` fields
(legacy timing-only rows leave them null). The memory column in the summary is the
**task footprint** = peak RSS − post-import baseline, which cancels the difference
between an old version's isolated venv and the base env.

`make_version_plots.py` writes `plots/versions/summary.md`; copy its table into
[`../PERF.md`](../PERF.md) after a run.

## Reference environment

Recorded in [`../PERF.md`](../PERF.md) alongside the numbers it produced, since
the hardware and package versions are part of the result. Numbers are
hardware-specific; re-run on your machine for local figures. The point is the
*ratios*, which are stable across machines.
