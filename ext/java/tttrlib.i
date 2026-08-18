// SPDX-License-Identifier: BSD-3-Clause
//
// Java top-level SWIG module for tttrlib.
//
// Java counterpart of ext/python/tttrlib.i. Re-uses the SAME language-neutral
// fragments in ext/python/ (resolved via -I ext/python); misc_types.i pulls in
// jarrays.i (instead of numpy.i) when SWIGJAVA is defined. Java supports both
// std::shared_ptr and directors, so PdaCallback can be subclassed from Java.

%module(directors="1") tttrlib

// Load the native JNI library when the wrapper class is initialised.
%pragma(java) jniclasscode=%{
  static {
    try {
      System.loadLibrary("tttrlibjni");
    } catch (UnsatisfiedLinkError e) {
      // Fall back to extracting a bundled native from the JAR (see NativeLoader).
      io.github.fluorescencetools.tttrlib.NativeLoader.load();
    }
  }
%}

%feature("autodoc", "2");

// Keep SWIG output quiet by default (same suppressions as Python).
#pragma SWIG nowarn=302,389,401,453,501,505,511

// Python-only helper macros used by shared fragments -> no-op for Java.
// (Java has no GIL; the heavy calls run without any global lock anyway.)
%define TTTRLIB_NOGIL(Method) %enddef

// Java supports shared_ptr and directors.
%include <std_shared_ptr.i>

// Shared C++ core. **Not** the same include list as ext/python/tttrlib.i --
// that claim used to be here and was wrong by 18 files. See board ticket
// T-20260811-09 for the measured diff and what closing it needs; the short
// version is that each binding's list has drifted independently, so a
// subsystem can be complete in Python and absent here without anything
// failing. `tools/check_swig_multilang.sh` does not catch it: it proves the
// wrappers generate, not that they expose the same API.
%include "info.h"
%include "misc_types.i"
/* The registry: pure data, identical in every language, and the one case
   that covers a lot of surface at once. */
#ifndef TTTRLIB_WITHOUT_REGISTRY
%include "Registry.i"
#endif
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
/* Burst features: BVA and 2CDE. BurstFeature.i first -- BVA and TwoCDE derive
   from it, and its %exception governs both. */
%include "BurstFeature.i"
%include "BVA.i"
%include "TwoCDE.i"
%include "BurstML.i"
#endif
/* Hidden Markov models. Restraints and constraints first: HMM.i names both.
   HmmSurrogate.i must follow NeuralNet.i -- the surrogate IS a neural net, and
   an earlier %include emits an unqualified `NeuralNet` that does not compile. */
#ifndef TTTRLIB_WITHOUT_HMM
%include "HMMRestraints.i"
%include "HMMConstraints.i"
%include "HMM.i"
#endif
#ifndef TTTRLIB_WITHOUT_MATH
%include "NeuralNet.i"
/* k-d tree nearest neighbours, and the mutual-reachability MST behind HDBSCAN */
%include "Cluster.i"
/* the log-domain HMM lattice: IN_ARRAY in, INPLACE buffers out -- the
   preallocate-and-fill shape java can marshal */
%include "HmmLattice.i"
#endif
/* Sampling.i is NOT here: sample_from_cdf returns through
   ARGOUTVIEWM_ARRAY1, and jarrays.i defines NO argout typemaps at any
   rank -- see its note at "output typemaps ... intentionally NOT
   defined". Adding it compiles but emits
   sample_from_cdf(double[], double[], int, SWIGTYPE_p_p_double,
   SWIGTYPE_p_int, boolean): inputs fine, output uncallable.
   In r and js, whose argout views cover ranks 1-3. */
/* Deconvolution.i and Jitter.i are NOT here for the same reason as Sampling.i
   above -- they return through ARGOUTVIEWM_ARRAY2, and jarrays.i has no argout
   typemap at any rank. They are in the R and JavaScript lists, whose array
   typemaps cover ranks 1-3. See tools/binding_parity_exceptions.txt. */
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

/* Columnar tables and their HDF5 form. HistogramNd.i must come first:
   DataStore.h's free functions name tttrlib::hist::Axis and HistogramNd, and
   without their declarations SWIG emits an unqualified `hist::Axis`. */
%include "HistogramNd.i"
%include "DataStore.i"
/* CSV: reads and writes a DataStore, so it follows DataStore.i. */
#ifndef TTTRLIB_WITHOUT_IO_CSV
%include "CsvReader.i"
%include "CsvWriter.i"
#endif
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

// TIFF I/O for 2D/3D arrays
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
#ifndef TTTRLIB_WITHOUT_PDA
%include "Pda3cCore.i"
#endif


/* The photon simulator.
   LAST, and that is load-bearing: Sim.i is the only place stdint.i is included,
   and including it earlier changes how SWIG resolves int64_t in the R and Java
   wrappers -- differently across SWIG versions. Keep it at the end, as
   ext/python/tttrlib.i does. */
#ifndef TTTRLIB_WITHOUT_SIMULATION
%include "Sim.i"
#endif

/* Live correlation, decay histogram, phasor and intensity trace. After Sim.i,
   as in ext/python/tttrlib.i. Its only array typemap is IN_ARRAY1, which
   jarrays.i implements, so it needs no per-language surface. */
#ifndef TTTRLIB_WITHOUT_STREAMING
%include "Streaming.i"
#endif

/* Java-only convenience helpers (bulk array accessors) */
%include "helpers.i"
