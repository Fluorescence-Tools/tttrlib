.. currentmodule:: tttrlib

.. _changes_0_23:

Version 0.26
============
* Improved support for Photon-HDF5

Version 0.25
------------
* Add simpler option for burst search
* Added get_supported_container_names
* Added ttrlib to bioconda

Version 0.24
------------
* Add support for sm files
* Add support for CZ CF3 FCS files
* Improved type tttr file type inference to mitigate crashes

Version 0.23
------------
* Consider background with certain fraction in mean lifetime (0.23.6)
* Added new correlator
* use pocketfft instead of fftw3
* add option to output full mask in ´selection_by_count_rate´ (0.23.10)
* add flip method to TTTRMask (0.23.10)

Version 0.22
------------
* Added option to ignore line stop (and use pixel duration)

Version 0.21
------------
* Added support for a PQ FLIM equipped Zeiss LSM 980
* Fixed memory issues
* Computation of mean lifetime & mean arrival times for TTTRRanges
* Compute average count rates for TTTR objects
* Compute mean micro time in TTTRRange and TTTR
* Added arm64 (Raspberry Pi) and PPC64LE builds
* reintroduced phasor
* added option to select micro time range when filling CLSMImage
* added method to strip tttr indices from TTTRRanges
* added TTTRSelection class for more advanced selections (count rates, etc.)
* added option to crop the content of CLSMImage instances
* added transformation (ie moving of TTTR indices in a CLSMImage instance)
* added TTTRMask class to consolidate selections and masks
* added option to rebin CLSMImages (0.21.7)
* added method to stack frames in images and option to stack
  all frames when initializing CLSMImages (0.21.8)
* added example how to use fit23 for MLE analysis of FLIM data & option
  to compute histograms on TTTRRange (0.21.9)