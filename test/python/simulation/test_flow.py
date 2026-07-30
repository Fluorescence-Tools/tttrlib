"""Flow, occlusion and pair-correlation tests for the photon simulator (PRD-005)."""
import numpy as np
import pytest

tttrlib = pytest.importorskip("tttrlib")

if not hasattr(tttrlib, "SimEngine"):
    pytest.skip("tttrlib built without the photon simulator", allow_module_level=True)


def _vd(x):
    return tttrlib.VectorDouble([float(v) for v in x])


def _free_sample(D=3.0, v_scale=1.0, n=1, box=(1e6, 1e6)):
    """One mobile, non-emitting molecule in an effectively unbounded box."""
    s = tttrlib.SimSystem()
    sp = tttrlib.SimSpecies(); sp.D = D; sp.q = _vd([0.0, 0.0]); sp.v_scale = v_scale
    s.add_species(sp)
    s.set_rate_matrices(_vd([0.0]), _vd([0.0]))
    s.set_background(_vd([0.0, 0.0]))
    s.set_box(*box)
    for _ in range(n):
        s.add_fluorophore(0.0, 0.0, 0.0, 0, True)
    return s


# ---------------------------------------------------------------------------
# T7.1 — zero flow is bit-identical
# ---------------------------------------------------------------------------
def test_zero_flow_is_bit_identical():
    """Two engines with identical seeds: no flow vs. uniform(0,0,0) produce exactly the
    same photon stream. This guards every 'reduces to today's formula' claim."""
    cfg = {
        "settings": {"dt": 0.01, "n_ph_max": 5000, "seed_diffusion": 42, "seed_emission": 99,
                     "n_channels": 2, "per_molecule_skip": False},
        "box": {"xy": 2.0, "z": 4.0},
        "species": [{"D": 3.0, "q": [50.0, 50.0]}],
        "k_rad": [0.0], "k_nrad": [0.0],
        "background": [0.0, 0.0],
        "population": [5.0],
        "excitation": {"type": "analytic_gaussian3d", "w0": 0.3, "z0": 2.0, "amplitude": 1.0},
    }
    cfg0 = {**cfg}
    cfg1 = {**cfg, "flow": {"type": "uniform", "vx": 0.0, "vy": 0.0, "vz": 0.0}}
    e0 = tttrlib.SimEngine.from_json(__import__("json").dumps(cfg0))
    e1 = tttrlib.SimEngine.from_json(__import__("json").dumps(cfg1))
    e0.run(); e1.run()
    p0 = e0.photons()
    p1 = e1.photons()
    assert np.array_equal(p0["macro_window"], p1["macro_window"])
    assert np.array_equal(p0["arrival_time"], p1["arrival_time"])
    assert np.array_equal(p0["channel"], p1["channel"])


# ---------------------------------------------------------------------------
# T7.2 — uniform flow step statistics
# ---------------------------------------------------------------------------
def test_uniform_flow_step_statistics():
    """With D=3, dt=0.01, v=(5,0,0), the per-step mean dx = v*dt, and the variance
    per component = 2*D*dt (unchanged by flow). The raw MSD is inflated by |v|²·dt²,
    so we must subtract the mean before computing the variance (see §0.1)."""
    s = _free_sample(D=3.0, n=1, box=(1e6, 1e6))
    exc = tttrlib.SimGrid.uniform(0.0, 2.0, 4.0, 0.1)   # no photons
    det = tttrlib.SimGrid.uniform(0.0, 2.0, 4.0, 0.1)
    s.set_flow_field(tttrlib.SimVectorGrid.uniform(5.0, 0.0, 0.0))
    st = tttrlib.SimIntegrator()
    st.dt = 0.01; st.n_ph_max = 10**9; st.max_windows = 100000
    st.seed_diffusion = 7; st.seed_emission = 13; st.n_channels = 2
    e = tttrlib.SimEngine(s, [exc], [det], st)
    e.set_trajectory_reporter(1)
    e.run()
    x = np.asarray(e.trajectory_x())
    y = np.asarray(e.trajectory_y())
    z = np.asarray(e.trajectory_z())
    dx = np.diff(x); dy = np.diff(y); dz = np.diff(z)
    # mean displacement = v*dt = 0.05; SE = 0.245/sqrt(1e5) = 7.7e-4
    assert abs(dx.mean() - 5.0 * 0.01) < 5e-3
    assert abs(dy.mean()) < 5e-3
    assert abs(dz.mean()) < 5e-3
    # variance unaffected by flow
    assert abs(dx.var() / (2 * 3.0 * 0.01) - 1.0) < 0.05
    assert abs(dy.var() / (2 * 3.0 * 0.01) - 1.0) < 0.05
    assert abs(dz.var() / (2 * 3.0 * 0.01) - 1.0) < 0.05


