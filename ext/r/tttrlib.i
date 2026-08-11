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

// ── Scoped enums are passed as plain integers in R ─────────────────────────
//
// SWIG's R backend emits two different names for the same constant when the
// enum is an `enum class` at NAMESPACE scope: the accessor function is
// R_swig_ColumnType_ColumnType_Float64_get while the defineEnumeration table
// that has to read it asks for R_swig_ColumnType_Float64_get. The table is a
// lazily-evaluated promise, so nothing fails until R first converts a symbolic
// name -- and then it fails with "not available for .Call()", nowhere near the
// cause.
//
// Six enums are affected and every one of them is unusable from R today:
// AxisKind, HistStorage, ColumnType, Hdf5WriteMode, SuperResMethod, TiffDType.
// (A scoped enum nested in a CLASS is fine -- DataStore::Combine spells both
// halves the same way -- which is why only some of them break.)
//
// Mapping them to int sidesteps the generated table entirely: R passes the
// integer value. This is applied by name rather than to `enum SWIGTYPE`
// wholesale, because the five enums that DO resolve already take symbolic
// names from R and that is their API.
// The `in` half of the int typemap already casts (SWIG writes
// static_cast<T>(INTEGER(x)[0])), but the `out` half hands the enum straight to
// Rf_ScalarInteger, and a scoped enum has no implicit conversion to int -- so
// each one also needs an `out` that says the cast out loud.
%define TTTRLIB_R_ENUM_AS_INT(TYPE)
%apply int { TYPE };
%typemap(out) TYPE %{ $result = Rf_ScalarInteger(static_cast<int>($1)); %}
%enddef

TTTRLIB_R_ENUM_AS_INT(tttrlib::hist::AxisKind)
TTTRLIB_R_ENUM_AS_INT(tttrlib::hist::HistStorage)
TTTRLIB_R_ENUM_AS_INT(tttrlib::data::ColumnType)
TTTRLIB_R_ENUM_AS_INT(tttrlib::io::Hdf5WriteMode)
TTTRLIB_R_ENUM_AS_INT(tttrlib::io::PtoType)

TTTRLIB_R_ENUM_AS_INT(tttrlib::TiffDType)
TTTRLIB_R_ENUM_AS_INT(SuperResMethod)

// ── uint64 is a double in R, not an integer ────────────────────────────────
//
// SWIG's R backend routes every integer wider than `int` through as.integer(),
// which is 32-bit and silently NA above 2^31 -- so a payload offset and a
// payload size, both uint64, came back from R as NA. A double carries an
// integer exactly to 2^53, so this is a straight improvement of 22 bits and it
// covers every offset and size short of an 8-petabyte file.
//
// It does NOT cover a PTO object's FileUID, and must not: even a double stores
// only every 2048th integer at uid magnitudes. A uid crosses to R as a
// character string instead -- see the SWIGR typemaps in ext/python/Pto.i, which
// are name-matched and therefore win over this one for the `uid` parameters.
// Everything here is a magnitude rather than an identity, and a double carries
// those exactly.
//
// Applied to std::uint64_t by name rather than to `unsigned long long`, so the
// existing R behaviour of every other 64-bit parameter in the library is
// untouched -- this is the container's API, not a global policy change.
%typemap(in)  std::uint64_t %{ $1 = static_cast<std::uint64_t>(Rf_asReal($input)); %}
%typemap(out) std::uint64_t %{ $result = Rf_ScalarReal(static_cast<double>($1)); %}
%typemap(rtype)      std::uint64_t "numeric"
%typemap(scoercein)  std::uint64_t "$input = as.numeric($input);"
%typemap(scoerceout) std::uint64_t ""
%typemap(rtypecheck) std::uint64_t "is.numeric($arg)"

// Shared C++ core. **Not** the same include list as ext/python/tttrlib.i --
// that claim used to be here and was wrong by 17 files. See board ticket
// T-20260811-09 for the measured diff and what closing it needs; the short
// version is that each binding's list has drifted independently, so a
// subsystem can be complete in Python and absent here without anything
// failing. `tools/check_swig_multilang.sh` does not catch it: it proves the
// wrappers generate, not that they expose the same API.
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
%include "BurstSignificance.i"
%include "BurstFilter.i"
%include "BurstFeatureExtractor.i"
/* Burst features: BVA and 2CDE. BurstFeature.i first -- BVA and TwoCDE derive
   from it, and its %exception governs both. */
