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
.venvs/fretbursts/bin/python  competitors/bench_fretbursts.py
.venvs/pybromo/bin/python     competitors/bench_pybromo.py
.venvs/flimkit/bin/python     competitors/bench_flimkit.py
.venvs/h2mm_c/bin/python       competitors/bench_h2mm_c.py
.venvs/h2mm_numba/bin/python   competitors/bench_h2mm_numba.py
python make_plots.py                  # -> plots/*.png
```

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
