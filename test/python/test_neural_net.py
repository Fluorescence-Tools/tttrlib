"""Tests for tttrlib.NeuralNet — the general MLP used by surrogate models.

These exercise the class on its own terms (no photon data, no H2MM), because it
is meant to be reusable: any future surrogate supplies its own feature extractor
and output decoder but shares this net, its scalers, and its JSON format.
"""
import json
import os
import tempfile

import numpy as np
import pytest

import tttrlib


def _toy_problem(n=1500, seed=1234):
    """Two smooth targets over ``[-1, 1]^2`` — learnable, but not linear."""
    rng = np.random.default_rng(seed)
    X = rng.uniform(-1.0, 1.0, size=(n, 2))
    Y = np.column_stack([
        np.sin(3.0 * X[:, 0]) + X[:, 1] ** 2,
        X[:, 0] * X[:, 1],
    ])
    return X, Y


def _small_options(**kw):
    opt = tttrlib.TrainOptions()
    opt.hidden_layer_sizes = tttrlib.VectorInt32([32, 32])
    opt.max_iter = 200
    opt.batch_size = 64
    opt.learning_rate = 3e-3
    opt.seed = 7
    for k, v in kw.items():
        setattr(opt, k, v)
    return opt


def _handmade_net(w1, b1, w2, b2, act="relu"):
    """Build a 2-layer net from explicit weights via the JSON loader."""
    doc = {
        "format": "tttrlib.neural_net",
        "version": 1,
        "layers": [
            {"n_in": w1.shape[1], "n_out": w1.shape[0], "activation": act,
             "weight": w1.ravel().tolist(), "bias": b1.tolist()},
            {"n_in": w2.shape[1], "n_out": w2.shape[0], "activation": "identity",
             "weight": w2.ravel().tolist(), "bias": b2.tolist()},
        ],
    }
    return tttrlib.NeuralNet.from_json_string(json.dumps(doc))


# ---------------------------------------------------------------------------
# Forward pass against a NumPy reference
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    "act,fn",
    [
        ("relu", lambda z: np.maximum(z, 0.0)),
        ("identity", lambda z: z),
        ("tanh", np.tanh),
        ("logistic", lambda z: 1.0 / (1.0 + np.exp(-z))),
    ],
)
def test_forward_matches_numpy(act, fn):
    """Each activation must reproduce the obvious NumPy expression exactly."""
    rng = np.random.default_rng(0)
    w1 = rng.normal(size=(5, 3))
    b1 = rng.normal(size=5)
    w2 = rng.normal(size=(2, 5))
    b2 = rng.normal(size=2)
    net = _handmade_net(w1, b1, w2, b2, act=act)

    x = rng.normal(size=3)
    expect = w2 @ fn(w1 @ x + b1) + b2
    got = net.predict_np(x)
    np.testing.assert_allclose(got, expect, rtol=0, atol=1e-12)


def test_batch_matches_single():
    """predict_batch must agree with per-sample predict to roundoff."""
    rng = np.random.default_rng(3)
    net = _handmade_net(rng.normal(size=(6, 4)), rng.normal(size=6),
                        rng.normal(size=(3, 6)), rng.normal(size=3))
    X = rng.normal(size=(10, 4))
    batch = net.predict_batch_np(X)
    single = np.array([net.predict_np(row) for row in X])
    np.testing.assert_allclose(batch, single, rtol=0, atol=1e-12)


def test_introspection():
    rng = np.random.default_rng(5)
    w1, b1 = rng.normal(size=(6, 4)), rng.normal(size=6)
    w2, b2 = rng.normal(size=(3, 6)), rng.normal(size=3)
    net = _handmade_net(w1, b1, w2, b2)

    assert net.n_inputs() == 4
    assert net.n_outputs() == 3
    assert net.n_layers() == 2
    assert net.n_parameters() == w1.size + b1.size + w2.size + b2.size
    np.testing.assert_allclose(net.layer_weights(0), w1, rtol=0, atol=1e-15)
    np.testing.assert_allclose(net.layer_bias(1), b2, rtol=0, atol=1e-15)