%include "BurstFeature.i"
%include "BVA.i"
%include "TwoCDE.i"
%include "BurstML.i"
/* Hidden Markov models. Restraints and constraints first: HMM.i names both.
   HmmSurrogate.i must follow NeuralNet.i -- the surrogate IS a neural net, and
   an earlier %include emits an unqualified `NeuralNet` that does not compile. */
%include "HMMRestraints.i"
%include "HMMConstraints.i"
%include "HMM.i"
%include "NeuralNet.i"
/* k-d tree nearest neighbours, and the mutual-reachability MST behind HDBSCAN */
%include "Cluster.i"
/* Richardson-Lucy and Wiener deconvolution over the vendored FFT */
%include "Deconvolution.i"
%include "Jitter.i"
%include "Sampling.i"
%include "HMMSurrogate.i"
%include "MicrotimeLinearization.i"
%include "GopichSzabo.i"
%include "PhotonCountingHistogram.i"

%include "Histogram.i"

/* Columnar tables and their HDF5 form. The R conformance runner
   exercises the same group-tree cases as the other three bindings.
   HistogramNd.i comes first and is not optional: DataStore.h's free functions
   name tttrlib::hist::Axis and HistogramNd, and without their declarations SWIG
   emits an unqualified `hist::Axis` that does not compile. */
%include "HistogramNd.i"
%include "DataStore.i"
/* CSV: reads and writes a DataStore, so it follows DataStore.i. */
%include "CsvReader.i"
%include "CsvWriter.i"
%include "Hdf5Table.i"
%include "StoreFile.i"
%include "Pto.i"
/* One vocabulary for a table in a file, whatever the file is. Must follow
   StoreFile.i, Hdf5Table.i, Csv.i and Pto.i: it dispatches to all four.

   NOTE for an R caller: `read_table_into` takes a std::vector<std::string> for
   its `columns`, and SWIG-R generates a dispatcher for such a function that
   NOTHING can satisfy -- it tests for a wrapped VectorString while the typemap
   behind it coerces a character vector, so a proxy passes the dispatcher and
   fails the typemap and a character vector does the reverse. Call the numbered
   overload directly: `read_table_into__SWIG_0(store, spec, group, cols, first,
   n)`. Same defect and same workaround as `pto_read_store`; it is a property of
   the parameter type, so it reappears on every function that takes one.
   test/r/conformance.R has a worked example. */
%include "Table.i"

/* Decoding a buffer, reading a container in pieces, and the whole B&H
   ".set" sidecar. RecordStream.i must follow TTTR.i and Pto.i:
   it decodes into a TTTR and returns the raw bytes as a std::vector. */
%include "RecordStream.i"
%include "BhSet.i"

/* Correlation of data */
%include "Correlator.i"
/* 2D fluorescence-decay correlation: the photon-pair pass (PRD-036). */
%include "Fdc2D.i"


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

/* Five interfaces that carry no NumPy typemaps, so they need no per-language
   surface -- they were simply never added to this list. Restored 2026-08-11
   (T-20260811-09): background estimation, spectral crosstalk, recurrence
   analysis, maximum-entropy lifetime distributions, and blind IRF recovery.
   Order mirrors ext/python/tttrlib.i. */
%include "RecurrenceAnalysis.i"
%include "SpectralCrosstalk.i"
%include "BackgroundEstimation.i"
%include "MaxEnt.i"
%include "BlindIRF.i"
/* Maximum-entropy TCSPC: lifetime and FRET-distance distributions. Its inputs
   go through IN_ARRAY1 and its outputs through ARGOUTVIEWM_ARRAY1/2, all of
   which rarrays.i implements -- unlike jarrays.i, which has no rank-2 argout,
   so this file stays declared for java. */
%include "MaxEntTcspc.i"
%include "Pda3cCore.i"


/* The photon simulator.
   LAST, and that is load-bearing: Sim.i is the only place stdint.i is included,
   and including it earlier changes how SWIG resolves int64_t in the R and Java
   wrappers -- differently across SWIG versions. Keep it at the end, as
   ext/python/tttrlib.i does. */
%include "Sim.i"

/* Live correlation, decay histogram, phasor and intensity trace. After Sim.i,
   as in ext/python/tttrlib.i. Its only array typemap is IN_ARRAY1, which
   rarrays.i implements, so it needs no per-language surface. */
%include "Streaming.i"
