// SPDX-License-Identifier: BSD-3-Clause
%{
#include "NeuralNet.h"
%}

// Training/inference inputs arrive as 2D NumPy arrays (rows = samples).
%apply(double* IN_ARRAY2, int DIM1, int DIM2) {(const double* X, int n_samples, int n_features)}
%apply(double* IN_ARRAY2, int DIM1, int DIM2) {(const double* Y, int n_samples_y, int n_targets)}
%apply(double* IN_ARRAY2, int DIM1, int DIM2) {(const double* X, int n_rows, int n_cols)}

// Layer list for introspection (std::vector<double>/<int> come from misc_types.i).
%template(VectorDenseLayer) std::vector<tttrlib::DenseLayer>;

// Training is the only long-running kernel here; inference is microseconds.
TTTRLIB_NOGIL(tttrlib::NeuralNet::train)

// Map C++ exceptions (malformed models, shape mismatches) onto Python ones.
%exception {
    try {
        $action
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}

%include "NeuralNet.h"

#ifdef SWIGPYTHON
%extend tttrlib::NeuralNet {
    %pythoncode %{
    def predict_np(self, x):
        """Forward pass for one sample; returns a float64 array."""
        import numpy as np
        return np.asarray(self.predict(np.asarray(x, dtype=float).ravel().tolist()),
                          dtype=float)

    def predict_batch_np(self, X):
        """Forward pass for a 2D array of samples; returns (n_rows, n_outputs)."""
        import numpy as np
        X = np.ascontiguousarray(np.atleast_2d(np.asarray(X, dtype=float)))
        flat = np.asarray(self.predict_batch(X), dtype=float)
        return flat.reshape(X.shape[0], self.n_outputs())

    @property
    def loss_curve_(self):
        import numpy as np
        return np.asarray(self.get_loss_curve(), dtype=float)

    @property
    def validation_curve_(self):
        import numpy as np
        return np.asarray(self.get_validation_curve(), dtype=float)

    def layer_weights(self, i):
        """Weight matrix of layer ``i`` as an ``(n_out, n_in)`` array."""
        import numpy as np
        # Keep the vector proxy alive: indexing it yields a view whose owner
        # would otherwise be collected before the fields are read.
        layers = self.get_layers()
        layer = layers[i]
        return np.asarray(layer.weight, dtype=float).reshape(layer.n_out, layer.n_in)

    def layer_bias(self, i):
        """Bias vector of layer ``i``."""
        import numpy as np
        layers = self.get_layers()
        layer = layers[i]
        return np.asarray(layer.bias, dtype=float)

    def __repr__(self):
        dims = [self.n_inputs()] + [self.get_layers()[i].n_out
                                    for i in range(self.n_layers())]
        return "NeuralNet({}, {} params)".format(
            "->".join(str(d) for d in dims), self.n_parameters())
    %}
}

%extend tttrlib::NeuralNet {
    %pythoncode %{
    @staticmethod
    def train_np(X, Y, options=None):
        """Fit a network to 2D arrays ``X`` (samples x features) and ``Y``."""
        import numpy as np
        X = np.ascontiguousarray(np.atleast_2d(np.asarray(X, dtype=float)))
        Y = np.ascontiguousarray(np.atleast_2d(np.asarray(Y, dtype=float)))
        if Y.shape[0] != X.shape[0] and Y.shape[1] == X.shape[0]:
            Y = np.ascontiguousarray(Y.T)
        return NeuralNet.train(X, Y, options if options is not None else TrainOptions())
    %}
}
#endif
