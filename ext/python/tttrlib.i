%module(directors="1", package="tttrlib") tttrlib
%feature("kwargs", 1);
%feature("autodoc", "2");
%include "documentation.i"

%{
// This fixes numpy int casting to std::vector,int>
// (see: https://github.com/swig/swig/issues/888)
#define SWIG_PYTHON_CAST_MODE
// This is needed for numpy as you need SWIG_FILE_WITH_INIT
#define SWIG_FILE_WITH_INIT
#include <assert.h>
%}

#ifdef VERBOSE_TTTRLIB
// Warning 302: Identifier redefined (ignored) (Renamed from 'pair< std::shared_ptr< TTTR >,std::shared_ptr< TTTR > >'),
// Warning 389: operator[] ignored (consider using %extend)
// Warning 401: Nothing known about base class
// Warning 453: Can't apply (double *IN_ARRAY2,int DIM1,DIM2). No typemaps are defined.
// Warning 511: Ignore overloaded functions
#pragma SWIG nowarn= 302, 389, 401, 453, 501, 505, 511
#endif

%pythonbegin "./ext/python/tttrlib.py"

%include "info.h"
%include "misc_types.i"

%include "TTTRHeader.i"
%include "TTTRRange.i"
%include "TTTRSelection.i"
%include "TTTR.i"

%include "Histogram.i"

/* Correlation of data */
%include "Correlator.i"

/* Microscopy */
%include "CLSM.i"
%include "Localization.i"

/* Phasor analysis */
%include "DecayPhasor.i"

/* Photon distribution analysis */
%include "Pda.i"

/* convolution */
%include "fsconv.i"

/* DecayFit(s) */
%include "DecayFit.i"
