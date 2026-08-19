// SPDX-License-Identifier: BSD-3-Clause
//
// The differentiable MLP core (modules/math/include/MlpCore.h) and the Dual.h
// operators it needs.
//
//   c++ -std=c++17 -O2 -I modules/math/include test/cpp/test_mlp_core.cpp \
//       -o /tmp/test_mlp_core && /tmp/test_mlp_core
//
// A wrong derivative does not crash and does not fail to compile: the training
// converges, to the wrong place, or the physics residual is minimised against
// the wrong Laplacian. So every derivative here is checked two independent
// ways -- against central differences, which share no code with the reverse
// pass, and (for the input derivatives) against the forward-mode Dual pass,
// through the dot-product identity <w, J v> == <J^T w, v>. The Taylor
// companions of orders 1 and 2 are checked against first and second central
// differences along the same direction, and the adjoint of the augmented pass
// against central differences of a loss that uses all three outputs.
//
// Exit status is the number of failed checks.

#include "MlpCore.h"
#include "Dual.h"
#include "GradVec.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

using tttrlib::Activation;
using tttrlib::DenseLayer;
using tttrlib::Dual;
using tttrlib::GradVec;
namespace mc = tttrlib::mlpcore;

static int g_failures = 0;

static void report(bool ok, const char* what, double value, double tol) {
    if (!ok) {
        std::printf("  FAIL  %s (%.3g > %.3g)\n", what, value, tol);
        ++g_failures;
    } else {
        std::printf("  ok    %s (%.3g)\n", what, value);
    }
}

// Deterministic LCG so the test needs no <random> and no seed juggling.
struct Lcg {
    std::uint64_t s;
    explicit Lcg(std::uint64_t seed) : s(seed) {}
    double uniform() {  // in (-1, 1)
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>((s >> 11) & ((1ULL << 53) - 1)) / static_cast<double>(1ULL << 52) - 1.0;
    }
};

static std::vector<DenseLayer> make_net(const std::vector<int>& dims,
                                        const std::vector<Activation>& acts, Lcg& rng) {
    std::vector<DenseLayer> layers;
    for (size_t l = 0; l + 1 < dims.size(); ++l) {
        DenseLayer d;
        d.n_in = dims[l];
        d.n_out = dims[l + 1];
        d.activation = acts[l];
        d.weight.resize(static_cast<size_t>(d.n_out) * d.n_in);
        d.bias.resize(static_cast<size_t>(d.n_out));
        for (auto& w : d.weight) w = 0.9 * rng.uniform();
        for (auto& b : d.bias) b = 0.3 * rng.uniform();
        layers.push_back(d);
    }
    return layers;
}

static double max_abs_diff(const std::vector<double>& a, const std::vector<double>& b) {
    double m = 0.0;
    for (size_t i = 0; i < a.size(); ++i) m = std::max(m, std::abs(a[i] - b[i]));
    return m;
}

static double dot(const std::vector<double>& a, const std::vector<double>& b) {
    double s = 0.0;
    for (size_t i = 0; i < a.size(); ++i) s += a[i] * b[i];
    return s;
}

// --------------------------------------------------------------------------
// 1. Activation derivatives f', f'', f''' vs central differences of f
// --------------------------------------------------------------------------
static void test_activation_derivatives() {
    std::printf("activation derivatives vs central differences\n");
    const Activation acts[] = {Activation::Identity, Activation::ReLU, Activation::Tanh,
                               Activation::Sigmoid, Activation::Softplus, Activation::SiLU,
                               Activation::Sin};
    const double zs[] = {-2.3, -0.7, 0.4, 1.9, 3.1};
    for (Activation a : acts) {
        double worst1 = 0, worst2 = 0, worst3 = 0;
        for (double z : zs) {
            const double h = 1e-4;
            auto f = [&](double x) { return mc::act_value(x, a); };
            const double fd1 = (f(z + h) - f(z - h)) / (2 * h);
            const double fd2 = (f(z + h) - 2 * f(z) + f(z - h)) / (h * h);
            const double fd3 = (f(z + 2 * h) - 2 * f(z + h) + 2 * f(z - h) - f(z - 2 * h)) / (2 * h * h * h);
            double f1, f2, f3;
            mc::act_derivs(z, f(z), a, f1, f2, f3);
            worst1 = std::max(worst1, std::abs(f1 - fd1));
            worst2 = std::max(worst2, std::abs(f2 - fd2));
            worst3 = std::max(worst3, std::abs(f3 - fd3));
        }
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s f'", tttrlib::activation_to_string(a).c_str());
        report(worst1 < 1e-7, buf, worst1, 1e-7);
        std::snprintf(buf, sizeof buf, "%s f''", tttrlib::activation_to_string(a).c_str());
        report(worst2 < 1e-5, buf, worst2, 1e-5);
        std::snprintf(buf, sizeof buf, "%s f'''", tttrlib::activation_to_string(a).c_str());
        report(worst3 < 1e-3, buf, worst3, 1e-3);
    }
}