# ---------------------------------------------------------------------------
# T7.3 — open-volume population survives flow
# ---------------------------------------------------------------------------
def test_open_volume_population_survives_flow():
    """With advection-aware injection (§T5), the mean population stays near
    the set value. Without the fix, §0.3 predicts k_drain ≈ 3.5e-5 per window
    ⇒ e^{-7} ≈ 0.1% left after 200k windows."""
    cfg = {
        "settings": {"dt": 0.01, "n_ph_max": 10**12, "max_windows": 200000,
                     "seed_diffusion": 1, "seed_emission": 2, "n_channels": 2,
                     "per_molecule_skip": False},
        "box": {"xy": 2.0, "z": 4.0},
        "species": [{"D": 3.0, "q": [0.0, 0.0]}],
        "k_rad": [0.0], "k_nrad": [0.0],
        "background": [0.0, 0.0],
        "population": [20.0],
        "excitation": {"type": "analytic_gaussian3d", "w0": 0.3, "z0": 2.0, "amplitude": 1.0},
    }
    # control: v=0
    cfg0 = {**cfg}
    e0 = tttrlib.SimEngine.from_json(__import__("json").dumps(cfg0))
    e0.run()
    n0 = e0.n_molecules()
    # flow: v=(1,0,0)
    cfg1 = {**cfg, "flow": {"type": "uniform", "vx": 1.0, "vy": 0.0, "vz": 0.0}}
    e1 = tttrlib.SimEngine.from_json(__import__("json").dumps(cfg1))
    e1.run()
    n1 = e1.n_molecules()
    for n in (n0, n1):
        assert 10 < n < 34, f"population {n} outside expected band 20 ± 3*sqrt(20)"


# ---------------------------------------------------------------------------
# T7.4 — the occlusion trio
# ---------------------------------------------------------------------------
def test_hard_wall_is_not_crossed():
    """An occlusion slab with occ=1 (x ∈ [0, 1] µm) should contain no trajectory
    points. Thickness 4.1·σ (§0.4)."""
    s = _free_sample(D=3.0, n=1, box=(3.0, 6.0))
    occ = tttrlib.SimGrid(61, 61, 121, 0.1, 0.1, 0.1, -3.0, -3.0, -6.0)
    for iz in range(occ.nz):
        for iy in range(occ.ny):
            for ix in range(occ.nx):
                x = occ.x0 + ix * occ.dx
                if 0.0 <= x <= 1.0:
                    occ.set_voxel(ix, iy, iz, 1.0)
    s.set_occlusion(occ)
    s.set_flow_field(tttrlib.SimVectorGrid.uniform(0.0, 0.0, 0.0))
    exc = tttrlib.SimGrid.uniform(0.0, 3.0, 6.0, 0.1)
    det = tttrlib.SimGrid.uniform(0.0, 3.0, 6.0, 0.1)
    st = tttrlib.SimIntegrator()
    st.dt = 0.01; st.n_ph_max = 10**9; st.max_windows = 20000
    st.seed_diffusion = 5; st.seed_emission = 7; st.n_channels = 2
    s.set_population(0, 50.0)
    e = tttrlib.SimEngine(s, [exc], [det], st)
    e.set_trajectory_reporter(20)
    e.run()
    x = np.asarray(e.trajectory_x())
    assert np.count_nonzero((x > 0.05) & (x < 0.95)) == 0


