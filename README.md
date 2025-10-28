Here’s a **reduced-duplication version** of your README that keeps all information intact but removes redundant or repeated sections (notably multiple installation/build parts, repeated file format lists, and repeated explanations of usage). It’s also slightly reorganized for clarity and flow while preserving your original tone and details.

---

# tttrlib

[![Anaconda](https://anaconda.org/tpeulen/tttrlib/badges/version.svg)](https://anaconda.org/tpeulen/tttrlib)
[![PyPI](https://badge.fury.io/py/tttrlib.svg)](https://pypi.org/project/tttrlib/)
![Conda Build](https://github.com/fluorescence-tools/tttrlib/actions/workflows/conda-build.yml/badge.svg)
![Build Wheels](https://github.com/fluorescence-tools/tttrlib/actions/workflows/build-wheels.yml/badge.svg)

---

## Overview

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

---

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
print(data.header.json)
print(data.header.to_csv())
```

### Cross-correlate photon streams

```python
corr = tttrlib.Correlator(channels=([1], [2]), tttr=data)
taus, g2 = corr.x_axis, corr.correlation
```

### Create intensity images (CLSM)

```python
clsm = tttrlib.CLSMImage("image.ptu", fill=True)
img = clsm.intensity
```

### Minimal burst search

```python
L, m, T = 30, 10, 1e-3
ranges = data.burst_search(L=L, m=m, T=T)
bursts = list(zip(ranges[0::2], ranges[1::2]))
```

---

## Verbose / Debug Output

Set the environment variable `TTTRLIB_VERBOSE=1` (or any truthy value) to enable detailed logs.

| Platform               | Command                      |
| ---------------------- | ---------------------------- |
| **Linux/macOS (bash)** | `export TTTRLIB_VERBOSE=1`   |
| **Windows CMD**        | `set TTTRLIB_VERBOSE=1`      |
| **Windows PowerShell** | `$env:TTTRLIB_VERBOSE = "1"` |

Include verbose logs and a minimal reproduction when reporting issues.

---

## Supported File Formats

* **PicoQuant:** PicoHarp/TimeHarp/HydraHarp (`ptu`, `ht3`, T2/T3)
* **Becker & Hickl:** `spc132`, `spc630` (256 & 4096 mode)
* **Photon-HDF5:** open standard format

---

## Contributing

To add support for a new format:

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

Copyright 2007–2024 tttrlib developers
Licensed under the **BSD-3-Clause** license.

[3]: https://github.com/Fluorescence-Tools/tttrlib/blob/main/doc/logos/mashup.png?raw=true "tttrlib FLIM"

