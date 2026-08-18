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

// A C++ exception that reaches the Node-API boundary untranslated does NOT
// become a JavaScript exception -- it terminates the process:
//
//     libc++abi: terminating due to uncaught exception of type
//     std::runtime_error: BurstFeature: unknown stream 'donor'
//
// which is what `new BVA(bf).compute()` used to do before the streams were
// configured. Python survives the same call because it raises a RuntimeError.
//
// The shared fragments do install %exception blocks, but they are scoped: the
// one in BurstFeatureExtractor.i ends with a bare `%exception;`, and the global
// one lives in MicrotimeLinearization.i, which is included AFTER BVA.i and
// TwoCDE.i -- so those two, among others, were left bare.
//
// This installs the handler FIRST, so it covers everything that follows. A
// later per-file %exception still overrides it for its own scope, which is what
// those blocks are for.
%include <exception.i>
%exception {
    try {
        $action
    } catch (const std::invalid_argument& e) {
        SWIG_exception(SWIG_ValueError, e.what());
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    } catch (...) {
        SWIG_exception(SWIG_UnknownError, "Unknown exception");
    }
}

// Shared C++ core. **Not** the same include list as ext/python/tttrlib.i --
// that claim used to be here and was wrong by 16 files. See board ticket
// T-20260811-09 for the measured diff and what closing it needs; the short
// version is that each binding's list has drifted independently, so a
// subsystem can be complete in Python and absent here without anything
// failing. `tools/check_swig_multilang.sh` does not catch it: it proves the
// wrappers generate, not that they expose the same API.
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
#ifndef TTTRLIB_WITHOUT_BURST
%include "BurstSignificance.i"
%include "BurstSearchMaxTree.i"
%include "BurstFilter.i"
%include "BurstFeatureExtractor.i"
%include "BurstFeature.i"
%include "BVA.i"
%include "TwoCDE.i"
%include "BurstML.i"
#endif
#ifndef TTTRLIB_WITHOUT_HMM
%include "HMMRestraints.i"
%include "HMMConstraints.i"
%include "HMM.i"
#endif
#ifndef TTTRLIB_WITHOUT_MATH
%include "NeuralNet.i"
/* k-d tree nearest neighbours, and the mutual-reachability MST behind HDBSCAN */
%include "Cluster.i"
/* Kalman filter recursion over a count-rate trace */
%include "Kalman.i"
/* Watershed flood and marching-squares contours -- region segmentation */
%include "Watershed.i"
/* Richardson-Lucy and Wiener deconvolution over the vendored FFT */
%include "Deconvolution.i"
%include "Jitter.i"
/* the log-domain HMM lattice over a caller-supplied frame matrix */
%include "HmmLattice.i"
%include "Sampling.i"
#endif
#ifndef TTTRLIB_WITHOUT_HMM
%include "HMMSurrogate.i"
#endif
%include "MicrotimeLinearization.i"
#ifndef TTTRLIB_WITHOUT_KINETICS
%include "GopichSzabo.i"
#endif
#ifndef TTTRLIB_WITHOUT_FLUCTUATION
%include "PhotonCountingHistogram.i"
#endif

%include "Histogram.i"
%include "HistogramNd.i"
%include "DataStore.i"
#ifndef TTTRLIB_WITHOUT_IO_CSV
%include "CsvReader.i"
%include "CsvWriter.i"
#endif
// The columnar HDF5 reader has landed in the Python module, which is what the
// note here used to be waiting for. ext/js/pkg/index.js already exposed
// readHdf5()/writeHdf5() behind a feature check, so adding this line is all it
// took; test/js/conformance.test.mjs runs the group-tree cases through
// it. This list is again NOT identical to ext/python/tttrlib.i's -- see above.
#ifndef TTTRLIB_WITHOUT_IO_HDF5_TABLE
%include "Hdf5Table.i"
#endif
#ifndef TTTRLIB_WITHOUT_IO_STORE
%include "StoreFile.i"
#endif
#ifndef TTTRLIB_WITHOUT_IO_PTO
%include "Pto.i"
#endif
/* One vocabulary for a table in a file, whatever the file is. Must follow
   StoreFile.i, Hdf5Table.i, Csv.i and Pto.i: it dispatches to all four. */
