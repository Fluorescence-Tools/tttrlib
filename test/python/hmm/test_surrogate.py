"""Tests for tttrlib.HmmSurrogate — the amortised neural estimator for H2MM.

The C++ feature extractor must reproduce the reference NumPy implementation
*exactly*, because a surrogate trained in Python (scikit-learn) has to be
executable here and vice versa. The reference below is a direct transcription of
that implementation, kept in the test so tttrlib's suite does not depend on
ChiSurf; ``test_matches_chisurf_reference`` additionally checks against the real
thing when it happens to be importable.

The three places a naive port silently diverges are all pinned here:
histogram bin-edge handling and density normalisation, quantile interpolation,
and population-versus-sample standard deviation.
"""
import json
import os
import tempfile

import numpy as np
import pytest

import tttrlib

_AC_LAGS = np.array([1, 2, 4, 8, 16, 32], dtype=np.int64)
_WINDOW = 12


# ---------------------------------------------------------------------------
# Reference implementation (NumPy) — mirrors chisurf .../burst_h2mm/core/surrogate.py
# ---------------------------------------------------------------------------


def _reference_features(times, streams_list, n_streams):
    """Feature vector computed the way the Python/numba implementation does."""
    streams = np.concatenate([np.asarray(s, dtype=np.int64) for s in streams_list])
    offsets = np.concatenate([[0], np.cumsum([len(s) for s in streams_list])]).astype(np.int64)

    scale = 1.0 / max(n_streams - 1, 1)
    n = streams.shape[0]
    mu = float((streams * scale).sum() / n) if n else 0.0

    # Windowed local FRET, via the same *sliding* two-pointer sum the numba
    # kernel uses. This must not be "cleaned up" into a fresh slice.mean() per
    # photon: the running add/subtract accumulates float error differently, and
    # values sitting on a histogram bin edge then land in different bins.
    loc = np.empty(n)
    for b in range(offsets.shape[0] - 1):
        s, e = int(offsets[b]), int(offsets[b + 1])
        a = s
        c = e if s + _WINDOW + 1 > e else s + _WINDOW + 1
        wsum = 0.0
        for k in range(a, c):
            wsum += streams[k] * scale
        for j in range(s, e):
            na = s if j - _WINDOW < s else j - _WINDOW
            nc = e if j + _WINDOW + 1 > e else j + _WINDOW + 1
            while a < na:
                wsum -= streams[a] * scale
                a += 1
            while c < nc:
                wsum += streams[c] * scale
                c += 1
            loc[j] = wsum / (c - a)

    # photon-lag autocorrelation
    ac = np.zeros(_AC_LAGS.shape[0])
    for li, lag in enumerate(_AC_LAGS):
        num, cnt = 0.0, 0
        for b in range(offsets.shape[0] - 1):
            s, e = int(offsets[b]), int(offsets[b + 1])
            m = e - s
            if m > lag:
                x = streams[s:e] * scale - mu
                num += float((x[: m - lag] * x[lag:]).sum()) / (m - lag)
                cnt += 1
        ac[li] = num / cnt if cnt else 0.0

    feats = [mu]
    hist, _ = np.histogram(loc, bins=10, range=(0.0, 1.0), density=True)
    feats += [float(v) for v in hist]
    feats += [float(v) for v in np.quantile(loc, [0.1, 0.25, 0.5, 0.75, 0.9])]
    feats += [float(v) for v in ac]

    # inter-photon gaps: dt to the next photon, excluding each burst's last
    dts = []
    for t in times:
        t = np.asarray(t, dtype=np.int64)
        if t.size > 1:
            dts.append(np.diff(t))
    dt = np.concatenate(dts) if dts else np.zeros(1)
    feats += [float(dt.mean()), float(dt.std())]
    return np.asarray(feats, dtype=np.float64)


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


