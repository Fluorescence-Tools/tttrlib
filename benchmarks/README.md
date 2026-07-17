# tttrlib benchmarks

Reproducible, apples-to-apples speed comparisons of **tttrlib** against the
common open-source FLIM / single-molecule / TCSPC packages — **on CPU, no GPU**.

Every competitor runs in its own isolated environment, but each one fits, reads,
searches or simulates the **identical input** that tttrlib does (the shared
inputs are written to `results/shared/` by the tttrlib harness and loaded by
every competitor), so the wall-clock numbers are directly comparable.

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
| H2MM (photon-by-photon HMM) | `H2MM` | [H2MM_C](https://github.com/harripd/H2MMpythonlib) (reference C), ChiSurf numba | simulated 3-state, 200k photons |
| Diffusion simulation | `SimEngine` | [PyBroMo](https://github.com/OpenSMFS/PyBroMo) | 20 molecules, 1 s, matched D/box/PSF |
| H2MM (photon-by-photon HMM) | `H2MM.optimize` (SQUAREM) | [H2MM_C](https://github.com/harripd/H2MMpythonlib) (Harris/Pirchi C ref), chisurf numba engine | simulated 3-state, 200k photons |

## What tttrlib now wins

CPU only, no GPU, identical data. Numbers below are best-of-N wall time on the
reference machine (lower time / higher throughput is better).

| Task | tttrlib | Best competitor | Result |
|------|--------:|----------------:|:------------------|
| Single-curve lifetime fit (one detector) | **0.25 ms** | flimlib LMA 2.30 ms | **9.3×** (36× batched) |
| H2MM Baum-Welch (3-state, 200k photons) | **104 ms** (SQUAREM) · 331 ms (plain EM) | H2MM_C 793 ms · numba 389 ms | **7.6×** vs C ref (2.4× plain) |
| Burst search | **2.7 ms** | FRETBursts 14.2 ms | **5.3×** |
| Diffusion simulation (coasting) | **0.50 s** | PyBroMo 2.19 s | **4.4×** |
| Per-pixel reconvolution MLE (CPU) | **0.40 s** | FLIMKit **GPU** 1.15 s · CPU 1.0 s · flimlib 203 s | **2.9× vs GPU** |
| CLSM intensity image | **19 ms** | ptufile 22 ms | **1.2×** |
| TTTR file reading | **24 ms** | ptufile 26 ms | **1.1×** |
| Fast lifetime (moments) map | **34 ms** | flimlib RLD 43 ms | **1.3×** |
| ↳ re-tune IRF / background on a built map | **0.1 ms** | flimlib RLD 43 ms | **~400×** |

tttrlib is at or ahead of the best specialized tool in every category. Per-task
charts: [`plots/`](plots/). Raw per-run records: `results/*.jsonl`.

Two of these wins come from optional fast paths:

- **CLSM intensity** uses `CLSMImage(..., build_pixels=False)` — a single-pass
  "virtual fill" that skips the eager allocation of per-pixel photon-index
  containers and scatter-adds accepted photons straight into the image. It is
  **byte-identical** to the classic `fill()` output (verified on PTU and HT3,
  single- and multi-frame). Per-pixel containers are then materialized *lazily* on
  the first `fill()`, so `build_pixels=False` stays correct for the full FLIM
  workflow (fill → mean-lifetime / decay / phasor all byte-identical). The default
  `build_pixels=True` keeps the classic eager behaviour.
- **Simulation** uses the coasting integrator (`SimIntegrator.per_molecule_skip`),
  which stops stepping molecules far outside the detection volume. Same photon
  statistics, ~3× less wall time than the fixed-dt baseline.

## Caveats and honest notes

- **Fast reading is a close race** (~1.05×) — I/O- and memory-bandwidth-bound, so
  no tool pulls far ahead. tttrlib is ahead, but the margin is small and can flip
  on other hardware. The moments lifetime map went from a tie to ~1.3× after the
  per-frame moment build was parallelized (integer accumulators, per-frame sums
  reduced to the stacked image).
- **Derived FLIM maps share one photon pass and cache their moments.** The first
  mean-lifetime / mean-micro-time / phasor map costs one pass; re-tuning the IRF,
  background or resolution afterwards is an O(pixels) correction (~0.1 ms, ~400×
  vs a tool that recomputes). Real interactive FLIM builds once and retunes many
  times, so this is the dominant cost in practice.
- **You don't need a GPU — CPU tttrlib beats the GPU option.** FLIMKit ships a GPU
  backend (MLX / CUDA / MPS / ROCm). On this machine its GPU path runs on the Apple
  M1 Pro GPU via MLX. For per-pixel reconvolution FLIM fitting, tttrlib's CPU
  `fit_map` (~400 ms, stable) is **1.3-2.9x faster than FLIMKit on the GPU**
  (0.5-1.2 s, which varies run-to-run) — the GPU is no faster than FLIMKit's own CPU
  path (~0.9 s). Per-pixel fitting is a
  swarm of tiny independent fits with branching, which GPUs handle poorly, so the
  GPU brings no benefit here. Every tttrlib number in this suite is CPU-only.
  (Caveat: this is a laptop integrated GPU and the discrete-exponential per-pixel
  model; a dedicated datacenter GPU running a batched continuous-distribution fit
  is a different regime we don't test.)
- **Correlation is multi-tau vs direct.** tttrlib's `Correlator` uses the
  standard multi-tau algorithm (logarithmic photon coarsening); pycorrelate does an
  exact direct per-bin correlation. Both yield the same FCS curve (verified: both
  peak at G=2.28 over the same lag range), but multi-tau is algorithmically cheaper,
  which is most of the 13x.
- **Use the batch fit API.** A `FitNExp` single-curve call is ~4× slower per fit
  than the multithreaded `fit_many` / `fit_map` path; for many curves or an image,
  batch them.
- **Numbers are hardware-specific** (Apple M1 Pro). Re-run on your machine; the
  stable quantity is the ratio.

## Reproducing

```bash
cd benchmarks
./build_envs.sh                       # one uv venv per competitor (base env untouched)
python bench_tttrlib.py               # tttrlib side + writes results/shared/ inputs
python bench_h2mm.py                   # tttrlib H2MM (plain EM + SQUAREM + Viterbi)
.venvs/flimlib/bin/python     competitors/bench_flimlib.py
.venvs/read/bin/python        competitors/bench_ptufile.py
.venvs/fretbursts/bin/python  competitors/bench_fretbursts.py
.venvs/pybromo/bin/python     competitors/bench_pybromo.py
.venvs/flimkit/bin/python     competitors/bench_flimkit.py
.venvs/h2mm_c/bin/python       competitors/bench_h2mm_c.py
.venvs/h2mm_numba/bin/python   competitors/bench_h2mm_numba.py
python make_plots.py                  # -> plots/*.png
```

## Reference environment

- **Apple M1 Pro** (6 performance + 2 efficiency cores), 16 GB, macOS 26.5.
- Python 3.10 (base) / 3.12 (FLIMKit); **tttrlib 0.27.0**, flimlib 2.2.5,
  ptufile 2025.5.10, FRETBursts 0.8.3, PyBroMo 0.8.1, FLIMKit git-main.
- tttrlib built with its runtime-dispatched NEON kernels + OpenMP (no GPU).

Numbers are hardware-specific; re-run on your machine for local figures. The
point is the *ratios*, which are stable across machines.
