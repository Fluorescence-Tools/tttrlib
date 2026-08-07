"""Core photon-simulation engine tests: RNG, diffusion, emission, encoder."""
import numpy as np
import pytest

tttrlib = pytest.importorskip("tttrlib")

if not hasattr(tttrlib, "SimEngine"):
    pytest.skip("tttrlib built without the photon simulator", allow_module_level=True)


def _vd(x):
    return tttrlib.VectorDouble([float(v) for v in x])


def _point_sample(q=(50.0, 50.0), D=0.0, mobile=False, n=1):
    s = tttrlib.SimSystem()
    sp = tttrlib.SimSpecies(); sp.D = D; sp.q = _vd(q)
    s.add_species(sp)
    s.set_rate_matrices(_vd([0.0]), _vd([0.0]))
    s.set_background(_vd([0.0] * len(q)))
    for _ in range(n):
        s.add_fluorophore(0.0, 0.0, 0.0, 0, mobile)
    return s


def test_immobile_count_rate_and_channel_split():
    s = _point_sample(q=(50.0, 50.0))
    exc = tttrlib.SimGrid.gaussian3d(0.3, 1.0, 1.5, 3.0, 0.05, 1.0)
    st = tttrlib.SimIntegrator(); st.dt = 0.01; st.n_channels = 2
    st.n_ph_max = 40000; st.max_windows = 10 ** 8
    eng = tttrlib.SimEngine(s, exc, tttrlib.VectorSimGrid([]), st)
    eng.run()
    assert eng.n_photons() >= 40000
    # rate = Iex(0)*sum(q)*dt = 1*100*0.01 = 1 photon/window
    per_window = eng.n_photons() / eng.current_window()
    assert 0.9 < per_window < 1.1
    ch = np.asarray(eng.channel())
    frac0 = (ch == 0).mean()
    assert abs(frac0 - 0.5) < 0.03


def test_within_window_time_sorted():
    s = _point_sample()
    exc = tttrlib.SimGrid.gaussian3d(0.3, 1.0, 1.5, 3.0, 0.05, 1.0)
    st = tttrlib.SimIntegrator(); st.dt = 0.01; st.n_channels = 2; st.n_ph_max = 20000
    st.max_windows = 10 ** 8
    eng = tttrlib.SimEngine(s, exc, tttrlib.VectorSimGrid([]), st); eng.run()
    T = np.asarray(eng.macro_window()); t = np.asarray(eng.arrival_time())
    same = T[1:] == T[:-1]
    assert np.all(t[1:][same] >= t[:-1][same])
    assert np.all((t >= 0) & (t < st.dt))


def test_diffusion_msd_recovers_D():
    D, dt = 3.0, 0.01
    s = tttrlib.SimSystem()
    sp = tttrlib.SimSpecies(); sp.D = D; sp.q = _vd([0.0, 0.0]); s.add_species(sp)
    s.set_rate_matrices(_vd([0.0]), _vd([0.0])); s.set_background(_vd([0.0, 0.0]))
    s.set_box(1e6, 1e6); s.add_fluorophore(0.0, 0.0, 0.0, 0, True)
    exc = tttrlib.SimGrid.uniform(0.0, 1.0, 2.0, 0.5)  # no photons, pure diffusion
    st = tttrlib.SimIntegrator(); st.dt = dt; st.n_channels = 2
    st.n_ph_max = 10 ** 9; st.max_windows = 100000
    eng = tttrlib.SimEngine(s, exc, tttrlib.VectorSimGrid([]), st)
    eng.set_trajectory_reporter(1); eng.run()
    p = np.c_[np.asarray(eng.trajectory_x()), np.asarray(eng.trajectory_y()),
              np.asarray(eng.trajectory_z())]
    msd = np.mean(np.sum(np.diff(p, axis=0) ** 2, axis=1))
    assert abs(msd / (6 * D * dt) - 1.0) < 0.05


def test_open_volume_equilibrates_population():
    s = tttrlib.SimSystem()
    sp = tttrlib.SimSpecies(); sp.D = 3.0; sp.q = _vd([50.0, 50.0]); s.add_species(sp)
    s.set_rate_matrices(_vd([0.0]), _vd([0.0])); s.set_background(_vd([0.0, 0.0]))
    s.set_box(2.0, 4.0); s.set_population(0, 5.0)
    exc = tttrlib.SimGrid.gaussian3d(0.3, 2.0, 2.0, 4.0, 0.1, 1.0)
    st = tttrlib.SimIntegrator(); st.dt = 0.01; st.n_channels = 2
    st.n_ph_max = 20000; st.max_windows = 10 ** 7
    eng = tttrlib.SimEngine(s, exc, tttrlib.VectorSimGrid([]), st); eng.run()
    assert 0 < eng.n_molecules() < 50   # bounded around the set population of ~5


