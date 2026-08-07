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

// Shared C++ core -- identical %include list to ext/python/tttrlib.i.
%include "info.h"
%include "misc_types.i"
/* The registry: pure data, identical in every language, and the one case
   that covers a lot of surface at once. */
%include "Registry.i"
%include "FileCheck.i"
%include "TTTRHeader.i"
%include "TTTRRange.i"
%include "TTTRSelection.i"
%include "TTTR.i"
%include "TTTRMask.i"
%include "Channel.i"
%include "BurstFilter.i"
%include "BurstFeatureExtractor.i"
/* Burst features: BVA and 2CDE. BurstFeature.i first -- BVA and TwoCDE derive
   from it, and its %exception governs both. */
%include "BurstFeature.i"
%include "BVA.i"
%include "TwoCDE.i"
/* Hidden Markov models. Restraints and constraints first: HMM.i names both.
   HmmSurrogate.i must follow NeuralNet.i -- the surrogate IS a neural net, and
   an earlier %include emits an unqualified `NeuralNet` that does not compile. */
%include "HMMRestraints.i"
%include "HMMConstraints.i"
%include "HMM.i"
%include "NeuralNet.i"
%include "HMMSurrogate.i"
%include "MicrotimeLinearization.i"

%include "Histogram.i"

/* Columnar tables and their HDF5 form. HistogramNd.i must come first:
   DataStore.h's free functions name tttrlib::hist::Axis and HistogramNd, and
   without their declarations SWIG emits an unqualified `hist::Axis`. */
%include "HistogramNd.i"
%include "DataStore.i"
/* CSV: reads and writes a DataStore, so it follows DataStore.i. */
%include "CsvReader.i"
%include "CsvWriter.i"
%include "Hdf5Table.i"
%include "StoreFile.i"
%include "Pto.i"
/* One vocabulary for a table in a file, whatever the file is. Must follow
   StoreFile.i, Hdf5Table.i, Csv.i and Pto.i: it dispatches to all four. */
%include "Table.i"

/* Decoding a buffer, reading a container in pieces, and the whole B&H
   ".set" sidecar. RecordStream.i must follow TTTR.i and Pto.i:
   it decodes into a TTTR and returns the raw bytes as a std::vector. */
%include "RecordStream.i"
%include "BhSet.i"

/* Correlation of data */
%include "Correlator.i"

/* Microscopy */
%include "CLSM.i"
%include "CLSMSuperRes.i"
%include "Localization.i"

// TIFF I/O for 2D/3D arrays
%include "Tiff.i"

/* Phasor analysis */
%include "DecayPhasor.i"

/* Photon distribution analysis */
%include "Pda.i"

/* convolution */
%include "DecayConvolution.i"

/* DecayFit(s) */
%include "DecayFit.i"

/* The photon simulator.
   LAST, and that is load-bearing: Sim.i is the only place stdint.i is included,
   and including it earlier changes how SWIG resolves int64_t in the R and Java
   wrappers -- differently across SWIG versions. Keep it at the end, as
   ext/python/tttrlib.i does. */
%include "Sim.i"

/* Java-only convenience helpers (bulk array accessors) */
%include "helpers.i"
