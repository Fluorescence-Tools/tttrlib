// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_MLPCORE_H
#define TTTRLIB_MLPCORE_H

// Validation: A/B-TESTED 2026-08-19 -- forward pass vs sklearn MLPRegressor weights (1e-10,
//   test/python/test_neural_net.py); every gradient (parameters, inputs, tangents, orders 0-2)
//   vs central differences and vs the forward-mode Dual<GradVec<N>> dot-product identity
//   <w, J v> == <J^T w, v> in test/cpp/test_mlp_core.cpp; training numerics vs the pre-split
//   NeuralNet::train (predictions equal to 1e-12 on the same seed).
//   Register: okf/testing/math-kernel-validation.md

// The differentiable core of a dense multilayer perceptron -- forward pass,
// reverse-mode backward pass, and the same two augmented with a directional
// Taylor expansion of the *input* to second order, so that a loss may depend
// on dy/dx and d2y/dx2 (a physics residual) and still be differentiated with
// respect to the weights by plain backpropagation.
//
// Header-only and std-only on purpose. `NeuralNet` (NeuralNet.h) is the
// library-facing shell -- training with Adam, JSON round trip, the registry
// entry, standard scalers -- and it delegates every derivative to this file.
// imp.bff carries a verbatim copy of this header under its `internal/`
// directory (the way pcg and nlohmann/json are vendored there) so that a
// network trained here can be evaluated and differentiated inside a coordinate-
// space solver without linking tttrlib. Keep this file free of anything that
// would make that copy diverge: no Mat.h, no json, no registry, no OpenMP
// beyond a `simd` hint that compiles to nothing without -fopenmp.
//
// Layout conventions, shared with sklearn's `coefs_` after a transpose:
//   * `DenseLayer::weight` is row-major `n_out x n_in`, `weight[o*n_in + i]`.
//   * A batch is row-major `n_rows x width`; sample `r` is the contiguous
//     slice `[r*width, (r+1)*width)`.
//   * The flat parameter vector (`flatten` / `unflatten` / gradients) is the
//     layers in order, each as its weight matrix followed by its bias.
//
// The Taylor augmentation. Feed the network a first-order tangent `v` per
// sample (`order >= 1`) and it carries, next to every activation `a0 = f(z0)`,
// the directional derivative `a1 = da/dv = f'(z0) z1` and, at `order == 2`,
// the directional second derivative `a2 = d2a/dv2 = f''(z0) z1^2 + f'(z0) z2`
// (with `z1 = W a1`, `z2 = W a2`, and the input's `a2` identically zero).
// The output's `a1` is `J v` and its `a2` is `v^T H v`. Because these are
// ordinary elementwise and linear operations, the adjoint of the augmented
// forward is another backward pass with the same GEMMs plus f'' and f''' --
// which is exactly what a residual loss `L(y, Jv, v^T H v)` needs for
// dL/dW. A Laplacian is the sum of `v^T H v` over the unit directions, i.e.
// `n_in` augmented passes; nothing here needs a tape.
//
// The GEMM is a template policy so the library can route the batch products
// through Mat.h's SIMD kernels while a vendored copy runs on the portable
// loops below. Both are validated against each other by test_mlp_core.cpp.
//
// A whole trained model -- layers plus the input/output StandardScalers -- is
// `MlpModel`; `model_predict` / `model_backward` apply the scalers and their
// chain rule so a caller stays in physical units, and `model_from_json` /
// `model_to_json` (templated on the JSON type, so still std-only here) read
// and write the `tttrlib.neural_net` document. That is the complete contract
// a consumer needs to take a network trained by `NeuralNet::train` or
// scikit-learn and evaluate and differentiate it elsewhere.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef TTTRLIB_MLPCORE_NAMESPACE
#define TTTRLIB_MLPCORE_NAMESPACE tttrlib
#endif

#if defined(_OPENMP) && !defined(_MSC_VER)
#define TTTRLIB_MLPCORE_SIMD _Pragma("omp simd")
#else
#define TTTRLIB_MLPCORE_SIMD
#endif

namespace TTTRLIB_MLPCORE_NAMESPACE {

/// Elementwise nonlinearity applied after a dense layer.
///
/// The first four are scikit-learn's set and keep its names in JSON
/// (`identity`, `relu`, `tanh`, `logistic`). The last three are smooth
/// activations for networks whose *derivatives* with respect to the input are
/// part of the loss: `softplus` and `silu` are C-infinity and `sin` is the
/// activation of choice for periodic or highly oscillatory targets. ReLU has a
/// zero second derivative everywhere, which is why a physics residual with a
/// diffusion term cannot train through it.
enum class Activation { Identity, ReLU, Tanh, Sigmoid, Softplus, SiLU, Sin };

/// Parse an activation name; scikit-learn's spellings are accepted, plus
/// `sigmoid`, `linear`, `softplus`, `silu`/`swish` and `sin`. Throws on an
/// unknown name.
inline Activation activation_from_string(const std::string& name) {
    if (name == "identity" || name == "linear") return Activation::Identity;
    if (name == "relu") return Activation::ReLU;
    if (name == "tanh") return Activation::Tanh;
    if (name == "logistic" || name == "sigmoid") return Activation::Sigmoid;
    if (name == "softplus") return Activation::Softplus;
    if (name == "silu" || name == "swish") return Activation::SiLU;
    if (name == "sin") return Activation::Sin;
    throw std::runtime_error("NeuralNet: unknown activation '" + name + "'");
}

/// Inverse of activation_from_string(); sklearn's names where they exist.
inline std::string activation_to_string(Activation a) {
    switch (a) {
        case Activation::Identity: return "identity";
        case Activation::ReLU: return "relu";
        case Activation::Tanh: return "tanh";
        case Activation::Sigmoid: return "logistic";
        case Activation::Softplus: return "softplus";
        case Activation::SiLU: return "silu";
        case Activation::Sin: return "sin";
    }
    return "identity";
}

/// One fully-connected layer, `y = act(W x + b)`.
///
/// `weight` is row-major with shape `n_out x n_in`, i.e. `weight[o*n_in + i]`.
/// scikit-learn stores `coefs_` transposed (`n_in x n_out`); importers must
/// transpose.
struct DenseLayer {
    std::vector<double> weight;
    std::vector<double> bias;
    int n_in = 0;
    int n_out = 0;
    Activation activation = Activation::ReLU;
};

/// Elementwise standardisation, `(x - mean) / scale`.
///
/// Mirrors scikit-learn's `StandardScaler` so a net trained in Python and one
/// trained here are interchangeable. Empty `mean`/`scale` means "identity" -- a
/// model may carry no scaler at all.
struct StandardScaler {
    std::vector<double> mean;
    std::vector<double> scale;

    /// Whether this scaler does anything (non-empty mean/scale).
    bool active() const { return !mean.empty(); }
    /// Number of features; 0 when inactive.
    int size() const { return static_cast<int>(mean.size()); }

    /// Fit mean/scale from `X` (row-major, `n_rows x n_cols`); population
    /// standard deviation (ddof = 0) and a zero-variance column gets scale 1,
    /// both as scikit-learn does.
    void fit(const double* X, int n_rows, int n_cols) {
        mean.assign(static_cast<size_t>(n_cols), 0.0);
        scale.assign(static_cast<size_t>(n_cols), 1.0);
        if (n_rows <= 0 || n_cols <= 0) return;
        for (int i = 0; i < n_rows; ++i)
            for (int j = 0; j < n_cols; ++j)
                mean[static_cast<size_t>(j)] += X[static_cast<size_t>(i) * n_cols + j];
        for (int j = 0; j < n_cols; ++j) mean[static_cast<size_t>(j)] /= n_rows;
        for (int j = 0; j < n_cols; ++j) {
            double var = 0.0;
            for (int i = 0; i < n_rows; ++i) {
                const double d = X[static_cast<size_t>(i) * n_cols + j] - mean[static_cast<size_t>(j)];
                var += d * d;
            }
            const double sd = std::sqrt(var / static_cast<double>(n_rows));
            scale[static_cast<size_t>(j)] = (sd == 0.0) ? 1.0 : sd;
        }
    }
    /// Apply `(x - mean) / scale` to `v` in place.
    void transform(std::vector<double>& v) const {
        if (!active()) return;
        if (v.size() != mean.size())
            throw std::runtime_error("StandardScaler::transform: length mismatch");
        for (size_t i = 0; i < v.size(); ++i) v[i] = (v[i] - mean[i]) / scale[i];
    }
    /// Apply `x * scale + mean` to `v` in place.
    void inverse_transform(std::vector<double>& v) const {
        if (!active()) return;
        if (v.size() != mean.size())
            throw std::runtime_error("StandardScaler::inverse_transform: length mismatch");
        for (size_t i = 0; i < v.size(); ++i) v[i] = v[i] * scale[i] + mean[i];
    }
};

/// A complete model: the layers plus the input and output scalers a trained
/// network carries. This is what a `tttrlib.neural_net` JSON document holds,
/// and what mlpcore's scaler-aware entry points below operate on -- so a
/// network trained by `NeuralNet::train` (or scikit-learn) evaluates and
/// differentiates identically wherever this header is compiled.
struct MlpModel {
    std::vector<DenseLayer> layers;
    StandardScaler x_scaler;
    StandardScaler y_scaler;

