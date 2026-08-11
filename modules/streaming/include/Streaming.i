// SPDX-License-Identifier: BSD-3-Clause
//
// This file is the streaming module's SWIG interface, and it lives with the
// module like every other one. Until 2026-08-11 a second, DIVERGENT copy sat
// in `ext/python/Streaming.i`, left behind by the module relocation
// (`ac3cadda2`) -- and that copy was the one SWIG compiled, because
// `%include "Streaming.i"` resolves against the including file's own directory
// first. Edits made here did nothing at all, silently. The shadow copy is
// deleted; if you add one back, nothing here is compiled again.
%{
#include "StreamingCorrelator.h"
#include "StreamingCLSMImage.h"
#include "StreamingBurstDetector.h"
#include "StreamingDecayHistogram.h"
#include "StreamingIntensityTrace.h"
%}

// Map exception translations.
%exception {
    try {
        $action
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}

// A chunk crosses the language boundary ONCE.
//
// The consumers here take (pointer, n) and loop in C++; the Python `push_np`
// helpers below used to loop in *Python* over `push_photon`, which measured
// 1.13 us/photon -- 30x a numpy histogram of the same photons, and enough to
// eat half a core on a 100 kHz live acquisition. The array overloads existed
// and were simply not reachable from Python: no numpy typemap mapped an
// ndarray onto their pointer arguments, so `push_photons(array, None, n)`
// raised.
//
// These typemaps + the `push_arrays` methods below make one chunk one call.
// The parameter names are deliberately distinctive: %apply is global and keyed
// by name, so a generic `(double* w, int n)` here would silently rewrite
// unrelated signatures in other modules.
%apply (unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long* st_macro_times, int st_n_macro_times)}
%apply (double* IN_ARRAY1, int DIM1)             {(double* st_weights, int st_n_weights)}
%apply (int* IN_ARRAY1, int DIM1)                {(int* st_channels, int st_n_channels)}
%apply (unsigned short* IN_ARRAY1, int DIM1)     {(unsigned short* st_microtimes, int st_n_microtimes)}

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
%extend tttrlib::StreamingIntensityTrace {
    void push_photon(uint64_t macro_time) {
        $self->push_photon(macro_time, 1.0);
    }
}

%include "StreamingCLSMImage.h"
%include "StreamingCorrelator.h"
%include "StreamingBurstDetector.h"
%include "StreamingDecayHistogram.h"
%include "StreamingIntensityTrace.h"

#ifdef SWIGPYTHON
%extend tttrlib::StreamingCorrelator {
    // One chunk, one call. `st_n_weights == 0` means unit weights and
    // `st_n_channels == 0` means the autocorrelation (both channels fed).
    void push_arrays(
        unsigned long long* st_macro_times, int st_n_macro_times,
        double* st_weights, int st_n_weights,
        int* st_channels, int st_n_channels
    ) {
        if (st_n_weights != 0 && st_n_weights != st_n_macro_times)
            throw std::invalid_argument("push_arrays: weights and macro times differ in length");
        if (st_n_channels != 0 && st_n_channels != st_n_macro_times)
            throw std::invalid_argument("push_arrays: channels and macro times differ in length");
        const uint64_t* mt = reinterpret_cast<const uint64_t*>(st_macro_times);
        const double* w = st_n_weights ? st_weights : nullptr;
        if (st_n_channels)
            $self->push_photons(mt, w, st_channels, st_n_macro_times);
        else
            $self->push_photons(mt, w, st_n_macro_times);
    }

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

    def push_np(self, macro_times, weights=None, channels=None):
        """Push a whole chunk of photons in one call.

        `channels` selects the cross-correlation: channel 0 and channel 1 are
        the two correlated streams and the lag runs 0 -> 1, exactly as in the
        batch correlator. Omitting it feeds every photon to both channels --
        the autocorrelation.
        """
        import numpy as np
        mt = np.ascontiguousarray(macro_times, dtype=np.uint64)
        w = (np.empty(0, dtype=float) if weights is None
             else np.ascontiguousarray(weights, dtype=float))
        c = (np.empty(0, dtype=np.int32) if channels is None
             else np.ascontiguousarray(channels, dtype=np.int32))
        self.push_arrays(mt, w, c)

    def __repr__(self):
        return (f"StreamingCorrelator(n_bins={self.n_bins()}, "
                f"n_casc={self.n_casc()}, photons={self.photon_count()})")
    %}
}