def test_per_molecule_skip_preserves_statistics():
    """Per-molecule coasting keeps count-rate and open-volume population.

    Far molecules sleep and catch up exactly on wake; the coast is bounded by the
    distance to the nearest boundary (focus or box surface), so a sleeper reaches
    neither the focus (no missed photons) nor the surface (no missed deaths).
    """
    def run(skip, n_ph_max, max_windows):
        s = tttrlib.SimSystem()
        sp = tttrlib.SimSpecies(); sp.D = 3.0; sp.q = _vd([200.0, 20.0]); s.add_species(sp)
        s.set_rate_matrices(_vd([0.0]), _vd([0.0])); s.set_background(_vd([2.0, 2.0]))
        s.set_box(5.0, 5.0); s.set_population(0, 5.0)
        exc = tttrlib.SimGrid.gaussian3d(0.3, 2.0, 5.0, 5.0, 0.1, 1.0)
        st = tttrlib.SimIntegrator(); st.dt = 0.001; st.n_channels = 2
        st.n_ph_max = n_ph_max; st.max_windows = max_windows
        st.per_molecule_skip = skip
        eng = tttrlib.SimEngine(s, exc, tttrlib.VectorSimGrid([]), st); eng.run()
        return eng

    # count rate preserved vs fixed-dt
    r_off = run(False, 6000, 0); r_on = run(True, 6000, 0)
    rate_off = r_off.n_photons() / r_off.current_window()
    rate_on = r_on.n_photons() / r_on.current_window()
    assert abs(rate_on / rate_off - 1.0) < 0.08

    # open-volume population stays bounded (does not grow with window count)
    for mw in (100000, 400000):
        assert 0 < run(True, 10 ** 9, mw).n_molecules() < 40


def test_counter_rng_seek_is_constant_time():
    """Reseeding a counter-based RNG must not cost more the further in you seek.

    `SimEngine` reseeds every molecule every window at `window * kWindowStride`,
    so if `reset` walks to that offset by *burning* draws -- as it once did --
    the cost grows with the window index and the whole run becomes quadratic in
    window count.  At a few thousand molecules that reached ~1e10 draws and
    never finished, so `test_rng_thread_count_independent[Philox]` hung and no
    full suite run could complete.

    Asserted as a *ratio against Xoshiro*, whose reset was always O(1), so the
    comparison cancels machine speed and CI load rather than hard-coding a
    wall-clock budget.  The regression it guards against is worth ~1e5x here, so
    a 20x bound is loose enough never to flap and tight enough to catch it.
    """
    import time

    def run(kind, max_windows):
        s = tttrlib.SimSystem()
        sp = tttrlib.SimSpecies(); sp.D = 0.0; sp.q = _vd([50.0, 50.0]); s.add_species(sp)
        s.set_rate_matrices(_vd([0.0]), _vd([0.0])); s.set_background(_vd([0.0, 0.0]))
        rng = np.random.RandomState(3)
        for _ in range(400):
            s.add_fluorophore(*rng.uniform(-0.8, 0.8, 3).tolist(), 0, False)
        exc = tttrlib.SimGrid.gaussian3d(0.3, 1.0, 1.0, 2.0, 0.05, 1.0)
        st = tttrlib.SimIntegrator(); st.dt = 0.01; st.n_channels = 2
        st.n_ph_max = 10 ** 9          # window-limited, so the reseed cost dominates
        st.max_windows = max_windows
        st.rng_kind = getattr(tttrlib, "SimRngKind_" + kind)
        eng = tttrlib.SimEngine(s, exc, tttrlib.VectorSimGrid([]), st)
        eng.set_num_threads(1); eng.set_parallel_threshold(10 ** 18)
        t0 = time.perf_counter(); eng.run()
        return time.perf_counter() - t0

    windows = 3000
    t_ref = max(run("Xoshiro", windows), 1e-4)
    t_ctr = run("Philox", windows)
    assert t_ctr < 20.0 * t_ref, (
        f"Philox reseed looks superlinear in the counter: {t_ctr:.3f}s vs "
        f"Xoshiro {t_ref:.3f}s -- SimCounterRandom::reset should seek, not burn")


# only Mt19937 is expensive here — the other three are ~0.1s, and marking the
# whole function would drop them from the fast lane for nothing
@pytest.mark.parametrize("kind", [
    "Xoshiro", "Pcg", "Philox",
    pytest.param("Mt19937", marks=pytest.mark.slow),
])
def test_rng_thread_count_independent(kind):
    def run(threads):
        s = tttrlib.SimSystem()
        sp = tttrlib.SimSpecies(); sp.D = 0.0; sp.q = _vd([50.0, 50.0]); s.add_species(sp)
        s.set_rate_matrices(_vd([0.0]), _vd([0.0])); s.set_background(_vd([0.0, 0.0]))
        rng = np.random.RandomState(3)
        for _ in range(4000):
            s.add_fluorophore(*rng.uniform(-0.8, 0.8, 3).tolist(), 0, False)
        exc = tttrlib.SimGrid.gaussian3d(0.3, 1.0, 1.0, 2.0, 0.05, 1.0)
        st = tttrlib.SimIntegrator(); st.dt = 0.01; st.n_channels = 2
        st.n_ph_max = 60000; st.max_windows = 10 ** 8
        st.rng_kind = getattr(tttrlib, "SimRngKind_" + kind)
        eng = tttrlib.SimEngine(s, exc, tttrlib.VectorSimGrid([]), st)
        eng.set_num_threads(threads)
        eng.set_parallel_threshold(2048 if threads > 1 else 10 ** 18)
        eng.run()
        return np.asarray(eng.macro_window()), np.asarray(eng.channel())
    t1, c1 = run(1)
    t8, c8 = run(8)
    assert np.array_equal(t1, t8) and np.array_equal(c1, c8)


