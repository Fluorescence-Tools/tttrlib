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

#include <algorithm>
#include <cmath>
#include <cstddef>
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
        for (size_t i = 0; i < nz; ++i) ws.a[l + 1][i] = act_value(ws.z[l][i], ly.activation);

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
            for (size_t i = 0; i < nz; ++i) {
                double f1, f2, f3;
                act_derivs(ws.z[l][i], ws.a[l + 1][i], ly.activation, f1, f2, f3);
                const double z1 = ws.z1[l][i];
                ws.a1[l + 1][i] = f1 * z1;
                if (order >= 2) ws.a2[l + 1][i] = f2 * z1 * z1 + f1 * ws.z2[l][i];
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

    std::vector<double> zbar0, zbar1, zbar2, gW, tmp;

    for (size_t l = L; l-- > 0;) {
        const DenseLayer& ly = layers[l];
        const size_t nz = static_cast<size_t>(n_rows) * ly.n_out;
        const size_t nw = static_cast<size_t>(ly.n_out) * ly.n_in;
        const bool has_a2 = order >= 2 && !ws.a2[l].empty();

        zbar0.resize(nz);
        if (order >= 1) zbar1.resize(nz);
        if (order >= 2) zbar2.resize(nz);

        for (size_t i = 0; i < nz; ++i) {
            double f1, f2, f3;
            act_derivs(ws.z[l][i], ws.a[l + 1][i], ly.activation, f1, f2, f3);
            double zb0 = abar0[i] * f1;
            if (order >= 1) {
                const double z1 = ws.z1[l][i];
                double zb1 = abar1[i] * f1;
                zb0 += abar1[i] * f2 * z1;
                if (order >= 2) {
                    const double z2 = ws.z2[l][i];
                    zbar2[i] = abar2[i] * f1;
                    zb1 += abar2[i] * 2.0 * f2 * z1;
                    zb0 += abar2[i] * (f3 * z1 * z1 + f2 * z2);
                }
                zbar1[i] = zb1;
            }
            zbar0[i] = zb0;
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

}  // namespace mlpcore
#endif  // SWIG
}  // namespace TTTRLIB_MLPCORE_NAMESPACE

#undef TTTRLIB_MLPCORE_SIMD

#endif  // TTTRLIB_MLPCORE_H