# ---------------------------------------------------------------------------
# Malformed models must fail at load, not mid-forward
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    "doc,needle",
    [
        # weight count inconsistent with n_in*n_out
        ({"layers": [{"n_in": 2, "n_out": 3, "activation": "relu",
                      "weight": [1, 2, 3], "bias": [0, 0, 0]}]}, "weight"),
        # layers do not chain
        ({"layers": [{"n_in": 2, "n_out": 2, "activation": "relu",
                      "weight": [1, 2, 3, 4], "bias": [0, 0]},
                     {"n_in": 5, "n_out": 1, "activation": "identity",
                      "weight": [1, 1, 1, 1, 1], "bias": [0]}]}, "expects"),
        # unknown activation
        ({"layers": [{"n_in": 2, "n_out": 2, "activation": "banana",
                      "weight": [1, 2, 3, 4], "bias": [0, 0]}]}, "activation"),
        # bias length wrong
        ({"layers": [{"n_in": 2, "n_out": 2, "activation": "relu",
                      "weight": [1, 2, 3, 4], "bias": [0]}]}, "bias"),
        # scaler length does not match the input width
        ({"layers": [{"n_in": 2, "n_out": 1, "activation": "identity",
                      "weight": [1, 1], "bias": [0]}],
          "x_scaler": {"mean": [0, 0, 0], "scale": [1, 1, 1]}}, "x_scaler"),
    ],
)
def test_malformed_model_rejected(doc, needle):
    doc = dict(doc)
    doc["format"] = "tttrlib.neural_net"
    with pytest.raises(Exception) as exc:
        tttrlib.NeuralNet.from_json_string(json.dumps(doc))
    assert needle in str(exc.value)


def test_wrong_input_width_raises():
    rng = np.random.default_rng(9)
    net = _handmade_net(rng.normal(size=(4, 3)), rng.normal(size=4),
                        rng.normal(size=(2, 4)), rng.normal(size=2))
    with pytest.raises(Exception):
        net.predict_np(np.zeros(7))


def test_bad_json_raises():
    with pytest.raises(Exception):
        tttrlib.NeuralNet.from_json_string("{not json")


# ---------------------------------------------------------------------------
# Serialisation
# ---------------------------------------------------------------------------


def test_json_round_trip_preserves_predictions():
    X, Y = _toy_problem(n=400)
    net = tttrlib.NeuralNet.train_np(X, Y, _small_options(max_iter=40))

    back = tttrlib.NeuralNet.from_json_string(net.to_json_string())
    np.testing.assert_allclose(back.predict_batch_np(X), net.predict_batch_np(X),
                               rtol=0, atol=1e-12)


def test_json_file_round_trip():
    X, Y = _toy_problem(n=400)
    net = tttrlib.NeuralNet.train_np(X, Y, _small_options(max_iter=40))

    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "net.json")
        net.to_json_file(path)
        doc = json.load(open(path))
        assert doc["format"] == "tttrlib.neural_net"
        back = tttrlib.NeuralNet.from_json_file(path)
    np.testing.assert_allclose(back.predict_batch_np(X), net.predict_batch_np(X),
                               rtol=0, atol=1e-12)


# ---------------------------------------------------------------------------
# Training
# ---------------------------------------------------------------------------


def test_training_reduces_loss_and_beats_mean_predictor():
    """The point of training: fit better than predicting the target mean."""
    X, Y = _toy_problem(n=1500)
    Xte, Yte = _toy_problem(n=500, seed=99)

    net = tttrlib.NeuralNet.train_np(X, Y, _small_options())

    loss = net.loss_curve_
    assert loss.size > 1
    assert loss[-1] < loss[0], "training loss did not decrease"

    pred = net.predict_batch_np(Xte)
    sse = float(((pred - Yte) ** 2).sum())
    sst = float(((Y.mean(axis=0) - Yte) ** 2).sum())
    assert sse < 0.05 * sst, f"net barely beat the mean predictor ({sse=}, {sst=})"


def test_training_is_deterministic_for_a_seed():
    X, Y = _toy_problem(n=600)
    a = tttrlib.NeuralNet.train_np(X, Y, _small_options(max_iter=30))
    b = tttrlib.NeuralNet.train_np(X, Y, _small_options(max_iter=30))
    np.testing.assert_allclose(a.predict_batch_np(X), b.predict_batch_np(X),
                               rtol=0, atol=0)


def test_scalers_are_fitted_and_stored():
    """Training must standardise and keep the scalers, so predict() is unscaled."""
    X, Y = _toy_problem(n=600)
    X = X * 1000.0 + 5000.0  # far from zero mean / unit variance
    net = tttrlib.NeuralNet.train_np(X, Y, _small_options(max_iter=40))

    doc = json.loads(net.to_json_string())
    assert doc["x_scaler"]["mean"], "x_scaler was not stored"
    np.testing.assert_allclose(doc["x_scaler"]["mean"], X.mean(axis=0), rtol=1e-9)
    np.testing.assert_allclose(doc["x_scaler"]["scale"], X.std(axis=0), rtol=1e-9)


