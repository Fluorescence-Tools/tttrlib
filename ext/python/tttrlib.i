// SPDX-License-Identifier: BSD-3-Clause
// Linking issues against Python in Windows
%begin %{
#ifdef _MSC_VER
#define SWIG_PYTHON_INTERPRETER_NO_DEBUG 
#endif
%}

%pythonbegin %{
from __future__ import annotations
%}

%module(directors="1", package="tttrlib") tttrlib
%feature("kwargs", 1);
%feature("autodoc", "2");
%include "documentation.i"

%{
// This fixes numpy int casting to std::vector,int>
// (see: https://github.com/swig/swig/issues/888)
#define SWIG_PYTHON_CAST_MODE
    // SWIG_FILE_WITH_INIT was historically used to signal numpy init,
    // but SWIG 4.3 documents it as having no effect. Kept for reference.
    #define SWIG_FILE_WITH_INIT
#include <assert.h>
%}

%{
// RAII: release the Python GIL for the scope's lifetime and re-acquire it on
// destruction — including during C++ exception unwinding, so the catch handlers
// in TTTRLIB_NOGIL (which call the Python C-API via SWIG_exception) run with the
// GIL held.
//
// The module is ALSO generated with -threads, which releases the GIL around
// every wrapped call before this guard runs. Releasing an already-released
// GIL is a fatal Python error, so the guard only acts when this thread still
// holds it — that makes the two mechanisms compose, and keeps TTTRLIB_NOGIL
// correct on its own if -threads is ever dropped.
struct tttrlib_gil_release {
    PyThreadState *_save;
    tttrlib_gil_release()  { _save = PyGILState_Check() ? PyEval_SaveThread() : nullptr; }
    ~tttrlib_gil_release() { if (_save) PyEval_RestoreThread(_save); }
};
%}

// Release the GIL around a heavy, Python-object-free method while preserving the
// project's standard std::exception -> Python exception translation (see the
// global %exception in MicrotimeLinearization.i). Apply before the header %include.
//
// The release itself now comes from the module-wide -threads flag: SWIG wraps
// $action -- INCLUDING inside this custom %exception -- in its own
// BEGIN/END_ALLOW pair, so a guard here would release an already-released GIL,
// which is a fatal Python error, not a no-op. What this macro still adds over
// -threads alone is the exception translation. If -threads is ever dropped,
// put `tttrlib_gil_release _gil_guard;` back above $action.
%define TTTRLIB_NOGIL(Method)
%exception Method {
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
%enddef

// Keep SWIG output quiet by default (runtime verbosity is controlled via TTTRLIB_VERBOSE).
// Warning 302: Identifier redefined (ignored) (Renamed from 'pair< std::shared_ptr< TTTR >,std::shared_ptr< TTTR > >'),
// Warning 389: operator[] ignored (consider using %extend)
// Warning 401: Nothing known about base class
// Warning 453: Can't apply (double *IN_ARRAY2,int DIM1,DIM2). No typemaps are defined.
// Warning 511: Ignore overloaded functions
#pragma SWIG nowarn= 302, 389, 401, 453, 501, 505, 511

%pythoncode %{
import sys
import warnings

if sys.version_info[0] < 3:
    from importlib_metadata import version
else:
    from importlib.metadata import version

try:
    __version__ = version(__package__ or __name__)
except Exception:
    __version__ = "0.0.0"


class ExperimentalWarning(UserWarning):
    """Warning for experimental features."""
    pass


def mark_experimental(cls, message=None):
    """Mark a class as experimental by patching its __init__ and docstring."""
    if message is None:
        message = (
            "This class is experimental and may change or be removed in a future "
            "release. Use with caution."
        )
    
    original_init = cls.__init__ if hasattr(cls, '__init__') else None
    
    def __init__(*args, **kwargs):
        warnings.warn(
            f"{cls.__name__}: {message}",
            ExperimentalWarning,
            stacklevel=2
        )
        if original_init is not None:
            return original_init(*args, **kwargs)
    
    cls.__init__ = __init__
    cls.__experimental__ = True
    
    if cls.__doc__:
        cls.__doc__ = f".. warning:: Experimental\n\n{cls.__doc__}"
    else:
        cls.__doc__ = f".. warning:: Experimental\n\n{message}"
    
    return cls


def experimental(cls):
    """Decorator to mark a class as experimental."""
    return mark_experimental(cls)

%}

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
%include "BurstML.i"
%include "HMMRestraints.i"
%include "HMMConstraints.i"
%include "HMM.i"
%include "NeuralNet.i"
/* k-d tree nearest neighbours, and the mutual-reachability MST behind HDBSCAN */
%include "Cluster.i"
/* Kalman filter recursion over a count-rate trace */
%include "Kalman.i"
/* Richardson-Lucy and Wiener deconvolution over the vendored FFT */
%include "Deconvolution.i"
%include "Jitter.i"
/* the log-domain HMM lattice over a caller-supplied frame matrix -- the
   binned-trace counterpart to the photon-stream HMM above */
%include "HmmLattice.i"
%include "Sampling.i"
%include "HMMSurrogate.i"
%include "MicrotimeLinearization.i"
%include "GopichSzabo.i"
%include "PhotonCountingHistogram.i"
%include "RecurrenceAnalysis.i"
%include "SpectralCrosstalk.i"
%include "BackgroundEstimation.i"
%include "MaxEnt.i"
%include "BlindIRF.i"
%include "MaxEntTcspc.i"
%include "DecayPatternFit.i"
%include "Pda3cCore.i"

%include "Histogram.i"
%include "HistogramNd.i"
%include "DataStore.i"
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
/* 2D fluorescence-decay correlation: the photon-pair pass. */
%include "Fdc2D.i"


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
// %include "DecayFitMLEWrapper.i"  // Not ready yet

/* Photon simulator */
%include "Sim.i"

/* Streaming / online analysis */
%include "Streaming.i"