// --------------------------------------------------------------------------
// 2. New Dual operators vs hand derivatives
// --------------------------------------------------------------------------
static void test_dual_ops() {
    std::printf("Dual tanh/sin/cos/sqrt/pow/min/max\n");
    const double x = 0.83;
    Dual<double> d(x, 1.0);
    using tttrlib::tanh; using tttrlib::sin; using tttrlib::cos;
    using tttrlib::sqrt; using tttrlib::pow; using tttrlib::min; using tttrlib::max;
    report(std::abs(tanh(d).grad - (1 - std::tanh(x) * std::tanh(x))) < 1e-15, "tanh'", tanh(d).grad, 1e-15);
    report(std::abs(sin(d).grad - std::cos(x)) < 1e-15, "sin'", sin(d).grad, 1e-15);
    report(std::abs(cos(d).grad + std::sin(x)) < 1e-15, "cos'", cos(d).grad, 1e-15);
    report(std::abs(sqrt(d).grad - 0.5 / std::sqrt(x)) < 1e-15, "sqrt'", sqrt(d).grad, 1e-15);
    report(std::abs(pow(d, 2.5).grad - 2.5 * std::pow(x, 1.5)) < 1e-14, "pow'", pow(d, 2.5).grad, 1e-14);
    Dual<double> e(1.5, 7.0);
    report(min(d, e).grad == 1.0 && max(d, e).grad == 7.0, "min/max pick the winner's derivative", 0.0, 0.0);
    report((d <= 0.83) && (d >= 0.83) && (d == 0.83) && !(d != 0.83) && (0.83 <= d) && (1.0 >= d),
           "comparisons against double", 0.0, 0.0);
}

// --------------------------------------------------------------------------
// 3. GEMM policy vs naive triple loop
// --------------------------------------------------------------------------
static void test_gemm() {
    std::printf("PortableGemm nn/nt/tn vs naive loops\n");
    Lcg rng(11);
    const int M = 5, N = 7, K = 6;
    std::vector<double> A(M * K), B(K * N), Bt(N * K), At(K * M), C(M * N), R(M * N);
    for (auto& v : A) v = rng.uniform();
    for (auto& v : B) v = rng.uniform();
    for (int k = 0; k < K; ++k) for (int j = 0; j < N; ++j) Bt[j * K + k] = B[k * N + j];
    for (int k = 0; k < K; ++k) for (int i = 0; i < M; ++i) At[k * M + i] = A[i * K + k];
    for (int i = 0; i < M; ++i) for (int j = 0; j < N; ++j) {
        double s = 0; for (int k = 0; k < K; ++k) s += A[i * K + k] * B[k * N + j]; R[i * N + j] = s;
    }
    mc::PortableGemm::nn(M, N, K, A.data(), B.data(), C.data());
    report(max_abs_diff(C, R) < 1e-14, "nn", max_abs_diff(C, R), 1e-14);
    mc::PortableGemm::nt(M, N, K, A.data(), Bt.data(), C.data());
    report(max_abs_diff(C, R) < 1e-14, "nt", max_abs_diff(C, R), 1e-14);
    mc::PortableGemm::tn(M, N, K, At.data(), B.data(), C.data());
    report(max_abs_diff(C, R) < 1e-14, "tn", max_abs_diff(C, R), 1e-14);
}

