// SPDX-License-Identifier: BSD-3-Clause
#include "NeuralNet.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>

#include "Mat.h"
#include "SimPcgRandom.h"
#include "nlohmann/json.hpp"

namespace tttrlib {

namespace {

using json = nlohmann::json;

// The dense linear algebra runs through Mat.h (this directory) rather than
// Eigen.  Mat.h is header-only, std-only C++17, and its GEMM is cache-friendly
// ikj nesting with OpenMP — the matrix product is the only O(n^3) kernel in
// training, and everything else is element-wise or a reduction.

/// Apply an activation elementwise, in place.
void apply_activation(Mat& Z, Activation a) {
    switch (a) {
        case Activation::Identity:
            break;
        case Activation::ReLU:
            relu_inplace(Z);
            break;
        case Activation::Tanh:
            tanh_inplace(Z);
            break;
        case Activation::Sigmoid:
            sigmoid_inplace(Z);
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
void apply_activation_grad(Mat& dA, const Mat& A, Activation a) {
    switch (a) {
        case Activation::Identity:
            break;
        case Activation::ReLU:
            dA %= (A > 0.0);
            break;
        case Activation::Tanh:
            dA %= (1.0 - square(A));
            break;
        case Activation::Sigmoid:
            dA %= (A % (1.0 - A));
            break;
    }
}

/// Fused Adam update: one pass over the parameter array, zero temporaries.
/// The expression-template chain ``m = b1*m + (1-b1)*g; v = b2*v + (1-b2)*g^2;
/// p -= lr*(m/bc1)/(sqrt(v/bc2)+eps)`` would allocate five intermediate matrices
/// per layer per batch; this lambda does the whole thing in a single pass.
void adam_step(Mat& p, const Mat& g, Mat& m, Mat& v,
               double lr, double beta1, double beta2,
               double bc1, double bc2, double eps) {
    const size_t n = static_cast<size_t>(p.n_elem());
    double* pp = p.memptr();
    const double* gp = g.memptr();
    double* mp = m.memptr();
    double* vp = v.memptr();
#ifndef _MSC_VER  /* MSVC: C7660 without -openmp:experimental */
    #pragma omp simd
#endif
    for (size_t i = 0; i < n; ++i) {
        mp[i] = beta1 * mp[i] + (1.0 - beta1) * gp[i];
        vp[i] = beta2 * vp[i] + (1.0 - beta2) * gp[i] * gp[i];
        pp[i] -= lr * (mp[i] / bc1) / (std::sqrt(vp[i] / bc2) + eps);
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

/// Copy ``count`` rows from ``src`` selected by ``order[from .. from+count)``
/// into a new row-major matrix.
Mat gather_rows(const Mat& src, const std::vector<int>& order,
                int from, int count) {
    Mat out(count, src.n_cols());
    const int nc = src.n_cols();
    for (int i = 0; i < count; ++i) {
        const double* srow = src.memptr() +
            static_cast<size_t>(order[from + i]) * nc;
        std::copy(srow, srow + nc,
                  out.memptr() + static_cast<size_t>(i) * nc);
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// StandardScaler
// ---------------------------------------------------------------------------

void StandardScaler::fit(const double* X, int n_rows, int n_cols) {
    mean.assign(static_cast<size_t>(n_cols), 0.0);
    scale.assign(static_cast<size_t>(n_cols), 1.0);
    if (n_rows <= 0 || n_cols <= 0) return;

    // two-pass column statistics: avoids naming the Mat.h free function
    // ``mean()`` here, which collides with this struct's ``mean`` member.
    for (int i = 0; i < n_rows; ++i)
        for (int j = 0; j < n_cols; ++j)
            mean[static_cast<size_t>(j)] += X[static_cast<size_t>(i) * n_cols + j];
    for (int j = 0; j < n_cols; ++j)
        mean[static_cast<size_t>(j)] /= n_rows;

    // Population standard deviation (ddof=0), matching sklearn's StandardScaler.
    for (int j = 0; j < n_cols; ++j) {
        double var = 0.0;
        for (int i = 0; i < n_rows; ++i) {
            double d = X[static_cast<size_t>(i) * n_cols + j] - mean[static_cast<size_t>(j)];
            var += d * d;
        }
        double sd = std::sqrt(var / static_cast<double>(n_rows));
        scale[static_cast<size_t>(j)] = (sd == 0.0) ? 1.0 : sd;
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

    Mat A(n_rows, n_cols, X);

    if (x_scaler_.active()) {
        Mat mu(1, n_cols, x_scaler_.mean.data());
        Mat sd(1, n_cols, x_scaler_.scale.data());
        A.each_row() -= mu;
        A.each_row() /= sd;
    }

    for (const DenseLayer& l : layers_) {
        // weight is row-major n_out x n_in; A * W.t() is the forward GEMM.
        Mat W(l.n_out, l.n_in, l.weight.data());
        Mat b(1, l.n_out, l.bias.data());
        Mat Z = A * W.t();
        Z.each_row() += b;
        apply_activation(Z, l.activation);
        A = std::move(Z);
    }

    if (y_scaler_.active()) {
        Mat mu(1, n_outputs(), y_scaler_.mean.data());
        Mat sd(1, n_outputs(), y_scaler_.scale.data());
        A.each_row() %= sd;
        A.each_row() += mu;
    }

    return std::vector<double>(A.memptr(), A.memptr() + A.n_elem());
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

    Mat Xs(n_samples, n_features, X);
    Mat Ys(n_samples, n_targets, Y);
    {
        Mat xm(1, n_features, net.x_scaler_.mean.data());
        Mat xs(1, n_features, net.x_scaler_.scale.data());
        Xs.each_row() -= xm;
        Xs.each_row() /= xs;
        Mat ym(1, n_targets, net.y_scaler_.mean.data());
        Mat ys(1, n_targets, net.y_scaler_.scale.data());
        Ys.each_row() -= ym;
        Ys.each_row() /= ys;
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
    std::vector<Mat> W(n_layers);
    std::vector<Mat> b(n_layers);      // bias: n_out x 1 column vector
    std::vector<Activation> acts(n_layers);
    for (size_t l = 0; l < n_layers; ++l) {
        const int n_in = dims[l], n_out = dims[l + 1];
        const double limit = std::sqrt(6.0 / (n_in + n_out));
        W[l].set_size(n_out, n_in);
        for (int i = 0; i < n_out; ++i)
            for (int j = 0; j < n_in; ++j)
                W[l](i, j) = (2.0 * rng.random0i1e() - 1.0) * limit;
        b[l].set_size(n_out, 1);
        b[l].zeros();
        // Hidden layers use the chosen nonlinearity; the output layer is linear
        // because this is a regressor.
        acts[l] = (l + 1 == n_layers) ? Activation::Identity : opt.activation;
    }

    // --- Adam state
    std::vector<Mat> mW(n_layers), vW(n_layers);
    std::vector<Mat> mb(n_layers), vb(n_layers);
    for (size_t l = 0; l < n_layers; ++l) {
        mW[l].set_size(W[l].n_rows(), W[l].n_cols()); mW[l].zeros();
        vW[l].set_size(W[l].n_rows(), W[l].n_cols()); vW[l].zeros();
        mb[l].set_size(b[l].n_rows(), 1); mb[l].zeros();
        vb[l].set_size(b[l].n_rows(), 1); vb[l].zeros();
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

    Mat Xtr = gather_rows(Xs, order, 0, n_train);
    Mat Ytr = gather_rows(Ys, order, 0, n_train);
    Mat Xva = gather_rows(Xs, order, n_train, n_val);
    Mat Yva = gather_rows(Ys, order, n_train, n_val);

    // Forward pass keeping post-activation values for the backward pass.
    auto forward = [&](const Mat& input, std::vector<Mat>& A) {
        A.resize(n_layers + 1);
        A[0] = input;
        for (size_t l = 0; l < n_layers; ++l) {
            Mat Z = A[l] * W[l].t();
            Z.each_row() += b[l].t();
            apply_activation(Z, acts[l]);
            A[l + 1] = std::move(Z);
        }
    };
    auto mse = [&](const Mat& in, const Mat& target) {
        if (in.n_rows() == 0) return 0.0;
        std::vector<Mat> A;
        forward(in, A);
        return accu(square(A[n_layers] - target)) / (2.0 * in.n_rows());
    };

    std::vector<int> batch_order(n_train);
    std::iota(batch_order.begin(), batch_order.end(), 0);

    double best_val = std::numeric_limits<double>::infinity();
    int n_bad = 0;
    long long adam_t = 0;
    std::vector<Mat> best_W = W;
    std::vector<Mat> best_b = b;

    for (int epoch = 0; epoch < opt.max_iter; ++epoch) {
        for (int i = n_train - 1; i > 0; --i)
            std::swap(batch_order[i], batch_order[rng.next_u32() % static_cast<uint32_t>(i + 1)]);

        double epoch_loss = 0.0;
        int n_batches = 0;

        for (int start = 0; start < n_train; start += opt.batch_size) {
            const int bs = std::min(opt.batch_size, n_train - start);
            Mat xb = gather_rows(Xtr, batch_order, start, bs);
            Mat yb = gather_rows(Ytr, batch_order, start, bs);

            std::vector<Mat> A;
            forward(xb, A);

            // dL/dA for L = ||A - y||^2 / (2*bs)
            Mat dA = (A[n_layers] - yb) / static_cast<double>(bs);
            epoch_loss += accu(square(A[n_layers] - yb)) / (2.0 * bs);
            ++n_batches;

            ++adam_t;
            const double bc1 = 1.0 - std::pow(opt.beta1, static_cast<double>(adam_t));
            const double bc2 = 1.0 - std::pow(opt.beta2, static_cast<double>(adam_t));

            for (size_t li = n_layers; li-- > 0;) {
                apply_activation_grad(dA, A[li + 1], acts[li]);
                // Backprop for a dense layer is exactly two GEMMs.
                Mat gW = dA.t() * A[li];
                Mat gb = sum(dA, 0).t();
                if (li > 0) dA = dA * W[li];

                if (opt.alpha > 0.0) gW += opt.alpha * W[li];  // L2 on weights only

                adam_step(W[li], gW, mW[li], vW[li],
                          opt.learning_rate, opt.beta1, opt.beta2,
                          bc1, bc2, opt.epsilon);
                adam_step(b[li], gb, mb[li], vb[li],
                          opt.learning_rate, opt.beta1, opt.beta2,
                          bc1, bc2, opt.epsilon);
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
        dl.weight.assign(W[l].memptr(), W[l].memptr() + W[l].n_elem());
        dl.bias.assign(b[l].memptr(), b[l].memptr() + b[l].n_elem());
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
