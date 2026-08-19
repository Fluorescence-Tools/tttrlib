// SPDX-License-Identifier: BSD-3-Clause
#include "NeuralNet.h"
#include "Registry.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <new>
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
//
// The forward/backward passes themselves live in MlpCore.h and take the GEMM
// as a policy: here it is Mat.h's SIMD kernels, in the copy imp.bff vendors it
// is the portable loop. Same arithmetic, same operation order, so a network
// trained here evaluates identically there.
struct MatGemm {
    static void nn(int M, int N, int K, const double* A, const double* B, double* C) {
        mat_detail::gemm_nn(M, N, K, A, B, C);
    }
    static void nt(int M, int N, int K, const double* A, const double* B, double* C) {
        mat_detail::gemm_nt(M, N, K, A, B, C);
    }
    static void tn(int M, int N, int K, const double* A, const double* B, double* C) {
        mat_detail::gemm_tn(M, N, K, A, B, C);
    }
};

/// Fused Adam update: one pass over the flat parameter array, zero temporaries.
/// The expression-template chain ``m = b1*m + (1-b1)*g; v = b2*v + (1-b2)*g^2;
/// p -= lr*(m/bc1)/(sqrt(v/bc2)+eps)`` would allocate five intermediate arrays
/// per step; this does the whole thing in a single pass.
void adam_step(double* pp, const double* gp, double* mp, double* vp, size_t n,
               double lr, double beta1, double beta2,
               double bc1, double bc2, double eps) {
#ifndef _MSC_VER  /* MSVC: C7660 without -openmp:experimental */
    #pragma omp simd
#endif
    for (size_t i = 0; i < n; ++i) {
        mp[i] = beta1 * mp[i] + (1.0 - beta1) * gp[i];
        vp[i] = beta2 * vp[i] + (1.0 - beta2) * gp[i] * gp[i];
        pp[i] -= lr * (mp[i] / bc1) / (std::sqrt(vp[i] / bc2) + eps);
    }
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
// NeuralNet — construction and validation
// ---------------------------------------------------------------------------

NeuralNet::NeuralNet(std::vector<DenseLayer> layers,
                     StandardScaler x_scaler,
                     StandardScaler y_scaler) {
    model_.layers = std::move(layers);
    model_.x_scaler = std::move(x_scaler);
    model_.y_scaler = std::move(y_scaler);
    validate();
}

void NeuralNet::validate() const { model_.validate(); }

long long NeuralNet::n_parameters() const {
    return static_cast<long long>(mlpcore::n_parameters(model_.layers));
}

// ---------------------------------------------------------------------------
// NeuralNet — inference
// ---------------------------------------------------------------------------

std::vector<double> NeuralNet::predict_batch(const double* X, int n_rows, int n_cols) const {
    if (model_.layers.empty()) throw std::runtime_error("NeuralNet::predict: model has no layers");
    if (n_cols != n_inputs())
        throw std::runtime_error("NeuralNet::predict: got " + std::to_string(n_cols) +
                                 " features, expected " + std::to_string(n_inputs()));
    if (n_rows <= 0) return {};
    std::vector<double> y, d1, d2;
    mlpcore::model_predict<MatGemm>(model_, X, n_rows, 0, nullptr, y, d1, d2);
    return y;
}

std::vector<double> NeuralNet::predict(const std::vector<double>& x) const {
    if (static_cast<int>(x.size()) != n_inputs())
        throw std::runtime_error("NeuralNet::predict: got " + std::to_string(x.size()) +
                                 " features, expected " + std::to_string(n_inputs()));
    return predict_batch(x.data(), 1, static_cast<int>(x.size()));
}

// ---------------------------------------------------------------------------
// NeuralNet — derivatives (MlpCore.h)
// ---------------------------------------------------------------------------

namespace {

void check_batch(const char* who, int n_rows, int n_cols, int want_cols,
                 const char* what) {
    if (n_cols != want_cols)
        throw std::runtime_error(std::string("NeuralNet::") + who + ": " + what + " has " +
                                 std::to_string(n_cols) + " columns, expected " +
                                 std::to_string(want_cols));
    if (n_rows < 0)
        throw std::runtime_error(std::string("NeuralNet::") + who + ": negative row count");
}

}  // namespace

NeuralNet::Derivatives NeuralNet::predict_derivatives(
        const double* X, int n_rows, int n_cols,
        const double* V, int n_rows_v, int n_cols_v, int order) const {
    if (model_.layers.empty()) throw std::runtime_error("NeuralNet::predict_derivatives: model has no layers");
    if (order < 0 || order > 2)
        throw std::runtime_error("NeuralNet::predict_derivatives: order must be 0, 1 or 2");
    check_batch("predict_derivatives", n_rows, n_cols, n_inputs(), "X");
    if (order >= 1) {
        check_batch("predict_derivatives", n_rows_v, n_cols_v, n_inputs(), "V");
        if (n_rows_v != n_rows)
            throw std::runtime_error("NeuralNet::predict_derivatives: X has " + std::to_string(n_rows) +
                                     " rows but V has " + std::to_string(n_rows_v));
    }
    Derivatives out;
    mlpcore::model_predict<MatGemm>(model_, X, n_rows, order, order >= 1 ? V : nullptr,
                                    out.y, out.dy_dv, out.d2y_dv2);
    return out;
}

std::vector<double> NeuralNet::jacobian(const std::vector<double>& x) const {
    const int n_in = n_inputs(), n_out = n_outputs();
    if (static_cast<int>(x.size()) != n_in)
        throw std::runtime_error("NeuralNet::jacobian: got " + std::to_string(x.size()) +
                                 " features, expected " + std::to_string(n_in));
    // One augmented row per input direction: row j carries x and e_j.
    std::vector<double> X(static_cast<size_t>(n_in) * n_in), V(static_cast<size_t>(n_in) * n_in, 0.0);
    for (int j = 0; j < n_in; ++j) {
        std::copy(x.begin(), x.end(), X.begin() + static_cast<std::ptrdiff_t>(j) * n_in);
        V[static_cast<size_t>(j) * n_in + j] = 1.0;
    }
    Derivatives d = predict_derivatives(X.data(), n_in, n_in, V.data(), n_in, n_in, 1);
    // d.dy_dv is n_in x n_out (row j = column j of J); transpose to n_out x n_in.
    std::vector<double> J(static_cast<size_t>(n_out) * n_in);
    for (int j = 0; j < n_in; ++j)
        for (int k = 0; k < n_out; ++k)
            J[static_cast<size_t>(k) * n_in + j] = d.dy_dv[static_cast<size_t>(j) * n_out + k];
    return J;
}

std::vector<double> NeuralNet::hessian(const std::vector<double>& x, int output) const {
    const int n_in = n_inputs(), n_out = n_outputs();
    if (static_cast<int>(x.size()) != n_in)
        throw std::runtime_error("NeuralNet::hessian: got " + std::to_string(x.size()) +
                                 " features, expected " + std::to_string(n_in));
    if (output < 0 || output >= n_out)
        throw std::runtime_error("NeuralNet::hessian: output index out of range");
    // Polarisation: H_ij = (q(e_i+e_j) - q(e_i) - q(e_j)) / 2 with q(v) = v^T H v.
    // Rows 0..n_in-1 are the unit directions, then one row per pair i<j.
    const int n_pairs = n_in * (n_in - 1) / 2;
    const int rows = n_in + n_pairs;
    std::vector<double> X(static_cast<size_t>(rows) * n_in), V(static_cast<size_t>(rows) * n_in, 0.0);
    for (int r = 0; r < rows; ++r)
        std::copy(x.begin(), x.end(), X.begin() + static_cast<std::ptrdiff_t>(r) * n_in);
    for (int j = 0; j < n_in; ++j) V[static_cast<size_t>(j) * n_in + j] = 1.0;
    {
        int r = n_in;
        for (int i = 0; i < n_in; ++i)
            for (int j = i + 1; j < n_in; ++j, ++r) {
                V[static_cast<size_t>(r) * n_in + i] = 1.0;
                V[static_cast<size_t>(r) * n_in + j] = 1.0;
            }
    }
    Derivatives d = predict_derivatives(X.data(), rows, n_in, V.data(), rows, n_in, 2);
    auto q = [&](int r) { return d.d2y_dv2[static_cast<size_t>(r) * n_out + output]; };
    std::vector<double> H(static_cast<size_t>(n_in) * n_in);
    for (int i = 0; i < n_in; ++i) H[static_cast<size_t>(i) * n_in + i] = q(i);
    {
        int r = n_in;
        for (int i = 0; i < n_in; ++i)
            for (int j = i + 1; j < n_in; ++j, ++r) {
                const double h = 0.5 * (q(r) - q(i) - q(j));
                H[static_cast<size_t>(i) * n_in + j] = h;
                H[static_cast<size_t>(j) * n_in + i] = h;
            }
    }
    return H;
}

NeuralNet::Backward NeuralNet::backward(const double* X, int n_rows, int n_cols,
                                        const double* dY, int n_rows_y, int n_cols_y) const {
    return backward_derivatives(X, n_rows, n_cols, nullptr, 0, n_inputs(),
                                dY, n_rows_y, n_cols_y, nullptr, 0, 0, nullptr, 0, 0);
}

NeuralNet::Backward NeuralNet::backward_derivatives(
        const double* X, int n_rows, int n_cols,
        const double* V, int n_rows_v, int n_cols_v,
        const double* dY, int n_rows_y, int n_cols_y,
        const double* dY1, int n_rows_y1, int n_cols_y1,
        const double* dY2, int n_rows_y2, int n_cols_y2) const {
    if (model_.layers.empty()) throw std::runtime_error("NeuralNet::backward: model has no layers");
    const int n_in = n_inputs(), n_out = n_outputs();
    check_batch("backward", n_rows, n_cols, n_in, "X");
    check_batch("backward", n_rows_y, n_cols_y, n_out, "dY");
    if (n_rows_y != n_rows)
        throw std::runtime_error("NeuralNet::backward: X has " + std::to_string(n_rows) +
                                 " rows but dY has " + std::to_string(n_rows_y));
    const bool have1 = dY1 != nullptr && n_rows_y1 > 0;
    const bool have2 = dY2 != nullptr && n_rows_y2 > 0;
    const int order = have2 ? 2 : (have1 ? 1 : 0);
    if (have1) {
        check_batch("backward", n_rows_y1, n_cols_y1, n_out, "dY1");
        if (n_rows_y1 != n_rows) throw std::runtime_error("NeuralNet::backward: dY1 row count differs from X");
    }
    if (have2) {
        check_batch("backward", n_rows_y2, n_cols_y2, n_out, "dY2");
        if (n_rows_y2 != n_rows) throw std::runtime_error("NeuralNet::backward: dY2 row count differs from X");
    }
    if (order >= 1) {
        check_batch("backward", n_rows_v, n_cols_v, n_in, "V");
        if (n_rows_v != n_rows || V == nullptr)
            throw std::runtime_error("NeuralNet::backward: derivative adjoints given but V is missing or has the wrong row count");
    }
    Backward out;
    mlpcore::model_backward<MatGemm>(model_, X, n_rows, order >= 1 ? V : nullptr,
                                     dY, have1 ? dY1 : nullptr, have2 ? dY2 : nullptr,
                                     out.dparams, out.dx, out.dv);
    return out;
}

namespace {

/// Hand a vector to the caller as a malloc-ed buffer (numpy takes ownership).
void to_argout(const std::vector<double>& v, double** out, int* n) {
    const size_t bytes = std::max<size_t>(v.size(), 1) * sizeof(double);
    auto* buf = static_cast<double*>(std::malloc(bytes));
    if (!buf) throw std::bad_alloc();
    std::copy(v.begin(), v.end(), buf);
    *out = buf;
    *n = static_cast<int>(v.size());
}

}  // namespace

void NeuralNet::predict_batch_out(const double* X, int n_rows, int n_cols,
                                  double** out_y, int* n_out_y) const {
    to_argout(predict_batch(X, n_rows, n_cols), out_y, n_out_y);
}

void NeuralNet::predict_derivatives_out(const double* X, int n_rows, int n_cols,
                                        const double* V, int n_rows_v, int n_cols_v, int order,
                                        double** out_y, int* n_out_y,
                                        double** out_dy_dv, int* n_out_dy_dv,
                                        double** out_d2y_dv2, int* n_out_d2y_dv2) const {
    Derivatives d = predict_derivatives(X, n_rows, n_cols, V, n_rows_v, n_cols_v, order);
    to_argout(d.y, out_y, n_out_y);
    to_argout(d.dy_dv, out_dy_dv, n_out_dy_dv);
    to_argout(d.d2y_dv2, out_d2y_dv2, n_out_d2y_dv2);
}

void NeuralNet::backward_derivatives_out(const double* X, int n_rows, int n_cols,
                                         const double* V, int n_rows_v, int n_cols_v,
                                         const double* dY, int n_rows_y, int n_cols_y,
                                         const double* dY1, int n_rows_y1, int n_cols_y1,
                                         const double* dY2, int n_rows_y2, int n_cols_y2,
                                         double** out_dparams, int* n_out_dparams,
                                         double** out_dx, int* n_out_dx,
                                         double** out_dv, int* n_out_dv) const {
    Backward b = backward_derivatives(X, n_rows, n_cols, V, n_rows_v, n_cols_v,
                                      dY, n_rows_y, n_cols_y, dY1, n_rows_y1, n_cols_y1,
                                      dY2, n_rows_y2, n_cols_y2);
    to_argout(b.dparams, out_dparams, n_out_dparams);
    to_argout(b.dx, out_dx, n_out_dx);
    to_argout(b.dv, out_dv, n_out_dv);
}

void NeuralNet::get_parameters_out(double** out_params, int* n_out_params) const {
    to_argout(get_parameters(), out_params, n_out_params);
}

void NeuralNet::set_parameters(const double* params, int n_params) {
    if (model_.layers.empty()) throw std::runtime_error("NeuralNet::set_parameters: model has no layers");
    if (n_params < 0) throw std::runtime_error("NeuralNet::set_parameters: negative length");
    mlpcore::unflatten(model_.layers, params, static_cast<size_t>(n_params));
}

std::vector<double> NeuralNet::get_parameters() const {
    std::vector<double> p;
    mlpcore::flatten(model_.layers, p);
    return p;
}

void NeuralNet::set_parameters(const std::vector<double>& params) {
    if (model_.layers.empty()) throw std::runtime_error("NeuralNet::set_parameters: model has no layers");
    mlpcore::unflatten(model_.layers, params.data(), params.size());
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
    net.model_.x_scaler.fit(X, n_samples, n_features);
    net.model_.y_scaler.fit(Y, n_samples, n_targets);

    Mat Xs(n_samples, n_features, X);
    Mat Ys(n_samples, n_targets, Y);
    {
        Mat xm(1, n_features, net.model_.x_scaler.mean.data());
        Mat xs(1, n_features, net.model_.x_scaler.scale.data());
        Xs.each_row() -= xm;
        Xs.each_row() /= xs;
        Mat ym(1, n_targets, net.model_.y_scaler.mean.data());
        Mat ys(1, n_targets, net.model_.y_scaler.scale.data());
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

    // Glorot-uniform init, as used by scikit-learn's MLP. Hidden layers use the
    // chosen nonlinearity; the output layer is linear because this is a
    // regressor.
    net.model_.layers.resize(n_layers);
    for (size_t l = 0; l < n_layers; ++l) {
        DenseLayer& dl = net.model_.layers[l];
        const int n_in = dims[l], n_out = dims[l + 1];
        const double limit = std::sqrt(6.0 / (n_in + n_out));
        dl.n_in = n_in;
        dl.n_out = n_out;
        dl.activation = (l + 1 == n_layers) ? Activation::Identity : opt.activation;
        dl.weight.resize(static_cast<size_t>(n_out) * n_in);
        for (int i = 0; i < n_out; ++i)
            for (int j = 0; j < n_in; ++j)
                dl.weight[static_cast<size_t>(i) * n_in + j] = (2.0 * rng.random0i1e() - 1.0) * limit;
        dl.bias.assign(static_cast<size_t>(n_out), 0.0);
    }

    // --- flat parameter vector, its gradient, and the Adam state
    std::vector<double> params;
    mlpcore::flatten(net.model_.layers, params);
    const size_t n_params = params.size();
    std::vector<double> grad(n_params, 0.0), adam_m(n_params, 0.0), adam_v(n_params, 0.0);
    // Which flat entries are biases (no L2 on those).
    std::vector<char> is_bias(n_params, 0);
    {
        size_t p = 0;
        for (const DenseLayer& dl : net.model_.layers) {
            p += dl.weight.size();
            std::fill(is_bias.begin() + static_cast<std::ptrdiff_t>(p),
                      is_bias.begin() + static_cast<std::ptrdiff_t>(p + dl.bias.size()), 1);
            p += dl.bias.size();
        }
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

    // Half mean squared error of the network on a batch, ``||y - t||^2 / (2 n)``.
    mlpcore::Workspace ws;
    auto mse = [&](const Mat& in, const Mat& target) {
        if (in.n_rows() == 0) return 0.0;
        mlpcore::forward<MatGemm>(net.model_.layers, in.memptr(), in.n_rows(), ws, 0);
        const std::vector<double>& y = ws.output();
        double acc = 0.0;
        const double* t = target.memptr();
        for (size_t i = 0; i < y.size(); ++i) {
            const double d = y[i] - t[i];
            acc += d * d;
        }
        return acc / (2.0 * in.n_rows());
    };

    std::vector<int> batch_order(n_train);
    std::iota(batch_order.begin(), batch_order.end(), 0);

    double best_val = std::numeric_limits<double>::infinity();
    int n_bad = 0;
    long long adam_t = 0;
    std::vector<double> best_params = params;
    std::vector<double> dY;

    for (int epoch = 0; epoch < opt.max_iter; ++epoch) {
        for (int i = n_train - 1; i > 0; --i)
            std::swap(batch_order[i], batch_order[rng.next_u32() % static_cast<uint32_t>(i + 1)]);

        double epoch_loss = 0.0;
        int n_batches = 0;

        for (int start = 0; start < n_train; start += opt.batch_size) {
            const int bs = std::min(opt.batch_size, n_train - start);
            Mat xb = gather_rows(Xtr, batch_order, start, bs);
            Mat yb = gather_rows(Ytr, batch_order, start, bs);

            mlpcore::forward<MatGemm>(net.model_.layers, xb.memptr(), bs, ws, 0);
            const std::vector<double>& y = ws.output();

            // dL/dy for L = ||y - t||^2 / (2*bs), and the loss itself
            dY.resize(y.size());
            double batch_loss = 0.0;
            for (size_t i = 0; i < y.size(); ++i) {
                const double d = y[i] - yb.memptr()[i];
                dY[i] = d / static_cast<double>(bs);
                batch_loss += d * d;
            }
            epoch_loss += batch_loss / (2.0 * bs);
            ++n_batches;

            std::fill(grad.begin(), grad.end(), 0.0);
            mlpcore::backward<MatGemm>(net.model_.layers, ws, dY.data(), nullptr, nullptr, grad.data());

            if (opt.alpha > 0.0)  // L2 on weights only
                for (size_t i = 0; i < n_params; ++i)
                    if (!is_bias[i]) grad[i] += opt.alpha * params[i];

            ++adam_t;
            const double bc1 = 1.0 - std::pow(opt.beta1, static_cast<double>(adam_t));
            const double bc2 = 1.0 - std::pow(opt.beta2, static_cast<double>(adam_t));
            adam_step(params.data(), grad.data(), adam_m.data(), adam_v.data(), n_params,
                      opt.learning_rate, opt.beta1, opt.beta2, bc1, bc2, opt.epsilon);
            mlpcore::unflatten(net.model_.layers, params.data(), n_params);
        }

        net.loss_curve_.push_back(n_batches ? epoch_loss / n_batches : 0.0);

        if (n_val > 0) {
            const double vl = mse(Xva, Yva);
            net.validation_curve_.push_back(vl);
            if (vl < best_val - opt.tol) {
                best_val = vl;
                n_bad = 0;
                best_params = params;
            } else if (++n_bad >= opt.n_iter_no_change) {
                break;  // patience exhausted; keep the best weights seen
            }
        }
    }

    if (n_val > 0)  // restore the best-validation weights, as sklearn does
        mlpcore::unflatten(net.model_.layers, best_params.data(), best_params.size());

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
    NeuralNet net;
    net.model_ = mlpcore::model_from_json(j);  // validates
    return net;
}

namespace {
std::string read_file_bytes(const std::string& path, const char* who) {
    std::ifstream fh(path, std::ios::binary);
    if (!fh) throw std::runtime_error(std::string("NeuralNet::") + who + ": cannot open '" + path + "'");
    std::stringstream ss;
    ss << fh.rdbuf();
    return ss.str();
}
}  // namespace

NeuralNet NeuralNet::from_onnx_file(const std::string& path) {
    const std::string bytes = read_file_bytes(path, "from_onnx_file");
    NeuralNet net;
    net.model_ = mlpcore::model_from_onnx(bytes);  // validates
    return net;
}

NeuralNet NeuralNet::from_safetensors_file(const std::string& path, const std::string& hidden_activation) {
    const std::string bytes = read_file_bytes(path, "from_safetensors_file");
    NeuralNet net;
    net.model_ = mlpcore::model_from_safetensors<json>(
        reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), hidden_activation);
    return net;
}

NeuralNet NeuralNet::from_json_file(const std::string& path) {
    std::ifstream fh(path);
    if (!fh) throw std::runtime_error("NeuralNet: cannot open '" + path + "'");
    std::stringstream ss;
    ss << fh.rdbuf();
    return from_json_string(ss.str());
}

std::string NeuralNet::to_json_string(int indent) const {
    return mlpcore::model_to_json<json>(model_).dump(indent);
}

void NeuralNet::to_json_file(const std::string& path, int indent) const {
    std::ofstream fh(path);
    if (!fh) throw std::runtime_error("NeuralNet: cannot write '" + path + "'");
    fh << to_json_string(indent);
}

} // namespace tttrlib

// ---- registry entries (Registry.h, core): declared next to the code, registered
// when this library loads; a static consumer links the archive whole.
namespace {
const char* const kNeuralNetEntry = R"JSON({
  "name": "neural_net",
  "label": "Feed-forward neural network (dense layers, training, derivatives, standard scaler)",
  "summary": "A small dense network with sklearn's activations plus softplus / silu / sin, mini-batch training, a StandardScaler, and exact derivatives -- of the loss with respect to the weights for any caller-supplied loss, and of the outputs with respect to the inputs to second order -- for surrogates and physics-informed models inside the library.",
  "description": "Dense layers with relu / tanh / logistic / identity activations (sklearn's names) and the smooth softplus / silu / sin, backpropagation training with the usual options, JSON round trip, and a scaler; validated against sklearn's MLP on the same weights. The same forward and backward passes are exposed as a differentiable building block: backward() maps the adjoint of the outputs to the adjoint of weights and inputs; predict_derivatives() / backward_derivatives() carry a directional Taylor expansion of the input to second order so a loss on dy/dx and d2y/dx2 (a PDE residual) can be trained through; jacobian() / hessian() give the full input derivatives of one sample; get_parameters() / set_parameters() expose the flat parameter vector for an outside optimiser such as L-BFGS. The kernels are header-only in MlpCore.h and shared verbatim with imp.bff. Models trained elsewhere load from ONNX (the MLP subset, read without an ONNX library) or safetensors.",
  "operation_type": "model_fitting",
  "method": "predict",
  "params_schema": {
    "type": "object",
    "properties": {
      "hidden": {
        "type": "array",
        "items": {
          "type": "integer"
        },
        "title": "Hidden layer sizes"
      },
      "activation": {
        "type": "string",
        "title": "Activation",
        "default": "relu",
        "enum": [
          "identity",
          "logistic",
          "tanh",
          "relu",
          "softplus",
          "silu",
          "sin"
        ]
      },
      "learning_rate": {
        "type": "number",
        "title": "Learning rate",
        "default": 0.001
      }
    }
  },
  "inputs": {
    "required": [
      "features"
    ]
  },
  "outputs": {
    "columns": [
      "prediction"
    ]
  },
  "references": [
    {
      "type": "journal",
      "authors": "Rumelhart, D. E., Hinton, G. E., Williams, R. J.",
      "title": "Learning representations by back-propagating errors",
      "journal": "Nature",
      "year": 1986,
      "volume": "323",
      "pages": "533-536"
    }
  ],
  "api": [
    "NeuralNet",
    "MlpModel",
    "DenseLayer",
    "TrainOptions",
    "StandardScaler",
    "activation_from_string",
    "activation_to_string",
    "NeuralNet.backward",
    "NeuralNet.backward_derivatives",
    "NeuralNet.predict_derivatives",
    "NeuralNet.jacobian",
    "NeuralNet.hessian",
    "NeuralNet.get_parameters",
    "NeuralNet.set_parameters",
    "NeuralNet.from_onnx_file",
    "NeuralNet.from_safetensors_file",
    "NeuralNetBackward",
    "NeuralNetDerivatives"
  ],
  "can_replay": false
})JSON";
bool register_neuralnet_entries() {
    tttrlib::register_algorithm_json("math", "neural_net", kNeuralNetEntry);
    return true;
}
const bool kNeuralNetRegistered = register_neuralnet_entries();
}  // namespace
