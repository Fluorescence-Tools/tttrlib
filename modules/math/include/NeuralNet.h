// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_NEURALNET_H
#define TTTRLIB_NEURALNET_H

// Validation: A/B-TESTED 2026-08-19 -- forward pass vs sklearn MLPRegressor weights (1e-10, test/python/test_neural_net.py);
//   training head-to-head with sklearn Adam on one regression task. test/python/misc/test_math_ab_numerics.py.
//   Derivatives (backward, jacobian, hessian, Taylor orders 0-2) vs central differences and the Dual<GradVec<N>>
//   dot-product identity in test/cpp/test_mlp_core.cpp and test/python/test_neural_net.py.
//   Register: okf/testing/math-kernel-validation.md

#include <cstddef>
#include <string>
#include <vector>

// Activation, DenseLayer, StandardScaler, MlpModel, the JSON format and every
// derivative kernel live in MlpCore.h (header-only, std-only, shared verbatim
// with imp.bff). This header is the library shell around them: training with
// Adam, file I/O, the registry entry, and the batch entry points the bindings
// expose.
#include "MlpCore.h"

namespace tttrlib {

/**
 * @brief Hyper-parameters for :func:`NeuralNet::train`.
 *
 * Defaults follow scikit-learn's ``MLPRegressor`` (Adam, ReLU, L2 ``alpha``,
 * early stopping on a held-out split) so C++-trained and Python-trained nets are
 * comparable.
 */
struct TrainOptions {
    std::vector<int> hidden_layer_sizes = {256, 256, 128};
    Activation activation = Activation::ReLU;
    int max_iter = 800;             ///< maximum epochs
    int batch_size = 200;
    double learning_rate = 1e-3;    ///< Adam step size
    double beta1 = 0.9;
    double beta2 = 0.999;
    double epsilon = 1e-8;
    double alpha = 1e-4;            ///< L2 penalty on weights (not biases)
    bool early_stopping = true;
    double validation_fraction = 0.1;
    int n_iter_no_change = 10;
    double tol = 1e-4;              ///< minimum improvement to reset the patience counter
    int seed = 0;
    bool verbose = false;
};

/**
 * @brief What :func:`NeuralNet::predict_derivatives` returns.
 *
 * Every field is row-major ``n_rows x n_outputs`` in the unscaled units of the
 * network's inputs and outputs. ``dy_dv`` is the directional derivative
 * ``J v`` and ``d2y_dv2`` the directional second derivative ``v^T H v`` along
 * the per-sample direction ``v``; each is empty below the order it belongs to.
 */
struct NeuralNetDerivatives {
    std::vector<double> y;
    std::vector<double> dy_dv;
    std::vector<double> d2y_dv2;
};

/**
 * @brief What :func:`NeuralNet::backward` returns.
 *
 * ``dparams`` is the gradient of the caller's loss with respect to every
 * parameter, in the layout of :func:`NeuralNet::get_parameters`; ``dx`` and
 * ``dv`` are the gradients with respect to the inputs and the directions, each
 * ``n_rows x n_inputs`` in unscaled units (``dv`` is zero for an order-0 pass).
 */
struct NeuralNetBackward {
    std::vector<double> dparams;
    std::vector<double> dx;
    std::vector<double> dv;
};

/**
 * @brief A feed-forward multilayer perceptron with optional input/output scaling.
 *
 * Deliberately domain-agnostic: it knows nothing about photons, H2MM, or any
 * other tttrlib concept, so several surrogate models can share it.  A surrogate
 * supplies only its own feature extractor and output decoder — see
 * :class:`HmmSurrogate` for the first consumer.
 *
 * Both directions are supported.  :func:`train` fits weights with Adam and
 * explicit backpropagation (four transposed GEMMs for a four-layer net —
 * reverse-mode automatic differentiation is *far* slower here and is not used).
 * :func:`predict` runs a forward pass.  Models round-trip through JSON, so a net
 * trained by scikit-learn can be executed here and vice versa.
 *
 * The network is also a differentiable building block: :func:`backward` turns
 * the adjoint of the outputs into the adjoint of the weights and inputs for any
 * loss the caller computes; :func:`predict_derivatives` and
 * :func:`backward_derivatives` extend that to losses on ``dy/dx`` and
 * ``d2y/dx2`` (a physics residual), and :func:`get_parameters` /
 * :func:`set_parameters` expose the flat parameter vector an outside optimiser
 * works on. Every derivative is computed by MlpCore.h; the forward and backward
 * passes used by :func:`train` are the same code.
 *
 * Internally the dense products run through the self-contained ``Mat.h``
 * (header-only, std-only C++17); that is an implementation detail of the
 * ``.cpp`` and does not leak into this header or the bindings.
 */
class NeuralNet {
public:
    NeuralNet() = default;
    explicit NeuralNet(std::vector<DenseLayer> layers,
                       StandardScaler x_scaler = {},
                       StandardScaler y_scaler = {});

