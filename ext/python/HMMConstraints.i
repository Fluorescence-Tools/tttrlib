// SPDX-License-Identifier: BSD-3-Clause
%{
#include "HMMConstraints.h"
%}

// Priors and hard constraints for MAP fitting.  Must be wrapped *before* HMM.i,
// which takes a `const HmmConstraints*`: without the declaration SWIG emits an
// unqualified `HmmConstraints` cast that does not compile, because the class
// lives in namespace tttrlib.

// nlohmann::json is not a wrapped type: exposing these would hand Python an
// opaque handle that it cannot serialise and SWIG cannot free.  The string
// forms below are the supported round-trip.
%ignore tttrlib::HmmConstraints::to_json;
%ignore tttrlib::HmmConstraints::from_json;

// Validation throws (bad concentrations, out-of-range indices, fixed entries
// summing past 1).  Without a handler the exception unwinds through the wrapper
// and **aborts the interpreter** rather than raising -- validation that kills
// the process is worse than none.  Same scoped pattern as HMM.i.
%exception {
    try {
        $action
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}

%include "HMMConstraints.h"

#ifdef SWIGPYTHON
%extend tttrlib::HmmConstraints {

%pythoncode %{

    def __repr__(self):
        return "HmmConstraints(n_states=%d, n_symbols=%d, %s)" % (
            self.n_states(), self.n_symbols(),
            "empty" if self.is_empty() else "constrained")
%}

}
#endif