def _make_dataset(n_bursts=40, burst_len=60, n_streams=2, seed=0, mean_gap=4):
    """Simulate a small two-state dataset and load it into an HMM engine."""
    rng = np.random.default_rng(seed)
    times, streams = [], []
    for _ in range(n_bursts):
        gaps = rng.poisson(mean_gap, burst_len - 1) + 1
        t = np.concatenate([[0], np.cumsum(gaps)]).astype(np.int64)
        # two-state-ish emission so the local-FRET features have structure
        state = rng.random(burst_len) < 0.5
        e = np.where(state, 0.75, 0.25)
        s = (rng.random(burst_len) < e).astype(np.int32)
        if n_streams > 2:
            s = rng.integers(0, n_streams, burst_len).astype(np.int32)
        times.append(t)
        streams.append(s)

    engine = tttrlib.HMM()
    engine.set_bursts(
        tttrlib.VectorVectorInt64([tttrlib.VectorInt64(t.tolist()) for t in times]),
        tttrlib.VectorVectorInt32([tttrlib.VectorInt32(s.tolist()) for s in streams]),
        n_streams,
    )
    return engine, times, streams


# ---------------------------------------------------------------------------
# Feature parity — the whole risk of the port
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    "n_bursts,burst_len,n_streams,seed,mean_gap",
    [
        (40, 60, 2, 0, 4),
        (10, 200, 2, 1, 7),
        (100, 15, 2, 2, 2),      # bursts shorter than the window and most lags
        (25, 80, 3, 3, 4),       # more than two streams
        (5, 40, 4, 4, 11),
        (3, 5, 2, 5, 1),         # every burst shorter than every lag but 1,2,4
    ],
)
def test_features_match_numpy_reference(n_bursts, burst_len, n_streams, seed, mean_gap):
    engine, times, streams = _make_dataset(n_bursts, burst_len, n_streams, seed, mean_gap)
    got = tttrlib.HmmSurrogate.features(engine)
    expect = _reference_features(times, streams, n_streams)

    assert got.shape == (tttrlib.HmmSurrogate.N_FEATURES,)
    np.testing.assert_allclose(got, expect, rtol=0, atol=1e-12)


def test_feature_count_is_stable():
    """24 = 1 mean + 10 histogram + 5 quantiles + 6 lags + 2 gap statistics."""
    assert tttrlib.HmmSurrogate.N_FEATURES == 24


def test_features_are_permutation_invariant():
    """Reordering bursts must not change the summary."""
    rng = np.random.default_rng(11)
    _, times, streams = _make_dataset(30, 50, 2, seed=8)
    order = rng.permutation(len(times))

    def build(ts, ss):
        e = tttrlib.HMM()
        e.set_bursts(
            tttrlib.VectorVectorInt64([tttrlib.VectorInt64(np.asarray(t).tolist()) for t in ts]),
            tttrlib.VectorVectorInt32([tttrlib.VectorInt32(np.asarray(s).tolist()) for s in ss]),
            2,
        )
        return e

    a = tttrlib.HmmSurrogate.features(build(times, streams))
    b = tttrlib.HmmSurrogate.features(build([times[i] for i in order],
                                             [streams[i] for i in order]))
    np.testing.assert_allclose(a, b, rtol=0, atol=1e-12)


def test_matches_chisurf_reference():
    """Cross-check against the actual ChiSurf implementation when importable."""
    surrogate = pytest.importorskip(
        "chisurf.plugins.burst.burst_h2mm.core.surrogate",
        reason="ChiSurf not importable in this environment",
    )
    h2mm_py = pytest.importorskip("chisurf.plugins.burst.burst_h2mm.core.h2mm")

    engine, times, streams = _make_dataset(30, 70, 2, seed=21)
    data = h2mm_py.prepare_bursts([np.asarray(t) for t in times],
                                  [np.asarray(s) for s in streams], 2)
    expect = surrogate.extract_features(data)
    got = tttrlib.HmmSurrogate.features(engine)
    np.testing.assert_allclose(got, expect, rtol=0, atol=1e-12)


# ---------------------------------------------------------------------------
# encode / decode
# ---------------------------------------------------------------------------


def _random_model(n, p, rng):
    obs = rng.dirichlet(np.ones(p), size=n)
    obs = obs[np.argsort(-obs[:, 0])]
    trans = np.eye(n)
    for i in range(n):
        for j in range(n):
            if i != j:
                trans[i, j] = 10.0 ** rng.uniform(-2.8, -1.3)
        trans[i, i] = 1.0 - (trans[i].sum() - trans[i, i])
    prior = np.full(n, 1.0 / n)
    return prior, trans, obs