// --------------------------------------------------------------------------
// 4. flatten / unflatten round trip
// --------------------------------------------------------------------------
static void test_flatten() {
    std::printf("flatten/unflatten\n");
    Lcg rng(5);
    auto net = make_net({3, 4, 2}, {Activation::Tanh, Activation::Identity}, rng);
    std::vector<double> p;
    mc::flatten(net, p);
    report(p.size() == mc::n_parameters(net) && p.size() == 3 * 4 + 4 + 4 * 2 + 2, "size", double(p.size()), 0);
    for (auto& v : p) v += 1.0;
    mc::unflatten(net, p.data(), p.size());
    std::vector<double> q;
    mc::flatten(net, q);
    report(max_abs_diff(p, q) == 0.0, "round trip", max_abs_diff(p, q), 0);
    bool threw = false;
    try { mc::unflatten(net, p.data(), p.size() - 1); } catch (const std::exception&) { threw = true; }
    report(threw, "length mismatch throws", 0, 0);
}

// --------------------------------------------------------------------------
// 5. Forward: values vs the Dual single-sample pass; J v both ways; v^T H v vs FD
// --------------------------------------------------------------------------
static void test_forward_taylor(Activation hidden) {
    std::printf("forward Taylor orders, hidden=%s\n", tttrlib::activation_to_string(hidden).c_str());
    Lcg rng(21);
    const int n_in = 3, n_out = 2, n = 4;
    auto net = make_net({n_in, 6, 5, n_out}, {hidden, hidden, Activation::Identity}, rng);
    std::vector<double> X(n * n_in), V(n * n_in);
    for (auto& v : X) v = rng.uniform();
    for (auto& v : V) v = rng.uniform();

    mc::Workspace ws;
    mc::forward(net, X.data(), n, ws, 2, V.data());

    // Values and J v from the Dual pass, sample by sample.
    double worst_y = 0, worst_jv = 0, worst_jv_fd = 0, worst_h_fd = 0;
    for (int r = 0; r < n; ++r) {
        std::vector<Dual<double>> x(n_in);
        for (int i = 0; i < n_in; ++i) x[i] = Dual<double>(X[r * n_in + i], V[r * n_in + i]);
        std::vector<Dual<double>> y;
        mc::predict_scalar(net, x.data(), y);
        for (int k = 0; k < n_out; ++k) {
            worst_y = std::max(worst_y, std::abs(y[k].val - ws.output()[r * n_out + k]));
            worst_jv = std::max(worst_jv, std::abs(y[k].grad - ws.output_d1()[r * n_out + k]));
        }
        // Central differences along v for J v and v^T H v.
        const double h = 1e-4;
        std::vector<double> xp(n_in), xm(n_in), x0(n_in);
        for (int i = 0; i < n_in; ++i) {
            x0[i] = X[r * n_in + i];
            xp[i] = x0[i] + h * V[r * n_in + i];
            xm[i] = x0[i] - h * V[r * n_in + i];
        }
        std::vector<double> yp, ym, y0;
        mc::predict_scalar(net, xp.data(), yp);
        mc::predict_scalar(net, xm.data(), ym);
        mc::predict_scalar(net, x0.data(), y0);
        for (int k = 0; k < n_out; ++k) {
            const double fd1 = (yp[k] - ym[k]) / (2 * h);
            const double fd2 = (yp[k] - 2 * y0[k] + ym[k]) / (h * h);
            worst_jv_fd = std::max(worst_jv_fd, std::abs(fd1 - ws.output_d1()[r * n_out + k]));
            worst_h_fd = std::max(worst_h_fd, std::abs(fd2 - ws.output_d2()[r * n_out + k]));
        }
    }
    report(worst_y < 1e-13, "y: batch == Dual", worst_y, 1e-13);
    report(worst_jv < 1e-13, "J v: order-1 == Dual", worst_jv, 1e-13);
    report(worst_jv_fd < 1e-6, "J v: order-1 == central difference", worst_jv_fd, 1e-6);
    report(worst_h_fd < 1e-4, "v^T H v: order-2 == second central difference", worst_h_fd, 1e-4);

    // Full Jacobian in one Dual<GradVec<3>> pass equals the columns from three order-1 passes.
    {
        std::vector<Dual<GradVec<3>>> x(n_in);
        for (int i = 0; i < n_in; ++i) x[i] = Dual<GradVec<3>>(X[i], GradVec<3>::Unit(i));
        std::vector<Dual<GradVec<3>>> y;
        mc::predict_scalar(net, x.data(), y);
        double worst = 0;
        for (int j = 0; j < n_in; ++j) {
            std::vector<double> e(n_in, 0.0);
            e[j] = 1.0;
            mc::Workspace w1;
            mc::forward(net, X.data(), 1, w1, 1, e.data());
            for (int k = 0; k < n_out; ++k)
                worst = std::max(worst, std::abs(y[k].grad[j] - w1.output_d1()[k]));
        }
        report(worst < 1e-13, "Jacobian: Dual<GradVec<3>> == order-1 columns", worst, 1e-13);
    }
}

