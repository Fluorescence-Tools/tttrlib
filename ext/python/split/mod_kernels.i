// SPDX-License-Identifier: BSD-3-Clause
// Split Python extension `tttrlib.kernels`: the array kernels that depend on
// nothing but NumPy -- neural net, k-d tree / k-means / HDBSCAN pieces, Kalman,
// watershed and marching squares, deconvolution, jitter, the HMM lattice,
// sampling.
%module(directors="1", package="tttrlib") kernels
#define TTTRLIB_TEMPLATES_IMPORTED
%include "split/common.i"
// The SWIG library pieces (std_vector, numpy.i and its import_array, the
// typemaps) must be *included* here so their runtime fragments are emitted into
// this wrapper; the %template instantiations inside are guarded off and come
// from core through the %import below.
%include "misc_types.i"
%{
#include "TTTR.h"
#include "TTTRMask.h"
#include "Channel.h"
%}
%import "split/mod_core.i"
%pythoncode %{
_tttrlib = _kernels
from tttrlib.core import *
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
%include "NeuralNet.i"
%include "Cluster.i"
%include "Kalman.i"
%include "Watershed.i"
%include "Deconvolution.i"
%include "Jitter.i"
%include "HmmLattice.i"
%include "Sampling.i"

%include "stdint.i"
