"""State-trajectory (event-based) output of the photon simulator (PRD-005).

The engine already reports positions/states on a fixed stride. That cannot represent a state
that is entered and left between two samples, so these tests pin the event log against the
analytic two-state process it is supposed to reproduce exactly, and against the stride
reporter in the regime where the two disagree.
"""
import numpy as np
import pytest

tttrlib = pytest.importorskip("tttrlib")
if not hasattr(tttrlib, "SimEngine"):
    pytest.skip("tttrlib built without the photon simulator", allow_module_level=True)


def _vd(x):
    return tttrlib.VectorDouble([float(v) for v in x])


def two_state_engine(k01, k10, n_mol=1, windows=20000, dt=0.01, seed=7, q=0.0):
    """A static two-state molecule exchanging spontaneously at (k01, k10), per macro-time unit."""
    s = tttrlib.SimSystem()
    for _ in range(2):
        sp = tttrlib.SimSpecies()
        sp.D = 0.0
        sp.q = _vd([q])
        s.add_species(sp)
    s.set_rate_matrices(_vd([0.0, 0.0, 0.0, 0.0]),            # k_rad: no light-driven transitions
                        _vd([0.0, k01, k10, 0.0]))            # k_nrad: row-major i->j
    s.set_background(_vd([0.0]))
    s.set_box(50.0, 50.0)
    for _ in range(n_mol):
        s.add_fluorophore(0.0, 0.0, 0.0, 0, False)
    st = tttrlib.SimIntegrator()
    st.dt = dt
    st.n_channels = 1
    st.n_ph_max = 10 ** 12
    st.max_windows = windows
    st.seed_diffusion = seed
    st.seed_emission = seed + 1
    eng = tttrlib.SimEngine(s, tttrlib.SimGrid.gaussian3d(0.3, 2.0, 4.0, 8.0, 0.2, 1.0),
                            tttrlib.VectorSimGrid([]), st)
    return eng


# --- the log itself ---------------------------------------------------------------------

def test_nothing_is_recorded_unless_the_log_is_switched_on():
    eng = two_state_engine(20.0, 10.0, windows=2000)
    eng.run()
    assert eng.n_state_events() == 0


def test_the_log_opens_with_a_birth_carrying_the_initial_state():
    eng = two_state_engine(20.0, 10.0, windows=2000)
    eng.set_state_log(True)
    eng.run()
    tr = eng.state_trajectory()
    assert tr["from"][0] == -1 and tr["to"][0] == 0     # born in state 0
    assert tr["window"][0] == 0 and tr["time"][0] == 0.0
    assert (tr["from"][1:] >= 0).all()                  # exactly one birth for one molecule


def test_transitions_chain_so_every_state_leaves_the_one_it_entered():
    eng = two_state_engine(30.0, 15.0, windows=5000)
    eng.set_state_log(True)
    eng.run()
    tr = eng.state_trajectory()
    assert tr["from"][1:].tolist() == tr["to"][:-1].tolist()
    assert np.all(np.diff(tr["macro_time"]) >= 0)       # time-ordered


def test_the_log_is_deterministic_and_thread_count_independent():
    def log_of(threads):
        eng = two_state_engine(25.0, 25.0, n_mol=40, windows=3000)
        eng.set_num_threads(threads)
        eng.set_parallel_threshold(4)
        eng.set_state_log(True)
        eng.run()
        tr = eng.state_trajectory()
        return np.column_stack([tr["macro_time"], tr["molecule"], tr["from"], tr["to"]])

    a, b, c = log_of(1), log_of(1), log_of(4)
    assert np.array_equal(a, b)
    assert np.array_equal(a, c)


# --- does it reproduce the process it claims to? ----------------------------------------