// --------------------------------------------------------------------------
// 6. Backward: dot-product identity, and every gradient vs central differences
// --------------------------------------------------------------------------
static void test_backward(Activation hidden) {
    std::printf("backward, hidden=%s\n", tttrlib::activation_to_string(hidden).c_str());
    Lcg rng(33);
    const int n_in = 3, n_out = 2, n = 5;
    auto net = make_net({n_in, 5, 4, n_out}, {hidden, hidden, Activation::Identity}, rng);
    std::vector<double> X(n * n_in), V(n * n_in), W(n * n_out), C1(n * n_out), C2(n * n_out);
    for (auto& v : X) v = rng.uniform();
    for (auto& v : V) v = rng.uniform();
    for (auto& v : W) v = rng.uniform();
    for (auto& v : C1) v = rng.uniform();
    for (auto& v : C2) v = rng.uniform();
    const size_t np = mc::n_parameters(net);

    // Dot-product identity: <w, J v> from the order-1 forward, <J^T w, v> from backward.
    {
        mc::Workspace ws;
        mc::forward(net, X.data(), n, ws, 1, V.data());
        const double lhs = dot(W, ws.output_d1());
        mc::Workspace ws0;
        mc::forward(net, X.data(), n, ws0, 0);
        std::vector<double> dp(np, 0.0), dX(n * n_in);
        mc::backward(net, ws0, W.data(), nullptr, nullptr, dp.data(), dX.data());
        const double rhs = dot(dX, V);
        report(std::abs(lhs - rhs) < 1e-12 * (1 + std::abs(lhs)), "<w,Jv> == <J^T w,v>", std::abs(lhs - rhs), 1e-12);
    }

    // Loss over all three Taylor outputs: L = <W,y> + <C1,Jv> + <C2,v^T H v>.
    auto loss = [&](const std::vector<DenseLayer>& nn, const std::vector<double>& x, const std::vector<double>& v) {
        mc::Workspace ws;
        mc::forward(nn, x.data(), n, ws, 2, v.data());
        return dot(W, ws.output()) + dot(C1, ws.output_d1()) + dot(C2, ws.output_d2());
    };

    mc::Workspace ws;
    mc::forward(net, X.data(), n, ws, 2, V.data());
    std::vector<double> dp(np, 0.0), dX(n * n_in), dV(n * n_in);
    mc::backward(net, ws, W.data(), C1.data(), C2.data(), dp.data(), dX.data(), dV.data());

    const double h = 1e-5;
    // parameters
    {
        std::vector<double> p;
        mc::flatten(net, p);
        double worst = 0, scale = 0;
        for (size_t i = 0; i < np; ++i) {
            auto pp = p; pp[i] += h;
            auto pm = p; pm[i] -= h;
            auto np_ = net; mc::unflatten(np_, pp.data(), np);
            auto nm_ = net; mc::unflatten(nm_, pm.data(), np);
            const double fd = (loss(np_, X, V) - loss(nm_, X, V)) / (2 * h);
            worst = std::max(worst, std::abs(fd - dp[i]));
            scale = std::max(scale, std::abs(dp[i]));
        }
        report(worst < 1e-6 * (1 + scale), "dL/dparams (orders 0+1+2) vs central differences", worst, 1e-6 * (1 + scale));
    }
    // inputs
    {
        double worst = 0;
        for (size_t i = 0; i < X.size(); ++i) {
            auto xp = X; xp[i] += h;
            auto xm = X; xm[i] -= h;
            const double fd = (loss(net, xp, V) - loss(net, xm, V)) / (2 * h);
            worst = std::max(worst, std::abs(fd - dX[i]));
        }
        report(worst < 1e-6, "dL/dx vs central differences", worst, 1e-6);
    }
    // directions
    {
        double worst = 0;
        for (size_t i = 0; i < V.size(); ++i) {
            auto vp = V; vp[i] += h;
            auto vm = V; vm[i] -= h;
            const double fd = (loss(net, X, vp) - loss(net, X, vm)) / (2 * h);
            worst = std::max(worst, std::abs(fd - dV[i]));
        }
        report(worst < 1e-6, "dL/dv vs central differences", worst, 1e-6);
    }
    // Order-0 path alone: plain backprop of <W, y> -- the training gradient.
    {
        mc::Workspace w0;
        mc::forward(net, X.data(), n, w0, 0);
        std::vector<double> dp0(np, 0.0);
        mc::backward(net, w0, W.data(), nullptr, nullptr, dp0.data());
        std::vector<double> p;
        mc::flatten(net, p);
        double worst = 0;
        for (size_t i = 0; i < np; ++i) {
            auto pp = p; pp[i] += h;
            auto pm = p; pm[i] -= h;
            auto np_ = net; mc::unflatten(np_, pp.data(), np);
            auto nm_ = net; mc::unflatten(nm_, pm.data(), np);
            mc::Workspace a, b;
            mc::forward(np_, X.data(), n, a, 0);
            mc::forward(nm_, X.data(), n, b, 0);
            const double fd = (dot(W, a.output()) - dot(W, b.output())) / (2 * h);
            worst = std::max(worst, std::abs(fd - dp0[i]));
        }
        report(worst < 1e-7, "order-0 dL/dparams vs central differences", worst, 1e-7);
    }
}

