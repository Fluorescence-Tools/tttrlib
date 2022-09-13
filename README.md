# tttrlib
[![pipeline status](https://gitlab.peulen.xyz/tpeulen/tttrlib/badges/master/pipeline.svg)](https://gitlab.peulen.xyz/tpeulen/tttrlib/badges/master/pipeline.svg)
[![Anaconda-Server Badge](https://anaconda.org/tpeulen/tttrlib/badges/installer/conda.svg)](https://anaconda.org/tpeulen/tttrlib)

## General description

tttrlib is a file format agnostic low level, high performance library to
read and process time-tagged-time resolved (TTTR) data acquired by
PicoQuant (PQ) and Becker & Hickl measurement devices/cards or TTTR
files in the open Photon-HDF format.

The library tttrlib facilitates the work with files containing
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

tttrlib is a library that facilitates the interaction with TTTR data that can be
used to develop data analysis pipelines e.g. for single-molecule and image
spectroscopy. tttrlib is not intended as end-user software for specific application
purposes.  

## Supported file formats

### PicoQuant (PQ)

* PicoHarp ptu, T2/T3
* HydraHarp ptu, T2/T3
* HydraHarp ht3, PTU

### Becker & Hickl (BH)

* spc132
* spc630 (256 & 4096 mode)

## Design goals

* Low memory footprint (keep objective large datasets, e.g.  FLIM in memory).
* Platform independent C/C++ library with interfaces for scripting libraries

## Capabilities

* Fast (IO limited) Reading TTTR files
* Generation / analysis of fluorescence decays
* Time window analysis
* Correlation of time event traces
* Filtering of time event traces to generate instrument response functions for fluorescence decays analysis without the need of independent measurements..
* Fast photon distribution analysis
* Fast selection of photons from a photon stream

Generation of fluorescence decay histograms tttrlib outperforms pure numpy and Python based
libraries by a factor of ~40.

## Implementation

Pure pure C/C++ high performance algorithms for real-time and interactive
analysis of TTTR data.

## Building and Installation

### C++ shared library

The C++ shared library can be installed from source with [cmake](https://cmake.org/):

```console
git clone --recursive https://github.com/fluorescence-tools/tttrlib.git
mkdir tttrlib/build; cd tttrlib/build
cmake ..
sudo make install
```

On Linux you can build and install a package instead (prefered):

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

For most users the later approach is recommended. Currently, pre-compiled
packages for the anaconda distribution system are available for:

* Windows: Python 3.7 (x64)
* Linux: Python 3.7 (x64)
* MacOs: Python 3.7 (x64)

Legacy 32-bit platforms and versions of programming languages, e.g, Python 2.7
are not supported.

## Documentation

The API of tttrlib as well as some use cases are documented
on its [web page](https://docs.peulen.xyz/tttrlib)

Note, tttrlib is highly experimental library in current development. In
case you notice unusual behaviour do not hesitate to contact the authors.

## License

tttrlib is released under the open source MPL 2.0 license.

[3]: https://docs.peulen.xyz/tttrlib/_images/sphx_glr_plot_read_clsm_data_002.png "tttrlib FLIM"