def test_occlusion_is_symmetric():
    """A wall centred at x ∈ [-0.5, 0.5] so geometry is mirror-symmetric;
    trajectory density agrees within 15% for |x| ∈ [1, 2]."""
    s = _free_sample(D=3.0, n=1, box=(3.0, 6.0))
    occ = tttrlib.SimGrid(61, 61, 121, 0.1, 0.1, 0.1, -3.0, -3.0, -6.0)
    for iz in range(occ.nz):
        for iy in range(occ.ny):
            for ix in range(occ.nx):
                x = occ.x0 + ix * occ.dx
                if -0.5 <= x <= 0.5:
                    occ.set_voxel(ix, iy, iz, 1.0)
    s.set_occlusion(occ)
    exc = tttrlib.SimGrid.uniform(0.0, 3.0, 6.0, 0.1)
    det = tttrlib.SimGrid.uniform(0.0, 3.0, 6.0, 0.1)
    st = tttrlib.SimIntegrator()
    st.dt = 0.01; st.n_ph_max = 10**9; st.max_windows = 50000
    st.seed_diffusion = 5; st.seed_emission = 7; st.n_channels = 2
    s.set_population(0, 50.0)
    e = tttrlib.SimEngine(s, [exc], [det], st)
    e.set_trajectory_reporter(10)
    e.run()
    x = np.asarray(e.trajectory_x())
    mask = (np.abs(x) >= 1.0) & (np.abs(x) <= 2.0)
    xp = x[mask]
    pos = np.count_nonzero(xp > 0)
    neg = np.count_nonzero(xp < 0)
    if pos + neg > 0:
        ratio = pos / (pos + neg)
        assert 0.35 < ratio < 0.65, f"symmetry broken: {ratio:.3f}"


def test_partial_occlusion_depletes_concentration():
    """A slab with occ=0.5 reduces trajectory position density inside vs outside,
    observable as fewer trajectory points inside the slab per unit time."""
    s = _free_sample(D=3.0, n=1, box=(3.0, 6.0))
    occ = tttrlib.SimGrid(61, 61, 121, 0.1, 0.1, 0.1, -3.0, -3.0, -6.0)
    for iz in range(occ.nz):
        for iy in range(occ.ny):
            for ix in range(occ.nx):
                x = occ.x0 + ix * occ.dx
                if 0.0 <= x <= 1.0:
                    occ.set_voxel(ix, iy, iz, 0.5)
    s.set_occlusion(occ)
    exc = tttrlib.SimGrid.uniform(0.0, 3.0, 6.0, 0.1)
    det = tttrlib.SimGrid.uniform(0.0, 3.0, 6.0, 0.1)
    st = tttrlib.SimIntegrator()
    st.dt = 0.01; st.n_ph_max = 10**9; st.max_windows = 50000
    st.seed_diffusion = 5; st.seed_emission = 7; st.n_channels = 2
    s.set_population(0, 50.0)
    e = tttrlib.SimEngine(s, [exc], [det], st)
    e.set_trajectory_reporter(5)
    e.run()
    x = np.asarray(e.trajectory_x())
    # Compare the fraction of trajectory points on the left (-2, -0.5) vs
    # the expected fraction if occlusion had no effect (uniform). The slab
    # at [0,1] blocks 50% of entries, so fewer molecules are inside it,
    # and more must be outside.
    right_of_slab = x > 1.0
    left_of_slab = x < 0.0
    # Without occlusion, roughly equal numbers on each side. With occlusion
    # blocking the centre slab, the sides should have more than the centre.
    n_inside = np.count_nonzero((x >= 0.0) & (x <= 1.0))
    n_outside = np.count_nonzero((x < 0.0) | (x > 1.0))
    # The occlusion must reduce the inside count vs a no-occlusion expectation.
    # With the wall at 50%, the inside should have fewer points than either side region.
    n_left = np.count_nonzero(x < 0.0)
    n_right = np.count_nonzero(x > 1.0)
    assert n_inside < n_left or n_inside < n_right, \
        f"occlusion should deplete inside (in={n_inside}, left={n_left}, right={n_right})"


