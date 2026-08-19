// SPDX-License-Identifier: BSD-3-Clause
%{
#include "NeuralNet.h"
%}

// Training/inference inputs arrive as 2D NumPy arrays (rows = samples).
%apply(double* IN_ARRAY2, int DIM1, int DIM2) {(const double* X, int n_samples, int n_features)}
%apply(double* IN_ARRAY2, int DIM1, int DIM2) {(const double* Y, int n_samples_y, int n_targets)}
%apply(double* IN_ARRAY2, int DIM1, int DIM2) {(const double* X, int n_rows, int n_cols)}
// Derivative entry points: directions and output adjoints are 2D arrays too.
// An adjoint with zero rows means "zero" (see NeuralNet::backward_derivatives).
%apply(double* IN_ARRAY2, int DIM1, int DIM2) {(const double* V, int n_rows_v, int n_cols_v)}
%apply(double* IN_ARRAY2, int DIM1, int DIM2) {(const double* dY, int n_rows_y, int n_cols_y)}
%apply(double* IN_ARRAY2, int DIM1, int DIM2) {(const double* dY1, int n_rows_y1, int n_cols_y1)}
%apply(double* IN_ARRAY2, int DIM1, int DIM2) {(const double* dY2, int n_rows_y2, int n_cols_y2)}
// Argout forms hand malloc-ed buffers to NumPy without a per-element copy.
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** out_y, int* n_out_y)}
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** out_dy_dv, int* n_out_dy_dv)}
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** out_d2y_dv2, int* n_out_d2y_dv2)}
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** out_dparams, int* n_out_dparams)}
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** out_dx, int* n_out_dx)}
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** out_dv, int* n_out_dv)}
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** out_params, int* n_out_params)}
%apply(double* IN_ARRAY1, int DIM1) {(const double* params, int n_params)}

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

%include "MlpCore.h"
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
        return self.predict_batch_out(X).reshape(X.shape[0], self.n_outputs())

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

    # --- derivatives -------------------------------------------------------
    def predict_derivatives_np(self, X, V=None, order=2):
        """Values and directional derivatives along ``V`` (one direction per row).

        Returns ``(y, dy_dv, d2y_dv2)`` as ``(n_rows, n_outputs)`` arrays; the
        higher ones are ``None`` below the requested ``order``.
        """
        import numpy as np
        X = np.ascontiguousarray(np.atleast_2d(np.asarray(X, dtype=float)))
        if order >= 1:
            V = np.ascontiguousarray(np.atleast_2d(np.asarray(V, dtype=float)))
        else:
            V = np.zeros((X.shape[0], X.shape[1]))
        y, d1, d2 = self.predict_derivatives_out(X, V, int(order))
        n, m = X.shape[0], self.n_outputs()
        return (y.reshape(n, m),
                d1.reshape(n, m) if order >= 1 else None,
                d2.reshape(n, m) if order >= 2 else None)

    def jacobian_np(self, x):
        """``dy/dx`` at one sample as an ``(n_outputs, n_inputs)`` array."""
        import numpy as np
        J = self.jacobian(np.asarray(x, dtype=float).ravel().tolist())
        return np.asarray(J, dtype=float).reshape(self.n_outputs(), self.n_inputs())

    def hessian_np(self, x, output=0):
        """``d2y_k/dx dx`` of output ``output`` at one sample, ``(n_inputs, n_inputs)``."""
        import numpy as np
        H = self.hessian(np.asarray(x, dtype=float).ravel().tolist(), int(output))
        return np.asarray(H, dtype=float).reshape(self.n_inputs(), self.n_inputs())

    def backward_np(self, X, dY, V=None, dY1=None, dY2=None):
        """Reverse pass: adjoint of the outputs -> adjoints of parameters and inputs.

        ``dY`` is ``dL/dy`` (``n_rows x n_outputs``). With ``V`` and ``dY1``
        (``dL/d(Jv)``) and/or ``dY2`` (``dL/d(v^T H v)``) the loss may also
        depend on the directional derivatives. Returns ``(dparams, dx, dv)``:
        ``dparams`` flat in :meth:`get_parameters` layout, ``dx``/``dv`` of
        shape ``(n_rows, n_inputs)``.
        """
        import numpy as np
        X = np.ascontiguousarray(np.atleast_2d(np.asarray(X, dtype=float)))
        dY = np.ascontiguousarray(np.atleast_2d(np.asarray(dY, dtype=float)))
        n_out = self.n_outputs()
        empty = np.zeros((0, n_out))
        if V is None:
            V = np.zeros((0, X.shape[1]))
        else:
            V = np.ascontiguousarray(np.atleast_2d(np.asarray(V, dtype=float)))
        dY1 = empty if dY1 is None else np.ascontiguousarray(np.atleast_2d(np.asarray(dY1, dtype=float)))
        dY2 = empty if dY2 is None else np.ascontiguousarray(np.atleast_2d(np.asarray(dY2, dtype=float)))
        dparams, dx, dv = self.backward_derivatives_out(X, V, dY, dY1, dY2)
        n = X.shape[0]
        return dparams, dx.reshape(n, self.n_inputs()), dv.reshape(n, self.n_inputs())

    @property
    def parameters(self):
        """All weights and biases as one flat float64 array (settable)."""
        return self.get_parameters_out()

    @parameters.setter
    def parameters(self, values):
        import numpy as np
        self.set_parameters(np.ascontiguousarray(np.asarray(values, dtype=float).ravel()))

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
