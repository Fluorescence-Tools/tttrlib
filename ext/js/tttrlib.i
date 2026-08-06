// SPDX-License-Identifier: BSD-3-Clause
//
// JavaScript (Node-API) top-level SWIG module for tttrlib.
//
// The JavaScript counterpart of ext/python/tttrlib.i. It re-uses the SAME
// language-neutral interface fragments in ext/python/ (resolved via
// -I ext/python on the SWIG command line); misc_types.i pulls in jsarrays.i
// (instead of numpy.i) when SWIGJAVASCRIPT is defined.
//
// The %include list below is deliberately IDENTICAL to ext/python/tttrlib.i's,
// module for module, in the same order. R and Java wrap a subset; JavaScript
// does not, because the burst and single-molecule web applications this binding
// exists for reach into the simulator, the HMM decoders and the decay fits just
// as the Python tools do. Anything Python can call, JavaScript can call.
//
// Not available here, and nowhere else to put the note:
//   * Directors. SWIG's Node-API backend generates none, so PdaCallback cannot
//     be subclassed from JavaScript -- exactly as in R. Pda's built-in models
//     are unaffected.
//   * The GIL. TTTRLIB_NOGIL is a no-op; JavaScript's single thread is released
//     by running the work in a worker_thread instead (see apps/ptu-webui).

%module tttrlib

%feature("autodoc", "2");
%include "documentation.i"

// Keep SWIG output quiet by default (same warning suppressions as Python).
#pragma SWIG nowarn=302,389,401,453,501,505,509,511

// Shared fragments use Python-only helper macros defined in the Python top
// module. TTTRLIB_NOGIL releases the Python GIL around heavy C++ calls; there is
// no GIL here, so it expands to nothing.
%define TTTRLIB_NOGIL(Method) %enddef

// Node-API converts a thrown C++ exception into a JavaScript exception on its
// own, but only for exceptions that escape the wrapper. The shared fragments
// install %exception blocks that call SWIG_exception(); those work unchanged.

// Shared C++ core -- identical %include list to ext/python/tttrlib.i.
%include "info.h"
%include "misc_types.i"
%include "Registry.i"
%include "FileCheck.i"
%include "TTTRHeader.i"
%include "TTTRRange.i"
%include "TTTRSelection.i"
%include "TTTR.i"
%include "TTTRMask.i"
%include "Channel.i"
%include "BurstSignificance.i"
%include "BurstFilter.i"
%include "BurstFeatureExtractor.i"
%include "BurstFeature.i"
%include "BVA.i"
%include "TwoCDE.i"
%include "HMMRestraints.i"
%include "HMMConstraints.i"
%include "HMM.i"
%include "NeuralNet.i"
%include "HmmSurrogate.i"
%include "MicrotimeLinearization.i"

%include "Histogram.i"
%include "HistogramNd.i"
%include "DataStore.i"
%include "CsvReader.i"
%include "CsvWriter.i"
// The columnar HDF5 reader has landed in the Python module, which is what the
// note here used to be waiting for. ext/js/pkg/index.js already exposed
// readHdf5()/writeHdf5() behind a feature check, so adding this line is all it
// took; test/js/conformance.test.mjs runs the PRD-019 group-tree cases through
// it. This list is again identical to ext/python/tttrlib.i's.
%include "Hdf5Table.i"

/* Correlation of data */
%include "Correlator.i"

/* Microscopy */
%include "CLSM.i"
%include "CLSMSuperRes.i"
%include "Localization.i"

/* TIFF I/O for 2D/3D arrays (imread / imwrite) */
%include "Tiff.i"

/* Phasor analysis */
%include "DecayPhasor.i"

/* Photon distribution analysis */
%include "Pda.i"

/* convolution */
%include "DecayConvolution.i"

/* DecayFit(s) */
%include "DecayFit.i"

/* Photon simulator (PRD-005) */
%include "Sim.i"