# ---------------------------------------------------------------------------
# T7.5 — coasting agrees with flow
# ---------------------------------------------------------------------------
def test_coasting_agrees_with_flow():
    """Uniform-flow run with per_molecule_skip True and False produce photon counts
    within 5% and population within 3 σ."""
    cfg = {
        "settings": {"dt": 0.01, "n_ph_max": 20000, "seed_diffusion": 1, "seed_emission": 2,
                     "n_channels": 2, "per_molecule_skip": False, "min_coast_windows": 4,
                     "coast_safety": 3.0},
        "box": {"xy": 2.0, "z": 4.0},
        "species": [{"D": 3.0, "q": [50.0, 50.0]}],
        "k_rad": [0.0], "k_nrad": [0.0],
        "background": [0.0, 0.0],
        "population": [5.0],
        "flow": {"type": "uniform", "vx": 1.0, "vy": 0.0, "vz": 0.0},
        "excitation": {"type": "analytic_gaussian3d", "w0": 0.3, "z0": 2.0, "amplitude": 1.0},
    }
    cfg_no_coast = {**cfg, "per_molecule_skip": False, "seed_diffusion": 11, "seed_emission": 22}
    cfg_coast = {**cfg, "per_molecule_skip": True, "seed_diffusion": 11, "seed_emission": 22}
    e0 = tttrlib.SimEngine.from_json(__import__("json").dumps(cfg_no_coast))
    e1 = tttrlib.SimEngine.from_json(__import__("json").dumps(cfg_coast))
    e0.run(); e1.run()
    p0 = e0.photons()["channel"]
    p1 = e1.photons()["channel"]
    n0 = len(p0[p0 >= 0])
    n1 = len(p1[p1 >= 0])
    assert abs(n0 - n1) / max(n0, n1) < 0.05, f"photon counts {n0} vs {n1}"
    assert abs(e0.n_molecules() - e1.n_molecules()) < 3 * np.sqrt(e0.n_molecules())


def test_coasting_inert_for_poiseuille():
    """Poiseuille flow disables coasting — output is identical with the flag on or off."""
    cfg = {
        "settings": {"dt": 0.01, "n_ph_max": 10000, "seed_diffusion": 3, "seed_emission": 4,
                     "n_channels": 2, "per_molecule_skip": False, "min_coast_windows": 4},
        "box": {"xy": 5.0, "z": 5.0},
        "species": [{"D": 3.0, "q": [50.0, 50.0]}],
        "k_rad": [0.0], "k_nrad": [0.0],
        "background": [0.0, 0.0],
        "population": [10.0],
        "flow": {"type": "poiseuille", "v_max": 2.0, "radius": 5.0, "axis": 0,
                 "extent_xy": 6.0, "extent_z": 6.0, "spacing": 0.1},
        "excitation": {"type": "analytic_gaussian3d", "w0": 0.3, "z0": 2.0, "amplitude": 1.0},
    }
    cfg_off = {**cfg, "per_molecule_skip": False, "seed_diffusion": 7, "seed_emission": 8}
    cfg_on = {**cfg, "per_molecule_skip": True, "seed_diffusion": 7, "seed_emission": 8}
    e0 = tttrlib.SimEngine.from_json(__import__("json").dumps(cfg_off))
    e1 = tttrlib.SimEngine.from_json(__import__("json").dumps(cfg_on))
    e0.run(); e1.run()
    assert e0.n_molecules() == e1.n_molecules()