def test_dwell_times_are_exponential_with_the_configured_rates():
    k01, k10 = 40.0, 25.0
    eng = two_state_engine(k01, k10, windows=200000)
    eng.set_state_log(True)
    eng.run()
    tr = eng.state_trajectory()
    t, state = tr["macro_time"], tr["to"]
    dwell, in_state = np.diff(t), state[:-1]
    assert dwell.size > 4000
    # Mean holding time in state i is 1/k_i out; 2% is ~3 sigma at this sample size.
    assert np.mean(dwell[in_state == 0]) == pytest.approx(1.0 / k01, rel=0.05)
    assert np.mean(dwell[in_state == 1]) == pytest.approx(1.0 / k10, rel=0.05)
    # Exponential: standard deviation equals the mean.
    d0 = dwell[in_state == 0]
    assert np.std(d0) == pytest.approx(np.mean(d0), rel=0.08)


def test_occupancy_over_the_whole_run_matches_the_equilibrium_populations():
    k01, k10 = 40.0, 25.0
    eng = two_state_engine(k01, k10, windows=200000)
    eng.set_state_log(True)
    eng.run()
    mols, frac = eng.state_occupancy(windows_per_bin=200000)
    assert mols.tolist() == [0]
    assert frac.shape == (1, 1, 2)
    assert frac[0, 0, 0] == pytest.approx(k10 / (k01 + k10), abs=0.01)
    assert frac.sum() == pytest.approx(1.0)


def test_fractions_sum_to_one_in_every_bin_for_a_molecule_present_throughout():
    eng = two_state_engine(60.0, 30.0, n_mol=5, windows=20000)
    eng.set_state_log(True)
    eng.run()
    _, frac = eng.state_occupancy(windows_per_bin=100)
    assert frac.shape == (5, 200, 2)
    assert np.allclose(frac.sum(axis=2), 1.0)


def test_bin_occupancy_spread_follows_the_exchange_rate():
    # Slow exchange relative to the bin => bins are nearly pure; fast exchange => all bins
    # sit near the equilibrium fraction. The bin-to-bin standard deviation separates them.
    def spread(k):
        eng = two_state_engine(k, k, windows=40000)
        eng.set_state_log(True)
        eng.run()
        _, frac = eng.state_occupancy(windows_per_bin=100)   # bin = 1.0 macro-time unit
        return float(np.std(frac[0, :, 0]))

    assert spread(0.5) > 0.35     # dwell 2.0 >> bin 1.0
    assert spread(500.0) < 0.05   # dwell 0.002 << bin 1.0


# --- what the stride reporter cannot do -------------------------------------------------

def test_the_event_log_sees_exchange_a_stride_snapshot_averages_away():
    # Exchange much faster than the snapshot stride: every strided sample is a single state,
    # so a stride-derived "fraction" can only ever be 0 or 1, while the true occupancy of
    # each window is close to the equilibrium 0.5.
    eng = two_state_engine(500.0, 500.0, windows=20000)
    eng.set_state_log(True)
    eng.set_trajectory_reporter(1)                 # snapshot every window — the finest stride
    eng.run()
    snap = np.asarray(eng.trajectory_species())
    assert set(np.unique(snap)) == {0, 1}          # a snapshot is one state, never a fraction

    _, frac = eng.state_occupancy(windows_per_bin=1)
    per_window = frac[0, :, 0]
    assert per_window.mean() == pytest.approx(0.5, abs=0.02)
    # The snapshot claims each window was purely one state; the log resolves the exchange
    # within it, so almost no window is pure.
    assert np.mean((per_window > 0.02) & (per_window < 0.98)) > 0.9
    assert eng.n_state_events() > 5 * len(snap)    # ~5 transitions per window, all recorded


# --- the reducer ------------------------------------------------------------------------

