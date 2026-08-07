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
interface** to four languages, all from the same engine:

- **Python** — the primary, most-tested binding; native NumPy arrays, integrates
  with SciPy/Matplotlib/Jupyter.
- **R** — native R vectors; see [doc/r-package.rst](doc/r-package.rst).
- **Java** — clean 64-bit `long` macro times; ships an
  [ImageJ/Fiji plugin](doc/imagej-plugin.rst).
- **JavaScript (Node.js)** — a Node-API addon covering the same surface as
  Python; TypedArrays, with 64-bit values as `BigInt` so macro times stay exact.
  See [doc/javascript-package.rst](doc/javascript-package.rst).

It runs on **Linux, macOS (Intel + Apple silicon), and Windows**, with
prebuilt packages (pip wheels, conda) for all three. Photon-stream operations
stay fast because the hot loops are vectorized C++ (with runtime-dispatched
AVX/NEON kernels and OpenMP), so the same performance is available from every
language.

tttrlib is intended for time-resolved fluorescence spectroscopy and imaging
workflows, including confocal single-molecule analysis, FCS/FCCS correlation,
fluorescence decay analysis, FLIM, CLSM, and image scanning microscopy.

> **Binding maturity:** the Python binding is the most thoroughly tested.
> JavaScript wraps the same full surface and is verified against the same
> cross-language reference values. The R and Java bindings share the same tested
> C++ core but wrap a smaller slice of the API and their language-specific layers
> have lighter test coverage — we are working to mirror the Python tests
> everywhere, through the shared conformance suite in
> [test/conformance](test/conformance). Please report any binding-specific
> issues.

## Performance — faster than the GPU, on the CPU

Speed is the point of tttrlib. The hot loops are vectorized C++ with
runtime-dispatched AVX/NEON kernels and OpenMP, so it stays fast on an ordinary
laptop CPU — **and you don't need a GPU to keep up with GPU-accelerated tools.**
On the same machine, tttrlib's CPU per-pixel FLIM fitting (0.40 s) beats FLIMKit
running on the GPU (0.54 s): the per-pixel fit is a swarm of tiny independent
fits with branching, which GPUs handle poorly. Highlights, CPU only, against the
common open-source tools on identical data:

![tttrlib speedup vs competitors](benchmarks/plots/summary_speedup.png)

| Task | tttrlib | Best competitor | Result |
|------|--------:|----------------:|:-------|
| **Single-curve lifetime fit** (one detector) | 0.23 ms | flimlib LMA 2.38 ms | **10×** (39× batched) |
| **H2MM** photon-by-photon HMM (Baum-Welch) | 0.10 s | H2MM_C (C ref) 0.79 s · numba 0.39 s | **7.6× vs C ref** |
| Correlation / FCS (multi-tau) | 0.13 s | pycorrelate 1.71 s (direct) | **13×** |
| Diffusion simulation (coasting) | 0.44 s | PyBroMo 2.23 s | **5.1×** |
| Burst search | 2.8 ms | FRETBursts 13.0 ms | **4.7×** |
| Per-pixel reconvolution MLE (CPU) | 0.40 s | FLIMKit **on GPU** 0.54 s · CPU 0.86 s | **1.4× vs GPU** |
| ↳ re-tune IRF on a built lifetime map | **0.09 ms** | flimlib RLD 44 ms (recomputes) | **~510×** |

**0.27.0 is a performance & memory release**: CLSM fill + structure is 2.7×
faster at −12% memory, and a 2.6 M-pixel FLIM image fills 5.3× faster at −40%
memory, versus 0.26.2.

<sub>Apple M1 Pro, CPU only, identical input files. **Full results, per-task
charts, cross-version tracking, methodology and honest trade-offs:
[`PERF.md`](PERF.md).**</sub>

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
burst selection, 10-39x faster than flimlib for reconvolution lifetime fitting,
and produces per-pixel FLIM lifetime maps faster than FLIMKit and flimlib — all
on CPU. See [`PERF.md`](PERF.md) for the full, reproducible comparison and the
cases where other tools win.

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
from-source build: **[doc/r-package.rst](doc/r-package.rst)**.

### Java / ImageJ

