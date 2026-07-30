"""Flow, occlusion and pair-correlation tests for the photon simulator (PRD-005)."""
import json

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
    """A uniform(0,0,0) field produces exactly the photon stream of a no-flow build.

    Note what this does and does not prove. The engine sets ``has_flow_`` only when
    ``max_speed() > kEps``, so a zero field short-circuits onto the no-flow code path and
    this test guards *that short-circuit* — it does not exercise the advected step or the
    advection-aware injection at all. The claim that those reduce to the old formulas at
    mu = 0 is covered by ``test_v_scale_zero_matches_no_flow`` below, which keeps the flow
    machinery switched on.
    """
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
    e0 = tttrlib.SimEngine.from_json(json.dumps(cfg0))
    e1 = tttrlib.SimEngine.from_json(json.dumps(cfg1))
    e0.run(); e1.run()
    p0 = e0.photons()
    p1 = e1.photons()
    assert np.array_equal(p0["macro_window"], p1["macro_window"])
    assert np.array_equal(p0["arrival_time"], p1["arrival_time"])
    assert np.array_equal(p0["channel"], p1["channel"])


def test_v_scale_zero_matches_no_flow():
    """With a real flow field but ``v_scale = 0`` the mu = 0 limit must reproduce no-flow.

    This is the test that actually exercises the new machinery: ``max_speed() > 0`` so
    ``has_flow_`` is true, the advected step runs (adding ``v * 0``), and injection goes
    through ``mean_influx_weight`` and ``random_entry_depth`` rather than the closed-form
    ``sigma/sqrt(2*pi)`` and ``random_erfc``. Because those are two different samplers of
    the *same* distribution the streams cannot be bit-identical, so the comparison is
    statistical: the injection rate, and hence the standing population and the photon
    count, must agree.

    Tolerance: the photon count is a sum over ~5 molecules diffusing through the focus for
    a fixed number of windows; 8 % is roughly 2 sigma of the run-to-run spread measured by
    varying only the seed, and a broken influx weight moves it by far more (the drain in
    ``test_open_volume_population_survives_flow`` is a factor of ~1000).
    """
    base = {
        "settings": {"dt": 0.01, "n_ph_max": 10 ** 9, "max_windows": 40000,
                     "seed_diffusion": 7, "seed_emission": 11,
                     "n_channels": 2, "per_molecule_skip": False},
        "box": {"xy": 2.0, "z": 4.0},
        "k_rad": [0.0], "k_nrad": [0.0],
        "background": [0.0, 0.0],
        "population": [5.0],
        "excitation": {"type": "analytic_gaussian3d", "w0": 0.3, "z0": 2.0, "amplitude": 1.0},
    }
    cfg0 = {**base, "species": [{"D": 3.0, "q": [50.0, 50.0]}]}
    cfg1 = {**base,
            "species": [{"D": 3.0, "q": [50.0, 50.0], "v_scale": 0.0}],
            "flow": {"type": "uniform", "vx": 2.0, "vy": 0.0, "vz": 0.0}}

    e0 = tttrlib.SimEngine.from_json(json.dumps(cfg0)); e0.run()
    e1 = tttrlib.SimEngine.from_json(json.dumps(cfg1)); e1.run()

    n0, n1 = e0.n_photons(), e1.n_photons()
    assert n0 > 1000 and n1 > 1000, f"too few photons to compare: {n0}, {n1}"
    assert abs(n0 - n1) / max(n0, n1) < 0.08, f"photon counts {n0} vs {n1}"


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
    e0 = tttrlib.SimEngine.from_json(json.dumps(cfg0))
    e0.run()
    n0 = e0.n_molecules()
    # flow: v=(1,0,0)
    cfg1 = {**cfg, "flow": {"type": "uniform", "vx": 1.0, "vy": 0.0, "vz": 0.0}}
    e1 = tttrlib.SimEngine.from_json(json.dumps(cfg1))
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