def test_early_stopping_records_validation_curve():
    X, Y = _toy_problem(n=800)
    net = tttrlib.NeuralNet.train_np(X, Y, _small_options(early_stopping=True))
    assert net.validation_curve_.size > 0
    assert net.validation_curve_.size == net.loss_curve_.size


def test_train_rejects_mismatched_rows():
    X, Y = _toy_problem(n=200)
    with pytest.raises(Exception):
        tttrlib.NeuralNet.train_np(X, Y[:100], _small_options(max_iter=5))


@pytest.mark.parametrize("act", ["relu", "tanh", "logistic"])
def test_training_works_for_each_hidden_activation(act):
    X, Y = _toy_problem(n=800)
    opt = _small_options(max_iter=120)
    opt.activation = tttrlib.activation_from_string(act)
    net = tttrlib.NeuralNet.train_np(X, Y, opt)
    assert net.loss_curve_[-1] < net.loss_curve_[0]


# ---------------------------------------------------------------------------
# Agreement with scikit-learn, which trains the surrogates today
# ---------------------------------------------------------------------------


def test_matches_sklearn_forward_pass():
    """A net imported from sklearn weights must predict identically.

    This is the contract that lets a surrogate be trained in Python and executed
    here: same weights, same scalers, same numbers.
    """
    sklearn = pytest.importorskip("sklearn")
    from sklearn.neural_network import MLPRegressor
    from sklearn.preprocessing import StandardScaler

    X, Y = _toy_problem(n=500)
    xs, ys = StandardScaler().fit(X), StandardScaler().fit(Y)
    mlp = MLPRegressor(hidden_layer_sizes=(16, 16), activation="relu",
                       max_iter=60, random_state=0)
    mlp.fit(xs.transform(X), ys.transform(Y))

    # sklearn stores coefs_ as (n_in, n_out); tttrlib wants row-major (n_out, n_in)
    layers = []
    n_hidden = len(mlp.coefs_) - 1
    for i, (w, b) in enumerate(zip(mlp.coefs_, mlp.intercepts_)):
        layers.append({
            "n_in": w.shape[0],
            "n_out": w.shape[1],
            "activation": mlp.activation if i < n_hidden else mlp.out_activation_,
            "weight": np.ascontiguousarray(w.T).ravel().tolist(),
            "bias": np.asarray(b).tolist(),
        })
    doc = {
        "format": "tttrlib.neural_net",
        "version": 1,
        "x_scaler": {"mean": xs.mean_.tolist(), "scale": xs.scale_.tolist()},
        "y_scaler": {"mean": ys.mean_.tolist(), "scale": ys.scale_.tolist()},
        "layers": layers,
    }
    net = tttrlib.NeuralNet.from_json_string(json.dumps(doc))

    expect = ys.inverse_transform(mlp.predict(xs.transform(X)))
    np.testing.assert_allclose(net.predict_batch_np(X), expect, rtol=0, atol=1e-10)


# ---------------------------------------------------------------------------
# Derivatives: the network as a differentiable building block (MlpCore.h)
# ---------------------------------------------------------------------------


def _scaled_net(seed=11, act="tanh"):
    """A trained net, so both scalers are active and the chain rule through
    them is exercised, not just the raw layers."""
    X, Y = _toy_problem(n=300, seed=seed)
    opt = _small_options(max_iter=30)
    opt.hidden_layer_sizes = tttrlib.VectorInt32([8, 8])
    opt.activation = tttrlib.activation_from_string(act)
    return tttrlib.NeuralNet.train_np(X, Y, opt)


@pytest.mark.parametrize("act", ["tanh", "logistic", "softplus", "silu", "sin"])
def test_new_activations_forward_and_json(act):
    """softplus / silu / sin evaluate to the textbook expression and round-trip
    through JSON under their own names."""
    fn = {
        "tanh": np.tanh,
        "logistic": lambda z: 1.0 / (1.0 + np.exp(-z)),
        "softplus": lambda z: np.logaddexp(0.0, z),
        "silu": lambda z: z / (1.0 + np.exp(-z)),
        "sin": np.sin,
    }[act]
    rng = np.random.default_rng(1)
    w1, b1 = rng.normal(size=(5, 3)), rng.normal(size=5)
    w2, b2 = rng.normal(size=(2, 5)), rng.normal(size=2)
    net = _handmade_net(w1, b1, w2, b2, act=act)
    x = rng.normal(size=3)
    np.testing.assert_allclose(net.predict_np(x), w2 @ fn(w1 @ x + b1) + b2, atol=1e-12)
    again = tttrlib.NeuralNet.from_json_string(net.to_json_string())
    assert json.loads(again.to_json_string())["layers"][0]["activation"] == act
    np.testing.assert_allclose(again.predict_np(x), net.predict_np(x), atol=0)


