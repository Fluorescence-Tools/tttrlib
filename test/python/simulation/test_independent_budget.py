"""PRD-007 G2: the photon-budget stopping contract on the independent/async engine.

The independent engine simulates each molecule's whole timeline over a fixed horizon `W`,
which makes it embarrassingly parallel and ~an order of magnitude faster than the window
engine -- but every molecule's birth count `Poisson(rate_in*W)` and birth time `U[0,W)` are
functions of `W`, so it could only ever be driven by a duration. BurstNet (and this repo's
own test idiom) drive the engine by a *photon* budget with `max_windows` as an "effectively
infinite" sentinel, which used to dispatch `run_independent(10**8)` and try to simulate 10^8
windows. `run()` now searches for the horizon instead and truncates at the budget.
"""
import numpy as np
import pytest

tttrlib = pytest.importorskip("tttrlib")

if not hasattr(tttrlib, "SimEngine"):
    pytest.skip("tttrlib built without the photon simulator", allow_module_level=True)

SENTINEL = 10 ** 8   # the "no window limit" idiom used across the simulation tests


def _vd(x):
    return tttrlib.VectorDouble([float(v) for v in x])


def _engine(independent, n_ph_max, max_windows, seed=12345, box=5.0):
    s = tttrlib.SimSystem()
    sp = tttrlib.SimSpecies(); sp.D = 3.0; sp.q = _vd([200.0, 20.0])
    s.add_species(sp)
    s.set_rate_matrices(_vd([0.0]), _vd([0.0]))
    s.set_background(_vd([2.0, 2.0]))
    s.set_box(box, box); s.set_population(0, 5.0)

    exc = tttrlib.SimGrid.gaussian3d(0.3, 2.0, 3.0, 3.0, 0.1, 1.0)

    st = tttrlib.SimIntegrator()
    st.dt = 0.001
    st.n_channels = 2
    st.n_ph_max = n_ph_max
    st.max_windows = max_windows
    st.per_molecule_skip = True
    st.seed_diffusion = seed
    st.independent_molecules = independent
    return tttrlib.SimEngine(s, exc, tttrlib.VectorSimGrid([]), st)


def _n_photons(eng):
    return int((np.asarray(eng.event_type()) == 0).sum())


def test_photon_budget_is_honoured_exactly():
    """The budget stops the run -- not the sentinel horizon."""
    eng = _engine(True, 20000, SENTINEL)
    eng.run()
    assert _n_photons(eng) == 20000
    # If the sentinel had been taken as the horizon this would be ~10^8.
    assert int(np.asarray(eng.macro_window()).max()) < SENTINEL // 10


def test_budget_works_without_any_window_limit():
    """max_windows = 0 used to fall through to the slow window engine."""
    eng = _engine(True, 15000, 0)
    eng.run()
    assert _n_photons(eng) == 15000


def test_count_rate_matches_the_window_engine():
    """Truncating a fixed-horizon realization must not distort the count rate."""
    win = _engine(False, 20000, SENTINEL); win.run()
    ind = _engine(True, 20000, SENTINEL); ind.run()

    rate_win = _n_photons(win) / (int(np.asarray(win.macro_window()).max()) + 1)
    rate_ind = _n_photons(ind) / (int(np.asarray(ind.macro_window()).max()) + 1)
    assert abs(rate_ind - rate_win) / rate_win < 0.10


def test_search_is_deterministic():
    """Same settings -> same stream, regardless of how many attempts the search took."""
    a = _engine(True, 12000, SENTINEL); a.run()
    b = _engine(True, 12000, SENTINEL); b.run()
    np.testing.assert_array_equal(np.asarray(a.macro_window()), np.asarray(b.macro_window()))
    np.testing.assert_array_equal(np.asarray(a.channel()), np.asarray(b.channel()))
    np.testing.assert_array_equal(np.asarray(a.arrival_time()), np.asarray(b.arrival_time()))


def test_window_horizon_still_binds_when_it_is_the_tighter_limit():
    """A real max_windows with an unreachable photon budget stops at the horizon,
    exactly as the fixed-dt engine does -- and identically to calling run_independent()."""
    searched = _engine(True, 10 ** 9, 200000); searched.run()
    direct = _engine(True, 10 ** 9, 200000); direct.run_independent(200000)

    assert _n_photons(searched) < 10 ** 9          # the budget was never reached
    np.testing.assert_array_equal(np.asarray(searched.macro_window()),
                                  np.asarray(direct.macro_window()))
    np.testing.assert_array_equal(np.asarray(searched.channel()),
                                  np.asarray(direct.channel()))


@pytest.mark.slow
def test_searched_horizon_does_not_bias_the_count_rate():
    """The one statistical risk in searching the horizon.

    The search accepts the first attempt that *reaches* the budget, which conditions on that
    realization having produced at least `n_ph_max` photons — a selection that could bias the
    count rate upward. Compare against the fixed-`dt` window engine across seeds; the paired
    ratio must sit on 1.0.
    """
    seeds = [11, 23, 37, 41, 53, 67, 71, 89]

    def rate(independent, seed):
        eng = _engine(independent, 20000, SENTINEL, seed=seed)
        eng.run()
        return _n_photons(eng) / (int(np.asarray(eng.macro_window()).max()) + 1)

    ratios = np.array([rate(True, s) / rate(False, s) for s in seeds])
    sem = ratios.std(ddof=1) / np.sqrt(len(seeds))
    assert abs(ratios.mean() - 1.0) < max(3 * sem, 0.02)


def test_budget_counts_photons_not_markers():
    """ALEX laser-switch markers ride in the same stream; they must not consume budget."""
    eng = _engine(True, 5000, SENTINEL)
    eng.run()
    et = np.asarray(eng.event_type())
    assert (et == 0).sum() == 5000
