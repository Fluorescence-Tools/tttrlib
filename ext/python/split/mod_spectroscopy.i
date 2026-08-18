// SPDX-License-Identifier: BSD-3-Clause
// Split Python extension `tttrlib.spectroscopy`: bursts, HMMs, kinetics,
// decays, PDA, PCH, corrections. Types of `core` are imported, not wrapped.
%module(directors="1", package="tttrlib") spectroscopy
#define TTTRLIB_TEMPLATES_IMPORTED
%include "split/common.i"
// The SWIG library pieces (std_vector, numpy.i and its import_array, the
// typemaps) must be *included* here so their runtime fragments are emitted into
// this wrapper; the %template instantiations inside are guarded off and come
// from core through the %import below.
%include "misc_types.i"
// %import brings a class's TYPE but not the %{ #include %} that %include
// would have; the type table's shared_ptr up-casts for imported classes are
// still emitted into this wrapper, so their headers must be visible.
%{
#include "TTTR.h"
#include "TTTRMask.h"
#include "Channel.h"
#ifndef TTTRLIB_WITHOUT_CLSM
#include "CLSMImage.h"
#endif
#ifndef TTTRLIB_WITHOUT_MATH
#include "NeuralNet.h"
#endif
%}
%import "split/mod_core.i"
%import "split/mod_kernels.i"      // HmmSurrogate takes a NeuralNet, BurstFeatureExtractor uses it
%pythoncode %{
_tttrlib = _spectroscopy
from tttrlib.core import *   # bare names in the helpers below (TTTR, Channel, VectorDouble, ...)
from tttrlib.kernels import *
%}


// The active global %exception here must be what this fragment inherited in
// the monolith's include order (a directive in an %imported file does not
// carry over); restated verbatim from the fragment that set it there.
%exception {
    try {
        $action
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}
#ifndef TTTRLIB_WITHOUT_BURST
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
// The active global %exception here must be what this fragment inherited in
// the monolith's include order (a directive in an %imported file does not
// carry over); restated verbatim from the fragment that set it there.
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
#ifndef TTTRLIB_WITHOUT_HMM
%include "HMMSurrogate.i"
#endif
// The active global %exception here must be what this fragment inherited in
// the monolith's include order (a directive in an %imported file does not
// carry over); restated verbatim from the fragment that set it there.
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
#ifndef TTTRLIB_WITHOUT_KINETICS
%include "GopichSzabo.i"
#endif
#ifndef TTTRLIB_WITHOUT_FLUCTUATION
%include "PhotonCountingHistogram.i"
#endif
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
%include "MaxEntTcspc.i"
%include "DecayPatternFit.i"
#endif
#ifndef TTTRLIB_WITHOUT_PDA
%include "Pda3cCore.i"
%include "Pda.i"
#endif
#ifndef TTTRLIB_WITHOUT_DECAY
%include "DecayConvolution.i"
%include "DecayFit.i"
#endif

// The monolith sees stdint.i once, at the very end, through Sim.i (see the note
// at the top of misc_types.i on why not earlier): SWIG resolves the typedefs
// after the whole parse, so std::uint32_t & co. become numbers everywhere.
// Each split module needs the same, at the same place.
%include "stdint.i"
