// SPDX-License-Identifier: BSD-3-Clause
// Split Python extension `tttrlib.imaging`: correlators, CLSM images and their
// super-resolution / localisation / phasor tools, and the streaming analyses
// (which reach every other module, so they live in the last one).
%module(directors="1", package="tttrlib") imaging
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
#ifndef TTTRLIB_WITHOUT_BURST
#include "BurstFilter.h"
#endif
#ifndef TTTRLIB_WITHOUT_DECAY
#include "DecayFitPrior.h"
#endif
%}
%import "split/mod_core.i"
%import "split/mod_kernels.i"
%import "split/mod_spectroscopy.i"
%pythoncode %{
_tttrlib = _imaging
from tttrlib.core import *
from tttrlib.formats import *   # CLSMSuperRes' helpers call imread/imwrite by bare name
from tttrlib.kernels import *
from tttrlib.spectroscopy import *
%}


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
#ifndef TTTRLIB_WITHOUT_FCS
%include "Correlator.i"
%include "Fdc2D.i"
#endif
#ifndef TTTRLIB_WITHOUT_CLSM
%include "CLSM.i"
#endif
#ifndef TTTRLIB_WITHOUT_SUPERRES
%include "CLSMSuperRes.i"
#endif
#ifndef TTTRLIB_WITHOUT_LOCALIZATION
%include "Localization.i"
#endif
#ifndef TTTRLIB_WITHOUT_CLSM
%include "DecayPhasor.i"
#endif
#ifndef TTTRLIB_WITHOUT_STREAMING
%include "Streaming.i"
#endif

// The monolith sees stdint.i once, at the very end, through Sim.i (see the note
// at the top of misc_types.i on why not earlier): SWIG resolves the typedefs
// after the whole parse, so std::uint32_t & co. become numbers everywhere.
// Each split module needs the same, at the same place.
%include "stdint.i"
