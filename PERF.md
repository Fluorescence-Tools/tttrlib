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
  pycorrelate 0.3, H2MM_C 1.0.
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
| **Single-curve lifetime fit** (one detector) | **0.25 ms** | flimlib LMA 2.34 ms | **9.4×** |
| ↳ batched (`fit_many`, per fit) | **0.07 ms** | flimlib LMA batch 2.29 ms | **33×** |
| **H2MM** photon-by-photon HMM (Baum–Welch) | **116 ms** (SQUAREM) · 403 ms (plain EM) | H2MM_C 914 ms · chisurf-numba 491 ms | **7.9× vs C ref** (2.3× plain) |
| Correlation / FCS (multi-tau) | **140 ms** | pycorrelate 1801 ms (direct) | **12.9×** |
| Diffusion simulation (coasting) | **0.51 s** | PyBroMo 2.40 s | **4.7×** |
| Burst search | **2.6 ms** | FRETBursts 13.8 ms | **5.3×** |
| Per-pixel reconvolution MLE (CPU) | **456 ms** | FLIMKit **GPU** 568 ms · CPU 739 ms · flimlib LMA 210 s | **1.25× vs GPU**, 1.6× vs its CPU |
| **Single-molecule localization** (2D Gaussian PSF) | **0.19 ms** (1 emitter) · 0.62 ms (3) | scipy `least_squares` 1.23 ms · 2.39 ms | **6.5×** · **3.9×** |
| Fast lifetime (moments) map | **11.7 ms** | flimlib RLD 43.5 ms | **3.7×** |
| ↳ re-tune IRF on an already-built map | **0.11 ms** | flimlib RLD 43.5 ms (recomputes) | **~400×** |
| CLSM intensity image | **14.8 ms** | ptufile 22.8 ms | **1.5×** |
| TTTR file reading | **23.2 ms** | ptufile 25.8 ms | **1.11×** |

Run 2026-07-20. The fast-lifetime map improved 36.0 ms → 11.7 ms (1.2× → 3.7×)
since the previous run, from `442c72d4` (deferred pixel allocation and cached
lifetime/phasor moments); every other figure moved only by run-to-run noise.

Datasets: 3.5 M-photon HydraHarp T3 PTU (reading, burst search, correlation),
512×512 confocal PTU (CLSM intensity), 256×256 FLIM HT3 (lifetime maps),
synthetic 256-bin decay (curve fits), simulated 3-state 200 k-photon trace
(H2MM), 20 molecules / 1 s with matched D, box and PSF (simulation).

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

**`DecayFit23/24/25/26` and `FitNExp` still use central differences.** The
gradient hook defaults to null, so the curve-fit and per-pixel-MLE rows above are
unaffected by this work; converting them is gated on measuring AD against the
SIMD convolution path they depend on, which cannot be templated.

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

### You do not need a GPU

FLIMKit ships a GPU backend (MLX / CUDA / MPS / ROCm); on this machine its GPU
path runs on the M1 Pro GPU via MLX. For per-pixel reconvolution FLIM fitting,
tttrlib's **CPU** `fit_map` (456 ms) beats it (568 ms), and the GPU is only
modestly ahead of FLIMKit's own CPU path (739 ms). Per-pixel fitting is a swarm
of tiny independent fits with branching, which GPUs handle poorly, so the GPU
brings little benefit in this regime.

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
.venvs/flimlib/bin/python      competitors/bench_flimlib.py
.venvs/read/bin/python         competitors/bench_ptufile.py
.venvs/fretbursts/bin/python   competitors/bench_fretbursts.py
.venvs/pybromo/bin/python      competitors/bench_pybromo.py
.venvs/flimkit/bin/python      competitors/bench_flimkit.py
.venvs/h2mm_c/bin/python       competitors/bench_h2mm_c.py
.venvs/h2mm_numba/bin/python   competitors/bench_h2mm_numba.py
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