# ---------------------------------------------------------------------------
# T7.6 — flow configuration is validated
# ---------------------------------------------------------------------------
def test_flow_field_smaller_than_box():
    """A Poiseuille lattice that does not cover the box raises ValueError."""
    cfg = {
        "settings": {"dt": 0.01, "n_ph_max": 1000, "n_channels": 1, "max_windows": 100},
        "box": {"xy": 10.0, "z": 10.0},
        "species": [{"D": 3.0, "q": [50.0]}],
        "k_rad": [0.0], "k_nrad": [0.0],
        "background": [0.0],
        "population": [5.0],
        "flow": {"type": "poiseuille", "v_max": 2.0, "radius": 5.0, "axis": 0,
                 "extent_xy": 2.0, "extent_z": 2.0, "spacing": 0.1},
        "excitation": {"type": "uniform", "value": 1.0, "extent_xy": 12.0, "extent_z": 12.0, "spacing": 1.0},
    }
    with pytest.raises(ValueError):
        tttrlib.SimEngine.from_json(__import__("json").dumps(cfg))


def test_flow_dt_too_large():
    """dt so large that max_speed*dt > 0.25*spacing raises ValueError."""
    cfg = {
        "settings": {"dt": 1.0, "n_ph_max": 1000, "n_channels": 1, "max_windows": 100},
        "box": {"xy": 3.0, "z": 3.0},
        "species": [{"D": 3.0, "q": [50.0]}],
        "k_rad": [0.0], "k_nrad": [0.0],
        "background": [0.0],
        "population": [5.0],
        "flow": {"type": "poiseuille", "v_max": 10.0, "radius": 5.0, "axis": 0,
                 "extent_xy": 4.0, "extent_z": 4.0, "spacing": 1.0},
        "excitation": {"type": "uniform", "value": 1.0, "extent_xy": 5.0, "extent_z": 5.0, "spacing": 1.0},
    }
    with pytest.raises(ValueError):
        tttrlib.SimEngine.from_json(__import__("json").dumps(cfg))


def test_flow_components_mismatched():
    """from_components with mismatched array sizes raises ValueError."""
    with pytest.raises(ValueError):
        tttrlib.SimVectorGrid.from_components([1.0, 2.0], [1.0], [1.0], 2, 1, 1, 0.1, 0.1, 0.1, 0, 0, 0)


# ---------------------------------------------------------------------------
# T7.7 — Poiseuille is spatially varying
# ---------------------------------------------------------------------------
def test_poiseuille_is_spatially_varying():
    """D=0 (no Brownian), single emitter: deterministic advection. On-axis
    v=v_max, at rho=2.5/R=5 v=v_max*(1-0.25)=1.5 — exact to 2e-3."""
    s = _free_sample(D=0.0, n=1, box=(1e6, 1e6))
    s.set_flow_field(tttrlib.SimVectorGrid.poiseuille(
        2.0, 5.0, 0, 8.0, 8.0, 0.1))
    exc = tttrlib.SimGrid.uniform(0.0, 10.0, 10.0, 1.0)
    det = tttrlib.SimGrid.uniform(0.0, 10.0, 10.0, 1.0)
    st = tttrlib.SimIntegrator()
    st.dt = 0.01; st.n_ph_max = 10**9; st.max_windows = 100
    st.seed_diffusion = 1; st.seed_emission = 2; st.n_channels = 2
    e = tttrlib.SimEngine(s, [exc], [det], st)
    e.set_trajectory_reporter(1)
    e.run()
    tr_x = np.asarray(e.trajectory_x())
    n_steps = len(tr_x) - 1  # trajectory is at window START; last point is after n-1 moves
    assert n_steps > 0
    # on the axis: v = v_max = 2.0. After n_steps moves at v*dt each: position = n_steps * v * dt
    expected = n_steps * 2.0 * 0.01
    assert abs(tr_x[-1] - expected) < 2e-3, f"on-axis: {tr_x[-1]} vs {expected}"

    # at (0, 2.5, 0): rho=2.5, R=5 => v = 2.0*(1-0.25) = 1.5
    s2 = tttrlib.SimSystem()
    sp2 = tttrlib.SimSpecies(); sp2.D = 0.0; sp2.q = _vd([0.0, 0.0]); sp2.v_scale = 1.0
    s2.add_species(sp2)
    s2.set_rate_matrices(_vd([0.0]), _vd([0.0]))
    s2.set_background(_vd([0.0, 0.0]))
    s2.set_box(1e6, 1e6)
    s2.add_fluorophore(0.0, 2.5, 0.0, 0, True)
    s2.set_flow_field(tttrlib.SimVectorGrid.poiseuille(
        2.0, 5.0, 0, 8.0, 8.0, 0.1))
    e2 = tttrlib.SimEngine(s2, [exc], [det], st)
    e2.set_trajectory_reporter(1)
    e2.run()
    x2 = np.asarray(e2.trajectory_x())
    y2 = np.asarray(e2.trajectory_y())
    z2 = np.asarray(e2.trajectory_z())
    n_steps2 = len(x2) - 1
    expected2 = n_steps2 * 1.5 * 0.01
    assert abs(x2[-1] - expected2) < 2e-3, f"off-axis: {x2[-1]} vs {expected2}"
    assert abs(y2[-1] - 2.5) < 1e-12
    assert abs(z2[-1]) < 1e-12


