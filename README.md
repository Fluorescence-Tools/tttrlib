# tttrlib
[![Anaconda-Server Badge](https://anaconda.org/tpeulen/tttrlib/badges/version.svg)](https://anaconda.org/tpeulen/tttrlib)
[![PyPI version](https://badge.fury.io/py/tttrlib.svg)](https://pypi.org/project/tttrlib/)
![conda build](https://github.com/fluorescence-tools/tttrlib/actions/workflows/conda-build.yml/badge.svg)


## General description
tttrlib is a file format agnostic high performance library to
read, process, and write time-tagged-time resolved (TTTR) data acquired by
PicoQuant (PQ) and Becker & Hickl measurement devices/cards or TTTR
files in the open Photon-HDF format.

The library facilitates the work with files containing
time-tagged time resolved photon streams by providing
a vendor independent C++ application programming interface (API)
for TTTR files that is wrapped by SWIG (Simplified Wrapper and Interface
Generator) for common scripting languages as Python as target languages
and non-scripting languages such as C# and Java including Octave,
Scilab and R. Currently, tttrlib is wrapped for the use in Python.

* Multi-dimensional histograms
* Correlation analysis
* Time-window analysis
* Photon distribution anaylsis
* FLIM image generation and analysis

![tttrlib FLIM][3]

`tttrlib` is programmed in C++ and wrapped for python. Thus, it can be used to integrate time-resolved data into 
advanced data analysis pipelines.

### Capabilities

* Fast reading TTTR files (IO limited)
* Generation / analysis of fluorescence decays
* Time window analysis
* Correlation of time event traces
* Filtering of time event traces to generate instrument response functions for fluorescence decays analysis without the need of independent measurements..
* Fast photon distribution analysis
* Fast selection of photons from a photon stream

Generation of fluorescence decay histograms tttrlib outperforms pure numpy and Python based
libraries by a factor of ~40.

## Documentation

### Installation
In an [anaconda](https://www.anaconda.com/) environment the library can
be installed by the following command:

```console
conda install -c tpeulen tttrlib
```

Alternatively, you can use pip to install `tttrlib`

```console
pip install tttrlib
```

### Usage
The API of tttrlib as well as some use cases are documented on its [web page](https://docs.peulen.xyz/tttrlib).
Below you find a small selection of code snippets.

Access photon data as follows:
```python
import tttrlib
fn = 'photon_stream.ptu'
data = tttrlib.TTTR(fn)

macro_times = data.macro_times
micro_times = data.micro_times
routing_channels = data.routing_channels
```

Print header-information:
```python
import tttrlib
fn = 'photon_stream.ptu'
data = tttrlib.TTTR(fn)
print(data.json)
```

Correlate photon streams:
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

Create intensity images from CLSM data:
```python
import tttrlib
fn = 'image.ptu'
data = tttrlib.TTTR(fn)
clsm = tttrlib.CLSM(data)

channels = [0, 1]
prompt_range = [0, 16000]
clsm.fill(channels=channels, micro_time_ranges=[prompt_range])

intensity_image = clsm.intensity
```

tttrlib is in active development. In case you notice unusual behaviour do not
hesitate to contact the authors.

## Supported file formats

### PicoQuant (PQ)
* PicoHarp ptu, T2/T3
* HydraHarp ptu, T2/T3
* HydraHarp ht3, PTU

### Becker & Hickl (BH)
* spc132
* spc630 (256 & 4096 mode)

### Photon HDF5

## Design goals
* Low memory footprint (keep objective large datasets, e.g., FLIM in memory).
* Platform independent C/C++ library with interfaces for scripting libraries

## Building and Installation

### C++ shared library

The C++ shared library can be installed from source with [cmake](https://cmake.org/):

```console
git clone --recursive https://github.com/fluorescence-tools/tttrlib.git
mkdir tttrlib/build; cd tttrlib/build
cmake ..
sudo make install
```

On Linux you can build and install a package instead:

### Python bindings

The Python bindings can be either be installed by downloading and compiling the source code or by using a
precompiled distribution for Python anaconda environment.

The following commands can be used to download and compile the source code:

```console
git clone --recursive https://github.com/fluorescence-tools/tttrlib.git
cd tttrlib
sudo python setup.py install
```

In an [anaconda](https://www.anaconda.com/) environment the library can
be installed by the following command:

```console
conda install -c tpeulen tttrlib
```

For most users, the latter approach is recommended. Currently, pre-compiled
packages for the anaconda distribution system are available for Windows (x86),
Linux (x86, ARM64, PPCle), and macOS (x86). Precompiled libary are linked against 
conda-forge HDF5 & Boost. Thus, the use of [miniforge](https://github.com/conda-forge/miniforge) 
is recommended.

Legacy 32-bit platforms and versions of programming languages, e.g., Python 2.7
are not supported.

## Citation
If you use this software please also check the pre-print:
>tttrlib: modular software for integrating fluorescence spectroscopy, imaging, and molecular modeling;
Thomas-Otavio Peulen, Katherina Hemmen, Annemarie Greife, Benjamin M. Webb, Suren Felekyan, Andrej Sali, 
Claus A. M. Seidel, Hugo Sanabria, Katrin G. Heinze; [https://arxiv.org/abs/2402.17252](arXiv:2402.17252)
>

## License

Copyright 2007-2024 tttrlib developers.
Licensed under the BSD-3-Clause

[3]: https://github.com/Fluorescence-Tools/tttrlib/blob/master/doc/logos/mashup.png?raw=true "tttrlib FLIM"