def _reference_occupancy(tr, dt, n_species, windows_per_bin, n_bins):
    """Literal per-event, per-bin accumulation — the definition the fast path must match."""
    bin_len = windows_per_bin * dt
    t_end = n_bins * bin_len
    mols = np.unique(tr["molecule"])
    out = np.zeros((mols.size, n_bins, n_species))
    order = np.lexsort((tr["macro_time"], tr["molecule"]))
    mol = tr["molecule"][order]
    time = tr["macro_time"][order]
    to = tr["to"][order]
    for mi, m in enumerate(mols):
        idx = np.flatnonzero(mol == m)
        for j, k in enumerate(idx):
            if to[k] < 0:
                continue
            lo = time[k]
            hi = time[idx[j + 1]] if j + 1 < idx.size else t_end
            lo, hi = max(lo, 0.0), min(hi, t_end)
            # Walk the bins one at a time and clip.
            for b in range(n_bins):
                a0, a1 = b * bin_len, (b + 1) * bin_len
                ov = min(hi, a1) - max(lo, a0)
                if ov > 0:
                    out[mi, b, int(to[k])] += ov
    return mols, out / bin_len


def test_the_vectorised_reducer_matches_a_literal_per_bin_accumulation():
    eng = two_state_engine(80.0, 45.0, n_mol=3, windows=4000)
    eng.set_state_log(True)
    eng.run()
    mols, frac = eng.state_occupancy(windows_per_bin=37)
    n_bins = frac.shape[1]
    ref_mols, ref = _reference_occupancy(eng.state_trajectory(), eng.settings().dt, 2, 37, n_bins)
    assert mols.tolist() == ref_mols.tolist()
    assert np.allclose(frac, ref, atol=1e-12)


def test_a_long_dwell_spanning_many_bins_fills_every_bin_it_covers():
    # One transition far into the run: the interior bins must each read a full 1.0, which is
    # the difference-array path in the reducer.
    eng = two_state_engine(0.05, 1e-9, windows=20000)   # leaves state 0 once, then stays
    eng.set_state_log(True)
    eng.run()
    _, frac = eng.state_occupancy(windows_per_bin=10)
    interior = frac[0, :, :].sum(axis=1)
    assert np.allclose(interior, 1.0)
    assert frac[0, -1, 1] == pytest.approx(1.0)          # ends in state 1
    assert frac[0, 0, 0] == pytest.approx(1.0)           # starts in state 0


def test_a_window_range_restricts_the_bins_without_shifting_them():
    eng = two_state_engine(50.0, 50.0, windows=10000)
    eng.set_state_log(True)
    eng.run()
    _, full = eng.state_occupancy(windows_per_bin=100)
    _, part = eng.state_occupancy(windows_per_bin=100, window_start=2000, window_stop=5000)
    assert part.shape[1] == 30
    assert np.allclose(part[0], full[0, 20:50], atol=1e-12)


def test_reducing_an_empty_log_says_so():
    eng = two_state_engine(20.0, 10.0, windows=500)
    eng.run()
    with pytest.raises(ValueError, match="set_state_log"):
        eng.state_occupancy()


# --- with diffusion, coasting and an open volume -----------------------------------------

def test_coasting_molecules_still_report_the_transitions_they_made_while_asleep():
    # per_molecule_skip lets a molecule far from the focus sleep for many windows; its state
    # still evolves under k_nrad and the catch-up must date those transitions correctly.
    def n_events(skip):
        s = tttrlib.SimSystem()
        for _ in range(2):
            sp = tttrlib.SimSpecies()
            sp.D = 1.0
            sp.q = _vd([10.0])
            s.add_species(sp)
        s.set_rate_matrices(_vd([0.0] * 4), _vd([0.0, 30.0, 30.0, 0.0]))
        s.set_background(_vd([0.0]))
        s.set_box(3.0, 6.0)
        for i in range(8):
            s.add_fluorophore(0.5 * i - 2.0, 0.0, 0.0, 0, True)
        st = tttrlib.SimIntegrator()
        st.dt = 0.01
        st.n_channels = 1
        st.n_ph_max = 10 ** 12
        st.max_windows = 4000
        st.per_molecule_skip = skip
        st.seed_diffusion = 3
        st.seed_emission = 4
        eng = tttrlib.SimEngine(s, tttrlib.SimGrid.gaussian3d(0.3, 2.0, 3.0, 6.0, 0.2, 1.0),
                                tttrlib.VectorSimGrid([]), st)
        eng.set_state_log(True)
        eng.run()
        tr = eng.state_trajectory()
        _, frac = eng.state_occupancy(windows_per_bin=4000)
        return eng.n_state_events(), tr, frac

    n_on, tr_on, frac_on = n_events(True)
    n_off, _, frac_off = n_events(False)
    # Coasting changes the RNG path, so the counts differ; what must not change is that the
    # transitions are recorded at all, in order, and give the same equilibrium occupancy.
    assert n_on > 500 and n_off > 500
    assert np.all(np.diff(tr_on["macro_time"]) >= 0)
    assert frac_on[:, 0, 0].mean() == pytest.approx(0.5, abs=0.05)
    assert frac_off[:, 0, 0].mean() == pytest.approx(0.5, abs=0.05)