@pytest.mark.parametrize("act", ["softplus", "silu", "sin"])
def test_training_works_for_smooth_activations(act):
    X, Y = _toy_problem(n=800)
    opt = _small_options(max_iter=120)
    opt.activation = tttrlib.activation_from_string(act)
    net = tttrlib.NeuralNet.train_np(X, Y, opt)
    assert net.loss_curve_[-1] < 0.5 * net.loss_curve_[0]


def test_parameters_round_trip_and_drive_predictions():
    net = _scaled_net()
    p = net.parameters
    assert p.shape == (net.n_parameters(),)
    # layout: layer 0 weight (row-major), layer 0 bias, layer 1 weight, ...
    w0 = net.layer_weights(0)
    np.testing.assert_array_equal(p[: w0.size], w0.ravel())
    x = np.array([0.2, -0.4])
    before = net.predict_np(x)
    net.parameters = p * 1.1
    assert not np.allclose(net.predict_np(x), before)
    net.parameters = p
    np.testing.assert_array_equal(net.predict_np(x), before)
    with pytest.raises(RuntimeError):
        net.set_parameters(p[:-1].tolist())


def _fd_grad(f, x, h=1e-6):
    g = np.zeros_like(x)
    for i in range(x.size):
        xp, xm = x.copy(), x.copy()
        xp[i] += h
        xm[i] -= h
        g[i] = (f(xp) - f(xm)) / (2 * h)
    return g


@pytest.mark.parametrize("act", ["tanh", "softplus", "silu", "sin"])
def test_backward_matches_finite_differences(act):
    """dL/dparams and dL/dx from backward() for an arbitrary loss, through the
    stored scalers, against central differences."""
    net = _scaled_net(act=act)
    rng = np.random.default_rng(2)
    X = rng.uniform(-1, 1, size=(6, 2))
    W = rng.normal(size=(6, net.n_outputs()))       # L = <W, y>
    dparams, dx, _ = net.backward_np(X, W)

    def loss_params(p):
        net.parameters = p
        return float(np.sum(W * net.predict_batch_np(X)))

    p0 = net.parameters
    fd = _fd_grad(loss_params, p0)
    net.parameters = p0
    np.testing.assert_allclose(dparams, fd, rtol=1e-6, atol=1e-8)

    def loss_x(xflat):
        return float(np.sum(W * net.predict_batch_np(xflat.reshape(X.shape))))

    np.testing.assert_allclose(dx.ravel(), _fd_grad(loss_x, X.ravel()), rtol=1e-6, atol=1e-8)


def test_jacobian_and_hessian_match_finite_differences():
    net = _scaled_net(act="tanh")
    x = np.array([0.3, -0.2])
    J = net.jacobian_np(x)
    assert J.shape == (net.n_outputs(), net.n_inputs())
    for k in range(net.n_outputs()):
        fd = _fd_grad(lambda xx: net.predict_np(xx)[k], x)
        np.testing.assert_allclose(J[k], fd, rtol=1e-6, atol=1e-8)
        H = net.hessian_np(x, k)
        assert H.shape == (2, 2)
        np.testing.assert_allclose(H, H.T, atol=1e-12)
        fdH = np.array([_fd_grad(lambda xx: net.jacobian_np(xx)[k, i], x, h=1e-5) for i in range(2)])
        np.testing.assert_allclose(H, fdH, rtol=1e-5, atol=1e-6)


def test_predict_derivatives_orders():
    net = _scaled_net(act="silu")
    rng = np.random.default_rng(4)
    X = rng.uniform(-1, 1, size=(5, 2))
    V = rng.normal(size=(5, 2))
    y0, d1, d2 = net.predict_derivatives_np(X, V, order=2)
    np.testing.assert_allclose(y0, net.predict_batch_np(X), atol=1e-13)
    # J v and v^T H v per sample from jacobian/hessian
    for r in range(5):
        J = net.jacobian_np(X[r])
        np.testing.assert_allclose(d1[r], J @ V[r], rtol=1e-10, atol=1e-12)
        for k in range(net.n_outputs()):
            H = net.hessian_np(X[r], k)
            np.testing.assert_allclose(d2[r, k], V[r] @ H @ V[r], rtol=1e-8, atol=1e-10)
    y_only, none1, none2 = net.predict_derivatives_np(X, order=0)
    assert none1 is None and none2 is None
    np.testing.assert_allclose(y_only, y0, atol=0)