#ifndef TTTRLIB_WITHOUT_IO_TABLE
%include "Table.i"
#endif

/* Decoding a buffer, reading a container in pieces, and the whole B&H
   ".set" sidecar. RecordStream.i must follow TTTR.i and Pto.i:
   it decodes into a TTTR and returns the raw bytes as a std::vector. */
%include "RecordStream.i"
%include "BhSet.i"

/* Correlation of data */
#ifndef TTTRLIB_WITHOUT_FCS
%include "Correlator.i"
/* 2D fluorescence-decay correlation: the photon-pair pass. */
%include "Fdc2D.i"
#endif


/* Microscopy */
#ifndef TTTRLIB_WITHOUT_CLSM
%include "CLSM.i"
#endif
#ifndef TTTRLIB_WITHOUT_SUPERRES
%include "CLSMSuperRes.i"
#endif
#ifndef TTTRLIB_WITHOUT_LOCALIZATION
%include "Localization.i"
#endif

/* TIFF I/O for 2D/3D arrays (imread / imwrite) */
#ifndef TTTRLIB_WITHOUT_IO_IMAGE
%include "Tiff.i"
#endif

/* Phasor analysis */
#ifndef TTTRLIB_WITHOUT_CLSM
%include "DecayPhasor.i"
#endif

/* Photon distribution analysis */
#ifndef TTTRLIB_WITHOUT_PDA
%include "Pda.i"
#endif

/* convolution */
#ifndef TTTRLIB_WITHOUT_DECAY
%include "DecayConvolution.i"

/* DecayFit(s) */
%include "DecayFit.i"
#endif

/* Five interfaces that carry no NumPy typemaps, so they need no per-language
   surface -- they were simply never added to this list. Restored 2026-08-11
   (T-20260811-09): background estimation, spectral crosstalk, recurrence
   analysis, maximum-entropy lifetime distributions, and blind IRF recovery.
   Order mirrors ext/python/tttrlib.i. */
#ifndef TTTRLIB_WITHOUT_BURST
%include "RecurrenceAnalysis.i"
#endif
#ifndef TTTRLIB_WITHOUT_CORRECTIONS
%include "SpectralCrosstalk.i"
%include "BackgroundEstimation.i"
%include "MaxEnt.i"
#endif
#ifndef TTTRLIB_WITHOUT_DECAY
%include "BlindIRF.i"
%include "DecayPatternFit.i"
#endif
/* Maximum-entropy TCSPC: lifetime and FRET-distance distributions. Its inputs
   go through IN_ARRAY1 and its outputs through ARGOUTVIEWM_ARRAY1/2, all of
   which jsarrays.i implements -- unlike jarrays.i, which has no rank-2 argout,
   so this file stays declared for java. */
#ifndef TTTRLIB_WITHOUT_DECAY
%include "MaxEntTcspc.i"
#endif
#ifndef TTTRLIB_WITHOUT_PDA
%include "Pda3cCore.i"
#endif


/* Photon simulator */
#ifndef TTTRLIB_WITHOUT_SIMULATION
%include "Sim.i"
#endif

/* Sim.i is the only interface that includes <stdint.i>, and after it SWIG
   resolves a `uint64_t` parameter past the name jsarrays.i keyed its
   fixed-width typemaps to -- so everything below here lost the BigInt half of
   the 64-bit contract (BUGS 2026-08-11). Re-register them. */
TTTRLIB_JS_FIXED_WIDTH_64_TYPEMAPS

/* Live correlation, decay histogram, phasor and intensity trace. After Sim.i,
   as in ext/python/tttrlib.i. Its only array typemap is IN_ARRAY1, which
   jsarrays.i implements, so it needs no per-language surface. */
#ifndef TTTRLIB_WITHOUT_STREAMING
%include "Streaming.i"
#endif