    int n_inputs() const { return layers.empty() ? 0 : layers.front().n_in; }
    int n_outputs() const { return layers.empty() ? 0 : layers.back().n_out; }

    /// Throw unless the layers chain and the scalers match the ends: a
    /// malformed model fails at load rather than producing silent nonsense
    /// mid-forward.
    void validate() const {
        if (layers.empty()) throw std::runtime_error("NeuralNet: model has no layers");
        for (size_t i = 0; i < layers.size(); ++i) {
            const DenseLayer& l = layers[i];
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
            if (i + 1 < layers.size() && l.n_out != layers[i + 1].n_in)
                throw std::runtime_error(
                    "NeuralNet: layer " + std::to_string(i) + " outputs " +
                    std::to_string(l.n_out) + " but layer " + std::to_string(i + 1) +
                    " expects " + std::to_string(layers[i + 1].n_in));
        }
        if (x_scaler.active() && x_scaler.size() != n_inputs())
            throw std::runtime_error("NeuralNet: x_scaler length " +
                                     std::to_string(x_scaler.size()) +
                                     " != n_inputs " + std::to_string(n_inputs()));
        if (y_scaler.active() && y_scaler.size() != n_outputs())
            throw std::runtime_error("NeuralNet: y_scaler length " +
                                     std::to_string(y_scaler.size()) +
                                     " != n_outputs " + std::to_string(n_outputs()));
    }
};

#ifndef SWIG  // the kernels are C++-only; the bindings go through NeuralNet
namespace mlpcore {

// ---------------------------------------------------------------------------
// Scalar activation, templated so a forward-mode dual number can flow through
// ---------------------------------------------------------------------------

/// `f(z)` for any scalar type providing `exp`, `log`, `tanh`, `sin` and
/// comparison with `double` (double, and tttrlib::Dual<G>).
///
/// The sigmoid is written as `1/(1+exp(-z))` -- the same expression Mat.h's
/// `sigmoid_inplace` uses -- so a network evaluated here and one evaluated by
/// the pre-split training code agree to the bit. Softplus is the numerically
/// safe form (`z + log(1+exp(-z))` above zero) so it neither overflows nor
/// loses the linear regime.
template <class T>
inline T act_value(const T& z, Activation a) {
    using std::exp;
    using std::log;
    using std::tanh;
    using std::sin;
    switch (a) {
        case Activation::Identity:
            return z;
        case Activation::ReLU:
            return (z > 0.0) ? z : T(0.0);
        case Activation::Tanh:
            return tanh(z);
        case Activation::Sigmoid:
            return 1.0 / (1.0 + exp(-z));
        case Activation::Softplus:
            return (z > 0.0) ? z + log(1.0 + exp(-z)) : log(1.0 + exp(z));
        case Activation::SiLU:
            return z * (1.0 / (1.0 + exp(-z)));
        case Activation::Sin:
            return sin(z);
    }
    return z;
}

/// Derivatives `f'`, `f''`, `f'''` at `z`, given `z` and the cached value
/// `a = f(z)`. Only the orders up to `order + 1` are meaningful for a pass of
/// that order (a plain backward needs `f'`, the first-order Taylor adjoint
/// `f''`, the second-order one `f'''`), but all three are cheap enough that
/// computing them unconditionally is simpler than three code paths.
inline void act_derivs(double z, double a, Activation act,
                       double& f1, double& f2, double& f3) {
    switch (act) {
        case Activation::Identity:
            f1 = 1.0; f2 = 0.0; f3 = 0.0;
            return;
        case Activation::ReLU:
            f1 = (z > 0.0) ? 1.0 : 0.0; f2 = 0.0; f3 = 0.0;
            return;
        case Activation::Tanh: {
            const double t = a;
            f1 = 1.0 - t * t;
            f2 = -2.0 * t * f1;
            f3 = -2.0 * f1 * (1.0 - 3.0 * t * t);
            return;
        }
        case Activation::Sigmoid: {
            const double s = a;
            f1 = s * (1.0 - s);
            f2 = f1 * (1.0 - 2.0 * s);
            f3 = f2 * (1.0 - 2.0 * s) - 2.0 * f1 * f1;
            return;
        }
        case Activation::Softplus: {
            const double s = 1.0 / (1.0 + std::exp(-z));
            f1 = s;
            f2 = s * (1.0 - s);
            f3 = f2 * (1.0 - 2.0 * s);
            return;
        }
        case Activation::SiLU: {
            const double s = 1.0 / (1.0 + std::exp(-z));
            const double s1 = s * (1.0 - s);
            const double s2 = s1 * (1.0 - 2.0 * s);
            const double s3 = s2 * (1.0 - 2.0 * s) - 2.0 * s1 * s1;
            f1 = s + z * s1;
            f2 = 2.0 * s1 + z * s2;
            f3 = 3.0 * s2 + z * s3;
            return;
        }
        case Activation::Sin: {
            const double c = std::cos(z);
            f1 = c; f2 = -a; f3 = -c;
            return;
        }
    }
    f1 = 1.0; f2 = 0.0; f3 = 0.0;
}

/// `a[i] = f(z[i])` for `n` doubles, the switch hoisted out of the loop so
/// each case is a plain vectorisable loop (a per-element `switch` cost 20-45 %
/// of a small-net training step, measured).
inline void act_apply(const double* z, double* a, size_t n, Activation act) {
    switch (act) {
        case Activation::Identity:
            if (a != z) std::copy(z, z + n, a);
            return;
        case Activation::ReLU:
            TTTRLIB_MLPCORE_SIMD
            for (size_t i = 0; i < n; ++i) a[i] = (z[i] > 0.0) ? z[i] : 0.0;
            return;
        case Activation::Tanh:
            for (size_t i = 0; i < n; ++i) a[i] = std::tanh(z[i]);
            return;
        case Activation::Sigmoid:
            for (size_t i = 0; i < n; ++i) a[i] = 1.0 / (1.0 + std::exp(-z[i]));
            return;
        case Activation::Softplus:
            for (size_t i = 0; i < n; ++i)
                a[i] = (z[i] > 0.0) ? z[i] + std::log(1.0 + std::exp(-z[i])) : std::log(1.0 + std::exp(z[i]));
            return;
        case Activation::SiLU:
            for (size_t i = 0; i < n; ++i) a[i] = z[i] / (1.0 + std::exp(-z[i]));
            return;
        case Activation::Sin:
            for (size_t i = 0; i < n; ++i) a[i] = std::sin(z[i]);
            return;
    }
}

/// `f1[i]`, and when `order >= 1` `f2[i]`, and when `order >= 2` `f3[i]`, for
/// `n` doubles -- same formulas as act_derivs(), switch hoisted, only the
/// orders a pass needs. `f2`/`f3` may be null below their order.
inline void act_derivs_n(const double* z, const double* a, size_t n, Activation act, int order,
                         double* f1, double* f2, double* f3) {
    switch (act) {
        case Activation::Identity:
            std::fill(f1, f1 + n, 1.0);
            if (order >= 1) std::fill(f2, f2 + n, 0.0);
            if (order >= 2) std::fill(f3, f3 + n, 0.0);
            return;
        case Activation::ReLU:
            TTTRLIB_MLPCORE_SIMD
            for (size_t i = 0; i < n; ++i) f1[i] = (z[i] > 0.0) ? 1.0 : 0.0;
            if (order >= 1) std::fill(f2, f2 + n, 0.0);
            if (order >= 2) std::fill(f3, f3 + n, 0.0);
            return;
        case Activation::Tanh:
            TTTRLIB_MLPCORE_SIMD
            for (size_t i = 0; i < n; ++i) f1[i] = 1.0 - a[i] * a[i];
            if (order >= 1) {
                TTTRLIB_MLPCORE_SIMD
                for (size_t i = 0; i < n; ++i) f2[i] = -2.0 * a[i] * f1[i];
            }
            if (order >= 2) {
                TTTRLIB_MLPCORE_SIMD
                for (size_t i = 0; i < n; ++i) f3[i] = -2.0 * f1[i] * (1.0 - 3.0 * a[i] * a[i]);
            }
            return;
        case Activation::Sigmoid:
            TTTRLIB_MLPCORE_SIMD
            for (size_t i = 0; i < n; ++i) f1[i] = a[i] * (1.0 - a[i]);
            if (order >= 1) {
                TTTRLIB_MLPCORE_SIMD
                for (size_t i = 0; i < n; ++i) f2[i] = f1[i] * (1.0 - 2.0 * a[i]);
            }
            if (order >= 2) {
                TTTRLIB_MLPCORE_SIMD
                for (size_t i = 0; i < n; ++i) f3[i] = f2[i] * (1.0 - 2.0 * a[i]) - 2.0 * f1[i] * f1[i];
            }
            return;
        case Activation::Softplus:
        case Activation::SiLU:
        case Activation::Sin:
            for (size_t i = 0; i < n; ++i) {
                double d1, d2, d3;
                act_derivs(z[i], a[i], act, d1, d2, d3);
                f1[i] = d1;
                if (order >= 1) f2[i] = d2;
                if (order >= 2) f3[i] = d3;
            }
            return;
    }
}

// ---------------------------------------------------------------------------
// GEMM policy
// ---------------------------------------------------------------------------

/// Portable, allocation-free row-major GEMMs on raw pointers. `nn`: C(MxN) =
/// A(MxK) B(KxN); `nt`: C = A(MxK) B(NxK)^T; `tn`: C = A(KxM)^T B(KxN). All
/// overwrite `C`. The ikj / dot / saxpy nestings below are the cache-friendly
/// ones for row-major operands; the SIMD hint is honoured with -fopenmp and
/// ignored otherwise.
struct PortableGemm {
    static void nn(int M, int N, int K, const double* A, const double* B, double* C) {
        std::fill(C, C + static_cast<size_t>(M) * N, 0.0);
        for (int i = 0; i < M; ++i) {
            const double* arow = A + static_cast<size_t>(i) * K;
            double* crow = C + static_cast<size_t>(i) * N;
            for (int k = 0; k < K; ++k) {
                const double aik = arow[k];
                const double* brow = B + static_cast<size_t>(k) * N;
                TTTRLIB_MLPCORE_SIMD
                for (int j = 0; j < N; ++j) crow[j] += aik * brow[j];
            }
        }
    }
    static void nt(int M, int N, int K, const double* A, const double* B, double* C) {
        for (int i = 0; i < M; ++i) {
            const double* arow = A + static_cast<size_t>(i) * K;
            double* crow = C + static_cast<size_t>(i) * N;
            for (int j = 0; j < N; ++j) {
                const double* brow = B + static_cast<size_t>(j) * K;
                double s = 0.0;
                TTTRLIB_MLPCORE_SIMD
                for (int k = 0; k < K; ++k) s += arow[k] * brow[k];
                crow[j] = s;
            }
        }
    }
    static void tn(int M, int N, int K, const double* A, const double* B, double* C) {
        std::fill(C, C + static_cast<size_t>(M) * N, 0.0);
        for (int k = 0; k < K; ++k) {
            const double* arow = A + static_cast<size_t>(k) * M;
            const double* brow = B + static_cast<size_t>(k) * N;
            for (int i = 0; i < M; ++i) {
                const double aki = arow[i];
                double* crow = C + static_cast<size_t>(i) * N;
                TTTRLIB_MLPCORE_SIMD
                for (int j = 0; j < N; ++j) crow[j] += aki * brow[j];
            }
        }
    }
};

// ---------------------------------------------------------------------------
// Parameter layout
// ---------------------------------------------------------------------------

/// Number of weights plus biases over all layers.
inline size_t n_parameters(const std::vector<DenseLayer>& layers) {
    size_t n = 0;
    for (const auto& l : layers) n += l.weight.size() + l.bias.size();
    return n;
}

/// Copy all parameters into `out` (resized), layer by layer, weight then bias.
inline void flatten(const std::vector<DenseLayer>& layers, std::vector<double>& out) {
    out.resize(n_parameters(layers));
    size_t p = 0;
    for (const auto& l : layers) {
        std::copy(l.weight.begin(), l.weight.end(), out.begin() + static_cast<std::ptrdiff_t>(p));
        p += l.weight.size();
        std::copy(l.bias.begin(), l.bias.end(), out.begin() + static_cast<std::ptrdiff_t>(p));
        p += l.bias.size();
    }
}

/// Inverse of flatten(): overwrite the layers' weights and biases from `in`.
/// The layer shapes are unchanged; throws when the length disagrees.
inline void unflatten(std::vector<DenseLayer>& layers, const double* in, size_t n) {
    if (n != n_parameters(layers))
        throw std::runtime_error("mlpcore::unflatten: got " + std::to_string(n) +
                                 " parameters, expected " +
                                 std::to_string(n_parameters(layers)));
    size_t p = 0;
    for (auto& l : layers) {
        std::copy(in + p, in + p + l.weight.size(), l.weight.begin());
        p += l.weight.size();
        std::copy(in + p, in + p + l.bias.size(), l.bias.begin());
        p += l.bias.size();
    }
}

// ---------------------------------------------------------------------------
// Batch forward / backward with the Taylor augmentation
// ---------------------------------------------------------------------------

/// Everything the backward pass needs from the forward pass, for one batch.
///
/// `a[l]` is the input of layer `l` (`a[0]` is the batch itself, `a[L]` the
/// output); `z[l]` the pre-activation of layer `l`. `a1/z1` and `a2/z2` are the
/// first- and second-order Taylor companions and are only filled to the order
/// the forward pass was asked for. `a2[0]` (the input's second-order term) is
/// identically zero and is left empty.
struct Workspace {
    int n_rows = 0;
    int order = 0;
    std::vector<std::vector<double>> a, z;
    std::vector<std::vector<double>> a1, z1;
    std::vector<std::vector<double>> a2, z2;
    /// Scratch for the activation derivatives (reused across calls).
    std::vector<double> f1, f2, f3;

