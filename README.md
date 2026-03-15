# tttrlib

[![Anaconda](https://anaconda.org/tpeulen/tttrlib/badges/version.svg)](https://anaconda.org/tpeulen/tttrlib)
[![PyPI](https://badge.fury.io/py/tttrlib.svg)](https://pypi.org/project/tttrlib/)
[![CI](https://github.com/Fluorescence-Tools/tttrlib/actions/workflows/ci.yml/badge.svg)](https://github.com/Fluorescence-Tools/tttrlib/actions/workflows/ci.yml)
[![Documentation](https://github.com/Fluorescence-Tools/tttrlib/actions/workflows/docs.yml/badge.svg)](https://github.com/Fluorescence-Tools/tttrlib/actions/workflows/docs.yml)

---

## General description
tttrlib is a file format agnostic high performance library to
read, process, and write time-tagged-time resolved (TTTR) data acquired by
PicoQuant (PQ) and Becker & Hickl measurement devices/cards or TTTR
files in the open Photon-HDF format.

**tttrlib** is a high-performance, file-format-agnostic library to read, process, and write **time-tagged time-resolved (TTTR)** data from
**PicoQuant**, **Becker & Hickl**, and **Photon-HDF5** files.

Written in **C++** with **Python bindings**, it provides a fast, vendor-independent API for handling photon streams and enables integration into advanced data analysis pipelines for time-resolved fluorescence spectroscopy and imaging.

![tttrlib FLIM][3]

### Key Features

* Fast TTTR file reading (IO-limited)
* Multi-dimensional histogramming
* Correlation analysis
* Fluorescence decay generation and analysis
* Photon distribution (FIDA/PCH)
* Burst and time-window selection
* FLIM and ISM image generation
* Experimental ISM tools (Adaptive Pixel Reassignment, Focus-ISM background rejection)

`tttrlib` typically outperforms pure Python implementations by
~40× in decay histogramming and ~2–5× in burst selection.


## Installation

We recommend using [**Miniforge**](https://github.com/conda-forge/miniforge) with the fast **mamba** solver.

### Conda / Mamba

**macOS / Linux**

```bash
mamba install -c bioconda tttrlib
```

**Windows**

```bash
mamba install -c tpeulen tttrlib
```

### pip (development version)

```bash
pip install git+https://github.com/fluorescence-tools/tttrlib
# or for the development branch
pip install git+https://github.com/fluorescence-tools/tttrlib@development
```

### From Source

```bash
git clone --recursive https://github.com/fluorescence-tools/tttrlib.git
cd tttrlib
pip install -e .
```

Precompiled packages are available for Windows, Linux (x86, ARM64, PPCle), and macOS.
Legacy 32-bit and Python 2.7 are not supported.

---

## Usage

See [**docs.peulen.xyz/tttrlib**](https://docs.peulen.xyz/tttrlib) for the full API and tutorials.
Below are minimal examples.

Detailed build instructions for developers are available in [BUILDING.md](BUILDING.md).

### Read TTTR data

```python
import tttrlib
data = tttrlib.TTTR("photon_stream.ptu")

macro = data.macro_times
micro = data.micro_times
routing = data.routing_channels
```

### Inspect header

```python
import tttrlib
fn = 'photon_stream.ptu'
data = tttrlib.TTTR(fn)
print(data.header.json)
print(data.header.to_csv())
```

### Cross-correlate photon streams

```python
import tttrlib
fn = 'photon_stream.ptu'
data = tttrlib.TTTR(fn)
correlator = tttrlib.Correlator(
    channels=([1], [2]),
    tttr=data
)
taus = correlator.x_axis,
correlation_amplitude = correlator.correlation
```

### Create intensity images (CLSM)

```python
import tttrlib
fn = 'image.ptu'
data = tttrlib.TTTR(fn)
clsm = tttrlib.CLSMImage(data)
channels = [0, 1]
prompt_range = [0, 16000]
clsm.fill(channels=channels, micro_time_ranges=[prompt_range])
intensity_image = clsm.intensity

# Alternatively
clsm = tttrlib.CLSMImage(fn, fill=True)
intensity_image = clsm.intensity

```

### Minimal burst search

```python
import tttrlib
import numpy as np

fn = 'photon_stream.ptu'
tttr = tttrlib.TTTR(fn)

# Bust selection
L, m, T = 30, 10, 1e-3  # min photons, window photons, window time [s]
ranges = tttr.burst_search(L=L, m=m, T=T)  # flat [start, stop, start, stop, ...]
bursts = list(zip(ranges[0::2], ranges[1::2]))
```

For PIE/ALEX data, add micro-time gating before burst search; see the tutorial for donor/acceptor prompt examples.
For details, parameters, and plotting examples, see the Burst Analysis tutorial.

## Supported File Formats

* **PicoQuant:** PicoHarp/TimeHarp/HydraHarp (`ptu`, `ht3`, T2/T3)
* **Becker & Hickl:** `spc132`, `spc630` (256 & 4096 mode)
* **Photon-HDF5:** open standard format

---

## Contributing

To add support for a new format / microscope:

1. Open a GitHub issue describing the format and instrument.
2. Share a small demo file (<100 MB) with expected results.
3. If relevant, document your workflow or analysis steps.

With this information, we can integrate and test the new format automatically.

---

## Design Goals

* Low memory footprint for large datasets (e.g. FLIM)
* Cross-platform C/C++ library with SWIG bindings (Python, C#, Java, etc.)
* Modular and extendable design for fluorescence spectroscopy and imaging

---

## Citation

If you use this software, please cite:

> **Thomas-Otavio Peulen**, Katherina Hemmen, Annemarie Greife, Benjamin M. Webb, Suren Felekyan, Andrej Sali, Claus A. M. Seidel, Hugo Sanabria, Katrin G. Heinze.
> *“tttrlib: modular software for integrating fluorescence spectroscopy, imaging, and molecular modeling.”*
> **Bioinformatics** 41 (2): btaf025 (2025).
> [https://doi.org/10.1093/bioinformatics/btaf025](https://doi.org/10.1093/bioinformatics/btaf025)

---

## License

Copyright 2007–2026 tttrlib developers
Licensed under the **BSD-3-Clause** license.

[3]: https://github.com/Fluorescence-Tools/tttrlib/blob/main/doc/logos/mashup.png?raw=true "tttrlib FLIM"