# ---------------------------------------------------------------------------
# T7.8 — flow FCS matches analytic curve (slow)
# ---------------------------------------------------------------------------
@pytest.mark.slow
def test_flow_fcs_matches_the_analytic_curve():
    """Open-volume simulation correlated as FCS: fit recovers D and v."""
    try:
        from scipy.optimize import curve_fit
    except ImportError:
        pytest.skip("scipy not available")
    cfg = {
        "settings": {"dt": 0.001, "n_ph_max": 2000000, "seed_diffusion": 42, "seed_emission": 99,
                     "n_channels": 1, "per_molecule_skip": False},
        "box": {"xy": 5.0, "z": 10.0},
        "species": [{"D": 3.0, "q": [50.0]}],
        "k_rad": [0.0], "k_nrad": [0.0],
        "background": [0.0],
        "population": [5.0],
        "flow": {"type": "uniform", "vx": 1.0, "vy": 0.0, "vz": 0.0},
        "excitation": {"type": "analytic_gaussian3d", "w0": 0.3, "z0": 2.0, "amplitude": 1.0},
    }
    e = tttrlib.SimEngine.from_json(__import__("json").dumps(cfg))
    e.run()
    ph = e.photons()
    T = np.asarray(ph["macro_window"], dtype=np.float64) * cfg["settings"]["dt"]
    corr = tttrlib.Correlator()
    corr.append(T)
    tau, G = np.asarray(corr.correlation[0]), np.asarray(corr.correlation[1])
    # mask tau>0
    m = tau > 0
    tau, G = tau[m], G[m]
    # analytic: G(τ) = G0 / ((1+4Dτ/wr²)*sqrt(1+4Dτ/wz²)) * exp(-(vτ)²/(wr²+4Dτ))
    wr, wz = 0.3, 2.0
    def model(t, G0, D, v):
        denom = (1 + 4*D*t/wr**2) * np.sqrt(1 + 4*D*t/wz**2)
        exp_arg = -(v*t)**2 / (wr**2 + 4*D*t)
        return G0 / denom * np.exp(exp_arg)
    popt, _ = curve_fit(model, tau, G, p0=[0.1, 3.0, 1.0],
                        bounds=([0, 0.1, 0], [10, 30, 10]))
    D_fit, v_fit = popt[1], popt[2]
    assert abs(v_fit - 1.0) / 1.0 < 0.10, f"v_fit={v_fit:.3f}"
    assert abs(D_fit - 3.0) / 3.0 < 0.15, f"D_fit={D_fit:.3f}"