def test_open_volume_molecules_get_a_birth_and_a_death():
    s = tttrlib.SimSystem()
    for _ in range(2):
        sp = tttrlib.SimSpecies()
        sp.D = 20.0
        sp.q = _vd([5.0])
        s.add_species(sp)
    s.set_rate_matrices(_vd([0.0] * 4), _vd([0.0, 40.0, 40.0, 0.0]))
    s.set_background(_vd([0.0]))
    s.set_box(1.0, 2.0)
    s.set_population(0, 4.0)
    st = tttrlib.SimIntegrator()
    st.dt = 0.01
    st.n_channels = 1
    st.n_ph_max = 10 ** 12
    st.max_windows = 3000
    eng = tttrlib.SimEngine(s, tttrlib.SimGrid.gaussian3d(0.3, 1.0, 1.0, 2.0, 0.1, 1.0),
                            tttrlib.VectorSimGrid([]), st)
    eng.set_state_log(True)
    eng.run()
    tr = eng.state_trajectory()
    assert (tr["from"] == -1).sum() > 4       # the seeded pool plus injected molecules
    assert (tr["to"] == -1).sum() > 0         # some left the box
    # A molecule occupies states only while it is alive, so its total occupancy time must
    # equal its lifetime: birth to death, or birth to the end of the run if it survives.
    mols, frac = eng.state_occupancy(windows_per_bin=100)
    dt = eng.settings().dt
    occupied = frac.sum(axis=(1, 2)) * 100 * dt
    assert frac.sum(axis=2).max() <= 1.0 + 1e-9
    for i, m in enumerate(mols):
        rows = tr["molecule"] == m
        born = tr["macro_time"][rows][0]
        died = tr["macro_time"][rows][-1] if tr["to"][rows][-1] == -1 \
            else eng.current_window() * dt
        assert occupied[i] == pytest.approx(died - born, abs=1e-9)


def test_independent_molecule_mode_records_the_same_kind_of_log():
    s = tttrlib.SimSystem()
    for _ in range(2):
        sp = tttrlib.SimSpecies()
        sp.D = 0.0
        sp.q = _vd([0.0])
        s.add_species(sp)
    s.set_rate_matrices(_vd([0.0] * 4), _vd([0.0, 35.0, 20.0, 0.0]))
    s.set_background(_vd([0.0]))
    s.set_box(50.0, 50.0)
    for _ in range(6):
        s.add_fluorophore(0.0, 0.0, 0.0, 0, False)
    st = tttrlib.SimIntegrator()
    st.dt = 0.01
    st.n_channels = 1
    st.n_ph_max = 10 ** 12
    st.max_windows = 20000
    st.independent_molecules = True
    eng = tttrlib.SimEngine(s, tttrlib.SimGrid.gaussian3d(0.3, 2.0, 4.0, 8.0, 0.2, 1.0),
                            tttrlib.VectorSimGrid([]), st)
    eng.set_state_log(True)
    eng.run()
    tr = eng.state_trajectory()
    assert (tr["from"] == -1).sum() == 6
    assert np.all(np.diff(tr["macro_time"]) >= 0)
    _, frac = eng.state_occupancy(windows_per_bin=20000)
    assert frac[:, 0, 0].mean() == pytest.approx(20.0 / 55.0, abs=0.03)