@pytest.mark.parametrize("n,p", [(2, 2), (3, 2), (2, 3), (4, 3)])
def test_encode_decode_round_trip(n, p):
    """decode(encode(m)) must return the same model, up to the log10 floor."""
    rng = np.random.default_rng(4)
    prior, trans, obs = _random_model(n, p, rng)
    m = tttrlib.HmmModel(tttrlib.VectorDouble(prior.ravel().tolist()),
                          tttrlib.VectorDouble(trans.ravel().tolist()),
                          tttrlib.VectorDouble(obs.ravel().tolist()))

    vec = tttrlib.HmmSurrogate.encode(m)
    assert len(vec) == tttrlib.HmmSurrogate.n_targets(n, p)

    back = tttrlib.HmmSurrogate.decode(vec, n, p)
    np.testing.assert_allclose(back.obs_np, obs, rtol=1e-9, atol=1e-9)
    np.testing.assert_allclose(back.trans_np, trans, rtol=1e-6, atol=1e-9)
    np.testing.assert_allclose(back.prior_np, prior, rtol=1e-9, atol=1e-9)


def test_decode_produces_valid_stochastic_model():
    """Even from noise, decode must return normalised rows."""
    rng = np.random.default_rng(6)
    n, p = 3, 2
    vec = rng.normal(size=tttrlib.HmmSurrogate.n_targets(n, p))
    m = tttrlib.HmmSurrogate.decode(tttrlib.VectorDouble(vec.tolist()), n, p)
    np.testing.assert_allclose(m.obs_np.sum(axis=1), 1.0, atol=1e-12)
    np.testing.assert_allclose(m.trans_np.sum(axis=1), 1.0, atol=1e-12)
    np.testing.assert_allclose(m.prior_np.sum(), 1.0, atol=1e-12)
    assert (m.trans_np >= 0).all() and (m.obs_np >= 0).all()


def test_decode_rejects_wrong_length():
    with pytest.raises(Exception):
        tttrlib.HmmSurrogate.decode(tttrlib.VectorDouble([0.1, 0.2]), 3, 2)


def test_encode_is_label_invariant():
    """Permuting state labels must not change the encoding."""
    rng = np.random.default_rng(13)
    n, p = 3, 2
    prior, trans, obs = _random_model(n, p, rng)
    perm = np.array([2, 0, 1])

    def enc(pr, tr, ob):
        m = tttrlib.HmmModel(tttrlib.VectorDouble(pr.ravel().tolist()),
                              tttrlib.VectorDouble(tr.ravel().tolist()),
                              tttrlib.VectorDouble(ob.ravel().tolist()))
        return np.asarray(tttrlib.HmmSurrogate.encode(m))

    np.testing.assert_allclose(
        enc(prior, trans, obs),
        enc(prior[perm], trans[np.ix_(perm, perm)], obs[perm]),
        rtol=0, atol=1e-12,
    )


# ---------------------------------------------------------------------------
# Training and estimation
# ---------------------------------------------------------------------------


def _train_small(n_states=2, n_streams=2, seed=0):
    opt = tttrlib.TrainOptions()
    opt.hidden_layer_sizes = tttrlib.VectorInt32([64, 64])
    opt.max_iter = 150
    opt.batch_size = 32
    opt.learning_rate = 3e-3
    return tttrlib.HmmSurrogate.train(
        n_states, n_streams, 200, 40, 60, 4.0, opt, seed)


def test_generate_training_set_shapes():
    X, Y = tttrlib.HmmSurrogate.generate_training_set(
        2, 2, n_samples=12, n_bursts=20, burst_len=40, seed=3)
    assert X.shape == (12, tttrlib.HmmSurrogate.N_FEATURES)
    assert Y.shape == (12, tttrlib.HmmSurrogate.n_targets(2, 2))
    assert np.isfinite(X).all() and np.isfinite(Y).all()
    # the simulated datasets must not all be identical
    assert X.std(axis=0).max() > 0


