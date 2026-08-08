// SPDX-License-Identifier: BSD-3-Clause
%{
#include "HMMRestraints.h"
%}

// Restraints are *scored*: they add Dirichlet pseudo-counts to the M-step and
// contribute log p to the objective.  Wrapped before HMM.i, which takes a
// `const HmmRestraints*` -- without the declaration SWIG emits an unqualified
// cast that does not compile, the class being in namespace tttrlib.

// nlohmann::json is not a wrapped type; exposing it hands Python an opaque
// handle it cannot serialise and SWIG cannot free.  Strings are the round-trip.
%ignore tttrlib::HmmRestraints::to_json;
%ignore tttrlib::HmmRestraints::from_json;

// Validation throws.  Without a handler the exception unwinds through the
// wrapper and aborts the interpreter rather than raising.
%exception {
    try {
        $action
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}

%include "HMMRestraints.h"

#ifdef SWIGPYTHON
%extend tttrlib::HmmRestraints {
%pythoncode %{
    @property
    def alpha_trans_np(self):
        """Transition-row Dirichlet concentrations as an (n, n) array."""
        import numpy as np
        n = self.n_states()
        return np.asarray(list(self.alpha_trans()), dtype=np.float64).reshape(n, n)

    @property
    def alpha_obs_np(self):
        """Emission-row Dirichlet concentrations as an (n, p) array."""
        import numpy as np
        n, p = self.n_states(), self.n_symbols()
        return np.asarray(list(self.alpha_obs()), dtype=np.float64).reshape(n, p)

    def __repr__(self):
        return "HmmRestraints(n_states=%d, n_symbols=%d, %s)" % (
            self.n_states(), self.n_symbols(),
            "flat" if self.is_flat() else "restrained")
%}
}
#endif
