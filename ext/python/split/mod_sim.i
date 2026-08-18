// SPDX-License-Identifier: BSD-3-Clause
// Split Python extension `tttrlib.sim`: the photon simulator.
%module(directors="1", package="tttrlib") sim
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
%}
%import "split/mod_core.i"
%pythoncode %{
_tttrlib = _sim
from tttrlib.core import *
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
%include "Sim.i"