def test_training_produces_usable_surrogate():
    s = _train_small()
    assert s.get_n_states() == 2
    assert s.get_n_streams() == 2
    assert s.get_features_version() == tttrlib.HmmSurrogate.FEATURES_VERSION
    assert s.get_net().n_inputs() == tttrlib.HmmSurrogate.N_FEATURES
    assert s.get_net().n_outputs() == tttrlib.HmmSurrogate.n_targets(2, 2)

    engine, _, _ = _make_dataset(40, 60, 2, seed=77)
    m = s.predict(engine)
    np.testing.assert_allclose(m.obs_np.sum(axis=1), 1.0, atol=1e-12)
    np.testing.assert_allclose(m.trans_np.sum(axis=1), 1.0, atol=1e-12)
    assert m.n_phot == engine.get_n_photons()


def test_surrogate_beats_a_constant_guess():
    """A trained surrogate must track the true emission better than a fixed guess."""
    s = _train_small(seed=5)
    rng = np.random.default_rng(31)

    err_net, err_const = [], []
    for trial in range(6):
        e_lo, e_hi = sorted(rng.uniform(0.15, 0.85, 2))
        times, streams = [], []
        for _ in range(60):
            t = np.concatenate([[0], np.cumsum(rng.poisson(4, 79) + 1)]).astype(np.int64)
            state = rng.random(80) < 0.5
            e = np.where(state, e_hi, e_lo)
            streams.append((rng.random(80) < e).astype(np.int32))
            times.append(t)
        engine = tttrlib.HMM()
        engine.set_bursts(
            tttrlib.VectorVectorInt64([tttrlib.VectorInt64(t.tolist()) for t in times]),
            tttrlib.VectorVectorInt32([tttrlib.VectorInt32(x.tolist()) for x in streams]),
            2,
        )
        pred = np.sort(s.predict(engine).obs_np[:, 1])
        truth = np.sort([e_lo, e_hi])
        err_net.append(np.abs(pred - truth).mean())
        err_const.append(np.abs(np.array([0.5, 0.5]) - truth).mean())

    assert np.mean(err_net) < np.mean(err_const), (
        f"surrogate ({np.mean(err_net):.3f}) no better than a constant guess "
        f"({np.mean(err_const):.3f})")


def test_predict_rejects_wrong_stream_count():
    s = _train_small()
    engine, _, _ = _make_dataset(10, 40, 3, seed=9)
    with pytest.raises(Exception) as exc:
        s.predict(engine)
    assert "n_streams" in str(exc.value)


# ---------------------------------------------------------------------------
# Serialisation
# ---------------------------------------------------------------------------


def test_json_round_trip():
    s = _train_small()
    engine, _, _ = _make_dataset(20, 50, 2, seed=44)

    back = tttrlib.HmmSurrogate.from_json_string(s.to_json_string())
    np.testing.assert_allclose(back.predict(engine).obs_np,
                               s.predict(engine).obs_np, rtol=0, atol=1e-12)


def test_json_file_round_trip_and_shape():
    s = _train_small()
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "surrogate.json")
        s.to_json_file(path)
        doc = json.load(open(path))
        assert doc["format"] == "tttrlib.hmm_surrogate"
        assert doc["n_states"] == 2 and doc["n_streams"] == 2
        assert doc["net"]["format"] == "tttrlib.neural_net"
        tttrlib.HmmSurrogate.from_json_file(path)


def test_stale_features_version_is_rejected():
    """A model built for a different feature layout must fail loudly."""
    s = _train_small()
    doc = json.loads(s.to_json_string())
    doc["features_version"] = tttrlib.HmmSurrogate.FEATURES_VERSION + 1
    with pytest.raises(Exception) as exc:
        tttrlib.HmmSurrogate.from_json_string(json.dumps(doc))
    assert "features_version" in str(exc.value)


def test_wrong_format_is_rejected():
    s = _train_small()
    doc = json.loads(s.to_json_string())
    doc["format"] = "tttrlib.neural_net"
    with pytest.raises(Exception):
        tttrlib.HmmSurrogate.from_json_string(json.dumps(doc))


def test_net_output_width_must_match_state_count():
    """A net whose output width disagrees with n_states must be refused."""
    s = _train_small()
    doc = json.loads(s.to_json_string())
    doc["n_states"] = 3  # net still produces the 2-state target width
    with pytest.raises(Exception) as exc:
        tttrlib.HmmSurrogate.from_json_string(json.dumps(doc))
    assert "outputs" in str(exc.value)
