// SPDX-License-Identifier: BSD-3-Clause
#include "NeuralNet.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>

#include <Eigen/Core>

#include "SimPcgRandom.h"
#include "nlohmann/json.hpp"

namespace tttrlib {

namespace {

using json = nlohmann::json;

// Row-major maps: tttrlib stores everything row-major, Eigen defaults to column
// major, so the storage order is spelled out rather than left to the default.
using RowMatrix = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
using RowMatrixMap = Eigen::Map<RowMatrix>;
using ConstRowMatrixMap = Eigen::Map<const RowMatrix>;

/// Apply an activation elementwise, in place.
void apply_activation(RowMatrix& Z, Activation a) {
    switch (a) {
        case Activation::Identity:
            break;
        case Activation::ReLU:
            Z = Z.cwiseMax(0.0);
            break;
        case Activation::Tanh:
            Z = Z.array().tanh().matrix();
            break;
        case Activation::Sigmoid:
            Z = (1.0 / (1.0 + (-Z.array()).exp())).matrix();
            break;
    }
}

/**
 * @brief Multiply ``dA`` by the activation derivative, given the *post*-activation
 *        values ``A``.
 *
 * Every supported activation has a derivative expressible in its own output,
 * which is why the forward pass caches activations rather than pre-activations.
 */
void apply_activation_grad(RowMatrix& dA, const RowMatrix& A, Activation a) {
    switch (a) {
        case Activation::Identity:
            break;
        case Activation::ReLU:
            dA = (A.array() > 0.0).select(dA, 0.0);
            break;
        case Activation::Tanh:
            dA.array() *= (1.0 - A.array().square());
            break;
        case Activation::Sigmoid:
            dA.array() *= A.array() * (1.0 - A.array());
            break;
    }
}

/// Read a JSON array of numbers into a vector, with a helpful error on misuse.
std::vector<double> as_double_vector(const json& j, const std::string& field) {
    if (!j.is_array())
        throw std::runtime_error("NeuralNet: field '" + field + "' must be an array");
    std::vector<double> v;
    v.reserve(j.size());
    for (const auto& e : j) {
        if (!e.is_number())
            throw std::runtime_error("NeuralNet: field '" + field + "' must contain only numbers");
        v.push_back(e.get<double>());
    }
    return v;
}

StandardScaler scaler_from_json(const json& j) {
    StandardScaler s;
    if (j.is_null() || j.empty()) return s;
    s.mean = as_double_vector(j.at("mean"), "scaler.mean");
    s.scale = as_double_vector(j.at("scale"), "scaler.scale");
    if (s.mean.size() != s.scale.size())
        throw std::runtime_error("NeuralNet: scaler mean/scale length mismatch");
    // A zero scale would divide by zero; sklearn maps zero-variance features to
    // a scale of 1, so mirror that rather than producing infinities.
    for (auto& v : s.scale)
        if (v == 0.0) v = 1.0;
    return s;
}

json scaler_to_json(const StandardScaler& s) {
    if (!s.active()) return json::object();
    return json{{"mean", s.mean}, {"scale", s.scale}};
}

} // namespace

// ---------------------------------------------------------------------------
// StandardScaler
// ---------------------------------------------------------------------------

void StandardScaler::fit(const double* X, int n_rows, int n_cols) {
    mean.assign(static_cast<size_t>(n_cols), 0.0);
    scale.assign(static_cast<size_t>(n_cols), 1.0);
    if (n_rows <= 0 || n_cols <= 0) return;

    ConstRowMatrixMap M(X, n_rows, n_cols);
    Eigen::VectorXd mu = M.colwise().mean();
    // Population standard deviation (ddof=0), matching sklearn's StandardScaler.
    Eigen::VectorXd var = (M.rowwise() - mu.transpose()).array().square().colwise().sum() / n_rows;
    for (int c = 0; c < n_cols; ++c) {
        mean[c] = mu[c];
        double sd = std::sqrt(var[c]);
        scale[c] = (sd == 0.0) ? 1.0 : sd;
    }
}

void StandardScaler::transform(std::vector<double>& v) const {
    if (!active()) return;
    if (v.size() != mean.size())
        throw std::runtime_error("StandardScaler::transform: length mismatch");
    for (size_t i = 0; i < v.size(); ++i) v[i] = (v[i] - mean[i]) / scale[i];
}

void StandardScaler::inverse_transform(std::vector<double>& v) const {
    if (!active()) return;
    if (v.size() != mean.size())
        throw std::runtime_error("StandardScaler::inverse_transform: length mismatch");
    for (size_t i = 0; i < v.size(); ++i) v[i] = v[i] * scale[i] + mean[i];
}

// ---------------------------------------------------------------------------
// Activation names
// ---------------------------------------------------------------------------

Activation activation_from_string(const std::string& name) {
    if (name == "identity" || name == "linear") return Activation::Identity;
    if (name == "relu") return Activation::ReLU;
    if (name == "tanh") return Activation::Tanh;
    if (name == "logistic" || name == "sigmoid") return Activation::Sigmoid;
    throw std::runtime_error("NeuralNet: unknown activation '" + name + "'");
}

std::string activation_to_string(Activation a) {
    switch (a) {
        case Activation::Identity: return "identity";
        case Activation::ReLU: return "relu";
        case Activation::Tanh: return "tanh";
        case Activation::Sigmoid: return "logistic";
    }
    return "identity";
}

// ---------------------------------------------------------------------------
// NeuralNet — construction and validation
// ---------------------------------------------------------------------------

NeuralNet::NeuralNet(std::vector<DenseLayer> layers,
                     StandardScaler x_scaler,
                     StandardScaler y_scaler)
    : layers_(std::move(layers)),
      x_scaler_(std::move(x_scaler)),
      y_scaler_(std::move(y_scaler)) {
    validate();
}

void NeuralNet::validate() const {
    if (layers_.empty())
        throw std::runtime_error("NeuralNet: model has no layers");

    for (size_t i = 0; i < layers_.size(); ++i) {
        const DenseLayer& l = layers_[i];
        if (l.n_in <= 0 || l.n_out <= 0)
            throw std::runtime_error("NeuralNet: layer " + std::to_string(i) +
                                     " has a non-positive dimension");
        const size_t want_w = static_cast<size_t>(l.n_in) * static_cast<size_t>(l.n_out);
        if (l.weight.size() != want_w)
            throw std::runtime_error(
                "NeuralNet: layer " + std::to_string(i) + " weight has " +
                std::to_string(l.weight.size()) + " entries, expected " +
                std::to_string(want_w) + " (n_out*n_in)");
        if (l.bias.size() != static_cast<size_t>(l.n_out))
            throw std::runtime_error(
                "NeuralNet: layer " + std::to_string(i) + " bias has " +
                std::to_string(l.bias.size()) + " entries, expected " +
                std::to_string(l.n_out));
        if (i + 1 < layers_.size() && l.n_out != layers_[i + 1].n_in)
            throw std::runtime_error(
                "NeuralNet: layer " + std::to_string(i) + " outputs " +
                std::to_string(l.n_out) + " but layer " + std::to_string(i + 1) +
                " expects " + std::to_string(layers_[i + 1].n_in));
    }

    if (x_scaler_.active() && x_scaler_.size() != n_inputs())
        throw std::runtime_error("NeuralNet: x_scaler length " +
                                 std::to_string(x_scaler_.size()) +
                                 " != n_inputs " + std::to_string(n_inputs()));
    if (y_scaler_.active() && y_scaler_.size() != n_outputs())
        throw std::runtime_error("NeuralNet: y_scaler length " +
                                 std::to_string(y_scaler_.size()) +
                                 " != n_outputs " + std::to_string(n_outputs()));
}

long long NeuralNet::n_parameters() const {
    long long n = 0;
    for (const auto& l : layers_)
        n += static_cast<long long>(l.weight.size() + l.bias.size());
    return n;
}

// ---------------------------------------------------------------------------
// NeuralNet — inference
// ---------------------------------------------------------------------------

std::vector<double> NeuralNet::predict_batch(const double* X, int n_rows, int n_cols) const {
    if (layers_.empty()) throw std::runtime_error("NeuralNet::predict: model has no layers");
    if (n_cols != n_inputs())
        throw std::runtime_error("NeuralNet::predict: got " + std::to_string(n_cols) +
                                 " features, expected " + std::to_string(n_inputs()));
    if (n_rows <= 0) return {};

    RowMatrix A = ConstRowMatrixMap(X, n_rows, n_cols);

    if (x_scaler_.active()) {
        Eigen::Map<const Eigen::VectorXd> mu(x_scaler_.mean.data(), n_cols);
        Eigen::Map<const Eigen::VectorXd> sd(x_scaler_.scale.data(), n_cols);
        A = (A.rowwise() - mu.transpose()).array().rowwise() / sd.transpose().array();
    }

    for (const DenseLayer& l : layers_) {
        ConstRowMatrixMap W(l.weight.data(), l.n_out, l.n_in);
        Eigen::Map<const Eigen::VectorXd> b(l.bias.data(), l.n_out);
        RowMatrix Z = (A * W.transpose()).rowwise() + b.transpose();
        apply_activation(Z, l.activation);
        A = std::move(Z);
    }

    if (y_scaler_.active()) {
        Eigen::Map<const Eigen::VectorXd> mu(y_scaler_.mean.data(), n_outputs());
        Eigen::Map<const Eigen::VectorXd> sd(y_scaler_.scale.data(), n_outputs());
        A = (A.array().rowwise() * sd.transpose().array()).matrix().rowwise() + mu.transpose();
    }

    std::vector<double> out(static_cast<size_t>(n_rows) * static_cast<size_t>(n_outputs()));
    RowMatrixMap(out.data(), n_rows, n_outputs()) = A;
    return out;
}

std::vector<double> NeuralNet::predict(const std::vector<double>& x) const {
    if (static_cast<int>(x.size()) != n_inputs())
        throw std::runtime_error("NeuralNet::predict: got " + std::to_string(x.size()) +
                                 " features, expected " + std::to_string(n_inputs()));
    return predict_batch(x.data(), 1, static_cast<int>(x.size()));
}

// ---------------------------------------------------------------------------
// NeuralNet — training (Adam + explicit backpropagation)
// ---------------------------------------------------------------------------

NeuralNet NeuralNet::train(
    const double* X, int n_samples, int n_features,
    const double* Y, int n_samples_y, int n_targets,
    const TrainOptions& opt
) {
    if (n_samples != n_samples_y)
        throw std::runtime_error("NeuralNet::train: X has " + std::to_string(n_samples) +
                                 " rows but Y has " + std::to_string(n_samples_y));
    if (n_samples <= 0 || n_features <= 0 || n_targets <= 0)
        throw std::runtime_error("NeuralNet::train: empty training set");
    if (opt.batch_size <= 0)
        throw std::runtime_error("NeuralNet::train: batch_size must be positive");

    // --- standardise inputs and targets, keeping the scalers with the model
    NeuralNet net;
    net.x_scaler_.fit(X, n_samples, n_features);
    net.y_scaler_.fit(Y, n_samples, n_targets);

    RowMatrix Xs = ConstRowMatrixMap(X, n_samples, n_features);
    RowMatrix Ys = ConstRowMatrixMap(Y, n_samples, n_targets);
    {
        Eigen::Map<const Eigen::VectorXd> xm(net.x_scaler_.mean.data(), n_features);
        Eigen::Map<const Eigen::VectorXd> xs(net.x_scaler_.scale.data(), n_features);
        Xs = (Xs.rowwise() - xm.transpose()).array().rowwise() / xs.transpose().array();
        Eigen::Map<const Eigen::VectorXd> ym(net.y_scaler_.mean.data(), n_targets);
        Eigen::Map<const Eigen::VectorXd> ys(net.y_scaler_.scale.data(), n_targets);
        Ys = (Ys.rowwise() - ym.transpose()).array().rowwise() / ys.transpose().array();
    }

    // --- layer geometry
    std::vector<int> dims;
    dims.push_back(n_features);
    for (int h : opt.hidden_layer_sizes)
        if (h > 0) dims.push_back(h);
    dims.push_back(n_targets);
    const size_t n_layers = dims.size() - 1;

    SimPcgRandom rng;
    rng.reset(static_cast<uint32_t>(opt.seed), 0, 0);

    // Glorot-uniform init, as used by scikit-learn's MLP.
    std::vector<RowMatrix> W(n_layers);
    std::vector<Eigen::VectorXd> b(n_layers);
    std::vector<Activation> acts(n_layers);
    for (size_t l = 0; l < n_layers; ++l) {
        const int n_in = dims[l], n_out = dims[l + 1];
        const double limit = std::sqrt(6.0 / (n_in + n_out));
        W[l].resize(n_out, n_in);
        for (int i = 0; i < n_out; ++i)
            for (int j = 0; j < n_in; ++j)
                W[l](i, j) = (2.0 * rng.random0i1e() - 1.0) * limit;
        b[l] = Eigen::VectorXd::Zero(n_out);
        // Hidden layers use the chosen nonlinearity; the output layer is linear
        // because this is a regressor.
        acts[l] = (l + 1 == n_layers) ? Activation::Identity : opt.activation;
    }

    // --- Adam state
    std::vector<RowMatrix> mW(n_layers), vW(n_layers);
    std::vector<Eigen::VectorXd> mb(n_layers), vb(n_layers);
    for (size_t l = 0; l < n_layers; ++l) {
        mW[l] = RowMatrix::Zero(W[l].rows(), W[l].cols());
        vW[l] = RowMatrix::Zero(W[l].rows(), W[l].cols());
        mb[l] = Eigen::VectorXd::Zero(b[l].size());
        vb[l] = Eigen::VectorXd::Zero(b[l].size());
    }

    // --- train/validation split for early stopping
    std::vector<int> order(n_samples);
    std::iota(order.begin(), order.end(), 0);
    for (int i = n_samples - 1; i > 0; --i)
        std::swap(order[i], order[rng.next_u32() % static_cast<uint32_t>(i + 1)]);

    int n_val = 0;
    if (opt.early_stopping && n_samples >= 10)
        n_val = std::max(1, static_cast<int>(n_samples * opt.validation_fraction));
    const int n_train = n_samples - n_val;
    if (n_train <= 0)
        throw std::runtime_error("NeuralNet::train: validation_fraction leaves no training rows");

    auto gather = [&](int from, int count, const RowMatrix& src) {
        RowMatrix out(count, src.cols());
        for (int i = 0; i < count; ++i) out.row(i) = src.row(order[from + i]);
        return out;
    };
    RowMatrix Xtr = gather(0, n_train, Xs), Ytr = gather(0, n_train, Ys);
    RowMatrix Xva = gather(n_train, n_val, Xs), Yva = gather(n_train, n_val, Ys);

    // Forward pass keeping post-activation values for the backward pass.
    auto forward = [&](const RowMatrix& input, std::vector<RowMatrix>& A) {
        A.resize(n_layers + 1);
        A[0] = input;
        for (size_t l = 0; l < n_layers; ++l) {
            RowMatrix Z = (A[l] * W[l].transpose()).rowwise() + b[l].transpose();
            apply_activation(Z, acts[l]);
            A[l + 1] = std::move(Z);
        }
    };
    auto mse = [&](const RowMatrix& in, const RowMatrix& target) {
        if (in.rows() == 0) return 0.0;
        std::vector<RowMatrix> A;
        forward(in, A);
        return (A[n_layers] - target).array().square().sum() / (2.0 * in.rows());
    };

    std::vector<int> batch_order(n_train);
    std::iota(batch_order.begin(), batch_order.end(), 0);

    double best_val = std::numeric_limits<double>::infinity();
    int n_bad = 0;
    long long adam_t = 0;
    std::vector<RowMatrix> best_W = W;
    std::vector<Eigen::VectorXd> best_b = b;

    for (int epoch = 0; epoch < opt.max_iter; ++epoch) {
        for (int i = n_train - 1; i > 0; --i)
            std::swap(batch_order[i], batch_order[rng.next_u32() % static_cast<uint32_t>(i + 1)]);

        double epoch_loss = 0.0;
        int n_batches = 0;

        for (int start = 0; start < n_train; start += opt.batch_size) {
            const int bs = std::min(opt.batch_size, n_train - start);
            RowMatrix xb(bs, n_features), yb(bs, n_targets);
            for (int i = 0; i < bs; ++i) {
                xb.row(i) = Xtr.row(batch_order[start + i]);
                yb.row(i) = Ytr.row(batch_order[start + i]);
            }

            std::vector<RowMatrix> A;
            forward(xb, A);

            // dL/dA for L = ||A - y||^2 / (2*bs)
            RowMatrix dA = (A[n_layers] - yb) / static_cast<double>(bs);
            epoch_loss += (A[n_layers] - yb).array().square().sum() / (2.0 * bs);
            ++n_batches;

            ++adam_t;
            const double bc1 = 1.0 - std::pow(opt.beta1, static_cast<double>(adam_t));
            const double bc2 = 1.0 - std::pow(opt.beta2, static_cast<double>(adam_t));

            for (size_t li = n_layers; li-- > 0;) {
                apply_activation_grad(dA, A[li + 1], acts[li]);
                // Backprop for a dense layer is exactly two GEMMs.
                RowMatrix gW = dA.transpose() * A[li];
                Eigen::VectorXd gb = dA.colwise().sum().transpose();
                if (li > 0) dA = dA * W[li];

                if (opt.alpha > 0.0) gW += opt.alpha * W[li];  // L2 on weights only

                mW[li] = opt.beta1 * mW[li] + (1.0 - opt.beta1) * gW;
                vW[li] = opt.beta2 * vW[li].array() + (1.0 - opt.beta2) * gW.array().square();
                W[li].array() -= opt.learning_rate * (mW[li].array() / bc1) /
                                 ((vW[li].array() / bc2).sqrt() + opt.epsilon);

                mb[li] = opt.beta1 * mb[li] + (1.0 - opt.beta1) * gb;
                vb[li] = opt.beta2 * vb[li].array() + (1.0 - opt.beta2) * gb.array().square();
                b[li].array() -= opt.learning_rate * (mb[li].array() / bc1) /
                                 ((vb[li].array() / bc2).sqrt() + opt.epsilon);
            }
        }

        net.loss_curve_.push_back(n_batches ? epoch_loss / n_batches : 0.0);

        if (n_val > 0) {
            const double vl = mse(Xva, Yva);
            net.validation_curve_.push_back(vl);
            if (vl < best_val - opt.tol) {
                best_val = vl;
                n_bad = 0;
                best_W = W;
                best_b = b;
            } else if (++n_bad >= opt.n_iter_no_change) {
                break;  // patience exhausted; keep the best weights seen
            }
        }
    }

    if (n_val > 0) {  // restore the best-validation weights, as sklearn does
        W = best_W;
        b = best_b;
    }

    net.layers_.resize(n_layers);
    for (size_t l = 0; l < n_layers; ++l) {
        DenseLayer& dl = net.layers_[l];
        dl.n_in = dims[l];
        dl.n_out = dims[l + 1];
        dl.activation = acts[l];
        dl.weight.resize(static_cast<size_t>(dl.n_in) * static_cast<size_t>(dl.n_out));
        RowMatrixMap(dl.weight.data(), dl.n_out, dl.n_in) = W[l];
        dl.bias.assign(b[l].data(), b[l].data() + b[l].size());
    }
    net.validate();
    return net;
}

// ---------------------------------------------------------------------------
// NeuralNet — JSON serialisation
// ---------------------------------------------------------------------------

NeuralNet NeuralNet::from_json_string(const std::string& text) {
    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("NeuralNet: invalid JSON: ") + e.what());
    }

    if (j.contains("format") && j.at("format").get<std::string>() != "tttrlib.neural_net")
        throw std::runtime_error("NeuralNet: unexpected format '" +
                                 j.at("format").get<std::string>() +
                                 "', expected 'tttrlib.neural_net'");
    if (!j.contains("layers"))
        throw std::runtime_error("NeuralNet: document has no 'layers' array");

    std::vector<DenseLayer> layers;
    for (const auto& lj : j.at("layers")) {
        DenseLayer l;
        l.n_in = lj.at("n_in").get<int>();
        l.n_out = lj.at("n_out").get<int>();
        l.weight = as_double_vector(lj.at("weight"), "layer.weight");
        l.bias = as_double_vector(lj.at("bias"), "layer.bias");
        l.activation = activation_from_string(
            lj.contains("activation") ? lj.at("activation").get<std::string>() : "relu");
        layers.push_back(std::move(l));
    }

    StandardScaler xs, ys;
    if (j.contains("x_scaler")) xs = scaler_from_json(j.at("x_scaler"));
    if (j.contains("y_scaler")) ys = scaler_from_json(j.at("y_scaler"));

    return NeuralNet(std::move(layers), std::move(xs), std::move(ys));  // validates
}

NeuralNet NeuralNet::from_json_file(const std::string& path) {
    std::ifstream fh(path);
    if (!fh) throw std::runtime_error("NeuralNet: cannot open '" + path + "'");
    std::stringstream ss;
    ss << fh.rdbuf();
    return from_json_string(ss.str());
}

std::string NeuralNet::to_json_string(int indent) const {
    json j;
    j["format"] = "tttrlib.neural_net";
    j["version"] = 1;
    j["x_scaler"] = scaler_to_json(x_scaler_);
    j["y_scaler"] = scaler_to_json(y_scaler_);

    json layers = json::array();
    for (const DenseLayer& l : layers_) {
        layers.push_back(json{
            {"n_in", l.n_in},
            {"n_out", l.n_out},
            {"activation", activation_to_string(l.activation)},
            {"weight", l.weight},
            {"bias", l.bias},
        });
    }
    j["layers"] = std::move(layers);
    return j.dump(indent);
}

void NeuralNet::to_json_file(const std::string& path, int indent) const {
    std::ofstream fh(path);
    if (!fh) throw std::runtime_error("NeuralNet: cannot write '" + path + "'");
    fh << to_json_string(indent);
}

} // namespace tttrlib