// --------------------------------------------------------------------------
// 7. MlpModel with active scalers: physical-unit predictions and adjoints
// --------------------------------------------------------------------------
static void test_model_scalers() {
    std::printf("MlpModel with scalers: model_predict / model_backward vs raw layers and FD\n");
    Lcg rng(44);
    const int n_in = 2, n_out = 2, n = 4;
    tttrlib::MlpModel m;
    m.layers = make_net({n_in, 6, n_out}, {Activation::Tanh, Activation::Identity}, rng);
    m.x_scaler.mean = {0.3, -1.2}; m.x_scaler.scale = {2.0, 0.5};
    m.y_scaler.mean = {5.0, -2.0}; m.y_scaler.scale = {3.0, 0.25};
    m.validate();
    std::vector<double> X(n * n_in), V(n * n_in), W(n * n_out), C1(n * n_out), C2(n * n_out);
    for (auto& v : X) v = rng.uniform();
    for (auto& v : V) v = rng.uniform();
    for (auto& v : W) v = rng.uniform();
    for (auto& v : C1) v = rng.uniform();
    for (auto& v : C2) v = rng.uniform();

    // predictions equal the raw layers on standardised input, unstandardised after
    std::vector<double> y, d1, d2;
    mc::model_predict(m, X.data(), n, 2, V.data(), y, d1, d2);
    {
        double worst = 0;
        for (int r = 0; r < n; ++r) {
            std::vector<double> xs(n_in);
            for (int i = 0; i < n_in; ++i) xs[i] = (X[r * n_in + i] - m.x_scaler.mean[i]) / m.x_scaler.scale[i];
            std::vector<double> yr;
            mc::predict_scalar(m.layers, xs.data(), yr);
            for (int k = 0; k < n_out; ++k)
                worst = std::max(worst, std::abs(yr[k] * m.y_scaler.scale[k] + m.y_scaler.mean[k] - y[r * n_out + k]));
        }
        report(worst < 1e-13, "y through the scalers", worst, 1e-13);
    }
    // directional derivatives in physical units vs central differences of model_predict
    {
        const double h = 1e-4;
        double w1 = 0, w2 = 0;
        std::vector<double> Xp(X), Xm(X), yp, ym, t1, t2;
        for (size_t i = 0; i < X.size(); ++i) { Xp[i] += h * V[i]; Xm[i] -= h * V[i]; }
        mc::model_predict(m, Xp.data(), n, 0, nullptr, yp, t1, t2);
        mc::model_predict(m, Xm.data(), n, 0, nullptr, ym, t1, t2);
        for (size_t i = 0; i < y.size(); ++i) {
            w1 = std::max(w1, std::abs((yp[i] - ym[i]) / (2 * h) - d1[i]));
            w2 = std::max(w2, std::abs((yp[i] - 2 * y[i] + ym[i]) / (h * h) - d2[i]));
        }
        report(w1 < 1e-6, "J v in physical units vs FD", w1, 1e-6);
        report(w2 < 1e-4, "v^T H v in physical units vs FD", w2, 1e-4);
    }
    // adjoints of L = <W,y> + <C1,Jv> + <C2,vHv> vs central differences
    auto loss = [&](const tttrlib::MlpModel& mm, const std::vector<double>& x, const std::vector<double>& v) {
        std::vector<double> a, b, c;
        mc::model_predict(mm, x.data(), n, 2, v.data(), a, b, c);
        return dot(W, a) + dot(C1, b) + dot(C2, c);
    };
    std::vector<double> dp, dX, dV;
    mc::model_backward(m, X.data(), n, V.data(), W.data(), C1.data(), C2.data(), dp, dX, dV);
    const double h = 1e-5;
    {
        std::vector<double> p; mc::flatten(m.layers, p);
        double worst = 0;
        for (size_t i = 0; i < p.size(); ++i) {
            auto mp = m, mm_ = m; auto pp = p, pm = p; pp[i] += h; pm[i] -= h;
            mc::unflatten(mp.layers, pp.data(), pp.size()); mc::unflatten(mm_.layers, pm.data(), pm.size());
            worst = std::max(worst, std::abs((loss(mp, X, V) - loss(mm_, X, V)) / (2 * h) - dp[i]));
        }
        report(worst < 1e-6, "dL/dparams through the scalers vs FD", worst, 1e-6);
    }
    {
        double wx = 0, wv = 0;
        for (size_t i = 0; i < X.size(); ++i) {
            auto xp = X, xm = X; xp[i] += h; xm[i] -= h;
            wx = std::max(wx, std::abs((loss(m, xp, V) - loss(m, xm, V)) / (2 * h) - dX[i]));
            auto vp = V, vm = V; vp[i] += h; vm[i] -= h;
            wv = std::max(wv, std::abs((loss(m, X, vp) - loss(m, X, vm)) / (2 * h) - dV[i]));
        }
        report(wx < 1e-6, "dL/dx through the scalers vs FD", wx, 1e-6);
        report(wv < 1e-6, "dL/dv through the scalers vs FD", wv, 1e-6);
    }
    // StandardScaler::fit matches the two-pass population statistics
    {
        tttrlib::StandardScaler sc; sc.fit(X.data(), n, n_in);
        double worst = 0;
        for (int j = 0; j < n_in; ++j) {
            double mu = 0; for (int r = 0; r < n; ++r) mu += X[r * n_in + j]; mu /= n;
            double var = 0; for (int r = 0; r < n; ++r) var += (X[r * n_in + j] - mu) * (X[r * n_in + j] - mu);
            worst = std::max(worst, std::abs(sc.mean[j] - mu) + std::abs(sc.scale[j] - std::sqrt(var / n)));
        }
        report(worst < 1e-15, "StandardScaler::fit", worst, 1e-15);
        bool threw = false;
        tttrlib::MlpModel bad = m; bad.x_scaler.mean.push_back(0.0); bad.x_scaler.scale.push_back(1.0);
        try { bad.validate(); } catch (const std::exception&) { threw = true; }
        report(threw, "validate() rejects a scaler of the wrong length", 0, 0);
    }
}

int main() {
    test_activation_derivatives();
    test_dual_ops();
    test_gemm();
    test_flatten();
    for (Activation a : {Activation::Tanh, Activation::Sigmoid, Activation::Softplus,
                         Activation::SiLU, Activation::Sin}) {
        test_forward_taylor(a);
        test_backward(a);
    }
    // ReLU: only the order-0/1 paths are meaningful (f'' == 0), but they must work.
    test_backward(Activation::ReLU);
    test_model_scalers();
    std::printf("%d failure(s)\n", g_failures);
    return g_failures;
}
