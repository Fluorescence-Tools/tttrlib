"""JSON configuration, RNG selection, and trajectory output tests."""
import json

import numpy as np
import pytest

tttrlib = pytest.importorskip("tttrlib")
if not hasattr(tttrlib, "SimEngine"):
    pytest.skip("tttrlib built without the photon simulator", allow_module_level=True)


def _vd(x):
    return tttrlib.VectorDouble([float(v) for v in x])


def test_default_json_parses_and_builds():
    cfg = tttrlib.SimEngine.default_json()
    assert isinstance(json.loads(cfg), dict)
    eng = tttrlib.SimEngine.from_json(cfg)
    assert eng.n_photons() == 0


def test_background_without_decay_warns(capfd):
    """A background with no background_decay writes every background photon
    into micro-time channel 0 -- what scatter looks like, not what
    uncorrelated background is. Until the default changes, the config must
    at least be told (BUGS.md 2026-08-10)."""
    cfg = json.loads(tttrlib.SimEngine.default_json())
    cfg["background"] = [0.02, 0.02]
    cfg.pop("background_decay", None)
    tttrlib.SimEngine.from_json(json.dumps(cfg))
    assert "background_decay" in capfd.readouterr().err

    # Declaring the decay, or an all-zero background, is silent.
    cfg["background_decay"] = {"pattern": [1.0, 1.0], "dt": 0.008}
    tttrlib.SimEngine.from_json(json.dumps(cfg))
    del cfg["background_decay"]
    cfg["background"] = [0.0, 0.0]
    tttrlib.SimEngine.from_json(json.dumps(cfg))
    assert "background_decay" not in capfd.readouterr().err


@pytest.mark.slow
def test_json_configured_run_and_seed_changes_output():
    cfg = {
        "settings": {"dt": 0.01, "n_ph_max": 40000, "max_windows": 10 ** 8,
                     "seed_diffusion": 111, "seed_emission": 222, "n_channels": 2,
                     "rng_kind": "pcg", "rng_scope": "per_molecule"},
        "box": {"xy": 2.0, "z": 4.0},
        "species": [{"D": 3.0, "q": [50.0, 50.0]}],
        "k_rad": [0.0], "k_nrad": [0.0], "background": [0.0, 0.0], "population": [5.0],
        "excitation": {"type": "gaussian3d", "w0": 0.3, "z0": 2.0,
                       "extent_xy": 2.0, "extent_z": 4.0, "spacing": 0.1, "amplitude": 1.0},
        "detection": [],
    }
    e1 = tttrlib.SimEngine.from_json(json.dumps(cfg)); e1.run()
    cfg["settings"]["seed_diffusion"] = 999
    e2 = tttrlib.SimEngine.from_json(json.dumps(cfg)); e2.run()
    assert e1.n_photons() > 0 and e2.n_photons() > 0
    a, b = np.asarray(e1.macro_window()), np.asarray(e2.macro_window())
    assert not (len(a) == len(b) and np.array_equal(a, b))  # different seed => different stream


def test_json_decay_pattern_gives_microtimes():
    n, dt = 512, 0.032
    cfg = {
        "settings": {"dt": 0.01, "n_ph_max": 20000, "max_windows": 10 ** 8, "n_channels": 1,
                     "n_microtime_channels": n, "microtime_resolution": dt, "laser_period": n * dt},
        "species": [{"D": 0.0, "q": [1000.0],
                     "decay": {"pattern": np.exp(-np.arange(n) * dt / 3.0).tolist(), "dt": dt}}],
        "k_rad": [0.0], "k_nrad": [0.0], "background": [0.0], "emitters": [{"x": 0, "y": 0, "z": 0}],
        "excitation": {"type": "gaussian3d", "w0": 0.3, "z0": 1.0,
                       "extent_xy": 0.8, "extent_z": 1.0, "spacing": 0.04, "amplitude": 1.0},
    }
    eng = tttrlib.SimEngine.from_json(json.dumps(cfg)); eng.run()
    micro = np.asarray(eng.micro_time())[np.asarray(eng.event_type()) == 0]
    assert micro.max() > 0 and (micro * dt).mean() > 0.5   # lifetime ~3 ns, truncated axis


def test_trajectory_reporter_and_hdf5(tmp_path):
    s = tttrlib.SimSystem()
    sp = tttrlib.SimSpecies(); sp.D = 3.0; sp.q = _vd([20.0, 20.0]); s.add_species(sp)
    s.set_rate_matrices(_vd([0.0]), _vd([0.0])); s.set_background(_vd([0.0, 0.0]))
    s.set_box(50.0, 50.0)
    for _ in range(20):
        s.add_fluorophore(0.0, 0.0, 0.0, 0, True)
    st = tttrlib.SimIntegrator(); st.dt = 0.01; st.n_channels = 2
    st.n_ph_max = 10 ** 9; st.max_windows = 1000
    eng = tttrlib.SimEngine(s, tttrlib.SimGrid.gaussian3d(0.3, 2.0, 4.0, 8.0, 0.2, 1.0),
                            tttrlib.VectorSimGrid([]), st)
    eng.set_trajectory_reporter(100); eng.run()
    fr = np.asarray(eng.trajectory_frame())
    assert len(fr) == 20 * len(np.unique(fr))   # fixed population, all molecules each frame
    path = str(tmp_path / "traj.h5")
    eng.write_trajectory_hdf5(path)
    h5py = pytest.importorskip("h5py")
    with h5py.File(path, "r") as h:
        g = h["trajectory"]
        assert set(g.keys()) >= {"frame", "id", "x", "y", "z"}
        assert g["x"].shape[0] == len(fr)
        assert np.allclose(np.asarray(g["x"]), np.asarray(eng.trajectory_x()))
