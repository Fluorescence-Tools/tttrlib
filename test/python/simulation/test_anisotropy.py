"""Fluorescence anisotropy tests: photoselection + Perrin depolarisation."""
import numpy as np
import pytest

tttrlib = pytest.importorskip("tttrlib")
if not hasattr(tttrlib, "SimEngine"):
    pytest.skip("tttrlib built without the photon simulator", allow_module_level=True)


def _vd(x):
    return tttrlib.VectorDouble([float(v) for v in x])


def _steady_state_r(r0, D_rot, tau=4.0, n=1024, dt=0.032, nmol=1500):
    pattern = np.exp(-np.arange(n) * dt / tau)          # mono-exponential lifetime
    dec = tttrlib.SimDecay.from_pattern(_vd(pattern), dt, 0.0)
    s = tttrlib.SimSystem()
    sp = tttrlib.SimSpecies(); sp.D = 0.0; sp.q = _vd([100.0, 100.0])
    sp.r0 = r0; sp.l1 = 0.0; sp.l2 = 0.0; sp.D_rot = D_rot; sp.decay = dec
    s.add_species(sp)
    s.set_rate_matrices(_vd([0.0]), _vd([0.0])); s.set_background(_vd([0.0, 0.0]))
    for _ in range(nmol):
        s.add_fluorophore(0.0, 0.0, 0.0, 0, False)
    exc = tttrlib.SimGrid.gaussian3d(0.5, 1.0, 1.0, 2.0, 0.05, 1.0)
    st = tttrlib.SimIntegrator(); st.dt = 0.01; st.n_channels = 2
    st.n_ph_max = 700000; st.max_windows = 10 ** 9
    st.n_microtime_channels = n; st.microtime_resolution = dt; st.laser_period = n * dt
    eng = tttrlib.SimEngine(s, exc, tttrlib.VectorSimGrid([]), st); eng.run()
    ch = np.asarray(eng.channel())[np.asarray(eng.event_type()) == 0]
    i_par, i_perp = (ch == 0).sum(), (ch == 1).sum()
    return (i_par - i_perp) / (i_par + 2 * i_perp)


def test_immobile_anisotropy_matches_r0():
    for r0 in (0.4, 0.2):
        r = _steady_state_r(r0, 0.0)
        assert abs(r - r0) < 0.05


def test_perrin_depolarisation():
    tau = 4.0
    # tau/theta = 1  (theta = 1/6D_rot)  ->  r = r0/2
    D_half = 1.0 / (6.0 * tau)
    r_half = _steady_state_r(0.4, D_half, tau=tau)
    assert abs(r_half - 0.2) < 0.04
    # fast rotation -> strong depolarisation
    r_fast = _steady_state_r(0.4, 1.0, tau=tau)
    assert r_fast < 0.1
