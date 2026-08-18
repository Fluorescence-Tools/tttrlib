"""The correlator's methods are a table, not an if/else chain (T-20260818-05).

`set_correlation_method` refuses a name that is not registered instead of
warning at `run()` and returning an all-zero curve; "default" is the wahl
method; the built-ins are the three names the docs list; the numbers are
what they were.
"""
import numpy as np
import pytest
import tttrlib


def _stream(seed=0, n=20000):
    rng = np.random.default_rng(seed)
    t = np.sort(rng.integers(0, 10_000_000, n)).astype(np.uint64)
    return t, np.ones(n)


def test_the_built_in_names():
    assert list(tttrlib.Correlator.correlation_method_names()) == ["felekyan", "laurence", "wahl"]


def test_default_is_wahl():
    c = tttrlib.Correlator()
    c.method = "default"                 # %attributestring: the getter/setter pair is the property
    assert c.method == "wahl"
    c.method = ""
    assert c.method == "wahl"


def test_an_unknown_method_is_refused_at_set_time():
    c = tttrlib.Correlator()
    with pytest.raises(ValueError, match="registered: felekyan, laurence, wahl"):
        c.method = "felekian"


@pytest.mark.parametrize("method", ["wahl", "felekyan", "laurence"])
def test_each_method_still_correlates(method):
    t1, w1 = _stream(1)
    t2, w2 = _stream(2)
    c = tttrlib.Correlator(n_casc=8, n_bins=8, method=method)
    c.set_macrotimes(t1, t2)
    c.set_weights(w1, w2)
    y = np.asarray(c.correlation)
    assert y.size > 0 and np.isfinite(y).all()
    assert np.any(y != 0.0)
    if method != "felekyan":
        # two independent Poisson streams: G -> 1 at long lags
        assert abs(np.median(y[len(y) // 2:]) - 1.0) < 0.2
