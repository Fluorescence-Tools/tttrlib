// SPDX-License-Identifier: BSD-3-Clause
//
// R top-level SWIG module for tttrlib.
//
// This is the R counterpart of ext/python/tttrlib.i. It re-uses the SAME
// language-neutral interface fragments that live in ext/python/ (resolved via
// -I ext/python on the SWIG command line); the Python-only lines in those
// fragments are hidden behind #ifdef SWIGPYTHON guards, and misc_types.i pulls
// in rarrays.i (instead of numpy.i) when SWIGR is defined.

%module tttrlib

%feature("autodoc", "2");
%include "documentation.i"

// Keep SWIG output quiet by default (same warning suppressions as Python).
#pragma SWIG nowarn=302,389,401,453,501,505,511

// Some shared fragments use Python-only helper macros (defined in the Python
// top module ext/python/tttrlib.i). Provide no-op definitions here so the same
// fragments parse under -r. TTTRLIB_NOGIL releases the Python GIL around heavy
// C++ calls; R has no GIL, so it expands to nothing.
%define TTTRLIB_NOGIL(Method) %enddef

// Shared C++ core -- identical %include list to ext/python/tttrlib.i.
%include "info.h"
%include "misc_types.i"
%include "FileCheck.i"
%include "TTTRHeader.i"
%include "TTTRRange.i"
%include "TTTRSelection.i"
%include "TTTR.i"
%include "TTTRMask.i"
%include "Channel.i"
%include "BurstFilter.i"
%include "BurstFeatureExtractor.i"
%include "MicrotimeLinearization.i"

%include "Histogram.i"

/* Correlation of data */
%include "Correlator.i"

/* Microscopy */
%include "CLSM.i"
%include "CLSMSuperRes.i"
%include "Localization.i"

/* TIFF I/O for 2D/3D arrays */
%include "Tiff.i"

/* Phasor analysis */
%include "DecayPhasor.i"

/* Photon distribution analysis */
%include "Pda.i"

/* convolution */
%include "DecayConvolution.i"

/* DecayFit(s) */
%include "DecayFit.i"