The Java binding ships as an ImageJ/Fiji plugin — a single cross-platform JAR
you drop into `plugins/`. See the [ImageJ / Fiji plugin](#imagej--fiji-plugin)
section below and **[doc/imagej-plugin.rst](doc/imagej-plugin.rst)**.

### JavaScript / Node.js

```bash
npm install tttrlib
```

A Node-API addon (Node ≥ 12.17) covering the same surface as Python. Photon
arrays are TypedArrays and 64-bit values are `BigInt`, so macro times stay exact.
Full instructions, conventions and limitations:
**[doc/javascript-package.rst](doc/javascript-package.rst)**. A small web viewer
built on it lives in
[`examples/js/ptu-webapp/`](examples/js/ptu-webapp/README.md).

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
flat `[start, stop, start, stop, ...]` NumPy int64 array (no conversion
needed) and feed straight into the burst-consuming analyses (BVA, H2MM):

```python
import tttrlib

tttr = tttrlib.TTTR("photon_stream.ptu")

L, m, T = 30, 10, 1e-3  # min photons, window photons, window time [s]
ranges = tttr.burst_search(L=L, m=m, T=T)  # NumPy int64 array
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
[`doc/imagej-plugin.rst`](doc/imagej-plugin.rst).

## Supported file formats

🟢 works · 🟡 partial, see note · 🔴 not supported

| Format | Extension | Read | Write | Identified from contents | Notes · sample data |
|---|---|:--:|:--:|:--:|---|
| [PicoQuant PTU](doc/formats/picoquant-ptu.rst) | `.ptu` | 🟢 | 🟢 | 🟢 | PicoHarp, TimeHarp, HydraHarp; 8 T2/T3 record encodings · [files](https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/pq/ptu) |
| [PicoQuant HT3](doc/formats/picoquant-ht3.rst) | `.ht3` | 🟢 | 🟢 | 🟢 | HydraHarp v1/v2, plus SF macro-time compression · [files](https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/pq/ht3) |
| [Becker & Hickl SPC-130](doc/formats/becker-hickl-spc.rst) | `.spc` | 🟢 | 🟢 | 🟢 | [files](https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/bh) |
| [Becker & Hickl SPC-600 (256)](doc/formats/becker-hickl-spc.rst) | `.spc` | 🟢 | 🟢 | 🟡 | must be named: not distinguishable from other `.spc` by content · [files](https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/bh) |
| [Becker & Hickl SPC-600 (4096)](doc/formats/becker-hickl-spc.rst) | `.spc` | 🟢 | 🟢 | 🟡 | as above · [files](https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/bh) |
| [Becker & Hickl SPC-QC](doc/formats/becker-hickl-spc.rst) | `.spc` | 🟢 | 🟢 | 🟢 | QC-x04 and QC-x06; reads the `.set` sidecar for TAC range and imaging geometry · [files](https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/bh) |
| [Photon-HDF5](doc/formats/photon-hdf5.rst) | `.h5` `.hdf5` | 🟢 | 🟢 | 🟢 | stores decoded arrays, so any record encoding is acceptable · [files](https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/hdf) |
| [Zeiss ConfoCor3](doc/formats/zeiss-confocor3.rst) | `.raw` | 🟢 | 🟢 | 🟢 | [files](https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/cz) |
| [Single-molecule (SM)](doc/formats/single-molecule-sm.rst) | `.sm` | 🟢 | 🟢 | 🟢 | · [files](https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/sm) |
| [Photonscore LINCam](doc/formats/photonscore-lincam.rst) | `.photons` | 🟢 | 🟢 | 🟢 | D7; `x`/`y` positions are carried as marker events · [files](https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/photonscore) |
| [BrightEyes-TTM](doc/formats/brighteyes-ttm.rst) | `.ttr` | 🟢 | 🟢 | 🔴 | must be named: a bare `uint16` stream with no header or magic, so it can never be identified from contents. Micro times are 8-bit TDC codes, uncalibrated · [files](https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/brighteyes) |
| [FLIM LABS `STT1`](doc/formats/flim-labs-stt1.rst) | `.bin` | 🟡 | 🔴 | 🟡 | planned. The format is fully specified, but **no example file is published anywhere**, so a reader cannot be verified against real data — see below · [files](https://gitlab.peulen.xyz/skf/tttr-data/-/tree/main/flimlabs) |

Every supported container round-trips: writing photons into any of them and
reading them back returns identical arrival times. Micro times survive only
where the target format can hold them — SPC-130 and SPC-QC have 12 bits,
SPC-600 (256) has 8, ConfoCor3 has 1 and SM has none — so transcoding into a
narrower container is lossy by construction rather than by defect. A
BrightEyes `.ttr` holds 8 bits, and additionally carries only the pixel, line
and frame clocks, so a marker that is none of those is refused rather than
silently dropped.

Detection uses the extension as a hint, not an answer. Every format claiming the
extension is tried in turn and asked to recognise the contents; if none does,
every format that can identify itself from bytes is asked. A correctly formatted
file with an unhelpful name, or no extension at all, is still identified.

### FLIM LABS

FLIM LABS writes four different `.bin` formats and only one of them is a photon
stream. `SP01` (binned decay curves), `SPF1` (phasors), `IT02` (intensity
traces) and `FCS1` (correlation curves) are analysis products — there are no
photons left in them, so tttrlib has nothing to read them into. Only `STT1`, the
spectroscopy time tagger, is a TTTR container.

Its layout is known exactly: `STT1` magic, a little-endian `u32` header length,
a JSON header carrying the enabled channels and the laser period, then 17-byte
records of `{u8 event, f64 micro time (ns), f64 macro time (ns)}` which are
**not** sorted by arrival time. Event codes 70, 76 and 80 are ASCII `F`, `L` and
`P` for frame, line and pixel; anything else is a channel index.

What is missing is data. No `STT1` file is published in any FLIM LABS
repository, and the PyPI package that writes them is a Windows-only driver for
their FPGA hardware. If you have an `STT1` file and can share it, please open an
issue — that is the one thing standing between the specification and a tested
reader.

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
