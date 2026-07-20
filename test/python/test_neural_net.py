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