def _sealed_cavity_density_ratio(occ_val, windows=40000, seed=5, D=3.0, dt=0.01):
    """Return density(occluded slab) / density(free slab) for molecules sealed in a cavity.

    The cavity walls are ``occ = 1``, so no molecule can leave and none is injected: the
    chain is closed and reversible, which is precisely the condition under which the
    stationary law is exactly ``pi(x) ~ 1 - occ(x)``.
    """
    s = tttrlib.SimSystem()
    sp = tttrlib.SimSpecies(); sp.D = D; sp.q = _vd([0.0, 0.0]); s.add_species(sp)
    s.set_rate_matrices(_vd([0.0]), _vd([0.0])); s.set_background(_vd([0.0, 0.0]))
    s.set_box(1e6, 1e6)                       # no absorbing surface, no surface injection
    # Cavity |x|<=2, |y|<=1, |z|<=1 walled in by occ=1; the test slab sits on x in [0,1].
    g = tttrlib.SimGrid(61, 41, 41, 0.1, 0.1, 0.1, -3.0, -2.0, -2.0)
    for iz in range(g.nz):
        z = g.z0 + iz * g.dz
        for iy in range(g.ny):
            y = g.y0 + iy * g.dy
            for ix in range(g.nx):
                x = g.x0 + ix * g.dx
                if abs(x) > 2.0 or abs(y) > 1.0 or abs(z) > 1.0:
                    g.set_voxel(ix, iy, iz, 1.0)
                elif 0.0 <= x <= 1.0:
                    g.set_voxel(ix, iy, iz, occ_val)
    s.set_occlusion(g)
    rng = np.random.default_rng(seed)
    for _ in range(60):                       # all seeded in the free half
        s.add_fluorophore(float(rng.uniform(-1.8, -0.2)), float(rng.uniform(-0.8, 0.8)),
                          float(rng.uniform(-0.8, 0.8)), 0, True)
    exc = tttrlib.SimGrid.uniform(0.0, 3.0, 3.0, 0.2)
    st = tttrlib.SimIntegrator()
    st.dt = dt; st.n_ph_max = 10 ** 9; st.max_windows = windows
    st.seed_diffusion = seed; st.seed_emission = seed + 2; st.n_channels = 2
    e = tttrlib.SimEngine(s, [exc], tttrlib.VectorSimGrid([]), st)
    e.set_trajectory_reporter(10); e.run()
    x = np.asarray(e.trajectory_x())
    x = x[len(x) // 5:]                       # discard the approach to stationarity
    # Sample strictly inside the flat parts, clear of the one-voxel interpolation ramp.
    n_in = np.count_nonzero((x >= 0.15) & (x <= 0.85))
    n_free = np.count_nonzero((x >= -0.85) & (x <= -0.15))
    return n_in / max(n_free, 1), n_in, n_free


@pytest.mark.parametrize("occ_val", [0.0, 0.25, 0.5, 0.75])
def test_partial_occlusion_follows_one_minus_occ(occ_val):
    """An occ = q region holds (1 - q) times the density of the free region beside it.

    This is the defining property of the step rule "accept the destination with
    probability 1 - occ(destination)": with a symmetric proposal it satisfies detailed
    balance with respect to ``pi(x) ~ 1 - occ(x)``, so the rule is an excluded-volume /
    partial-accessibility medium, not a membrane with a permeability. Measuring it across
    several q is what distinguishes the two readings.

    The cavity must be **sealed**, and that is not a detail. Run the same comparison in the
    ordinary absorbing box with surface injection and the ratio comes out at 0.66 rather
    than 0.50 for q = 0.5 -- not because the rule is wrong but because that system never
    reaches local equilibrium: a molecule's residence time there (R^2/6D) is only ~3x the
    time to diffuse across the slab (L^2/2D), so the driven steady state is nowhere near
    the equilibrium the law describes. Sealing the cavity removes the turnover and the law
    is recovered to about 1%.

    Tolerance: 0.06 absolute. Measured deviations across q are <= 0.01 with this seed; the
    band covers seed-to-seed spread with room to spare, and any misreading of the mask
    (using accessibility instead of occlusion, say) moves the ratio by 0.25 or more.
    """
    ratio, n_in, n_free = _sealed_cavity_density_ratio(occ_val)
    assert n_free > 500, f"too few samples in the free slab: {n_free}"
    assert abs(ratio - (1.0 - occ_val)) < 0.06, (
        f"pi ~ 1-occ predicts {1.0 - occ_val:.3f}, measured {ratio:.3f} "
        f"(occluded={n_in}, free={n_free})"
    )


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
    e0 = tttrlib.SimEngine.from_json(json.dumps(cfg_no_coast))
    e1 = tttrlib.SimEngine.from_json(json.dumps(cfg_coast))
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
    e0 = tttrlib.SimEngine.from_json(json.dumps(cfg_off))
    e1 = tttrlib.SimEngine.from_json(json.dumps(cfg_on))
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
        tttrlib.SimEngine.from_json(json.dumps(cfg))


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
        tttrlib.SimEngine.from_json(json.dumps(cfg))


def test_flow_components_mismatched():
    """from_components with mismatched array sizes raises ValueError."""
    with pytest.raises(ValueError):
        tttrlib.SimVectorGrid.from_components([1.0, 2.0], [1.0], [1.0], 2, 1, 1, 0.1, 0.1, 0.1, 0, 0, 0)


# ---------------------------------------------------------------------------
def _advect_only(kind, midpoint, n_windows):
    """One molecule, D = 0, pure advection. Returns (final x, final radius / initial)."""
    s = tttrlib.SimSystem()
    sp = tttrlib.SimSpecies(); sp.D = 0.0; sp.q = _vd([0.0]); s.add_species(sp)
    s.set_rate_matrices(_vd([0.0]), _vd([0.0])); s.set_background(_vd([0.0]))
    s.set_box(1e6, 1e6)
    s.add_fluorophore(1.0, 0.0, 0.0, 0, True)
    if kind == "rotation":
        s.set_flow_field(tttrlib.SimVectorGrid.rotation(2.0, 2, 3.0, 3.0, 0.05))
    else:
        s.set_flow_field(tttrlib.SimVectorGrid.poiseuille(2.0, 5.0, 0, 8.0, 8.0, 0.1))
    st = tttrlib.SimIntegrator()
    st.dt = 0.001; st.n_channels = 1; st.n_ph_max = 10 ** 9
    st.max_windows = n_windows
    st.drift_midpoint = midpoint
    e = tttrlib.SimEngine(s, [tttrlib.SimGrid.uniform(0.0, 1.0, 1.0, 0.5)],
                          tttrlib.VectorSimGrid([]), st)
    e.set_trajectory_reporter(1000); e.run()
    x, y = np.asarray(e.trajectory_x()), np.asarray(e.trajectory_y())
    r = np.hypot(x, y)
    return float(x[-1]), float(r[-1] / r[0])


def test_drift_midpoint_option():
    """``drift_midpoint`` is a throughput knob whose safety depends on the field.

    The midpoint step costs one extra field lookup per step, which on a grid field is
    about 26 % of the run time, so it is worth turning off where it changes nothing. What
    decides that is whether the Euler drift map preserves phase-space volume:

    * **Poiseuille** has a nilpotent Jacobian (``v_x`` depends only on y and z), so
      ``tr(J^2) = 0``, Euler is volume-exact, and the trajectory is exact too because the
      transverse coordinates never move. On and off must agree **bit for bit**.
    * **Rotation** has zero strain and pure vorticity, so the determinant is
      ``1 + (omega*dt)^2 > 1`` and Euler spirals the molecule outward. On and off must
      differ, and visibly.

    The Poiseuille leg runs only 2000 windows on purpose: at 2 um/ms the molecule would
    otherwise advect past the edge of its own 8 um grid, where ``SimGrid::at`` returns 0
    and the flow silently stops. Both schemes then park at the edge and the comparison
    measures the parking point rather than the integrator.
    """
    x_on, _ = _advect_only("poiseuille", True, 2000)
    x_off, _ = _advect_only("poiseuille", False, 2000)
    assert x_on == x_off, f"pure shear must not care about the scheme: {x_on} vs {x_off}"

    _, r_on = _advect_only("rotation", True, 300000)
    _, r_off = _advect_only("rotation", False, 300000)
    assert abs(r_on - 1.0) < 1e-4, f"midpoint should conserve the radius, got {r_on:.4f}"
    assert r_off > 1.5, f"plain Euler should spiral outward, got {r_off:.4f}"


def test_rotation_preserves_radius():
    """A rigid rotation must not change a molecule's distance from the axis.

    With ``D = 0`` the motion is pure advection and the exact flow is a rotation, so the
    radius is conserved exactly. Explicit Euler is not: its map ``I + omega*dt*A`` has
    determinant ``1 + (omega*dt)^2 > 1``, so it inflates phase-space volume on every step
    and molecules spiral *outward*. The error is O(dt^2) per step but systematic rather
    than random, so it accumulates linearly in time instead of averaging away -- at
    ``omega*dt = 0.002`` the radius grew 1.82x over 3e5 windows (matching the determinant
    prediction of 1.822x) and drained an open volume by a third through the absorbing
    boundary, with no error raised anywhere.

    The drift is integrated with an explicit midpoint step for non-uniform fields, which
    takes the per-step volume error to ``(omega*dt)^4/4``. Tolerance 1e-4 is far below the
    0.82 that plain Euler produces here and far above the ~1e-6 that midpoint leaves.
    """
    omega, dt, n_windows = 2.0, 0.001, 300000
    s = tttrlib.SimSystem()
    sp = tttrlib.SimSpecies(); sp.D = 0.0; sp.q = _vd([0.0]); s.add_species(sp)
    s.set_rate_matrices(_vd([0.0]), _vd([0.0])); s.set_background(_vd([0.0]))
    s.set_box(1e6, 1e6)
    s.add_fluorophore(1.0, 0.0, 0.0, 0, True)          # radius exactly 1
    s.set_flow_field(tttrlib.SimVectorGrid.rotation(omega, 2, 3.0, 3.0, 0.05))
    st = tttrlib.SimIntegrator()
    st.dt = dt; st.n_ph_max = 10 ** 9; st.max_windows = n_windows; st.n_channels = 1
    e = tttrlib.SimEngine(s, [tttrlib.SimGrid.uniform(0.0, 1.0, 1.0, 0.5)],
                          tttrlib.VectorSimGrid([]), st)
    e.set_trajectory_reporter(1000); e.run()
    r = np.hypot(np.asarray(e.trajectory_x()), np.asarray(e.trajectory_y()))
    assert r.size > 10, "no trajectory recorded"
    assert abs(r[-1] / r[0] - 1.0) < 1e-4, f"radius grew {r[-1] / r[0]:.4f}x under rotation"


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
    """End-to-end: correlate an open-volume run and recover the simulated D and v.

    This is the proof that the flow physics is right, and it uses no pCF code at all --
    so a failure points at the simulator rather than at the analysis being validated.

    Choosing parameters that can actually see the flow is the whole difficulty. The flow
    term ``exp(-(v*tau)^2 / (wr^2 + 4*D*tau))`` only bites once the transit time across the
    waist is comparable to the diffusion time through it, i.e. once

        v  >~  4*D/wr      (here 4*0.5/0.3 = 6.7 um/ms, and v = 10 um/ms)

    Below that threshold the flow term is numerically invisible against diffusion and the
    fit cannot recover ``v`` however long the run: at D = 3, wr = 0.3 and v = 1 the term is
    3e-4 at the diffusion time, i.e. far under the shot noise. The regression that this
    test guards would be indistinguishable from that mis-specification, which is why the
    parameters are pinned here with the reasoning attached.

    Concentration is set so that N in the focus is about 1 (the usual FCS working point):
    ``V_eff = pi^1.5 * wr^2 * wz = 0.75 um^3`` against an ellipsoid box of 67 um^3, so a
    population of 90 gives N = 1.0 and an amplitude G(0) = 1/N = 1.0.

    Tolerances: 10 % on v and 15 % on D, against a measured accuracy of about 2 % at this
    photon count -- the band is loose enough not to flake on the seed and tight enough that
    the no-flow mis-fit (which lands at v = 0) fails it outright.
    """
    try:
        from scipy.optimize import curve_fit
    except ImportError:
        pytest.skip("scipy not available")

    D_true, v_true, wr, wz, dt = 0.5, 10.0, 0.3, 1.5, 0.001
    cfg = {
        "settings": {"dt": dt, "n_ph_max": 150000, "max_windows": 20000000,
                     "seed_diffusion": 42, "seed_emission": 99,
                     "n_channels": 1, "per_molecule_skip": False},
        "box": {"xy": 2.0, "z": 4.0},
        "species": [{"D": D_true, "q": [50.0]}],
        "k_rad": [0.0], "k_nrad": [0.0],
        "background": [0.0],
        "population": [90.0],
        "flow": {"type": "uniform", "vx": v_true, "vy": 0.0, "vz": 0.0},
        "excitation": {"type": "analytic_gaussian3d", "w0": wr, "z0": wz, "amplitude": 1.0},
    }
    e = tttrlib.SimEngine.from_json(json.dumps(cfg))
    e.run()

    # The population must survive the flow -- a molecule crosses this box in 0.2 ms, so a
    # diffusion-only influx would have drained it long before the photon budget was met.
    assert 60 < e.n_molecules() < 130, f"population sagged to {e.n_molecules()}"

    ph = e.photons()
    t = np.ascontiguousarray(np.asarray(ph["macro_window"], dtype=np.uint64))
    assert t.size > 100000, f"only {t.size} photons"

    corr = tttrlib.Correlator()
    corr.n_bins = 8
    corr.n_casc = 20
    w = np.ones(t.size, dtype=float)
    corr.set_macrotimes(t, t)
    corr.set_weights(w, w)
    corr.run()
    tau = np.asarray(corr.get_x_axis(), dtype=float) * dt
    g = np.asarray(corr.get_corr_normalized(), dtype=float) - 1.0
    keep = (tau > 0) & np.isfinite(g)
    tau, g = tau[keep], g[keep]

    def model(x, g0, d, v):
        return (g0 / ((1 + 4 * d * x / wr ** 2) * np.sqrt(1 + 4 * d * x / wz ** 2))
                * np.exp(-(v * x) ** 2 / (wr ** 2 + 4 * d * x)))

    popt, _ = curve_fit(model, tau, g, p0=[g[0], D_true, v_true],
                        bounds=([0, 0.01, 0], [100, 50, 200]), maxfev=20000)
    g0_fit, d_fit, v_fit = popt
    assert abs(v_fit - v_true) / v_true < 0.10, f"v_fit={v_fit:.3f} (true {v_true})"
    assert abs(d_fit - D_true) / D_true < 0.15, f"D_fit={d_fit:.3f} (true {D_true})"
    # The amplitude is 1/N, so it cross-checks that injection holds the *concentration*
    # and not merely a molecule count: a drifting density shows up here first.
    assert abs(g0_fit - 1.0) < 0.25, f"G(0)={g0_fit:.3f}, expected ~1/N = 1.0"


# ---------------------------------------------------------------------------
# T7.9 — the simulated FCS obeys the analytic equations across D
# ---------------------------------------------------------------------------
def _fcs_curve(D, v, n_ph=120000, seed=42, wr=0.3, wz=1.5, dt=0.001):
    """Simulate an open volume and return its normalised correlation (tau, G)."""
    cfg = {
        "settings": {"dt": dt, "n_ph_max": n_ph, "max_windows": 40000000,
                     "seed_diffusion": seed, "seed_emission": seed + 57,
                     "n_channels": 1, "per_molecule_skip": False},
        "box": {"xy": 2.0, "z": 4.0},
        "species": [{"D": D, "q": [50.0]}],
        "k_rad": [0.0], "k_nrad": [0.0], "background": [0.0],
        "population": [90.0],
        "excitation": {"type": "analytic_gaussian3d", "w0": wr, "z0": wz, "amplitude": 1.0},
    }
    if v:
        cfg["flow"] = {"type": "uniform", "vx": v, "vy": 0.0, "vz": 0.0}
    e = tttrlib.SimEngine.from_json(json.dumps(cfg))
    e.run()
    macro = np.ascontiguousarray(np.asarray(e.photons()["macro_window"], dtype=np.uint64))
    corr = tttrlib.Correlator()
    corr.n_bins = 8
    corr.n_casc = 20
    w = np.ones(macro.size)
    corr.set_macrotimes(macro, macro)
    corr.set_weights(w, w)
    corr.run()
    tau = np.asarray(corr.get_x_axis(), float) * dt
    g = np.asarray(corr.get_corr_normalized(), float) - 1.0
    # The last multi-tau cascades average a handful of photon pairs and are pure noise.
    keep = (tau > 0) & (tau <= 1.0) & np.isfinite(g)
    return tau[keep], g[keep], e.n_molecules()


@pytest.mark.slow
def test_fcs_agrees_with_the_analytic_curve_across_D():
    """The simulated correlation must *be* the textbook FCS curve, at every D.

    Two claims, and the second is the one a single-point check cannot make:

    1. Fitting the analytic 3-D diffusion curve
       ``G = G0 / ((1+4D tau/wr^2) sqrt(1+4D tau/wz^2))`` recovers the simulated ``D``.
    2. **Faster diffusion gives a faster correlation.** The diffusion time
       ``tau_D = wr^2/(4D)`` must fall monotonically as ``D`` rises — a simulation that
       ignored ``D`` entirely, or applied it in the wrong units, would still fit curve 1
       at a single point but cannot reproduce the trend.

    ``G(0) = 1/N`` is checked alongside: it depends on the concentration and not on ``D``,
    so it must stay put across the scan. A drifting amplitude would mean the surface
    injection is not holding the density.

    Tolerance: 20 % on D. Measured errors over 0.2-4.0 um^2/ms are within 12 % at this
    photon count; the slack covers seed variation and the mild bias from a box whose axial
    half-height (4 um) is only ~2.7 axial waists, which truncates the profile slightly.
    """
    try:
        from scipy.optimize import curve_fit
    except ImportError:
        pytest.skip("scipy not available")

    wr, wz = 0.3, 1.5

    def diffusion_only(t, g0, d):
        return g0 / ((1 + 4 * d * t / wr ** 2) * np.sqrt(1 + 4 * d * t / wz ** 2))

    d_true = [0.25, 1.0, 4.0]            # a 16x span
    fitted, tau_d, amplitudes = [], [], []
    for D in d_true:
        tau, g, n_mol = _fcs_curve(D, 0.0, wr=wr, wz=wz)
        assert tau.size > 20, f"too few lag points at D={D}"
        popt, _ = curve_fit(diffusion_only, tau, g, p0=[g[0], D],
                            bounds=([0, 1e-3], [100, 200]), maxfev=40000)
        g0_fit, d_fit = popt
        fitted.append(d_fit)
        tau_d.append(wr ** 2 / (4 * d_fit))
        amplitudes.append(g0_fit)

    for D, d_fit in zip(d_true, fitted):
        assert abs(d_fit - D) / D < 0.20, f"D={D} recovered as {d_fit:.3f}"

    # Faster diffusion => faster correlation, strictly.
    assert all(a > b for a, b in zip(tau_d, tau_d[1:])), (
        f"tau_D must fall as D rises, got {tau_d}")
    # ...and by roughly the right factor: tau_D ~ 1/D over a 16x span.
    assert 0.75 < (tau_d[0] / tau_d[-1]) / (d_true[-1] / d_true[0]) < 1.35, (
        f"tau_D should scale as 1/D: ratio {tau_d[0] / tau_d[-1]:.1f} "
        f"for a {d_true[-1] / d_true[0]:.0f}x change in D")
    # The amplitude is 1/N: set by concentration, not by D.
    assert max(amplitudes) / min(amplitudes) < 1.25, (
        f"G(0) should not track D, got {amplitudes}")


@pytest.mark.slow
def test_fcs_becomes_faster_with_flow():
    """Adding flow shortens the correlation at fixed D, and the fit sees it as v.

    The companion to the D scan: transport speeds the decay up whether it is diffusive or
    directed, but only the drift term is *directional*, so fitting a diffusion-only curve
    to a flowing sample reports an inflated D. That inflation is the signature the pCF
    work depends on, so it is asserted here rather than assumed.
    """
    try:
        from scipy.optimize import curve_fit
    except ImportError:
        pytest.skip("scipy not available")

    wr, wz, D = 0.3, 1.5, 0.5

    def full(t, g0, d, v):
        return (g0 / ((1 + 4 * d * t / wr ** 2) * np.sqrt(1 + 4 * d * t / wz ** 2))
                * np.exp(-(v * t) ** 2 / (wr ** 2 + 4 * d * t)))

    def half_time(tau, g):
        """Lag at which G has fallen to half its value at the shortest lag."""
        target = 0.5 * g[0]
        below = np.flatnonzero(g < target)
        return tau[below[0]] if below.size else np.inf

    tau0, g0, _ = _fcs_curve(D, 0.0, wr=wr, wz=wz)
    tau1, g1, _ = _fcs_curve(D, 10.0, wr=wr, wz=wz)

    assert half_time(tau1, g1) < half_time(tau0, g0), (
        "flow must shorten the correlation: "
        f"{half_time(tau1, g1):.4g} vs {half_time(tau0, g0):.4g} ms")

    popt, _ = curve_fit(full, tau1, g1, p0=[g1[0], D, 10.0],
                        bounds=([0, 0.01, 0], [100, 50, 200]), maxfev=40000)
    assert abs(popt[2] - 10.0) / 10.0 < 0.15, f"v recovered as {popt[2]:.2f}"
    assert abs(popt[1] - D) / D < 0.25, f"D recovered as {popt[1]:.3f}"


def test_coasting_does_not_rescan_the_flow_field():
    """Enabling coasting must not make a grid field dramatically slower.

    The coast decision consults the field's maximum speed, and ``SimVectorGrid::max_speed``
    walks every voxel. Taken per molecule per window that is 237k operations on a 51x51x91
    lattice — for a decision whose answer is always "a non-uniform field cannot coast".
    A 20k-window run that takes well under a second did not finish in ten minutes.

    The value is cached at construction now, and the non-uniform early-out happens before
    anything else, so coasting on a grid field costs the same as coasting off. Asserting a
    ratio rather than an absolute time keeps this meaningful on any machine; the bar is set
    at 3x, far above the ~1x it should be and far below the >1000x regression.
    """
    import time

    cfg = {
        "settings": {"dt": 0.001, "n_ph_max": 10 ** 12, "max_windows": 20000,
                     "seed_diffusion": 3, "seed_emission": 5, "n_channels": 1,
                     "per_molecule_skip": False},
        "box": {"xy": 2.0, "z": 4.0},
        "species": [{"D": 1.0, "q": [50.0]}],
        "k_rad": [0.0], "k_nrad": [0.0], "background": [0.0],
        "population": [200.0],
        "flow": {"type": "poiseuille", "v_max": 5.0, "radius": 3.0, "axis": 0,
                 "extent_xy": 2.5, "extent_z": 4.5, "spacing": 0.1},
        "excitation": {"type": "analytic_gaussian3d", "w0": 0.3, "z0": 1.5,
                       "amplitude": 1.0},
    }

    def elapsed(coast):
        c = json.loads(json.dumps(cfg))
        c["settings"]["per_molecule_skip"] = coast
        t0 = time.perf_counter()
        e = tttrlib.SimEngine.from_json(json.dumps(c))
        e.run()
        return time.perf_counter() - t0, e.n_photons()

    t_off, n_off = elapsed(False)
    t_on, n_on = elapsed(True)
    assert n_off > 0 and n_on > 0
    assert t_on < 3.0 * t_off + 0.5, (
        f"coasting made the grid field {t_on / max(t_off, 1e-9):.0f}x slower "
        f"({t_on:.2f}s vs {t_off:.2f}s) — the per-window field rescan is back")


def test_independent_mode_injects_flow_aware():
    """Independent-molecule mode must inject like the window engine does under flow.

    That mode does not run a live-molecule pool: it draws the number of injections from a
    Poisson over the whole horizon and gives each molecule an independent birth time and
    entry point. It therefore has its **own** copy of the surface-injection code, which is
    easy to leave behind — it was, and drew surface points uniformly with the diffusive-only
    entry depth while the window engine had been made advection-aware. The total rate was
    right (it comes from the shared ``rate_in_``), so the population looked fine; what was
    wrong was *where* molecules entered, with the upstream and downstream faces equally
    likely instead of weighted by the drift through them.

    The observable that catches it is the amplitude: ``G(0) = 1/N`` reports the density in
    the focus, so a skewed entry distribution shows up as a count rate that disagrees with
    the window engine even though both hold the same molecule count.
    """
    base = {
        "settings": {"dt": 0.001, "n_ph_max": 10 ** 12, "max_windows": 300000,
                     "seed_diffusion": 11, "seed_emission": 13, "n_channels": 1,
                     "per_molecule_skip": False},
        "box": {"xy": 2.0, "z": 4.0},
        "species": [{"D": 1.0, "q": [50.0]}],
        "k_rad": [0.0], "k_nrad": [0.0], "background": [0.0],
        "population": [90.0],
        "flow": {"type": "uniform", "vx": 8.0, "vy": 0.0, "vz": 0.0},
        "excitation": {"type": "analytic_gaussian3d", "w0": 0.3, "z0": 1.5,
                       "amplitude": 1.0},
    }
    win = tttrlib.SimEngine.from_json(json.dumps(base))
    win.run()

    ind_cfg = json.loads(json.dumps(base))
    ind_cfg["settings"]["independent_molecules"] = True
    ind = tttrlib.SimEngine.from_json(json.dumps(ind_cfg))
    ind.run_independent(300000)

    n_win, n_ind = win.n_photons(), ind.n_photons()
    assert n_win > 5000 and n_ind > 5000, f"too few photons: {n_win}, {n_ind}"
    # Both are ~Poisson in the number of focus transits, so the spread is a few per cent;
    # a uniform (non-advection-weighted) entry distribution moves this much further.
    assert abs(n_win - n_ind) / max(n_win, n_ind) < 0.15, (
        f"window mode and independent mode disagree on the count rate: "
        f"{n_win} vs {n_ind} photons")


# ---------------------------------------------------------------------------
# Cylindrical symmetry: the (rho, z) fast path
# ---------------------------------------------------------------------------
def _run_with_excitation(exc, windows=120000, seed=3):
    cfg = {
        "settings": {"dt": 0.001, "n_ph_max": 10 ** 12, "max_windows": windows,
                     "seed_diffusion": seed, "seed_emission": seed + 2, "n_channels": 1,
                     "per_molecule_skip": False},
        "box": {"xy": 2.0, "z": 4.0},
        "species": [{"D": 1.0, "q": [50.0]}],
        "k_rad": [0.0], "k_nrad": [0.0], "background": [0.0],
        "population": [200.0],
        "excitation": exc,
    }
    e = tttrlib.SimEngine.from_json(json.dumps(cfg))
    e.run()
    return e.n_photons()


def test_radial_psf_matches_the_lattice():
    """A (rho, z) table reproduces the x-y-z lattice for a symmetric PSF.

    The excitation of a confocal focus depends only on distance from the optical axis and
    on z, so storing it per (x, y, z) keeps one number per azimuth that is the same number.
    The radial form drops that axis: 81x81x161 becomes 41x161, which is the difference
    between streaming 8.5 MB from RAM on every lookup and reading 53 kB out of cache.

    Both must describe the same field, so the photon counts must agree. They are not
    bit-identical: the two interpolate differently off-node, and the radial one is in fact
    the more faithful of the two, since it has no azimuthal interpolation error at all --
    which is why the agreement is asserted at the few-per-cent level rather than exactly,
    and why the coarse grid disagrees more than the fine one.

    This is a speed path, not a replacement: it cannot represent an astigmatic focus (one
    with different x and y waists), a tilted PSF, or anything else that varies with
    azimuth. Those need the lattice, which stays the default.
    """
    fine = dict(type="gaussian3d", w0=0.3, z0=1.5, extent_xy=2.0, extent_z=4.0,
                spacing=0.05, amplitude=1.0)
    n_lattice = _run_with_excitation({**fine, "radial": False})
    n_radial = _run_with_excitation({**fine, "radial": True})
    assert n_lattice > 3000, f"too few photons to compare: {n_lattice}"
    assert abs(n_lattice - n_radial) / n_lattice < 0.05, (
        f"radial and lattice PSFs disagree: {n_lattice} vs {n_radial}")

    # ...and both must agree with the exact analytic Gaussian they discretise.
    n_analytic = _run_with_excitation(
        {"type": "analytic_gaussian3d", "w0": 0.3, "z0": 1.5, "amplitude": 1.0})
    assert abs(n_radial - n_analytic) / n_analytic < 0.05, (
        f"radial PSF disagrees with the analytic field: {n_radial} vs {n_analytic}")


def test_radial_psf_is_faster_and_resolution_independent():
    """Halving the voxel size must not cost the radial path anything.

    The point of dropping the azimuth is that the table stops depending on the lateral
    sampling: refining from 0.05 um to 0.025 um multiplies the lattice by eight (1.1 M ->
    8.3 M voxels) and its lookup cost with it, while the radial table only doubles in one
    direction and stays in cache. Measured: the lattice run goes 5.4 s -> 11.7 s while the
    radial one holds at 3.9 s.

    Asserted as a ratio so the test means the same on any machine, with a wide bar (the
    lattice must be at least 1.5x slower at fine spacing) well inside the ~2.9x measured.
    """
    import time

    fine = dict(type="gaussian3d", w0=0.3, z0=1.5, extent_xy=2.0, extent_z=4.0,
                spacing=0.025, amplitude=1.0)

    def timed(radial):
        t0 = time.perf_counter()
        n = _run_with_excitation({**fine, "radial": radial})
        return time.perf_counter() - t0, n

    t_lat, n_lat = timed(False)
    t_rad, n_rad = timed(True)
    assert n_lat > 3000 and n_rad > 3000
    assert t_lat > 1.5 * t_rad, (
        f"the radial table should be much cheaper at fine spacing: "
        f"lattice {t_lat:.2f}s vs radial {t_rad:.2f}s")
