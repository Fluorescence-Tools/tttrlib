.. _roadmap:

.. |ss| raw:: html

   <strike>

.. |se| raw:: html

   </strike>

Roadmap
=======

Purpose of this document
------------------------
This document list general directions that core contributors are interested
to see developed in tttrlib. The fact that an item is listed here is in
no way a promise that it will happen, as resources are limited. Rather, it
is an indication that help is welcomed on this topic.

Statement of purpose: tttrlib in 2021
---------------------------------------
``tttrlib`` is a library to read/write and process time-tagged time-resolved (TTTR) data.
``tttrlib`` supports different file formats. The data contained in the different file formats
can be processed using a unified application programming interface (API). This allows for
processing TTTR data without the need of converting the original data. ttrlib is programmed in C/C++
to maximize its performance. For the development of processing pipelines ``tttrlib`` is wrapped
for the popular scripting language in Python via SWIG.

``tttrlib`` is a low level, high performance library to read and process time-tagged time-resolved
(TTTR) data acquired by PicoQuant (PQ), Becker&Hickl (BH) measurement
devices/cards, and TTTR files of the open Photon-HDF format. ``tttrlib`` can be used
for:

   - Fluorescence lifetime image microscopy (FLIM)
   - Correlation analysis
   - Time-window analysis
   - Photon distribution analysis
   - Multi-dimensional histograms

``tttrlib`` is intended as a library for the development of analysis pipelines.

**Thus our main goals in this era are to**:

* continue maintaining a well-documented collection of reading/writing routines
  and basic TTTR-data processing routines;
* improve the ease for users to develop and publish external components
* improve inter-operability with modern data science tools (e.g. Pandas, Dask),
  molecular modeling and integrative modeling tools (IMP), existing smFRET
  tools (e.g. FRETbursts), and imaging tools

Architectural / general goals
-----------------------------
The list is numbered not as an indication of the order of priority, but to
make referring to specific points easier. Please add new entries only at the
bottom. Note that the crossed out entries are already done, and we try to keep
the document up to date as we work on these issues.


#. Improved handling of PicoQuant PTU files

   * better CLSM image handling (make use of existing meta data)
   * better handling of Leica SP5 and SP8 (meta) data

#. More didactic documentation

   * More and more options were added to tttrlib. As a result, the
     documentation is crowded which makes it hard for beginners to get the big
     picture. Some work could be done in prioritizing the information.

#. Make it easier for external users to write analysis pipelines with tttrlib

   * Integrate analysis pipelines of external users into documentation

#. Better interfaces for interactive development

   * add / imporve __repr__ where possible

#. Add command line tools for basic features

   * Correlation analysis
   * FLIM (intenstity, mean lifetime, etc.)
   * Decay histogram export

#. Integration into integrative modeling platform

   * Add tests and example how PDA can be used for scoring
