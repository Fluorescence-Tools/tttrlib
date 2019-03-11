 tttrlib
=========
[![Linux Build Status](https://travis-ci.org/Fluorescence-Tools/LabelLib.svg?branch=master)](https://travis-ci.org/Fluorescence-Tools/LabelLib)
[![Anaconda-Server Version](https://anaconda.org/tpeulen/tttrlib/badges/version.svg)](https://anaconda.org/tpeulen/tttrlib)
[![Anaconda-Server Downloads](https://anaconda.org/tpeulen/tttrlib/badges/downloads.svg)](https://anaconda.org/tpeulen/tttrlib)


tttrlib is a low level, high performance API to read and process time-tagged-time 
resolved (TTTR) data acquired by PicoQuant (PQ) and Becker&Hickl measurement devices/cards
or TTTR files in the open Photon-HDF format.


* Multi-dimensional histograms
* Correlation analysis
* Time-window analysis
* Photon distribution anaylsis
* FLIM image generation and analysis


tttrlib is NOT intended as ready-to-use software for specific application purposes. 



Design goals
------------

* Low memory footprint (keep objective large datasets, e.g.  FLIM in memory). 
  Particulary useful for FLIM.
* Platform independent C/C++ library with interfaces for scripting libraries 


Capabilities
------------


* Fast (IO limited) Reading TTTR files
* Generation / analysis of fluorescence decays
* Time window analysis
* Correlation of time event traces
* Filtering of time event traces to generate instrument response functions for fluorescence decays 
analysis without the need of independent measurements.. 
* Fast photon distribution analysis
* Fast selection of photons from a photon stream

Generation of fluorescence decay histograms tttrlib outperforms pure numpy and Python based
libraries by a factor of ~40  


Implementation
--------------

Pure pure C/C++ and CUDA based high performance algorithms for real-time and interactive 
analysis of TTTR data.



Examples
========


```python

```
  
  

Supported file formats
======================

PicoQuant (PQ)
--------------
* PicoHarp ptu, T2/T3
* HydraHarp ptu, T2/T3
* HydraHarp ht3, PTU


Becker & Hickl (BH)
-------------------
* spc132 
* spc630 (256 & 4096 mode)


License
=======

tttrlib is released under the open source MIT license.

