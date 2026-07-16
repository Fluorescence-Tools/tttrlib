.. currentmodule:: tttrlib

.. _changes_0_27:

Version 0.27
============
* **Photonscore LINCam ".photons" (D7) support**: New reader and writer for the
  position-sensitive photon-counting format written by Photonscore LINCam
  systems. ``tttrlib.TTTR("file.photons")`` decodes the paged, protobuf-style D7
  container (seed plus zigzag-varint delta streams) with no external protobuf
  dependency. Each photon's ``(x, y)`` position is stored in the flat TTTR
  stream as two marker events (``MARKER_POSITION_X`` / ``MARKER_POSITION_Y``,
  the coordinate carried in the marker micro time) that precede the photon, so
  no photonscore-specific code path is needed downstream: positions, decay and
  an image are recovered with standard accessors and, e.g., ``numpy.histogram2d``.
  ``TTTR.write("out.photons")`` writes a byte-exact D7 container. Both reader and
  writer are implemented in C++ and exposed through SWIG to Python, R and Java.
* **T2 <-> T3 record-mode conversion**: New ``TTTR.t2_to_t3(sync_rate= | sync_period=)``
  and ``TTTR.t3_to_t2()``. ``t2_to_t3`` re-derives the sync-period index (macro
  time) and dtime (micro time) from the single T2 time tag; ``t3_to_t2`` merges
  macro and micro into one fine time tag. ``T2 -> T3 -> T2`` is lossless for a
  fixed sync period; ``T3 -> T2`` preserves absolute arrival times at TAC
  resolution but drops the dtime/sync split. The converted objects carry the
  matching PicoQuant record type and can be written to any compatible container.

.. _changes_0_26:

Version 0.26
============
* **TIFF I/O for 2D/3D arrays**: New ``tttrlib.imread(path)`` / ``imwrite(path,
  array)`` read and write 2-D images and 3-D (multi-page) stacks as TIFF.
  ``imread`` auto-detects the pixel type (uint8/16/32, int32, float32/64) and
  returns a NumPy array of that dtype; ``imwrite`` picks the on-disk type from
  the array and supports ``none``/``lzw``/``packbits``/``deflate`` compression
  and BigTIFF. Backed by a bundled, statically-linked libtiff, so no new runtime
  dependency is added (built-in codecs only; ``WITH_TIFF_ZLIB`` adds deflate).
* Improved support for Photon-HDF5 for better ALEX support
* Transparent in-memory compression of TTTR and CLSMImage objects
* Added linearity correction for micro times
* Added multimolecule correction in PDA
* Improved DecayFit interface (JSON in/output)
* Add support for channel specific microtime LUTs (correct non-linearities in TAC) and shifts
* **Becker & Hickl SPCM Support**: New support for BH SPC-130/140/150 detectors with pixel-marker binning, automatic `.set` file parsing, dimension inference, and Frame 1 adjustment. Integrated truncated recording recovery.
* **CLSM Bug Fixes**: Fixed `get_fluorescence_decay` stack_frames bug where only the last frame was processed. Fixed `get_phasor` precision loss by using float instead of int calculation.

.. _changes_0_25:
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