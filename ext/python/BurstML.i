// SPDX-License-Identifier: BSD-3-Clause
%{
#include "BurstML.h"
%}

%include "BurstML.h"

#ifdef SWIGPYTHON
%extend tttrlib::BurstML {
    %pythoncode %{
    @property
    def num_bursts(self):
        return self.n_bursts()
    %}
}
#endif
