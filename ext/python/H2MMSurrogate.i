// SPDX-License-Identifier: BSD-3-Clause
%{
#include "H2MMSurrogate.h"
%}

// Simulation + training are the long-running kernels here.
TTTRLIB_NOGIL(tttrlib::H2mmSurrogate::train)
TTTRLIB_NOGIL(tttrlib::H2mmSurrogate::generate_training_set)

%exception {
    try {
        $action
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}

// generate_training_set fills two out-parameters, which does not map cleanly
// onto Python; hide it and expose a tuple-returning helper instead.
%ignore tttrlib::H2mmSurrogate::generate_training_set;

%include "H2MMSurrogate.h"

%extend tttrlib::H2mmSurrogate {
    /// Returns {X, Y} flattened row-major; the Python layer reshapes.
    static std::vector<std::vector<double>> _generate_training_set(
        int n_states, int n_streams, int n_samples,
        int n_bursts, int burst_len, double mean_dt, int seed
    ) {
        std::vector<double> X, Y;
        tttrlib::H2mmSurrogate::generate_training_set(
            n_states, n_streams, n_samples, n_bursts, burst_len, mean_dt, seed, X, Y);
        return {X, Y};
    }
}

%exception;

#ifdef SWIGPYTHON
%extend tttrlib::H2mmSurrogate {
    %pythoncode %{
    @staticmethod
    def features(data):
        """Feature vector for an H2MM dataset, as a float64 array."""
        import numpy as np
        return np.asarray(H2mmSurrogate.extract_features(data), dtype=float)

    @staticmethod
    def generate_training_set(n_states, n_streams, n_samples=250, n_bursts=150,
                              burst_len=80, mean_dt=4.0, seed=0):
        """Simulate labelled datasets; returns ``(X, Y)`` 2D float64 arrays."""
        import numpy as np
        flat = H2mmSurrogate._generate_training_set(
            n_states, n_streams, n_samples, n_bursts, burst_len, mean_dt, seed)
        n_y = H2mmSurrogate.n_targets(n_states, n_streams)
        X = np.asarray(flat[0], dtype=float).reshape(n_samples, H2mmSurrogate.N_FEATURES)
        Y = np.asarray(flat[1], dtype=float).reshape(n_samples, n_y)
        return X, Y

    def predict_dict(self, data):
        """Estimate a model and return it as a dict of NumPy arrays."""
        return self.predict(data).to_dict()

    def __repr__(self):
        return "H2mmSurrogate(n_states={}, n_streams={}, features_version={})".format(
            self.get_n_states(), self.get_n_streams(), self.get_features_version())
    %}
}
#endif