%extend tttrlib::StreamingBurstDetector {
    void push_arrays(unsigned long long* st_macro_times, int st_n_macro_times) {
        $self->push_photons(
            reinterpret_cast<const uint64_t*>(st_macro_times), st_n_macro_times);
    }

    %pythoncode %{
    @property
    def bursts(self):
        return self.get_burst_indices()

    @property
    def burst_count(self):
        return len(self.get_burst_indices()) // 2

    def push_np(self, macro_times):
        import numpy as np
        self.push_arrays(np.ascontiguousarray(macro_times, dtype=np.uint64))
    %}
}

%extend tttrlib::StreamingDecayHistogram {
    // `st_n_channels == 0` puts every photon in channel 0.
    void push_arrays(
        unsigned short* st_microtimes, int st_n_microtimes,
        int* st_channels, int st_n_channels
    ) {
        if (st_n_channels != 0 && st_n_channels != st_n_microtimes)
            throw std::invalid_argument("push_arrays: channels and microtimes differ in length");
        $self->push_photons(
            reinterpret_cast<const uint16_t*>(st_microtimes),
            st_n_channels ? st_channels : nullptr,
            st_n_microtimes);
    }

    %pythoncode %{
    @property
    def histogram(self):
        import numpy as np
        nc = self.n_channels()
        nb = self.n_bins()
        return np.asarray(self.get_histogram(), dtype=float).reshape(nc, nb)

    def push_np(self, microtimes, channels=None):
        import numpy as np
        mt = np.ascontiguousarray(microtimes, dtype=np.uint16)
        ch = (np.empty(0, dtype=np.int32) if channels is None
              else np.ascontiguousarray(channels, dtype=np.int32))
        self.push_arrays(mt, ch)
    %}
}

%extend tttrlib::StreamingPhasor {
    void push_arrays(unsigned short* st_microtimes, int st_n_microtimes) {
        $self->push_photons(
            reinterpret_cast<const uint16_t*>(st_microtimes), st_n_microtimes);
    }

    %pythoncode %{
    @property
    def phasor(self):
        return self.get_phasor()

    def push_np(self, microtimes):
        import numpy as np
        self.push_arrays(np.ascontiguousarray(microtimes, dtype=np.uint16))
    %}
}

%extend tttrlib::StreamingIntensityTrace {
    // `st_n_weights == 0` counts photons, which is what the batch
    // compute_intensity_trace does.
    void push_arrays(
        unsigned long long* st_macro_times, int st_n_macro_times,
        double* st_weights, int st_n_weights
    ) {
        if (st_n_weights != 0 && st_n_weights != st_n_macro_times)
            throw std::invalid_argument("push_arrays: weights and macro times differ in length");
        $self->push_photons(
            reinterpret_cast<const uint64_t*>(st_macro_times),
            st_n_weights ? st_weights : nullptr,
            st_n_macro_times);
    }

    %pythoncode %{
    @property
    def y(self):
        import numpy as np
        return np.asarray(self.counts(), dtype=float)

    @property
    def x(self):
        """Bin-start times of the retained bins, in seconds."""
        import numpy as np
        return np.asarray(self.get_time_axis(), dtype=float)

    def push_np(self, macro_times, weights=None):
        import numpy as np
        mt = np.ascontiguousarray(macro_times, dtype=np.uint64)
        w = (np.empty(0, dtype=float) if weights is None
             else np.ascontiguousarray(weights, dtype=float))
        self.push_arrays(mt, w)

    def __repr__(self):
        return (f"StreamingIntensityTrace(bin_width={self.bin_width():.3e} s, "
                f"bins={self.n_bins()}, photons={self.photon_count()})")
    %}
}
#endif