    /// The network output, `n_rows x n_out`.
    const std::vector<double>& output() const { return a.back(); }
    /// `J v` per sample, `n_rows x n_out` (order >= 1).
    const std::vector<double>& output_d1() const { return a1.back(); }
    /// `v^T H v` per sample and output, `n_rows x n_out` (order == 2).
    const std::vector<double>& output_d2() const { return a2.back(); }
};

/// Forward pass over a batch, keeping the intermediates for backward().
///
/// @param layers  the network
/// @param X       row-major `n_rows x layers[0].n_in`
/// @param n_rows  batch size
/// @param ws      receives the intermediates
/// @param order   0: values only; 1: also `J v`; 2: also `v^T H v`
/// @param V       row-major `n_rows x n_in` directions, one per sample;
///                required for `order >= 1`, ignored otherwise
template <class Gemm = PortableGemm>
inline void forward(const std::vector<DenseLayer>& layers,
                    const double* X, int n_rows,
                    Workspace& ws, int order = 0, const double* V = nullptr) {
    if (layers.empty()) throw std::runtime_error("mlpcore::forward: no layers");
    if (order < 0 || order > 2) throw std::runtime_error("mlpcore::forward: order must be 0, 1 or 2");
    if (order >= 1 && V == nullptr) throw std::runtime_error("mlpcore::forward: order >= 1 needs directions V");
    const size_t L = layers.size();
    const int n_in = layers.front().n_in;

    ws.n_rows = n_rows;
    ws.order = order;
    // Resize the outer vectors only when the shape changes, so a workspace
    // reused across mini-batches keeps its inner buffers (training calls this
    // thousands of times per epoch; re-allocating every layer each time was a
    // measurable 20 % on a small net).
    auto ensure = [](std::vector<std::vector<double>>& v, size_t n) {
        if (v.size() != n) v.resize(n);
    };
    ensure(ws.a, L + 1);
    ensure(ws.z, L);
    ensure(ws.a1, order >= 1 ? L + 1 : 0);
    ensure(ws.z1, order >= 1 ? L : 0);
    ensure(ws.a2, order >= 2 ? L + 1 : 0);
    ensure(ws.z2, order >= 2 ? L : 0);

    ws.a[0].assign(X, X + static_cast<size_t>(n_rows) * n_in);
    if (order >= 1) ws.a1[0].assign(V, V + static_cast<size_t>(n_rows) * n_in);
    if (order >= 2) ws.a2[0].clear();  // the input has no second-order term

    for (size_t l = 0; l < L; ++l) {
        const DenseLayer& ly = layers[l];
        const size_t nz = static_cast<size_t>(n_rows) * ly.n_out;

        // z0 = a0 W^T + b
        ws.z[l].resize(nz);
        Gemm::nt(n_rows, ly.n_out, ly.n_in, ws.a[l].data(), ly.weight.data(), ws.z[l].data());
        for (int r = 0; r < n_rows; ++r) {
            double* zr = ws.z[l].data() + static_cast<size_t>(r) * ly.n_out;
            TTTRLIB_MLPCORE_SIMD
            for (int o = 0; o < ly.n_out; ++o) zr[o] += ly.bias[static_cast<size_t>(o)];
        }
        // a0 = f(z0)
        ws.a[l + 1].resize(nz);
        act_apply(ws.z[l].data(), ws.a[l + 1].data(), nz, ly.activation);

        if (order >= 1) {
            // z1 = a1 W^T ; a1 = f'(z0) z1
            ws.z1[l].resize(nz);
            Gemm::nt(n_rows, ly.n_out, ly.n_in, ws.a1[l].data(), ly.weight.data(), ws.z1[l].data());
            ws.a1[l + 1].resize(nz);
            if (order >= 2) {
                // z2 = a2 W^T (zero for the first layer) ; a2 = f'' z1^2 + f' z2
                ws.z2[l].assign(nz, 0.0);
                if (!ws.a2[l].empty())
                    Gemm::nt(n_rows, ly.n_out, ly.n_in, ws.a2[l].data(), ly.weight.data(), ws.z2[l].data());
                ws.a2[l + 1].resize(nz);
            }
            ws.f1.resize(nz);
            if (order >= 2) ws.f2.resize(nz);
            act_derivs_n(ws.z[l].data(), ws.a[l + 1].data(), nz, ly.activation, order - 1,
                         ws.f1.data(), order >= 2 ? ws.f2.data() : nullptr, nullptr);
            const double* f1 = ws.f1.data();
            const double* z1 = ws.z1[l].data();
            double* a1 = ws.a1[l + 1].data();
            TTTRLIB_MLPCORE_SIMD
            for (size_t i = 0; i < nz; ++i) a1[i] = f1[i] * z1[i];
            if (order >= 2) {
                const double* f2 = ws.f2.data();
                const double* z2 = ws.z2[l].data();
                double* a2 = ws.a2[l + 1].data();
                TTTRLIB_MLPCORE_SIMD
                for (size_t i = 0; i < nz; ++i) a2[i] = f2[i] * z1[i] * z1[i] + f1[i] * z2[i];
            }
        }
    }
}

/// Reverse pass through a forward() of the same order.
///
/// Given the adjoints of the outputs -- `dY0` for the values, `dY1` for `J v`
/// (order >= 1), `dY2` for `v^T H v` (order == 2), each row-major `n_rows x
/// n_out`, any of the higher ones may be null meaning zero -- accumulate the
/// gradient of the loss with respect to every parameter into `dparams` (flat,
/// flatten() layout, length n_parameters(); **added to**, so zero it first)
/// and optionally return the gradient with respect to the input `dX` and the
/// direction `dV` (each `n_rows x n_in`, overwritten; either may be null).
///
/// The Taylor adjoint per layer, with `f1..f3` at `z0`:
///   zbar2 = abar2 f1
///   zbar1 = abar1 f1 + abar2 (2 f2 z1)
///   zbar0 = abar0 f1 + abar1 (f2 z1) + abar2 (f3 z1^2 + f2 z2)
///   dW   += zbar0^T a0 + zbar1^T a1 + zbar2^T a2 ;  db += sum_rows zbar0
///   abar_prev,k = zbar_k W
template <class Gemm = PortableGemm>
inline void backward(const std::vector<DenseLayer>& layers, const Workspace& ws,
                     const double* dY0, const double* dY1, const double* dY2,
                     double* dparams, double* dX = nullptr, double* dV = nullptr) {
    if (layers.empty()) throw std::runtime_error("mlpcore::backward: no layers");
    const size_t L = layers.size();
    const int n_rows = ws.n_rows;
    const int order = ws.order;
    if (ws.a.size() != L + 1) throw std::runtime_error("mlpcore::backward: workspace does not match the layers");
    if (dY0 == nullptr) throw std::runtime_error("mlpcore::backward: dY0 is required");
    if (order < 1 && (dY1 != nullptr || dY2 != nullptr))
        throw std::runtime_error("mlpcore::backward: dY1/dY2 given but the forward pass had order 0");
    if (order < 2 && dY2 != nullptr)
        throw std::runtime_error("mlpcore::backward: dY2 given but the forward pass had order < 2");

    // The offset of every layer's weight block in the flat parameter vector.
    std::vector<size_t> offset(L);
    {
        size_t p = 0;
        for (size_t l = 0; l < L; ++l) {
            offset[l] = p;
            p += layers[l].weight.size() + layers[l].bias.size();
        }
    }

    // Upstream adjoints of the current layer's activations, per order.
    const int n_out_last = layers.back().n_out;
    std::vector<double> abar0(dY0, dY0 + static_cast<size_t>(n_rows) * n_out_last);
    std::vector<double> abar1, abar2;
    if (order >= 1) {
        if (dY1) abar1.assign(dY1, dY1 + static_cast<size_t>(n_rows) * n_out_last);
        else abar1.assign(static_cast<size_t>(n_rows) * n_out_last, 0.0);
    }
    if (order >= 2) {
        if (dY2) abar2.assign(dY2, dY2 + static_cast<size_t>(n_rows) * n_out_last);
        else abar2.assign(static_cast<size_t>(n_rows) * n_out_last, 0.0);
    }

    std::vector<double> zbar0, zbar1, zbar2, gW, tmp, f1, f2, f3;

    for (size_t l = L; l-- > 0;) {
        const DenseLayer& ly = layers[l];
        const size_t nz = static_cast<size_t>(n_rows) * ly.n_out;
        const size_t nw = static_cast<size_t>(ly.n_out) * ly.n_in;
        const bool has_a2 = order >= 2 && !ws.a2[l].empty();

        zbar0.resize(nz);
        if (order >= 1) zbar1.resize(nz);
        if (order >= 2) zbar2.resize(nz);

        f1.resize(nz);
        if (order >= 1) f2.resize(nz);
        if (order >= 2) f3.resize(nz);
        act_derivs_n(ws.z[l].data(), ws.a[l + 1].data(), nz, ly.activation, order,
                     f1.data(), order >= 1 ? f2.data() : nullptr, order >= 2 ? f3.data() : nullptr);
        {
            const double* F1 = f1.data();
            const double* A0 = abar0.data();
            double* Z0 = zbar0.data();
            if (order == 0) {
                TTTRLIB_MLPCORE_SIMD
                for (size_t i = 0; i < nz; ++i) Z0[i] = A0[i] * F1[i];
            } else if (order == 1) {
                const double* F2 = f2.data();
                const double* A1 = abar1.data();
                const double* z1 = ws.z1[l].data();
                double* Z1 = zbar1.data();
                TTTRLIB_MLPCORE_SIMD
                for (size_t i = 0; i < nz; ++i) {
                    Z1[i] = A1[i] * F1[i];
                    Z0[i] = A0[i] * F1[i] + A1[i] * F2[i] * z1[i];
                }
            } else {
                const double* F2 = f2.data();
                const double* F3 = f3.data();
                const double* A1 = abar1.data();
                const double* A2 = abar2.data();
                const double* z1 = ws.z1[l].data();
                const double* z2 = ws.z2[l].data();
                double* Z1 = zbar1.data();
                double* Z2 = zbar2.data();
                TTTRLIB_MLPCORE_SIMD
                for (size_t i = 0; i < nz; ++i) {
                    Z2[i] = A2[i] * F1[i];
                    Z1[i] = A1[i] * F1[i] + A2[i] * 2.0 * F2[i] * z1[i];
                    Z0[i] = A0[i] * F1[i] + A1[i] * F2[i] * z1[i] + A2[i] * (F3[i] * z1[i] * z1[i] + F2[i] * z2[i]);
                }
            }
        }

        // dW = zbar0^T a0 (+ zbar1^T a1 + zbar2^T a2), db = column sums of zbar0
        double* dW = dparams + offset[l];
        double* db = dW + nw;
        gW.resize(nw);
        Gemm::tn(ly.n_out, ly.n_in, n_rows, zbar0.data(), ws.a[l].data(), gW.data());
        for (size_t i = 0; i < nw; ++i) dW[i] += gW[i];
        if (order >= 1) {
            Gemm::tn(ly.n_out, ly.n_in, n_rows, zbar1.data(), ws.a1[l].data(), gW.data());
            for (size_t i = 0; i < nw; ++i) dW[i] += gW[i];
        }
        if (has_a2) {
            Gemm::tn(ly.n_out, ly.n_in, n_rows, zbar2.data(), ws.a2[l].data(), gW.data());
            for (size_t i = 0; i < nw; ++i) dW[i] += gW[i];
        }
        for (int r = 0; r < n_rows; ++r) {
            const double* zr = zbar0.data() + static_cast<size_t>(r) * ly.n_out;
            TTTRLIB_MLPCORE_SIMD
            for (int o = 0; o < ly.n_out; ++o) db[o] += zr[o];
        }

        // Adjoints for the layer below: abar_prev,k = zbar_k W.
        const bool need_below = (l > 0) || dX != nullptr || dV != nullptr;
        if (!need_below) break;
        const size_t na = static_cast<size_t>(n_rows) * ly.n_in;
        if (l > 0 || dX) {
            tmp.resize(na);
            Gemm::nn(n_rows, ly.n_in, ly.n_out, zbar0.data(), ly.weight.data(), tmp.data());
            abar0.swap(tmp);
        }
        if (order >= 1 && (l > 0 || dV)) {
            tmp.resize(na);
            Gemm::nn(n_rows, ly.n_in, ly.n_out, zbar1.data(), ly.weight.data(), tmp.data());
            abar1.swap(tmp);
        }
        if (order >= 2 && l > 0) {
            tmp.resize(na);
            Gemm::nn(n_rows, ly.n_in, ly.n_out, zbar2.data(), ly.weight.data(), tmp.data());
            abar2.swap(tmp);
        }
    }

    const size_t nin = static_cast<size_t>(n_rows) * layers.front().n_in;
    if (dX) std::copy(abar0.begin(), abar0.begin() + static_cast<std::ptrdiff_t>(nin), dX);
    if (dV) {
        if (order >= 1) std::copy(abar1.begin(), abar1.begin() + static_cast<std::ptrdiff_t>(nin), dV);
        else std::fill(dV, dV + nin, 0.0);
    }
}

// ---------------------------------------------------------------------------
// Single-sample forward in an arbitrary scalar type (forward-mode AD)
// ---------------------------------------------------------------------------

/// Forward pass for one sample with values of type `T`.
///
/// `T = double` is the plain prediction. `T = Dual<GradVec<N>>` with the input
/// seeded by basis vectors returns the output *and* its full input Jacobian in
/// one pass; `T = Dual<double>` seeded along `v` returns `J v`. This is the
/// independent reference the reverse pass above is tested against (the
/// dot-product identity), and it is what a Dual-templated objective elsewhere
/// in the library calls when a network is one term of it. `x` has
/// `layers.front().n_in` entries; `y` is resized to `layers.back().n_out`.
template <class T>
inline void predict_scalar(const std::vector<DenseLayer>& layers,
                           const T* x, std::vector<T>& y) {
    if (layers.empty()) throw std::runtime_error("mlpcore::predict_scalar: no layers");
    std::vector<T> cur(x, x + layers.front().n_in), nxt;
    for (const DenseLayer& ly : layers) {
        nxt.assign(static_cast<size_t>(ly.n_out), T(0.0));
        for (int o = 0; o < ly.n_out; ++o) {
            T s(ly.bias[static_cast<size_t>(o)]);
            const double* wrow = ly.weight.data() + static_cast<size_t>(o) * ly.n_in;
            for (int i = 0; i < ly.n_in; ++i) s += wrow[i] * cur[static_cast<size_t>(i)];
            nxt[static_cast<size_t>(o)] = act_value(s, ly.activation);
        }
        cur.swap(nxt);
    }
    y.swap(cur);
}

// ---------------------------------------------------------------------------
// Scaler-aware entry points on a whole MlpModel
// ---------------------------------------------------------------------------
//
// The layers see standardised inputs and produce standardised outputs; these
// wrappers apply the scalers on the way in and undo them (and their chain
// rule) on the way out, so a caller works in physical units throughout:
//   x_s = (x - mu_x) / sigma_x,   v_s = v / sigma_x
//   y = yhat * sigma_y + mu_y,     Jv = Jhat v_s * sigma_y,   v^T H v likewise
//   adjoints: dL/dyhat = dL/dy * sigma_y ; dL/dx = dL/dx_s / sigma_x ; dL/dv = dL/dv_s / sigma_x

namespace detail {
inline void scale_in(std::vector<double>& X, int n_rows, int n_cols, const StandardScaler& s) {
    if (!s.active()) return;
    for (int r = 0; r < n_rows; ++r) {
        double* row = X.data() + static_cast<size_t>(r) * n_cols;
        for (int j = 0; j < n_cols; ++j)
            row[j] = (row[j] - s.mean[static_cast<size_t>(j)]) / s.scale[static_cast<size_t>(j)];
    }
}
inline void scale_by(std::vector<double>& X, int n_rows, int n_cols, const StandardScaler& s, bool divide) {
    if (!s.active()) return;
    for (int r = 0; r < n_rows; ++r) {
        double* row = X.data() + static_cast<size_t>(r) * n_cols;
        for (int j = 0; j < n_cols; ++j) {
            const double f = s.scale[static_cast<size_t>(j)];
            row[j] = divide ? row[j] / f : row[j] * f;
        }
    }
}
inline void unscale_out(std::vector<double>& Y, int n_rows, int n_cols, const StandardScaler& s) {
    if (!s.active()) return;
    for (int r = 0; r < n_rows; ++r) {
        double* row = Y.data() + static_cast<size_t>(r) * n_cols;
        for (int j = 0; j < n_cols; ++j)
            row[j] = row[j] * s.scale[static_cast<size_t>(j)] + s.mean[static_cast<size_t>(j)];
    }
}
}  // namespace detail

/// Values and directional derivatives of a model, in physical units.
///
/// `X` is `n_rows x n_inputs`; `V` (`order >= 1`) the per-sample direction.
/// On return `y` is `n_rows x n_outputs`; `dy_dv` (`order >= 1`) is `J v` and
/// `d2y_dv2` (`order == 2`) is `v^T H v`, each `n_rows x n_outputs`; the higher
/// ones are left empty below their order.
template <class Gemm = PortableGemm>
inline void model_predict(const MlpModel& m, const double* X, int n_rows, int order,
                          const double* V,
                          std::vector<double>& y, std::vector<double>& dy_dv,
                          std::vector<double>& d2y_dv2) {
    const int n_in = m.n_inputs(), n_out = m.n_outputs();
    if (order < 0 || order > 2) throw std::runtime_error("mlpcore::model_predict: order must be 0, 1 or 2");
    if (order >= 1 && V == nullptr) throw std::runtime_error("mlpcore::model_predict: order >= 1 needs directions V");
    y.clear(); dy_dv.clear(); d2y_dv2.clear();
    if (n_rows <= 0) return;
    std::vector<double> Xs(X, X + static_cast<size_t>(n_rows) * n_in), Vs;
    detail::scale_in(Xs, n_rows, n_in, m.x_scaler);
    if (order >= 1) {
        Vs.assign(V, V + static_cast<size_t>(n_rows) * n_in);
        detail::scale_by(Vs, n_rows, n_in, m.x_scaler, true);
    }
    // One workspace per thread, kept between calls: predict_batch is called
    // per sample or per small batch in tight loops (the HMM surrogate), and
    // re-allocating L+1 buffers each time was a measured 6 % on a small net.
    static thread_local Workspace ws;
    forward<Gemm>(m.layers, Xs.data(), n_rows, ws, order, order >= 1 ? Vs.data() : nullptr);
    y = ws.output();
    detail::unscale_out(y, n_rows, n_out, m.y_scaler);
    if (order >= 1) { dy_dv = ws.output_d1(); detail::scale_by(dy_dv, n_rows, n_out, m.y_scaler, false); }
    if (order >= 2) { d2y_dv2 = ws.output_d2(); detail::scale_by(d2y_dv2, n_rows, n_out, m.y_scaler, false); }
}

/// Reverse pass of a model for a loss on `y` (and, with `dY1`/`dY2`, on `J v`
/// and `v^T H v`), in physical units.
///
/// `dY0` is required (`n_rows x n_outputs`); `dY1`, `dY2` may be null (zero);
/// the Taylor order is 2 if `dY2` is given, 1 if only `dY1`, else 0, and `V`
/// is required for order >= 1. Returns `dparams` (flatten() layout, length
/// n_parameters(), overwritten), and `dX`, `dV` (`n_rows x n_inputs`; `dV`
/// is zero for order 0).
template <class Gemm = PortableGemm>
inline void model_backward(const MlpModel& m, const double* X, int n_rows, const double* V,
                           const double* dY0, const double* dY1, const double* dY2,
                           std::vector<double>& dparams, std::vector<double>& dX,
                           std::vector<double>& dV) {
    const int n_in = m.n_inputs(), n_out = m.n_outputs();
    if (dY0 == nullptr) throw std::runtime_error("mlpcore::model_backward: dY0 is required");
    const int order = dY2 ? 2 : (dY1 ? 1 : 0);
    if (order >= 1 && V == nullptr) throw std::runtime_error("mlpcore::model_backward: derivative adjoints given but V is null");
    dparams.assign(n_parameters(m.layers), 0.0);
    dX.assign(static_cast<size_t>(std::max(n_rows, 0)) * n_in, 0.0);
    dV.assign(static_cast<size_t>(std::max(n_rows, 0)) * n_in, 0.0);
    if (n_rows <= 0) return;

    std::vector<double> Xs(X, X + static_cast<size_t>(n_rows) * n_in), Vs;
    detail::scale_in(Xs, n_rows, n_in, m.x_scaler);
    if (order >= 1) {
        Vs.assign(V, V + static_cast<size_t>(n_rows) * n_in);
        detail::scale_by(Vs, n_rows, n_in, m.x_scaler, true);
    }
    std::vector<double> d0(dY0, dY0 + static_cast<size_t>(n_rows) * n_out), d1, d2;
    detail::scale_by(d0, n_rows, n_out, m.y_scaler, false);
    if (dY1) { d1.assign(dY1, dY1 + static_cast<size_t>(n_rows) * n_out); detail::scale_by(d1, n_rows, n_out, m.y_scaler, false); }
    if (dY2) { d2.assign(dY2, dY2 + static_cast<size_t>(n_rows) * n_out); detail::scale_by(d2, n_rows, n_out, m.y_scaler, false); }

    static thread_local Workspace ws;
    forward<Gemm>(m.layers, Xs.data(), n_rows, ws, order, order >= 1 ? Vs.data() : nullptr);
    backward<Gemm>(m.layers, ws, d0.data(), dY1 ? d1.data() : nullptr, dY2 ? d2.data() : nullptr,
                   dparams.data(), dX.data(), order >= 1 ? dV.data() : nullptr);
    detail::scale_by(dX, n_rows, n_in, m.x_scaler, true);
    if (order >= 1) detail::scale_by(dV, n_rows, n_in, m.x_scaler, true);
}

// ---------------------------------------------------------------------------
// JSON round trip, templated on the JSON type
// ---------------------------------------------------------------------------
//
// The document is the `tttrlib.neural_net` format, version 1:
//   { "format": "tttrlib.neural_net", "version": 1,
//     "x_scaler": {"mean": [...], "scale": [...]} | {},
//     "y_scaler": {...},
//     "layers": [ {"n_in", "n_out", "activation", "weight" (row-major n_out x n_in), "bias"}, ... ] }
// `Json` is any nlohmann::json-compatible type (`contains`, `at`, `is_array`,
// `is_number`, `get<T>()`, `push_back`, `Json::array()`, `Json::object()`).
// Templating on it keeps this header std-only while both tttrlib and imp.bff
// deserialise with the nlohmann copy they already vendor.

template <class Json>
inline std::vector<double> json_doubles(const Json& j, const std::string& field) {
    if (!j.is_array())
        throw std::runtime_error("NeuralNet: field '" + field + "' must be an array");
    std::vector<double> v;
    v.reserve(j.size());
    for (const auto& e : j) {
        if (!e.is_number())
            throw std::runtime_error("NeuralNet: field '" + field + "' must contain only numbers");
        v.push_back(e.template get<double>());
    }
    return v;
}

template <class Json>
inline StandardScaler scaler_from_json(const Json& j) {
    StandardScaler s;
    if (j.is_null() || j.empty()) return s;
    s.mean = json_doubles(j.at("mean"), "scaler.mean");
    s.scale = json_doubles(j.at("scale"), "scaler.scale");
    if (s.mean.size() != s.scale.size())
        throw std::runtime_error("NeuralNet: scaler mean/scale length mismatch");
    for (auto& v : s.scale)   // sklearn maps zero variance to scale 1
        if (v == 0.0) v = 1.0;
    return s;
}

template <class Json>
inline Json scaler_to_json(const StandardScaler& s) {
    if (!s.active()) return Json::object();
    Json j = Json::object();
    j["mean"] = s.mean;
    j["scale"] = s.scale;
    return j;
}

/// Parse a `tttrlib.neural_net` document; validates before returning.
template <class Json>
inline MlpModel model_from_json(const Json& j) {
    if (j.contains("format") && j.at("format").template get<std::string>() != "tttrlib.neural_net")
        throw std::runtime_error("NeuralNet: unexpected format '" +
                                 j.at("format").template get<std::string>() +
                                 "', expected 'tttrlib.neural_net'");
    if (!j.contains("layers"))
        throw std::runtime_error("NeuralNet: document has no 'layers' array");
    MlpModel m;
    for (const auto& lj : j.at("layers")) {
        DenseLayer l;
        l.n_in = lj.at("n_in").template get<int>();
        l.n_out = lj.at("n_out").template get<int>();
        l.weight = json_doubles(lj.at("weight"), "layer.weight");
        l.bias = json_doubles(lj.at("bias"), "layer.bias");
        l.activation = activation_from_string(
            lj.contains("activation") ? lj.at("activation").template get<std::string>() : "relu");
        m.layers.push_back(std::move(l));
    }
    if (j.contains("x_scaler")) m.x_scaler = scaler_from_json(j.at("x_scaler"));
    if (j.contains("y_scaler")) m.y_scaler = scaler_from_json(j.at("y_scaler"));
    m.validate();
    return m;
}

/// Serialise to a `tttrlib.neural_net` document.
template <class Json>
inline Json model_to_json(const MlpModel& m) {
    Json j = Json::object();
    j["format"] = "tttrlib.neural_net";
    j["version"] = 1;
    j["x_scaler"] = scaler_to_json<Json>(m.x_scaler);
    j["y_scaler"] = scaler_to_json<Json>(m.y_scaler);
    Json layers = Json::array();
    for (const DenseLayer& l : m.layers) {
        Json lj = Json::object();
        lj["n_in"] = l.n_in;
        lj["n_out"] = l.n_out;
        lj["activation"] = activation_to_string(l.activation);
        lj["weight"] = l.weight;
        lj["bias"] = l.bias;
        layers.push_back(std::move(lj));
    }
    j["layers"] = std::move(layers);
    return j;
}

// ---------------------------------------------------------------------------
// ONNX import: the MLP subset, through a minimal protobuf wire-format reader
// ---------------------------------------------------------------------------
//
// A network trained anywhere -- PyTorch, JAX, Keras, scikit-learn via skl2onnx
// -- can be saved as ONNX, and an MLP's ONNX graph is small: Gemm (or
// MatMul + Add) nodes carrying the weights as initializers, and elementwise
// activation nodes between them. Reading that needs no ONNX or protobuf
// library, only the wire format (varints and length-delimited fields), so the
// reader below keeps this header std-only. What it accepts:
//
//   * Gemm with alpha = beta = 1, transA = 0; transB = 1 (weights n_out x n_in,
//     PyTorch's layout) or 0 (n_in x n_out, transposed on read)
//   * MatMul followed by Add with a 1-D initializer (the bias), or no Add
//   * Relu, Tanh, Sigmoid, Softplus, Sin as the activation after a layer, the
//     SiLU pattern Sigmoid + Mul(x, Sigmoid(x)) that exporters emit for
//     nn.SiLU / swish, and the Softplus + Greater + Where pattern PyTorch's
//     new exporter emits for nn.Softplus's threshold
//   * Identity, Cast, Flatten, Reshape, Squeeze, Unsqueeze passed through
//   * initializers of type FLOAT (1) or DOUBLE (11), in raw_data or the typed
//     repeated fields
//
// Anything else -- a second input, a branch, an op outside that list -- throws
// with the op named. No scalers: an ONNX model normalises inside its graph or
// not at all, so the returned MlpModel has none. A float32 model is read into
// double and evaluates as the float32 model it is.

namespace onnx_detail {

struct Reader {
    const unsigned char* p;
    const unsigned char* end;
    bool done() const { return p >= end; }
    std::uint64_t varint() {
        std::uint64_t v = 0;
        int shift = 0;
        while (true) {
            if (p >= end) throw std::runtime_error("ONNX: truncated varint");
            const unsigned char b = *p++;
            v |= static_cast<std::uint64_t>(b & 0x7f) << shift;
            if (!(b & 0x80)) break;
            shift += 7;
            if (shift > 63) throw std::runtime_error("ONNX: varint too long");
        }
        return v;
    }
    /// Read one field header; returns false at the end of the message.
    bool field(unsigned& number, unsigned& wire) {
        if (done()) return false;
        const std::uint64_t tag = varint();
        number = static_cast<unsigned>(tag >> 3);
        wire = static_cast<unsigned>(tag & 7);
        return true;
    }
    /// A length-delimited payload as a sub-reader.
    Reader bytes() {
        const std::uint64_t n = varint();
        if (n > static_cast<std::uint64_t>(end - p)) throw std::runtime_error("ONNX: truncated field");
        Reader r{p, p + n};
        p += n;
        return r;
    }
    std::string str() { Reader r = bytes(); return std::string(reinterpret_cast<const char*>(r.p), static_cast<size_t>(r.end - r.p)); }
    float fixed32() {
        if (end - p < 4) throw std::runtime_error("ONNX: truncated fixed32");
        float f; std::memcpy(&f, p, 4); p += 4; return f;
    }
    double fixed64() {
        if (end - p < 8) throw std::runtime_error("ONNX: truncated fixed64");
        double d; std::memcpy(&d, p, 8); p += 8; return d;
    }
    void skip(unsigned wire) {
        switch (wire) {
            case 0: varint(); return;
            case 1: fixed64(); return;
            case 2: bytes(); return;
            case 5: fixed32(); return;
            default: throw std::runtime_error("ONNX: unsupported wire type " + std::to_string(wire));
        }
    }
};

struct Tensor {
    std::string name;
    std::vector<std::int64_t> dims;
    std::vector<double> data;
};

struct Node {
    std::string op;
    std::vector<std::string> inputs, outputs;
    double alpha = 1.0, beta = 1.0;
    std::int64_t transA = 0, transB = 0;
};

inline Tensor parse_tensor(Reader r) {
    Tensor t;
    std::int64_t dtype = 1;
    std::vector<unsigned char> raw;
    std::vector<double> typed;
    unsigned num, wire;
    while (r.field(num, wire)) {
        if (num == 1) {  // dims: packed or repeated varint
            if (wire == 2) { Reader d = r.bytes(); while (!d.done()) t.dims.push_back(static_cast<std::int64_t>(d.varint())); }
            else t.dims.push_back(static_cast<std::int64_t>(r.varint()));
        } else if (num == 2 && wire == 0) {
            dtype = static_cast<std::int64_t>(r.varint());
        } else if (num == 8 && wire == 2) {
            t.name = r.str();
        } else if (num == 9 && wire == 2) {
            Reader d = r.bytes();
            raw.assign(d.p, d.end);
        } else if (num == 4) {  // float_data
            if (wire == 2) { Reader d = r.bytes(); while (!d.done()) typed.push_back(d.fixed32()); }
            else typed.push_back(r.fixed32());
        } else if (num == 10) {  // double_data
            if (wire == 2) { Reader d = r.bytes(); while (!d.done()) typed.push_back(d.fixed64()); }
            else typed.push_back(r.fixed64());
        } else {
            r.skip(wire);
        }
    }
    if (!raw.empty()) {
        if (dtype == 1) {
            if (raw.size() % 4) throw std::runtime_error("ONNX: initializer '" + t.name + "' raw_data is not float32");
            t.data.resize(raw.size() / 4);
            for (size_t i = 0; i < t.data.size(); ++i) { float f; std::memcpy(&f, raw.data() + 4 * i, 4); t.data[i] = f; }
        } else if (dtype == 11) {
            if (raw.size() % 8) throw std::runtime_error("ONNX: initializer '" + t.name + "' raw_data is not float64");
            t.data.resize(raw.size() / 8);
            std::memcpy(t.data.data(), raw.data(), raw.size());
        } else {
            throw std::runtime_error("ONNX: initializer '" + t.name + "' has data_type " + std::to_string(dtype) +
                                     "; only FLOAT (1) and DOUBLE (11) are supported");
        }
    } else {
        t.data = typed;
    }
    return t;
}

inline Node parse_node(Reader r) {
    Node n;
    unsigned num, wire;
    while (r.field(num, wire)) {
        if (num == 1 && wire == 2) n.inputs.push_back(r.str());
        else if (num == 2 && wire == 2) n.outputs.push_back(r.str());
        else if (num == 4 && wire == 2) n.op = r.str();
        else if (num == 5 && wire == 2) {
            Reader a = r.bytes();
            std::string aname; double f = 0; std::int64_t i = 0; bool has_f = false, has_i = false;
            unsigned an, aw;
            while (a.field(an, aw)) {
                if (an == 1 && aw == 2) aname = a.str();
                else if (an == 2 && aw == 5) { f = a.fixed32(); has_f = true; }
                else if (an == 3 && aw == 0) { i = static_cast<std::int64_t>(a.varint()); has_i = true; }
                else a.skip(aw);
            }
            if (aname == "alpha" && has_f) n.alpha = f;
            else if (aname == "beta" && has_f) n.beta = f;
            else if (aname == "transA" && has_i) n.transA = i;
            else if (aname == "transB" && has_i) n.transB = i;
        } else {
            r.skip(wire);
        }
    }
    return n;
}

}  // namespace onnx_detail

/// Build a model from the bytes of an ONNX file (see the subset above).
inline MlpModel model_from_onnx(const unsigned char* data, size_t n) {
    using namespace onnx_detail;
    Reader model{data, data + n};
    bool have_graph = false;
    std::vector<Node> nodes;
    std::map<std::string, Tensor> inits;
    std::vector<std::string> graph_inputs, graph_outputs;
    unsigned num, wire;
    while (model.field(num, wire)) {
        if (num == 7 && wire == 2) {  // ModelProto.graph
            have_graph = true;
            Reader g = model.bytes();
            unsigned gn, gw;
            while (g.field(gn, gw)) {
                if (gn == 1 && gw == 2) nodes.push_back(parse_node(g.bytes()));
                else if (gn == 5 && gw == 2) { Tensor t = parse_tensor(g.bytes()); inits[t.name] = std::move(t); }
                else if ((gn == 11 || gn == 12) && gw == 2) {  // ValueInfoProto: field 1 name
                    Reader v = g.bytes();
                    unsigned vn, vw;
                    std::string name;
                    while (v.field(vn, vw)) { if (vn == 1 && vw == 2) name = v.str(); else v.skip(vw); }
                    (gn == 11 ? graph_inputs : graph_outputs).push_back(name);
                } else g.skip(gw);
            }
        } else {
            model.skip(wire);
        }
    }
    if (!have_graph) throw std::runtime_error("ONNX: no graph in the model (is this an ONNX file?)");

    // The graph input is the one that is not an initializer.
    std::string cur;
    for (const auto& name : graph_inputs) if (!inits.count(name)) { cur = name; break; }
    if (cur.empty()) throw std::runtime_error("ONNX: the graph has no non-initializer input");

    auto weight_of = [&](const std::string& name) -> const Tensor& {
        auto it = inits.find(name);
        if (it == inits.end()) throw std::runtime_error("ONNX: '" + name + "' is not an initializer; only constant weights are supported");
        return it->second;
    };
    auto set_activation = [&](std::vector<DenseLayer>& layers, Activation a, const std::string& op) {
        if (layers.empty()) throw std::runtime_error("ONNX: activation '" + op + "' before the first layer is not supported");
        if (layers.back().activation != Activation::Identity)
            throw std::runtime_error("ONNX: two activations in a row ('" + op + "') are not supported");
        layers.back().activation = a;
    };

    MlpModel m;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node& nd = nodes[i];
        if (nd.inputs.empty() || nd.outputs.empty()) throw std::runtime_error("ONNX: node '" + nd.op + "' without inputs/outputs");
        const std::string& op = nd.op;
        if (op == "Gemm") {
            if (nd.inputs[0] != cur) throw std::runtime_error("ONNX: Gemm does not consume the running tensor; only a chain is supported");
            if (nd.alpha != 1.0 || nd.beta != 1.0 || nd.transA != 0)
                throw std::runtime_error("ONNX: Gemm with alpha/beta != 1 or transA != 0 is not supported");
            const Tensor& W = weight_of(nd.inputs[1]);
            if (W.dims.size() != 2) throw std::runtime_error("ONNX: Gemm weight '" + W.name + "' is not 2-D");
            DenseLayer l;
            if (nd.transB) { l.n_out = static_cast<int>(W.dims[0]); l.n_in = static_cast<int>(W.dims[1]); l.weight = W.data; }
            else {
                l.n_in = static_cast<int>(W.dims[0]); l.n_out = static_cast<int>(W.dims[1]);
                l.weight.resize(W.data.size());
                for (int r = 0; r < l.n_in; ++r) for (int c = 0; c < l.n_out; ++c)
                    l.weight[static_cast<size_t>(c) * l.n_in + r] = W.data[static_cast<size_t>(r) * l.n_out + c];
            }
            l.bias.assign(static_cast<size_t>(l.n_out), 0.0);
            if (nd.inputs.size() > 2 && !nd.inputs[2].empty()) {
                const Tensor& b = weight_of(nd.inputs[2]);
                if (b.data.size() != static_cast<size_t>(l.n_out)) throw std::runtime_error("ONNX: Gemm bias '" + b.name + "' has the wrong length");
                l.bias = b.data;
            }
            l.activation = Activation::Identity;
            m.layers.push_back(std::move(l));
            cur = nd.outputs[0];
        } else if (op == "MatMul") {
            if (nd.inputs[0] != cur) throw std::runtime_error("ONNX: MatMul does not consume the running tensor; only a chain is supported");
            const Tensor& W = weight_of(nd.inputs[1]);
            if (W.dims.size() != 2) throw std::runtime_error("ONNX: MatMul weight '" + W.name + "' is not 2-D");
            DenseLayer l;
            l.n_in = static_cast<int>(W.dims[0]); l.n_out = static_cast<int>(W.dims[1]);
            l.weight.resize(W.data.size());
            for (int r = 0; r < l.n_in; ++r) for (int c = 0; c < l.n_out; ++c)
                l.weight[static_cast<size_t>(c) * l.n_in + r] = W.data[static_cast<size_t>(r) * l.n_out + c];
            l.bias.assign(static_cast<size_t>(l.n_out), 0.0);
            l.activation = Activation::Identity;
            cur = nd.outputs[0];
            // An Add with a 1-D initializer right after is the bias.
            if (i + 1 < nodes.size() && nodes[i + 1].op == "Add" && nodes[i + 1].inputs.size() == 2) {
                const Node& ad = nodes[i + 1];
                const std::string other = ad.inputs[0] == cur ? ad.inputs[1] : ad.inputs[1] == cur ? ad.inputs[0] : "";
                if (!other.empty() && inits.count(other)) {
                    const Tensor& b = inits[other];
                    if (b.data.size() == static_cast<size_t>(l.n_out)) { l.bias = b.data; cur = ad.outputs[0]; ++i; }
                }
            }
            m.layers.push_back(std::move(l));
        } else if (op == "Relu" || op == "Tanh" || op == "Softplus" || op == "Sin" || op == "Sigmoid") {
            if (nd.inputs[0] != cur) throw std::runtime_error("ONNX: '" + op + "' does not consume the running tensor; only a chain is supported");
            if (op == "Sigmoid" && i + 1 < nodes.size() && nodes[i + 1].op == "Mul" && nodes[i + 1].inputs.size() == 2) {
                // SiLU: x * sigmoid(x)
                const Node& mul = nodes[i + 1];
                const bool silu = (mul.inputs[0] == cur && mul.inputs[1] == nd.outputs[0]) ||
                                  (mul.inputs[1] == cur && mul.inputs[0] == nd.outputs[0]);
                if (silu) { set_activation(m.layers, Activation::SiLU, "SiLU"); cur = mul.outputs[0]; ++i; continue; }
            }
            set_activation(m.layers, op == "Relu" ? Activation::ReLU : op == "Tanh" ? Activation::Tanh :
                                     op == "Softplus" ? Activation::Softplus : op == "Sin" ? Activation::Sin :
                                     Activation::Sigmoid, op);
            const std::string x_in = cur;
            cur = nd.outputs[0];
            // PyTorch's nn.Softplus(threshold) exports as Softplus(x) then
            // Greater(x, threshold) and Where(greater, x, softplus(x)) -- the
            // linear regime above the threshold, which our softplus already
            // handles exactly. Fold the pattern.
            if (op == "Softplus" && i + 2 < nodes.size() && nodes[i + 1].op == "Greater" && nodes[i + 2].op == "Where" &&
                nodes[i + 1].inputs.size() == 2 && nodes[i + 1].inputs[0] == x_in && nodes[i + 2].inputs.size() == 3 &&
                nodes[i + 2].inputs[0] == nodes[i + 1].outputs[0] && nodes[i + 2].inputs[1] == x_in &&
                nodes[i + 2].inputs[2] == cur) {
                cur = nodes[i + 2].outputs[0];
                i += 2;
            }
        } else if (op == "Identity" || op == "Cast" || op == "Flatten" || op == "Reshape" || op == "Squeeze" || op == "Unsqueeze") {
            if (nd.inputs[0] != cur) throw std::runtime_error("ONNX: '" + op + "' does not consume the running tensor; only a chain is supported");
            cur = nd.outputs[0];
        } else {
            throw std::runtime_error("ONNX: unsupported op '" + op + "'; the MLP reader accepts Gemm, MatMul(+Add), Relu, Tanh, Sigmoid, Softplus, Sin, the SiLU pattern, and pass-through reshapes");
        }
    }
    if (m.layers.empty()) throw std::runtime_error("ONNX: no dense layer found in the graph");
    m.validate();
    return m;
}

inline MlpModel model_from_onnx(const std::string& bytes) {
    return model_from_onnx(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
}

// ---------------------------------------------------------------------------
// safetensors import: weights only, PyTorch state_dict names
// ---------------------------------------------------------------------------
//
// safetensors is the plain weights format (an 8-byte little-endian header
// length, a JSON header, then the tensor bytes). It carries no architecture,
// so the reader takes the conventions a PyTorch `nn.Sequential` / `nn.Linear`
// state_dict follows -- `<prefix>.weight` of shape (n_out, n_in) with an
// optional `<prefix>.bias`, layers ordered by the first integer in the prefix
// (then by name) -- and the activations from either the `__metadata__` map
// (`"activations": "tanh,tanh,identity"` per layer, or `"activation":
// "tanh"` for every hidden layer) or the `hidden_activation` argument, the
// output layer being linear. `Json` is any nlohmann-compatible type, as for
// model_from_json. dtypes F32 and F64.

template <class Json>
inline MlpModel model_from_safetensors(const unsigned char* data, size_t n,
                                       const std::string& hidden_activation = "tanh") {
    if (n < 8) throw std::runtime_error("safetensors: file shorter than its header length");
    std::uint64_t hlen = 0;
    for (int i = 7; i >= 0; --i) hlen = (hlen << 8) | data[i];
    if (hlen > n - 8) throw std::runtime_error("safetensors: header length exceeds the file");
    const std::string header(reinterpret_cast<const char*>(data + 8), static_cast<size_t>(hlen));
    const unsigned char* body = data + 8 + hlen;
    const size_t body_n = n - 8 - static_cast<size_t>(hlen);
    Json j = Json::parse(header);

    std::string act_hidden = hidden_activation, act_list;
    if (j.contains("__metadata__")) {
        const Json& md = j.at("__metadata__");
        if (md.contains("activation")) act_hidden = md.at("activation").template get<std::string>();
        if (md.contains("activations")) act_list = md.at("activations").template get<std::string>();
    }

    struct Entry { std::string prefix; long order; };
    std::vector<Entry> entries;
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string key = it.key();
        if (key == "__metadata__") continue;
        const std::string suffix = ".weight";
        if (key.size() > suffix.size() && key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0) {
            const std::string prefix = key.substr(0, key.size() - suffix.size());
            long order = -1;
            for (size_t k = 0; k < prefix.size(); ++k)
                if (prefix[k] >= '0' && prefix[k] <= '9') { order = std::atol(prefix.c_str() + k); break; }
            entries.push_back({prefix, order});
        }
    }
    if (entries.empty()) throw std::runtime_error("safetensors: no '<name>.weight' tensor found");
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.order != b.order ? a.order < b.order : a.prefix < b.prefix; });

    auto read = [&](const std::string& key, std::vector<std::int64_t>& shape) {
        const Json& t = j.at(key);
        const std::string dtype = t.at("dtype").template get<std::string>();
        shape.clear();
        for (const auto& d : t.at("shape")) shape.push_back(d.template get<std::int64_t>());
        const std::uint64_t a = t.at("data_offsets").at(0).template get<std::uint64_t>();
        const std::uint64_t b = t.at("data_offsets").at(1).template get<std::uint64_t>();
        if (b < a || b > body_n) throw std::runtime_error("safetensors: data_offsets of '" + key + "' out of range");
        std::vector<double> v;
        if (dtype == "F32") {
            v.resize(static_cast<size_t>((b - a) / 4));
            for (size_t i = 0; i < v.size(); ++i) { float f; std::memcpy(&f, body + a + 4 * i, 4); v[i] = f; }
        } else if (dtype == "F64") {
            v.resize(static_cast<size_t>((b - a) / 8));
            std::memcpy(v.data(), body + a, static_cast<size_t>(b - a));
        } else {
            throw std::runtime_error("safetensors: '" + key + "' has dtype " + dtype + "; only F32 and F64 are supported");
        }
        return v;
    };

    // Per-layer activations from the list, else hidden/identity.
    std::vector<std::string> acts;
    if (!act_list.empty()) {
        std::string cur;
        for (char c : act_list) { if (c == ',') { acts.push_back(cur); cur.clear(); } else if (c != ' ') cur += c; }
        acts.push_back(cur);
        if (acts.size() != entries.size())
            throw std::runtime_error("safetensors: 'activations' lists " + std::to_string(acts.size()) +
                                     " entries for " + std::to_string(entries.size()) + " layers");
    }

    MlpModel m;
    for (size_t li = 0; li < entries.size(); ++li) {
        DenseLayer l;
        std::vector<std::int64_t> shape;
        l.weight = read(entries[li].prefix + ".weight", shape);
        if (shape.size() != 2) throw std::runtime_error("safetensors: '" + entries[li].prefix + ".weight' is not 2-D");
        l.n_out = static_cast<int>(shape[0]);
        l.n_in = static_cast<int>(shape[1]);
        const std::string bkey = entries[li].prefix + ".bias";
        if (j.contains(bkey)) {
            std::vector<std::int64_t> bshape;
            l.bias = read(bkey, bshape);
        } else {
            l.bias.assign(static_cast<size_t>(l.n_out), 0.0);
        }
        const std::string a = !acts.empty() ? acts[li] : (li + 1 == entries.size() ? "identity" : act_hidden);
        l.activation = activation_from_string(a);
        m.layers.push_back(std::move(l));
    }
    m.validate();
    return m;
}

}  // namespace mlpcore
#endif  // SWIG
}  // namespace TTTRLIB_MLPCORE_NAMESPACE

#undef TTTRLIB_MLPCORE_SIMD

#endif  // TTTRLIB_MLPCORE_H
