// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_NEURALNET_H
#define TTTRLIB_NEURALNET_H

#include <cstddef>
#include <string>
#include <vector>

namespace tttrlib {

/**
 * @brief Elementwise standardisation, @f$ (x - \mu) / \sigma @f$.
 *
 * Mirrors the scikit-learn ``StandardScaler`` so a net trained in Python and one
 * trained here are interchangeable.  Empty ``mean``/``scale`` means "identity" —
 * a net may carry no scaler at all.
 */
struct StandardScaler {
    std::vector<double> mean;
    std::vector<double> scale;

    /// Whether this scaler does anything (non-empty mean/scale).
    bool active() const { return !mean.empty(); }
    /// Number of features; 0 when inactive.
    int size() const { return static_cast<int>(mean.size()); }

    /// Fit mean/scale from ``X`` (row-major, ``n_rows x n_cols``).
    void fit(const double* X, int n_rows, int n_cols);
    /// Apply @f$ (x-\mu)/\sigma @f$ to ``v`` in place.
    void transform(std::vector<double>& v) const;
    /// Apply @f$ x\sigma + \mu @f$ to ``v`` in place.
    void inverse_transform(std::vector<double>& v) const;
};

/// Elementwise nonlinearity applied after a dense layer.
enum class Activation { Identity, ReLU, Tanh, Sigmoid };

/// Parse a scikit-learn activation name; throws on an unknown name.
Activation activation_from_string(const std::string& name);
/// Inverse of :func:`activation_from_string`.
std::string activation_to_string(Activation a);

/**
 * @brief One fully-connected layer, @f$ y = \mathrm{act}(Wx + b) @f$.
 *
 * ``weight`` is row-major with shape ``n_out x n_in``, i.e.
 * ``weight[o*n_in + i]``.  Note scikit-learn stores ``coefs_`` transposed
 * (``n_in x n_out``); importers must transpose.
 */
struct DenseLayer {
    std::vector<double> weight;
    std::vector<double> bias;
    int n_in = 0;
    int n_out = 0;
    Activation activation = Activation::ReLU;
};

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
 * Internally the dense products run through Eigen; that is an implementation
 * detail of the ``.cpp`` and does not leak into this header or the bindings.
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
    int n_inputs() const { return layers_.empty() ? 0 : layers_.front().n_in; }
    int n_outputs() const { return layers_.empty() ? 0 : layers_.back().n_out; }
    int n_layers() const { return static_cast<int>(layers_.size()); }
    /// Total number of weights plus biases.
    long long n_parameters() const;
    const std::vector<DenseLayer>& get_layers() const { return layers_; }
    const StandardScaler& get_x_scaler() const { return x_scaler_; }
    const StandardScaler& get_y_scaler() const { return y_scaler_; }

    /**
     * @brief Throw unless the layers chain and the scalers match the ends.
     *
     * Called on construction and after JSON parsing so a malformed model fails
     * at load rather than producing silent nonsense mid-forward.
     */
    void validate() const;

private:
    std::vector<DenseLayer> layers_;
    StandardScaler x_scaler_;
    StandardScaler y_scaler_;
    std::vector<double> loss_curve_;
    std::vector<double> validation_curve_;
};

} // namespace tttrlib

#endif // TTTRLIB_NEURALNET_H
