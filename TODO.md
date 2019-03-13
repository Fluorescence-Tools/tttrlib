
Roadmap
=======

## v0.0.5
* Working test cases
* Automated compilation and testing
* Tested FCS
* User documentation of FCS and TTTR with use cases
* Webpage with API and User documentation and automatic integration of software changes
* Fully tested TTTR reading
* Fluorescence decay
* Laser scanning imaging


Documentation & Test cases
==========================

High priority
-------------
### High effort
### Low effort
* Write compilation guide for Windows, MacOS, and Linux


Medium priority
---------------
### High effort
### Low effort
1. Test cases with documentation for FCS


Low priority
------------
### High efforting
### Low effort
1. Test cases for histograms


Features
========

High prioritying
-------------
### High effort
1. Fluorescence decays
### Low effort


Medium priority
---------------
### High effort
* PDA
### Low effort


Low priority
------------
### High effort
### Low effort
* Fix number of TAC channels in PQ files. PQ files offer a divider, that reduces the effective resolution 
of the micro time and the effective number of TAC channels. This is currently not implemented.

Potential bugs
=============

High priority
-------------
### High effort
1. Check that all the variable types are still okay. To get the library compiled on Windows and MacOS I (TP) had to
change a lot of the variable type (unsigned long, unsigned long long, uint64_t, etc.).
2. Check that all the SWIG wrappers still work (associated with Bug 1.). 
### Low effort


Medium priority
---------------
### High effort
1. HDF5 problems when using hdf5>=1.8.16 with conda: It is still unclear where this bug comes from but it seems to
be related to https://github.com/conda-forge/hdf5-feedstock/issues/58
 
### Low effort


Low priority
------------
### High effort
1. Segmentation fault on MacOS with python 3.7 
### Low effort