    // --- serialisation ----------------------------------------------------
    /// Parse a ``tttrlib.neural_net`` JSON document.
    static NeuralNet from_json_string(const std::string& json);
    /// Read a ``tttrlib.neural_net`` JSON file.
    static NeuralNet from_json_file(const std::string& path);
    /// Serialise to JSON; ``indent < 0`` emits the compact form.
    std::string to_json_string(int indent = -1) const;
    /// Write the JSON document to ``path``.
    void to_json_file(const std::string& path, int indent = 2) const;

    // --- inference --------------------------------------------------------
    /**
     * @brief Forward pass for a single sample.
     * @param x Feature vector of length :func:`n_inputs`.
     * @return Output vector of length :func:`n_outputs`.
     */
    std::vector<double> predict(const std::vector<double>& x) const;

    /**
     * @brief Forward pass for a batch.
     * @param X Row-major ``n_rows x n_inputs`` feature matrix.
     * @param n_rows Number of samples.
     * @param n_cols Must equal :func:`n_inputs`.
     * @return Row-major ``n_rows x n_outputs`` predictions.
     */
    std::vector<double> predict_batch(const double* X, int n_rows, int n_cols) const;

    // --- derivatives (MlpCore.h) ---------------------------------------------
    using Derivatives = NeuralNetDerivatives;
    using Backward = NeuralNetBackward;

    /**
     * @brief Evaluate the network and its directional derivatives.
     *
     * For every sample the network is evaluated at ``x`` and expanded along the
     * direction ``v`` given in the same row of ``V``: ``y`` is the value,
     * ``dy_dv`` the directional derivative ``J v`` and ``d2y_dv2`` the
     * directional second derivative ``v^T H v`` (per output). Both are in the
     * *unscaled* units of ``x`` and ``y`` -- the stored scalers are applied and
     * undone inside. A Laplacian is the sum of ``d2y_dv2`` over the unit
     * directions; a full Jacobian or Hessian for one sample is :func:`jacobian`
     * / :func:`hessian`.
     *
     * @param X Row-major ``n_rows x n_inputs`` sample matrix.
     * @param V Row-major ``n_rows x n_inputs`` directions, one per sample.
     *          Ignored (may be null) for ``order == 0``.
     * @param order 0: values only; 1: also ``J v``; 2: also ``v^T H v``.
     */
    Derivatives predict_derivatives(const double* X, int n_rows, int n_cols,
                                    const double* V, int n_rows_v, int n_cols_v,
                                    int order = 2) const;

    /// Jacobian ``dy/dx`` of one sample, row-major ``n_outputs x n_inputs``.
    std::vector<double> jacobian(const std::vector<double>& x) const;

    /// Hessian ``d2y_k/dx dx`` of output ``output`` at one sample, row-major
    /// ``n_inputs x n_inputs``.
    std::vector<double> hessian(const std::vector<double>& x, int output) const;

    /**
     * @brief Reverse pass for an arbitrary loss on the outputs.
     *
     * Given ``dL/dy`` for every sample and output (``n_rows x n_outputs``,
     * row-major), returns ``dL/dparams`` and ``dL/dx``. This is the entry point
     * for using the network as one term of a larger differentiable model: the
     * caller's solver produces the adjoint of the network output, this returns
     * the adjoint of the weights. Nothing about the loss is assumed.
     */
    Backward backward(const double* X, int n_rows, int n_cols,
                      const double* dY, int n_rows_y, int n_cols_y) const;

    /**
     * @brief Reverse pass for a loss that also depends on ``J v`` and ``v^T H v``.
     *
     * The adjoints ``dY``, ``dY1`` (of ``dy_dv``) and ``dY2`` (of ``d2y_dv2``)
     * are each ``n_rows x n_outputs``; an empty ``dY1`` or ``dY2``
     * (``n_rows_y1 == 0`` / ``n_rows_y2 == 0``, pointer may be null) means
     * zero. The order of the underlying Taylor pass is 2 if ``dY2`` is given,
     * else 1 if ``dY1`` is given, else 0. This is what a physics-informed loss
     * -- a PDE residual in ``y``, its gradient and its Laplacian -- needs to
     * train the weights by gradient descent or L-BFGS.
     */
    Backward backward_derivatives(const double* X, int n_rows, int n_cols,
                                  const double* V, int n_rows_v, int n_cols_v,
                                  const double* dY, int n_rows_y, int n_cols_y,
                                  const double* dY1, int n_rows_y1, int n_cols_y1,
                                  const double* dY2, int n_rows_y2, int n_cols_y2) const;

