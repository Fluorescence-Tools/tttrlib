# tttrlib

[![Anaconda](https://anaconda.org/tpeulen/tttrlib/badges/version.svg)](https://anaconda.org/tpeulen/tttrlib)
[![PyPI](https://badge.fury.io/py/tttrlib.svg)](https://pypi.org/project/tttrlib/)
[![CI](https://github.com/Fluorescence-Tools/tttrlib/actions/workflows/ci.yml/badge.svg)](https://github.com/Fluorescence-Tools/tttrlib/actions/workflows/ci.yml)

> Quick links:
> [Documentation](https://docs.peulen.xyz/tttrlib) |
> [Example gallery](https://docs.peulen.xyz/tttrlib/stable/auto_examples/index.html) |
> [Python package](https://pypi.org/project/tttrlib/) |
> [Conda package](https://anaconda.org/tpeulen/tttrlib) |
> [Paper](https://doi.org/10.1093/bioinformatics/btaf025) |
> [Issues](https://github.com/Fluorescence-Tools/tttrlib/issues)

![tttrlib FLIM][3]

## Project description

**tttrlib** is a **high-performance, cross-platform, cross-language**,
file-format-agnostic library for reading, processing, and writing time-tagged
time-resolved (TTTR) photon data. It reads PicoQuant, Becker & Hickl, and
Photon-HDF5 files through one vendor-independent API.

The core is written in **C++** for speed and exposed through **one shared SWIG
interface** to three languages, all from the same engine:

- **Python** — the primary, most-tested binding; native NumPy arrays, integrates
  with SciPy/Matplotlib/Jupyter.
- **R** — native R vectors; see [docs/r-package.md](docs/r-package.md).
- **Java** — clean 64-bit `long` macro times; ships an
  [ImageJ/Fiji plugin](docs/imagej-plugin.md).

It runs on **Linux, macOS (Intel + Apple silicon), and Windows**, with
prebuilt packages (pip wheels, conda) for all three. Photon-stream operations
stay fast because the hot loops are vectorized C++ (with runtime-dispatched
AVX/NEON kernels and OpenMP), so the same performance is available from every
language.

tttrlib is intended for time-resolved fluorescence spectroscopy and imaging
workflows, including confocal single-molecule analysis, FCS/FCCS correlation,
fluorescence decay analysis, FLIM, CLSM, and image scanning microscopy.

> **Binding maturity:** the Python binding is the most thoroughly tested. The R
> and Java bindings share the same tested C++ core but their language-specific
> layers have lighter test coverage — we are working to mirror the Python tests
> in R and Java (plan:
> [PRDs/PRD-001-cross-language-test-parity.md](PRDs/PRD-001-cross-language-test-parity.md)). Please report
> any binding-specific issues.

## Performance — faster than the GPU, on the CPU

Speed is the point of tttrlib. The hot loops are vectorized C++ with
runtime-dispatched AVX/NEON kernels and OpenMP, so it stays fast on an ordinary
laptop CPU — **and you don't need a GPU to keep up with GPU-accelerated tools;
tttrlib on the CPU is faster than they are on the GPU.** On the same machine,
tttrlib's CPU per-pixel FLIM fitting (~0.40 s, stable) beats FLIMKit running on the
GPU (0.5–1.2 s, run-to-run) — **1.3–2.9× faster** across runs (the per-pixel fit is
a swarm of tiny independent fits with branching, which GPUs handle poorly, so the
GPU brings no benefit there). Every number below is
tttrlib on CPU only, against the common open-source tools on **identical data,
same machine** (reproducible suite in [`benchmarks/`](benchmarks/)):

![tttrlib speedup vs competitors](benchmarks/plots/summary_speedup.png)

### What tttrlib now wins

CPU only, no GPU, identical data.

| Task | tttrlib | Best competitor | Result |
|------|--------:|----------------:|:-------|
| **Single-curve lifetime fit** (one detector) | 0.23 ms | flimlib LMA 2.38 ms | **10×** (36× batched) |
| **H2MM** photon-by-photon HMM (Baum-Welch) | 0.10 s | H2MM_C (C ref) 0.79 s · numba 0.39 s | **7.6× vs C ref** |
| Burst search | 2.8 ms | FRETBursts 13.0 ms | **4.7×** |
| Correlation / FCS (multi-tau) | 0.13 s | pycorrelate 1.71 s (direct) | **13×** |
| Diffusion simulation (coasting) | 0.44 s | PyBroMo 2.23 s | **5.1×** |
| Per-pixel reconvolution MLE (CPU) | 0.40 s | FLIMKit **on GPU** 0.5–1.2 s · CPU 0.9 s | **1.3–2.9× vs GPU** |
| CLSM intensity image | 19 ms | ptufile 22 ms | **1.2×** |
| TTTR file reading | 24 ms | ptufile 26 ms | **1.1×** |
| Fast lifetime (moments) map | 36 ms | flimlib RLD 44 ms | **1.2×** |
| ↳ re-tune IRF on a built map | **0.1 ms** | flimlib RLD 43 ms (recomputes) | **~400×** |

<sub>Apple M1 Pro, CPU only, identical input files. Full methodology, per-task
charts, and honest trade-offs: [`benchmarks/README.md`](benchmarks/README.md).</sub>

Single-detector setups are first-class: `FitNExp` is a native C++ single- or
multi-exponential Poisson reconvolution fitter for one decay curve, with batched
`fit_many` / per-pixel `fit_map` variants that thread across cores. For
intensity-only imaging, `CLSMImage(..., build_pixels=False)` does a single-pass
"virtual fill" that beats dedicated PTU readers while staying byte-identical; the
default builds the full photon-to-pixel structure for downstream lifetime/FCS/PDA
analysis, and the simulator's coasting mode (`per_molecule_skip`) skips molecules
far from the focus.

## Reproducible workflows

The documentation includes executable examples and notebooks so analyses can be
read, modified, and re-run with explicit parameters. Typical workflows start
from a TTTR file, inspect metadata, select photons, and then compute derived
results such as decays, correlations, bursts, or images.

Start here:

- [Getting started](https://docs.peulen.xyz/tttrlib/stable/getting-started.html)
- [Example gallery](https://docs.peulen.xyz/tttrlib/stable/auto_examples/index.html)
- [User guide](https://docs.peulen.xyz/tttrlib/stable/user_guide.html)
- [Developer build notes](BUILDING.md)

## Technical features

- Fast TTTR file reading, typically limited by I/O throughput.
- Unified access to macro times, micro times, routing channels, event types,
  and file metadata.
- Multi-dimensional histogramming for photon data.
- Autocorrelation and cross-correlation analysis for FCS/FCCS.
- Fluorescence decay generation, convolution, phasor analysis, and fitting.
- Photon distribution analysis, including FIDA/PCH and PDA-related workflows.
- Burst and time-window selection for single-molecule experiments.
- CLSM, FLIM, and image scanning microscopy image generation.
- Experimental ISM tools, including adaptive pixel reassignment and Focus-ISM
  background rejection.

On representative workloads, tttrlib runs about 5x faster than FRETBursts for
burst selection, 9-36x faster than flimlib for reconvolution lifetime fitting,
and produces per-pixel FLIM lifetime maps faster than FLIMKit and flimlib — all
on CPU. See the [benchmark suite](benchmarks/) for the full, reproducible
comparison and the cases where other tools win.

## Installation

### pip

```bash
pip install tttrlib
```

Pre-built wheels are available on [PyPI](https://pypi.org/project/tttrlib/) for
Linux x86_64, macOS arm64/x86_64, and Windows x86_64 across supported Python
versions.

### Conda / Mamba

macOS and Linux users can install from Bioconda:

```bash
mamba install -c conda-forge -c bioconda tttrlib
```

Windows users can install from the `tpeulen` channel:

```bash
mamba install -c tpeulen tttrlib
```

We recommend [Miniforge](https://github.com/conda-forge/miniforge) with the
`mamba` solver for new scientific Python environments.

### From source

```bash
git clone https://github.com/fluorescence-tools/tttrlib.git
cd tttrlib
pip install -e .
```

### R

```bash
mamba install -c conda-forge -c tpeulen r-tttrlib
```

Native R vectors, S4 API. Linux/macOS. Full instructions, usage, and
from-source build: **[docs/r-package.md](docs/r-package.md)**.

### Java / ImageJ

The Java binding ships as an ImageJ/Fiji plugin — a single cross-platform JAR
you drop into `plugins/`. See the [ImageJ / Fiji plugin](#imagej--fiji-plugin)
section below and **[docs/imagej-plugin.md](docs/imagej-plugin.md)**.

Legacy 32-bit platforms and Python 2.7 are not supported.

## Minimal examples

### Read TTTR data

```python
import tttrlib

data = tttrlib.TTTR("photon_stream.ptu")

macro = data.macro_times
micro = data.micro_times
routing = data.routing_channels
```

### Inspect metadata

```python
import tttrlib

data = tttrlib.TTTR("photon_stream.ptu")
print(data.header.json)
print(data.header.to_csv())
```

### Cross-correlate photon streams

```python
import tttrlib

data = tttrlib.TTTR("photon_stream.ptu")
correlator = tttrlib.Correlator(channels=([1], [2]), tttr=data)

taus = correlator.x_axis
correlation_amplitude = correlator.correlation
```

### Create an intensity image from CLSM data

```python
import tttrlib

data = tttrlib.TTTR("image.ptu")
clsm = tttrlib.CLSMImage(data)

channels = [0, 1]
prompt_range = [0, 16000]
clsm.fill(channels=channels, micro_time_ranges=[prompt_range])

intensity_image = clsm.intensity
```

### Run a minimal burst search

The array-based APIs speak NumPy directly — burst boundaries come back as a
flat `[start, stop, start, stop, ...]` array and feed straight into the
burst-consuming analyses (BVA, H2MM) without any list conversion:

```python
import numpy as np
import tttrlib

tttr = tttrlib.TTTR("photon_stream.ptu")

L, m, T = 30, 10, 1e-3  # min photons, window photons, window time [s]
ranges = np.asarray(tttr.burst_search(L=L, m=m, T=T))
bursts = ranges.reshape(-1, 2)  # one [start, stop) row per burst

# Burst variance analysis on the same boundaries (NumPy array in/out)
bva = tttrlib.BVA(tttr)
bva.set_donor([0]); bva.set_acceptor([1])
bva.compute(ranges, 5)  # 5 photons per slice
pr_mean = bva.proximity_ratio_mean  # NumPy arrays
pr_std = bva.proximity_ratio_std
```

For PIE/ALEX data, add channel and micro-time gating before burst search. See
the single-molecule examples for donor/acceptor prompt and delayed selections.

## ImageJ / Fiji plugin

An ImageJ/Fiji plugin (`Plugins > tttrlib > Open TTTR CLSM Image`) opens
PTU/HT3/SPC confocal files and reconstructs Intensity, FastLifetime, Phasor,
Number & Brightness, and Decay outputs via the tttrlib engine.

Channel groups use syntax such as `1,3;2,4`, where each semicolon-delimited
group becomes one composite channel. PIE and micro-time ranges use syntax such
as `0,111;200,499;900,1200`, where each `start,stop` pair gates photons by
micro time.

The `Plugins > tttrlib > Decay from Mask` command computes the decay of a
selected ROI as a multi-column table, one column per routing channel.

**Install:** download the single cross-platform JAR `tttrlib_imagej-<version>.jar`
from the [Releases page](https://github.com/fluorescence-tools/tttrlib/releases)
(or the `tttrlib-imagej-plugin` artifact from a recent
[CI run](https://github.com/fluorescence-tools/tttrlib/actions)), drop it into
your `Fiji.app/plugins/` (or `ImageJ/plugins/`) folder, and restart. The JAR
bundles the native libraries for Linux, macOS (Intel + Apple silicon) and
Windows, so no extra setup is needed. Full instructions and usage:
[`docs/imagej-plugin.md`](docs/imagej-plugin.md).

## Supported file formats

- PicoQuant: PicoHarp, TimeHarp, HydraHarp (`ptu`, `ht3`, T2/T3)
- Becker & Hickl: `spc132`, `spc630` in 256 and 4096 mode
- Photon-HDF5: open photon-data format

## Feedback and contributions

tttrlib is open source and developed on GitHub. Please open an issue for bug
reports, format-support requests, documentation gaps, or questions about a
workflow.

For a new file format or microscope, include:

1. A short description of the format and instrument.
2. A small demo file when possible, preferably under 100 MB.
3. Expected metadata, photon counts, image dimensions, or analysis results.
4. Any processing steps needed to reproduce the result.

Small documentation fixes are welcome. Larger code contributions should start
with an issue so the format, tests, and expected behavior are clear.

## Design goals

- Low memory footprint for large TTTR and FLIM datasets.
- Cross-platform C/C++ library with bindings for Python and other languages.
- Modular analysis components that can be reused in custom workflows.
- Reproducible examples and notebooks for scientific analysis.

## Citation

If you use this software, please cite:

> Thomas-Otavio Peulen, Katherina Hemmen, Annemarie Greife, Benjamin M. Webb,
> Suren Felekyan, Andrej Sali, Claus A. M. Seidel, Hugo Sanabria,
> Katrin G. Heinze.
> "tttrlib: modular software for integrating fluorescence spectroscopy, imaging,
> and molecular modeling."
> Bioinformatics 41 (2): btaf025 (2025).
> https://doi.org/10.1093/bioinformatics/btaf025

## License

Copyright 2007-2026 tttrlib developers.
Licensed under the BSD-3-Clause license.

[3]: https://github.com/Fluorescence-Tools/tttrlib/blob/main/doc/logos/mashup.png?raw=true "tttrlib FLIM"
