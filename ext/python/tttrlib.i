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


# Apply experimental marking to SWIG classes that are not yet stable API.
# This patches CLSMISM to emit warnings when instantiated.
if 'CLSMISM' in dir():
    mark_experimental(
        CLSMISM,
        "CLSMISM is experimental and may change or be removed in a future "
        "release. Use with caution."
    )
%}

%include "info.h"
%include "misc_types.i"
%include "FileCheck.i"
%include "TTTRHeader.i"
%include "TTTRRange.i"
%include "TTTRSelection.i"
%include "TTTR.i"
%include "TTTRMask.i"
%include "Channel.i"
%include "BurstFilter.i"
%include "BurstFeatureExtractor.i"
%include "MicrotimeLinearization.i"

%include "Histogram.i"

/* Correlation of data */
%include "Correlator.i"

/* Microscopy */
%include "CLSM.i"
%include "CLSMISM.i"
%include "Localization.i"

/* Phasor analysis */
%include "DecayPhasor.i"

/* Photon distribution analysis */
%include "Pda.i"

/* convolution */
%include "DecayConvolution.i"


/* DecayFit(s) */
%include "DecayFit.i"
// %include "DecayFitMLEWrapper.i"  // Not ready yet