    /**
     * @brief Argout forms of the derivative entry points, for the bindings.
     *
     * Same semantics as :func:`predict_batch`, :func:`predict_derivatives`,
     * :func:`backward_derivatives` and :func:`get_parameters`; each result is
     * returned as a freshly ``malloc``-ed buffer plus its length, which the
     * caller owns (the Python bindings hand it to NumPy without copying — the
     * ``std::vector`` returns above cost a per-element conversion that
     * dominated a physics-informed training step). Empty results are one
     * ``malloc``-ed element with length 0.
     */
    void predict_batch_out(const double* X, int n_rows, int n_cols,
                           double** out_y, int* n_out_y) const;
    void predict_derivatives_out(const double* X, int n_rows, int n_cols,
                                 const double* V, int n_rows_v, int n_cols_v, int order,
                                 double** out_y, int* n_out_y,
                                 double** out_dy_dv, int* n_out_dy_dv,
                                 double** out_d2y_dv2, int* n_out_d2y_dv2) const;
    void backward_derivatives_out(const double* X, int n_rows, int n_cols,
                                  const double* V, int n_rows_v, int n_cols_v,
                                  const double* dY, int n_rows_y, int n_cols_y,
                                  const double* dY1, int n_rows_y1, int n_cols_y1,
                                  const double* dY2, int n_rows_y2, int n_cols_y2,
                                  double** out_dparams, int* n_out_dparams,
                                  double** out_dx, int* n_out_dx,
                                  double** out_dv, int* n_out_dv) const;
    void get_parameters_out(double** out_params, int* n_out_params) const;
    /// :func:`set_parameters` from a raw buffer of ``n_params`` doubles.
    void set_parameters(const double* params, int n_params);

    /// All weights and biases as one flat vector: layers in order, each as its
    /// row-major weight matrix followed by its bias (the layout ``backward``
    /// reports gradients in, and the one an optimiser such as L-BFGS works on).
    std::vector<double> get_parameters() const;
    /// Overwrite the parameters from a flat vector in :func:`get_parameters`
    /// layout; the layer shapes are unchanged. Throws on a length mismatch.
    void set_parameters(const std::vector<double>& params);

    // --- training ---------------------------------------------------------
    /**
     * @brief Fit a network to ``(X, Y)`` with Adam and explicit backpropagation.
     *
     * Input and output standardisation are fitted from the data and stored with
     * the model, so :func:`predict` consumes and produces unscaled values.
     *
     * @param X Row-major ``n_samples x n_features`` inputs.
     * @param Y Row-major ``n_samples x n_targets`` targets.
     * @param n_samples Number of rows in ``X`` and ``Y``.
     * @param n_features Columns of ``X``.
     * @param n_targets Columns of ``Y``.
     * @param options Hyper-parameters.
     */
    static NeuralNet train(
        const double* X, int n_samples, int n_features,
        const double* Y, int n_samples_y, int n_targets,
        const TrainOptions& options = TrainOptions()
    );

    /// Per-epoch training loss recorded by the most recent :func:`train`.
    const std::vector<double>& get_loss_curve() const { return loss_curve_; }
    /// Per-epoch validation loss; empty when ``early_stopping`` was off.
    const std::vector<double>& get_validation_curve() const { return validation_curve_; }

    // --- introspection ----------------------------------------------------
    int n_inputs() const { return model_.n_inputs(); }
    int n_outputs() const { return model_.n_outputs(); }
    int n_layers() const { return static_cast<int>(model_.layers.size()); }
    /// Total number of weights plus biases.
    long long n_parameters() const;
    const std::vector<DenseLayer>& get_layers() const { return model_.layers; }
    const StandardScaler& get_x_scaler() const { return model_.x_scaler; }
    const StandardScaler& get_y_scaler() const { return model_.y_scaler; }

    /**
     * @brief Throw unless the layers chain and the scalers match the ends.
     *
     * Called on construction and after JSON parsing so a malformed model fails
     * at load rather than producing silent nonsense mid-forward.
     */
    void validate() const;

    /// The layers and scalers as one :struct:`MlpModel` -- what the header-only
    /// kernels operate on, and what a vendored copy of MlpCore.h consumes.
    const MlpModel& get_model() const { return model_; }

private:
    MlpModel model_;
    std::vector<double> loss_curve_;
    std::vector<double> validation_curve_;
};

} // namespace tttrlib

#endif // TTTRLIB_NEURALNET_H
