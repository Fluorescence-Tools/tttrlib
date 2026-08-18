"""Operations compose *through the registry*.

`describe` / `defaults` / `resolve` / `compose` are the whole composition API:
a step is a registered name plus parameters, so a pipeline can be written by a
`.pto` provenance record, a UI or a config file without tttrlib knowing the
steps in advance. These tests run real pipelines that way -- simulate photons,
select bursts, extract features, correlate, segment an image -- and check the
results equal the direct calls.
"""
import json

import numpy as np
import pytest

import tttrlib


# ------------------------------------------------------------- resolution --

def test_describe_finds_an_entry_in_any_category():
    e = tttrlib.describe("phasor")
    assert e["category"] == "clsm"
    assert e["label"] and e["summary"] and e["description"]
    assert e["api"]


def test_an_unknown_name_is_refused_with_suggestions():
    with pytest.raises(ValueError) as excinfo:
        tttrlib.describe("phaser")
    assert "phasor" in str(excinfo.value)


def test_defaults_come_from_the_schema():
    assert tttrlib.defaults("photon_reassignment")["method"] == "esrrf"
    assert tttrlib.defaults("iso_contours")["vertex_connect_high"] is False


@pytest.mark.parametrize("name", sorted(json.loads(tttrlib.registry_json())["operation"]))
def test_every_operation_resolves_to_something_callable(name):
    """An operation a `.pto` names must be reachable: `resolve` returns either
    a function or the (class, method) pair whose instance the caller holds."""
    import inspect
    target = tttrlib.resolve(name)
    if isinstance(target, tuple):
        cls, attr = target
        assert callable(getattr(cls, attr))
    else:
        assert callable(target) or inspect.isclass(target)


# ------------------------------------------------------------- pipelines --

@pytest.fixture(scope="module")
def photons():
    """A simulated two-colour burst stream: ground truth we own."""
    rng = np.random.default_rng(7)
    macro, chan = [], []
    t = 0
    for _ in range(60):                      # 60 bursts on a sparse background
        t += rng.integers(20_000, 40_000)
        n = rng.integers(60, 140)
        arrivals = np.sort(rng.integers(0, 4_000, n)) + t
        macro.append(arrivals)
        chan.append(rng.integers(0, 2, n))
        t = arrivals[-1]
        gap = rng.integers(5, 20)
        macro.append(np.sort(rng.integers(0, 30_000, gap)) + t)
        chan.append(rng.integers(0, 2, gap))
    macro = np.sort(np.concatenate(macro).astype(np.uint64))
    chan = np.concatenate(chan).astype(np.int8)
    t = tttrlib.TTTR()
    t.append_events(macro, np.zeros(len(macro), np.uint16), chan,
                    np.zeros(len(macro), np.int8))
    # A bare TTTR has no macro-time calibration (-1), and every burst search
    # works in seconds, so give it one: 1 tick = 1 ns.
    t.header.set_macro_time_resolution(1e-9)
    return t


def test_a_burst_pipeline_written_as_registry_names(photons):
    """burst_selection -> burst_significance, composed from names only."""
    # burst_selection's entry is a TTTR method; resolve says so
    cls, attr = tttrlib.resolve("burst_selection")
    assert cls is tttrlib.TTTR and attr == "burst_search_by_name"

    search = tttrlib.compose(
        # the TTTR is the instance; `algorithm` is positional-only, so the
        # adapter passes it as an argument and the rest as parameters
        ("burst_selection", {"L": 20, "m": 5, "T": 5e-6},
         lambda t: ((t, "sliding_window"), {})),
    )
    bursts = np.asarray(search(photons))
    assert bursts.size, "the composed burst search found nothing"
    direct = np.asarray(photons.burst_search_by_name("sliding_window", L=20, m=5, T=5e-6))
    assert np.array_equal(bursts, direct)

    pipeline = tttrlib.compose(
        ("burst_significance", {"background_window": 0.05, "significance_mode": 2},
         lambda t: ((t, bursts.ravel().tolist()), {})),
    )
    sigma = np.asarray(pipeline(photons))
    expected = np.asarray(photons.burst_confidence(bursts.ravel().tolist(), 0.05, 2))
    assert np.array_equal(sigma, expected)
    assert sigma.size == bursts.shape[0]


def test_an_image_pipeline_matches_the_direct_calls():
    """region_segmentation -> iso_contours, two registered steps chained."""
    rng = np.random.default_rng(3)
    image = rng.random((24, 24))
    markers = np.zeros((24, 24), np.int32)
    markers[4, 4], markers[18, 18] = 1, 2

    pipeline = tttrlib.compose(
        ("region_segmentation", {}, lambda img: ((img, markers), {})),
        ("iso_contours", {"level": 1.5, "vertex_connect_high": False},
         lambda labels: ((np.asarray(labels, dtype=float),), {})),
    )
    out = np.asarray(pipeline(image))

    labels = np.asarray(tttrlib.watershed(image, markers), dtype=float)
    direct = np.asarray(tttrlib.marching_squares(labels, 1.5, False))
    assert np.array_equal(out, direct)
    assert pipeline.steps == ["region_segmentation", "iso_contours"]


def test_a_correlation_step_runs_through_the_registry(photons):
    """`fcs_correlation` names the Correlator; a pipeline builds and runs it."""
    entry = tttrlib.describe("fcs_correlation")
    assert "Correlator" in entry["api"]
    assert set(tttrlib.defaults("fcs_correlation")) >= {"method", "n_casc", "n_bins"}

    def correlate(t, method="wahl", n_casc=10, n_bins=8, **_):
        c = tttrlib.Correlator(n_bins=n_bins, n_casc=n_casc)
        c.method = method
        mt = np.asarray(t.macro_times, dtype=np.uint64)
        w = np.ones(len(mt))
        c.set_events(mt, w, mt, w)
        c.run()
        return np.asarray(c.get_corr_normalized())

    for method in tttrlib.registry("correlation_method"):
        g = correlate(photons, method=method, **{k: v for k, v in
                      tttrlib.defaults("fcs_correlation").items() if k in ("n_casc", "n_bins")})
        assert np.all(np.isfinite(g))


def test_a_pipeline_of_unregistered_names_fails_before_running_anything():
    with pytest.raises(ValueError):
        tttrlib.compose(("region_segmentation", {}), ("no_such_step", {}))
