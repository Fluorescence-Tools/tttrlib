"""Core photon-simulation engine tests (PRD-005): RNG, diffusion, emission, encoder."""
import numpy as np
import pytest

tttrlib = pytest.importorskip("tttrlib")

if not hasattr(tttrlib, "SimEngine"):
    pytest.skip("tttrlib built without the photon simulator", allow_module_level=True)


def _vd(x):
    return tttrlib.VectorDouble([float(v) for v in x])


def _point_sample(q=(50.0, 50.0), D=0.0, mobile=False, n=1):
    s = tttrlib.SimSample()
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
    st = tttrlib.SimSettings(); st.dt = 0.01; st.n_channels = 2
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
    st = tttrlib.SimSettings(); st.dt = 0.01; st.n_channels = 2; st.n_ph_max = 20000
    st.max_windows = 10 ** 8
    eng = tttrlib.SimEngine(s, exc, tttrlib.VectorSimGrid([]), st); eng.run()
    T = np.asarray(eng.macro_window()); t = np.asarray(eng.arrival_time())
    same = T[1:] == T[:-1]
    assert np.all(t[1:][same] >= t[:-1][same])
    assert np.all((t >= 0) & (t < st.dt))


def test_diffusion_msd_recovers_D():
    D, dt = 3.0, 0.01
    s = tttrlib.SimSample()
    sp = tttrlib.SimSpecies(); sp.D = D; sp.q = _vd([0.0, 0.0]); s.add_species(sp)
    s.set_rate_matrices(_vd([0.0]), _vd([0.0])); s.set_background(_vd([0.0, 0.0]))
    s.set_box(1e6, 1e6); s.add_fluorophore(0.0, 0.0, 0.0, 0, True)
    exc = tttrlib.SimGrid.uniform(0.0, 1.0, 2.0, 0.5)  # no photons, pure diffusion
    st = tttrlib.SimSettings(); st.dt = dt; st.n_channels = 2
    st.n_ph_max = 10 ** 9; st.max_windows = 100000
    eng = tttrlib.SimEngine(s, exc, tttrlib.VectorSimGrid([]), st)
    eng.set_trajectory_reporter(1); eng.run()
    p = np.c_[np.asarray(eng.trajectory_x()), np.asarray(eng.trajectory_y()),
              np.asarray(eng.trajectory_z())]
    msd = np.mean(np.sum(np.diff(p, axis=0) ** 2, axis=1))
    assert abs(msd / (6 * D * dt) - 1.0) < 0.05


def test_open_volume_equilibrates_population():
    s = tttrlib.SimSample()
    sp = tttrlib.SimSpecies(); sp.D = 3.0; sp.q = _vd([50.0, 50.0]); s.add_species(sp)
    s.set_rate_matrices(_vd([0.0]), _vd([0.0])); s.set_background(_vd([0.0, 0.0]))
    s.set_box(2.0, 4.0); s.set_population(0, 5.0)
    exc = tttrlib.SimGrid.gaussian3d(0.3, 2.0, 2.0, 4.0, 0.1, 1.0)
    st = tttrlib.SimSettings(); st.dt = 0.01; st.n_channels = 2
    st.n_ph_max = 20000; st.max_windows = 10 ** 7
    eng = tttrlib.SimEngine(s, exc, tttrlib.VectorSimGrid([]), st); eng.run()
    assert 0 < eng.n_molecules() < 50   # bounded around the set population of ~5


@pytest.mark.parametrize("kind", ["Xoshiro", "Pcg", "Philox", "Mt19937"])
def test_rng_thread_count_independent(kind):
    def run(threads):
        s = tttrlib.SimSample()
        sp = tttrlib.SimSpecies(); sp.D = 0.0; sp.q = _vd([50.0, 50.0]); s.add_species(sp)
        s.set_rate_matrices(_vd([0.0]), _vd([0.0])); s.set_background(_vd([0.0, 0.0]))
        rng = np.random.RandomState(3)
        for _ in range(4000):
            s.add_fluorophore(*rng.uniform(-0.8, 0.8, 3).tolist(), 0, False)
        exc = tttrlib.SimGrid.gaussian3d(0.3, 1.0, 1.0, 2.0, 0.05, 1.0)
        st = tttrlib.SimSettings(); st.dt = 0.01; st.n_channels = 2
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


def test_skip_empty_windows_preserves_count_rate():
    """PRD-007 G2: adaptive empty-window skipping keeps the count-rate statistics."""
    def run(skip):
        s = tttrlib.SimSample()
        sp = tttrlib.SimSpecies(); sp.D = 50.0; sp.q = _vd([50.0, 50.0]); s.add_species(sp)
        s.set_rate_matrices(_vd([0.0]), _vd([0.0])); s.set_background(_vd([0.0, 0.0]))
        s.set_box(2.0, 4.0); s.set_population(0, 5.0)
        exc = tttrlib.SimGrid.gaussian3d(0.3, 1.0, 1.2, 2.0, 0.05, 1.0)
        st = tttrlib.SimSettings(); st.dt = 0.001; st.n_channels = 2
        st.n_ph_max = 15000; st.max_windows = 10 ** 9
        st.skip_empty_windows = skip
        eng = tttrlib.SimEngine(s, exc, tttrlib.VectorSimGrid([]), st); eng.run()
        return eng.n_photons() / eng.current_window()
    rate_fixed, rate_skip = run(False), run(True)
    assert abs(rate_skip / rate_fixed - 1.0) < 0.06   # count rate preserved


def test_spc132_encoder_roundtrip(tmp_path):
    s = _point_sample()
    exc = tttrlib.SimGrid.gaussian3d(0.3, 1.0, 1.5, 3.0, 0.05, 1.0)
    st = tttrlib.SimSettings(); st.dt = 0.01; st.n_channels = 2; st.n_ph_max = 50000
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