def test_spc132_encoder_roundtrip(tmp_path):
    s = _point_sample()
    exc = tttrlib.SimGrid.gaussian3d(0.3, 1.0, 1.5, 3.0, 0.05, 1.0)
    st = tttrlib.SimIntegrator(); st.dt = 0.01; st.n_channels = 2; st.n_ph_max = 50000
    st.max_windows = 10 ** 8
    eng = tttrlib.SimEngine(s, exc, tttrlib.VectorSimGrid([]), st); eng.run()
    enc = tttrlib.SimMicrotimeEncoder()
    enc.n_channels = 2; enc.tw = 0.01
    enc.ch_conversion = tttrlib.VectorUint16([8, 0, 9, 1, 10, 2])
    rec = eng.encode(enc, tttrlib.SimRandom(1))
    words = np.frombuffer(bytes(bytearray(rec.bytes)), np.uint32)
    path = str(tmp_path / "sim.spc")
    with open(path, "wb") as f:
        f.write(bytes(enc.file_bytes(rec.bytes)))
    t = tttrlib.TTTR(path, "SPC-130")
    assert t.get_n_valid_events() == eng.n_photons()


# ──────────────────────────────────────────────────────────────────────────────
# Config errors are errors, not defaults
# ──────────────────────────────────────────────────────────────────────────────
def _minimal_config():
    """Return the smallest config that runs, as a mutable dict."""
    import json
    cfg = json.loads(tttrlib.SimEngine.default_json())
    cfg["settings"]["n_ph_max"] = 200
    cfg["settings"]["max_windows"] = 10 ** 6
    return cfg


def test_one_rate_matrix_without_the_other_is_refused():
    """Accepting one and dropping it would run the scheme static and say nothing.

    ``k_rad`` is scaled by the local excitation intensity and ``k_nrad`` is
    spontaneous, so a caller who supplies only the spontaneous rates means
    something definite. Silently ignoring both used to make the simulation come
    out with no exchange at all, which reads downstream as "the model cannot
    recover this rate" rather than as a configuration mistake.
    """
    cfg = _minimal_config()
    del cfg["k_rad"]                       # spontaneous rates only
    with pytest.raises(ValueError, match="together"):
        tttrlib.SimEngine.from_dict(cfg)

    cfg["k_rad"] = [0.0] * len(cfg["k_nrad"])
    tttrlib.SimEngine.from_dict(cfg)       # both present: fine


def test_a_misspelled_key_is_refused_rather_than_defaulted():
    """Every setting has a default, so a typo is otherwise invisible."""
    cfg = _minimal_config()
    cfg["settings"]["seed_emmision"] = 7          # one 's'
    with pytest.raises(ValueError, match="seed_emmision"):
        tttrlib.SimEngine.from_dict(cfg)

    cfg = _minimal_config()
    cfg["exitation"] = {"type": "uniform"}        # one 'c'
    with pytest.raises(ValueError, match="exitation"):
        tttrlib.SimEngine.from_dict(cfg)


def test_the_background_decay_takes_lifetimes_like_a_species_decay():
    """Scatter is written as a lifetime and a prompt, not as a hand-built array.

    ``species[].decay`` has always accepted ``lifetimes``/``amplitudes``/``irf``;
    ``background_decay`` accepted only a raw ``pattern`` and ignored the rest
    without complaint, so a background configured the natural way came out flat.
    """
    def background_delays_ns(decay_block):
        """Return background photon delays in ns, on the engine's micro-time grid."""
        cfg = _minimal_config()
        cfg["species"][0]["q"] = [0.0, 0.0]        # background only
        cfg["background"] = [0.5, 0.5]
        cfg["background_decay"] = decay_block
        eng = tttrlib.SimEngine.from_dict(cfg)
        eng.run()
        resolution = eng.settings().microtime_resolution
        return np.asarray(eng.photons()["micro_time"]) * resolution

    period = _minimal_config()["settings"]["laser_period"]
    n_bins, dt = 4096, 0.008

    lifetimes = background_delays_ns({"lifetimes": [2.0], "n_bins": n_bins, "dt": dt})
    assert lifetimes.size > 50
    # A 2 ns decay, not the half-period a flat background would give.
    assert lifetimes.mean() == pytest.approx(2.0, rel=0.3)
    assert lifetimes.mean() < 0.25 * period

    # And the raw-pattern form still works, unchanged.
    pattern = np.zeros(n_bins)
    pattern[125] = 1.0                              # 125 * 0.008 ns = 1.0 ns
    only = background_delays_ns({"pattern": list(pattern), "dt": dt})
    assert np.allclose(only, 1.0, atol=dt)
