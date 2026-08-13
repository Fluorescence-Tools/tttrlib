// SPDX-License-Identifier: BSD-3-Clause
%{
#include "DecayPatternFit.h"
%}

// Vector-of-vector patterns and the exception path both need std::exception
// mapped, matching MaxEnt.i's sibling engines.
%exception tttrlib::decay_pattern_fit {
    try {
        $action
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_ValueError, e.what());
    }
}

namespace tttrlib {};
%include "DecayPatternFit.h"
