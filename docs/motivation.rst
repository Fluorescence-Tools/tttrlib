Why an format agnostic reader for TTTR files
============================================

In this page we briefly introduce what the HDF5 format is and why it is important for single-molecule FRET data.

What are TTTR files?
--------------------

The TTTR ecosystem
------------------
Two main

PicoQuant

Becker&Hickl


Why original file formats for smFRET?
-------------------------------------

no conversion needed
original data preserved.
alternative formats an reader often do not process all header information in
the original data

data duplication if converted to alternative formats for processing
, need to keep original data

existing software use vendor specific file formats as in input

compare space usage HDF5 vs spc, and PTU


tttrlib in the fluorescence eco system
--------------------------------------
chisurf
fit2x
single-burst search
photon distribution analysis
PAM