// SPDX-License-Identifier: BSD-3-Clause
%{
#include "StreamingCorrelator.h"
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

%include "StreamingCorrelator.h"
%include "StreamingBurstDetector.h"
%include "StreamingDecayHistogram.h"

#ifdef SWIGPYTHON
%extend tttrlib::StreamingCorrelator {
    %pythoncode %{
    @property
    def x_axis(self):
        import numpy as np
        return np.asarray(self.get_x_axis(), dtype=float)

    @property
    def correlation(self):
        import numpy as np
        return np.asarray(self.get_correlation(), dtype=float)

    @property
    def correlation_normalized(self):
        import numpy as np
        return np.asarray(self.get_correlation_normalized(), dtype=float)

    def push_np(self, macro_times, weights=None):
        import numpy as np
        mt = np.ascontiguousarray(macro_times, dtype=np.uint64)
        if weights is None:
            self.push_photons(mt, None, len(mt))
        else:
            w = np.ascontiguousarray(weights, dtype=float)
            self.push_photons(mt, w, len(mt))

    def __repr__(self):
        return f"StreamingCorrelator(n_bins={self.n_bins}, n_casc={self.n_casc}, photons={self.photon_count})"
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
        self.push_photons(mt, len(mt))

    def __repr__(self):
        return f"StreamingBurstDetector(m={self.window_photons}, T={self.window_time}, photons={self.photon_count}, bursts={self.burst_count})"
    %}
}

%extend tttrlib::StreamingDecayHistogram {
    %pythoncode %{
    @property
    def histogram(self):
        import numpy as np
        return np.asarray(self.get_histogram(), dtype=float).reshape(self.n_channels, self.n_bins)

    def push_np(self, microtimes, channels=None):
        import numpy as np
        mt = np.ascontiguousarray(microtimes, dtype=np.uint16)
        if channels is None:
            self.push_photons(mt, None, len(mt))
        else:
            ch = np.ascontiguousarray(channels, dtype=np.int32)
            self.push_photons(mt, ch, len(mt))

    def __repr__(self):
        return f"StreamingDecayHistogram(bins={self.n_bins}, channels={self.n_channels}, total={self.total_count})"
    %}
}

%extend tttrlib::StreamingPhasor {
    %pythoncode %{
    @property
    def phasor(self):
        return self.get_phasor()

    def push_np(self, microtimes):
        import numpy as np
        mt = np.ascontiguousarray(microtimes, dtype=np.uint16)
        self.push_photons(mt, len(mt))

    def __repr__(self):
        g, s, n = self.get_phasor()
        return f"StreamingPhasor(g={g:.4f}, s={s:.4f}, n={n})"
    %}
}
#endif
