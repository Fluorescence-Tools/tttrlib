// SPDX-License-Identifier: BSD-3-Clause
// Split Python extension `tttrlib.core`: containers, headers, selections,
// histograms and the DataStore. Every other split module %imports this one;
// the file formats are `io`, the pure kernels `math`.
%module(directors="1", package="tttrlib") core
// When another split module %imports this file it has TTTRLIB_TEMPLATES_IMPORTED
// defined so that ITS misc_types.i wraps no templates; here the templates must
// be seen (as imported types), so lift the guard for the duration of this file.
#ifdef TTTRLIB_TEMPLATES_IMPORTED
#define TTTRLIB_CORE_IS_IMPORTED
#undef TTTRLIB_TEMPLATES_IMPORTED
#endif
%include "split/common.i"
%pythoncode %{
_tttrlib = _core   # hand-written helpers below call the C module by this name
%}

%include "info.h"
#ifndef TTTRLIB_CORE_IS_IMPORTED
%include "misc_types.i"    // the importing module has already included it itself
#endif
%include "Registry.i"
%include "FileCheck.i"
%include "TTTRHeader.i"
%include "TTTRRange.i"
%include "TTTRSelection.i"
%include "TTTR.i"
%include "TTTRMask.i"
%include "Channel.i"
%include "BurstSignificance.i"
%include "BurstSearchMaxTree.i"
%include "MicrotimeLinearization.i"   // its global %exception governs everything below, as in the monolith
%include "Histogram.i"
%include "HistogramNd.i"
%include "DataStore.i"

// The monolith sees stdint.i once, at the very end, through Sim.i (see the note
// at the top of misc_types.i on why not earlier): SWIG resolves the typedefs
// after the whole parse, so std::uint32_t & co. become numbers everywhere.
// Each split module needs the same, at the same place.
%include "stdint.i"

#ifdef TTTRLIB_CORE_IS_IMPORTED
#define TTTRLIB_TEMPLATES_IMPORTED
#endif
