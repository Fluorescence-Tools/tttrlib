"""CLSM scanning + FLIM micro-time tests (PRD-005)."""
import numpy as np
import pytest

tttrlib = pytest.importorskip("tttrlib")
if not hasattr(tttrlib, "SimEngine"):
    pytest.skip("tttrlib built without the photon simulator", allow_module_level=True)


def _vd(x):
    return tttrlib.VectorDouble([float(v) for v in x])


def test_clsm_scan_reconstructs_shape():
    N, px = 16, 0.5
    yy, xx = np.mgrid[0:N, 0:N]
    mask = (((xx - (N - 1) / 2) ** 2 + (yy - (N - 1) / 2) ** 2) <= 5 ** 2).astype(int)
    s = tttrlib.SimSystem()
    sp = tttrlib.SimSpecies(); sp.D = 0.0; sp.q = _vd([2000.0]); s.add_species(sp)
    s.set_rate_matrices(_vd([0.0]), _vd([0.0])); s.set_background(_vd([0.0]))
    for iy in range(N):
        for ix in range(N):
            if mask[iy, ix]:
                s.add_fluorophore(ix * px, iy * px, 0.0, 0, False)
    exc = tttrlib.SimGrid.gaussian3d(0.2, 1.0, 0.8, 1.0, 0.04, 1.0)
    st = tttrlib.SimIntegrator(); st.dt = 0.01; st.n_channels = 1; st.n_ph_max = 10 ** 9
    eng = tttrlib.SimEngine(s, exc, tttrlib.VectorSimGrid([]), st)
    eng.run_scan(tttrlib.SimScanner.uniform(N, N, 0.05, px, px, 0.0, 0.0,
                                            tttrlib.SimMarkerConfig(), False))
    data = tttrlib.TTTR(np.asarray(eng.macro_window(), np.uint64),
                        np.zeros(len(eng.macro_window()), np.uint16),
                        np.asarray(eng.channel(), np.int8),
                        np.asarray(eng.event_type(), np.int8))
    clsm = tttrlib.CLSMImage(tttr_data=data, marker_frame_start=[4], marker_line_start=1,
                             marker_line_stop=2, n_pixel_per_line=N, use_pixel_markers=True,
                             marker_pixel=8, settings={"n_lines": N})
    clsm.fill()
    rec = np.asarray(clsm.intensity)[0]
    assert rec.shape == (N, N)
    corr = np.corrcoef(rec.ravel(), mask.ravel())[0, 1]
    assert corr > 0.7
    assert rec[mask == 1].mean() > 10 * (rec[mask == 0].mean() + 1)


def test_flim_recovers_arbitrary_decay_pattern():
    n, dt = 4096, 0.008
    t = np.arange(n) * dt
    decay = np.exp(-t / 2.5) + 0.4 * np.exp(-t / 0.6)
    irf = np.zeros(n); irf[8] = 1.0; irf[12] = 0.6; irf[16] = 0.2
    pattern = np.array(tttrlib.SimDecay.convolve(_vd(decay), _vd(irf)))
    dec = tttrlib.SimDecay.from_pattern(_vd(pattern), dt, 0.0)

    s = tttrlib.SimSystem()
    sp = tttrlib.SimSpecies(); sp.D = 0.0; sp.q = _vd([1000.0]); sp.decay = dec
    s.add_species(sp)
    s.set_rate_matrices(_vd([0.0]), _vd([0.0])); s.set_background(_vd([0.0]))
    s.add_fluorophore(0.0, 0.0, 0.0, 0, False)
    st = tttrlib.SimIntegrator(); st.dt = 0.01; st.n_channels = 1; st.n_ph_max = 200000
    st.n_microtime_channels = n; st.microtime_resolution = dt; st.laser_period = n * dt
    eng = tttrlib.SimEngine(s, tttrlib.SimGrid.gaussian3d(0.3, 1.0, 0.8, 1.0, 0.04, 1.0),
                            tttrlib.VectorSimGrid([]), st)
    eng.run()
    micro = np.asarray(eng.micro_time())[np.asarray(eng.event_type()) == 0]
    hist = np.bincount(micro, minlength=n)[:n].astype(float)
    corr = np.corrcoef(hist / hist.sum(), pattern / pattern.sum())[0, 1]
    assert corr > 0.99


def test_background_micro_time_follows_pattern():
    """PRD-007 G1: background photons carry a configurable micro-time distribution."""
    n, dt = 512, 0.032
    bg_pat = np.exp(-np.arange(n) * dt / 0.5); bg_pat[:3] = 0.0   # scatter-like
    s = tttrlib.SimSystem()
    sp = tttrlib.SimSpecies(); sp.D = 0.0; sp.q = _vd([1.0]); s.add_species(sp)
    s.set_rate_matrices(_vd([0.0]), _vd([0.0]))
    s.set_background(_vd([1.0]))
    s.set_background_decay(tttrlib.SimDecay.from_pattern(_vd(bg_pat), dt, 0.0))
    s.add_fluorophore(100.0, 100.0, 100.0, 0, False)   # far from focus -> mostly background
    st = tttrlib.SimIntegrator(); st.dt = 0.01; st.n_channels = 1; st.n_ph_max = 60000
    st.n_microtime_channels = n; st.microtime_resolution = dt; st.laser_period = n * dt
    eng = tttrlib.SimEngine(s, tttrlib.SimGrid.gaussian3d(0.3, 1.0, 0.8, 1.0, 0.04, 1.0),
                            tttrlib.VectorSimGrid([]), st)
    eng.run()
    sp_arr = np.asarray(eng.emitting_species())
    micro = np.asarray(eng.micro_time())
    et = np.asarray(eng.event_type())
    bg = micro[(et == 0) & (sp_arr == 1)]        # emitting_species == n_species => background
    assert len(np.unique(bg)) > 5                # non-degenerate (not all 0)
    hist = np.bincount(bg, minlength=n)[:n].astype(float)
    assert np.corrcoef(hist / hist.sum(), bg_pat / bg_pat.sum())[0, 1] > 0.9