def test_backward_derivatives_matches_finite_differences():
    """The adjoint of the Taylor-augmented pass: a loss on y, J v and v^T H v,
    differentiated with respect to the parameters, inputs and directions."""
    net = _scaled_net(act="tanh")
    rng = np.random.default_rng(6)
    X = rng.uniform(-1, 1, size=(4, 2))
    V = rng.normal(size=(4, 2))
    C0, C1, C2 = (rng.normal(size=(4, net.n_outputs())) for _ in range(3))

    def loss(p=None, Xa=None, Va=None):
        if p is not None:
            net.parameters = p
        y, d1, d2 = net.predict_derivatives_np(X if Xa is None else Xa,
                                               V if Va is None else Va, order=2)
        return float(np.sum(C0 * y) + np.sum(C1 * d1) + np.sum(C2 * d2))

    dparams, dx, dv = net.backward_np(X, C0, V=V, dY1=C1, dY2=C2)
    p0 = net.parameters
    fd = _fd_grad(lambda p: loss(p=p), p0, h=1e-5)
    net.parameters = p0
    np.testing.assert_allclose(dparams, fd, rtol=1e-5, atol=1e-7)
    np.testing.assert_allclose(dx.ravel(), _fd_grad(lambda xf: loss(Xa=xf.reshape(X.shape)), X.ravel(), h=1e-5),
                               rtol=1e-5, atol=1e-7)
    np.testing.assert_allclose(dv.ravel(), _fd_grad(lambda vf: loss(Va=vf.reshape(V.shape)), V.ravel(), h=1e-5),
                               rtol=1e-5, atol=1e-7)


def test_pinn_poisson_1d():
    """End to end: a physics-informed fit of u'' = f on [0, 1], u(0) = u(1) = 0,
    with f = -pi^2 sin(pi x), so u = sin(pi x). The loss is the PDE residual at
    collocation points plus the boundary values; its gradient with respect to
    the weights comes from backward_derivatives (order 2, direction v = 1) and
    goes into L-BFGS. This is the whole reason the derivative API exists."""
    scipy = pytest.importorskip("scipy")
    from scipy.optimize import minimize

    # Untrained net with the right shape and smooth activations: build it from
    # a one-epoch training call on dummy data, then drop the scalers by
    # rebuilding from JSON without them.
    rng = np.random.default_rng(0)
    xc = np.linspace(0.0, 1.0, 41)[:, None]                 # collocation points
    xb = np.array([[0.0], [1.0]])                            # boundary points
    f = -np.pi ** 2 * np.sin(np.pi * xc)
    doc = {"format": "tttrlib.neural_net", "version": 1, "layers": []}
    dims = [1, 16, 16, 1]
    for i in range(3):
        n_in, n_out = dims[i], dims[i + 1]
        w = rng.normal(size=(n_out, n_in)) * np.sqrt(2.0 / (n_in + n_out))
        doc["layers"].append({"n_in": n_in, "n_out": n_out,
                              "activation": "tanh" if i < 2 else "identity",
                              "weight": w.ravel().tolist(), "bias": np.zeros(n_out).tolist()})
    net = tttrlib.NeuralNet.from_json_string(json.dumps(doc))
    ones = np.ones_like(xc)

    def objective(p):
        net.parameters = p
        _, _, u_xx = net.predict_derivatives_np(xc, ones, order=2)
        ub, _, _ = net.predict_derivatives_np(xb, order=0)
        r = u_xx - f
        loss = np.mean(r ** 2) + 10.0 * np.mean(ub ** 2)
        g_c, _, _ = net.backward_np(xc, np.zeros_like(r), V=ones, dY2=2 * r / r.size)
        g_b, _, _ = net.backward_np(xb, 10.0 * 2 * ub / ub.size)
        return loss, g_c + g_b

    res = minimize(objective, net.parameters, jac=True, method="L-BFGS-B",
                   options={"maxiter": 800, "ftol": 1e-14, "gtol": 1e-10})
    net.parameters = res.x
    xt = np.linspace(0, 1, 101)[:, None]
    err = np.abs(net.predict_batch_np(xt).ravel() - np.sin(np.pi * xt.ravel())).max()
    assert err < 2e-2, (err, res.message)
