// SPDX-License-Identifier: BSD-3-Clause
// Shared preamble of the split Python modules (generated from tttrlib.i's head; keep in step).
// Linking issues against Python in Windows
%begin %{
#ifdef _MSC_VER
#define SWIG_PYTHON_INTERPRETER_NO_DEBUG 
#endif
%}

%pythonbegin %{
from __future__ import annotations
%}

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


