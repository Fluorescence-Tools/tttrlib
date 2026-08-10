// SPDX-License-Identifier: BSD-3-Clause
%{
#include "StreamingCorrelator.h"
#include "StreamingCLSMImage.h"
#include "StreamingBurstDetector.h"
#include "StreamingDecayHistogram.h"
%}

// Map exception translations.
%exception {
    try {
        $action
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}

// SWIG doesn't parse C++ default arguments reliably; add explicit overloads
// before %include so Python callers can omit the weight.
%extend tttrlib::StreamingCorrelator {
    void push_photon(uint64_t macro_time) {
        $self->push_photon(macro_time, 1.0);
    }
    void push_photon(uint64_t macro_time, int channel) {
        $self->push_photon(macro_time, 1.0, channel);
    }
}
%extend tttrlib::StreamingDecayHistogram {
    void push_photon(uint16_t microtime) {
        $self->push_photon(microtime, 0, 1.0);
    }
    void push_photon(uint16_t microtime, int channel) {
        $self->push_photon(microtime, channel, 1.0);
    }
}
%extend tttrlib::StreamingPhasor {
    void push_photon(uint16_t microtime) {
        $self->push_photon(microtime, 1.0);
    }
}

%include "StreamingCLSMImage.h"
%include "StreamingCorrelator.h"
%include "StreamingBurstDetector.h"
%include "StreamingDecayHistogram.h"

#ifdef SWIGPYTHON
%extend tttrlib::StreamingCorrelator {
    %pythoncode %{
    @property
    def x_axis(self):
        import numpy as np
        return np.array(self.get_x_axis(), dtype=float)

    @property
    def correlation(self):
        import numpy as np
        return np.array(self.get_correlation(), dtype=float)

    @property
    def correlation_normalized(self):
        import numpy as np
        return np.array(self.get_correlation_normalized(), dtype=float)

    def push_np(self, macro_times, weights=None):
        import numpy as np
        mt = np.ascontiguousarray(macro_times, dtype=np.uint64)
        for t in mt:
            if weights is not None:
                self.push_photon(int(t), float(weights))
            else:
                self.push_photon(int(t))

    %}
}

%extend tttrlib::StreamingBurstDetector {
    %pythoncode %{
    @property
    def bursts(self):
        return self.get_burst_indices()

    @property
    def burst_count(self):
        return len(self.get_burst_indices()) // 2

    def push_np(self, macro_times):
        import numpy as np
        mt = np.ascontiguousarray(macro_times, dtype=np.uint64)
        for t in mt:
            self.push_photon(int(t))

    %}
}

%extend tttrlib::StreamingDecayHistogram {
    %pythoncode %{
    @property
    def histogram(self):
        import numpy as np
        nc = self.n_channels()
        nb = self.n_bins()
        return np.asarray(self.get_histogram(), dtype=float).reshape(nc, nb)

    def push_np(self, microtimes, channels=None):
        for i in range(len(microtimes)):
            ch = channels[i] if channels is not None else 0
            self.push_photon(int(microtimes[i]), int(ch))

    %}
}

%extend tttrlib::StreamingPhasor {
    %pythoncode %{
    @property
    def phasor(self):
        return self.get_phasor()

    def push_np(self, microtimes):
        for mt in microtimes:
            self.push_photon(int(mt))

    %}
}
#endif
