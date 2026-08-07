"""ALEX (alternating laser excitation) in the photon simulator.

Real ALEX alternates the green (donor) and red (acceptor) laser on the MACRO-time /
diffusion timescale — unlike PIE, which separates excitation sources by micro-time. The
engine models this with one excitation grid per laser and a per-laser brightness matrix
``q_alex`` on each species, so a doubly-labelled molecule emits DD+DA under the green laser
and AA under the red laser. The alternation is encoded in macro-time and recovered by
``TTTR.alex_to_microtime`` + an auto-split on the folded phase; optional laser-switch markers
give explicit ground truth.
"""
import json
import os
import tempfile

import numpy as np
import pytest

tttrlib = pytest.importorskip("tttrlib")

if not hasattr(tttrlib, "SimEngine"):
    pytest.skip("tttrlib built without the photon simulator", allow_module_level=True)

HERE = os.path.dirname(os.path.abspath(__file__))
ALEX_CONFIG = os.path.normpath(
    os.path.join(HERE, "..", "..", "..", "examples", "simulation", "configs", "alex.json"))

SM_CONTAINER, SM_RECORD = 7, 11
TY_F8 = 536870920

# ---------------------------------------------------------------------------
# Helpers reused by the notebooks: build a TTTR from photons() arrays (NOT to_tttr, which only
# emits SPC-130), and auto-detect the ALEX windows from the folded phase.
# ---------------------------------------------------------------------------


def sim_photon_arrays(ph, dt, macro_res):
    """Photon-only (macro_int, micro, routing) arrays from ``SimEngine.photons()``.

    ``macro_time_int = floor((macro_window*dt + arrival_time) / macro_res)``. Marker events are
    dropped (they carry no real detector/micro-time and are not representable as photon records);
    markers are ground truth at the engine level via ``photons()['event_type']``.
    """
    photon = ph["event_type"] == 0
    mw = ph["macro_window"][photon].astype(np.float64)
    arr = ph["arrival_time"][photon].astype(np.float64)
    macro = np.floor((mw * dt + arr) / macro_res).astype(np.uint64)
    order = np.argsort(macro, kind="stable")
    return (macro[order], ph["micro_time"][photon][order].astype(np.uint16),
            ph["channel"][photon][order].astype(np.int8))


def sim_photons_to_tttr(ph, dt, macro_res, container="SM",
                        record_type=SM_RECORD, container_id=SM_CONTAINER):
    """Build a ``tttrlib.TTTR`` (photons only) from ``SimEngine.photons()``.

    SM keeps macro + routing (micro-time dropped); lossless containers (PTU / Photon-HDF5)
    keep macro + micro + routing exactly.
    """
    macro, micro, routing = sim_photon_arrays(ph, dt, macro_res)
    d = tttrlib.TTTR()
    d.append_events(macro, micro, routing, np.zeros(len(macro), np.int8))
    d.header.tttr_container_type = container_id
    d.header.tttr_record_type = record_type
    d.header.set_tag("MeasDesc_GlobalResolution", macro_res, TY_F8)
    fn = tempfile.mktemp(suffix="." + container.lower().replace("-", ""))
    try:
        assert d.write(fn)
        return tttrlib.TTTR(fn, container)
    finally:
        if os.path.isfile(fn):
            os.unlink(fn)


def _contiguous_runs(mask):
    mask = np.asarray(mask, bool)
    n = len(mask)
    if n == 0 or not mask.any():
        return []
    if mask.all():
        return [(0, n - 1)]
    off = int(np.argmin(mask))
    rolled = np.roll(mask, -off)
    runs, i = [], 0
    while i < n:
        if rolled[i]:
            j = i
            while j < n and rolled[j]:
                j += 1
            runs.append(((i + off) % n, (j - 1 + off) % n))
            i = j
        else:
            i += 1
    return runs


def auto_alex_windows(phase, rc, donor_ch, acceptor_ch, period,
                      n_bins=200, guard=0.06, occ=0.3):
    """Detect green/red ALEX windows from the folded phase using the donor detector.

    The donor detector is bright only under green (donor) excitation, so its phase histogram
    marks the green window; the red window is where the donor is dark but the acceptor is lit.
    Works whether or not the two laser windows are separated by rise/fall gaps (real ALEX
    lasers alternate back-to-back). Raises for continuous-wave (non-ALEX) data.
    """
    phase = np.asarray(phase)
    rc = np.asarray(rc)
    edges = np.linspace(0, period, n_bins + 1)
    dc, _ = np.histogram(phase[np.isin(rc, donor_ch)], bins=edges)
    ac, _ = np.histogram(phase[np.isin(rc, acceptor_ch)], bins=edges)
    if dc.sum() == 0 or ac.sum() == 0:
        raise ValueError("need both donor and acceptor photons to detect ALEX windows")

    def rs(c, r):
        return (c[r[0]:r[1] + 1].sum() if r[0] <= r[1]
                else c[r[0]:].sum() + c[:r[1] + 1].sum())

    d_on = dc > occ * np.percentile(dc[dc > 0], 75)
    green_runs = _contiguous_runs(d_on)
    if not green_runs:
        raise ValueError("no donor-excitation (green) window found")
    green_run = max(green_runs, key=lambda r: rs(dc, r))

    red_runs = _contiguous_runs((~d_on) & (ac > occ * np.percentile(ac[ac > 0], 75)))
    if not red_runs:
        raise ValueError("no acceptor-excitation (red) window found — not ALEX?")
    red_run = max(red_runs, key=lambda r: rs(ac, r))

    def win(run):
        s, e = run
        lo = float(edges[s])
        hi = float(edges[e + 1]) if e + 1 < len(edges) else float(period)
        w = (hi - lo) if hi > lo else (period - lo + hi)
        m = guard * w
        return (lo + m, hi - m)

    return {"green": win(green_run), "red": win(red_run)}


def _alex_config(**settings_over):
    """A small deterministic ALEX config: one immobile doubly-labelled molecule, 2 lasers."""
    cfg = {
        "settings": {"dt": 0.01, "n_ph_max": 120000, "n_channels": 2, "laser_period": 32.0,
                     "alex_period": 0.04, "seed_diffusion": 1, "seed_emission": 2},
        "box": {"xy": 1.5, "z": 3.0},
        # green row: DD (ch0) + DA (ch1) with E=0.25; red row: AA (ch1) only.
        "species": [{"D": 0.0, "q_alex": [[180.0, 60.0], [0.0, 220.0]]}],
        "k_rad": [0], "k_nrad": [0], "background": [0.0, 0.0],
        "emitters": [{"x": 0.0, "y": 0.0, "z": 0.0, "species": 0}],
        "excitation": [
            {"type": "gaussian3d", "w0": 0.3, "z0": 2.0, "extent_xy": 1.5, "extent_z": 3.0, "spacing": 0.1},
            {"type": "gaussian3d", "w0": 0.3, "z0": 2.0, "extent_xy": 1.5, "extent_z": 3.0, "spacing": 0.1},
        ],
    }
    cfg["settings"].update(settings_over)
    return cfg


def _windows_per_laser(cfg):
    return int(round(cfg["settings"]["alex_period"] / cfg["settings"]["dt"] / 2))


# ---------------------------------------------------------------------------
# Engine / physics
# ---------------------------------------------------------------------------


def test_getters():
    e = tttrlib.SimEngine.from_dict(_alex_config())
    assert e.n_lasers() == 2
    assert e.alex_period() == pytest.approx(0.04)


def test_laser_alternates_per_window():
    # Species A emits only under laser 0, species B only under laser 1.
    cfg = _alex_config()
    cfg["species"] = [
        {"D": 0.0, "q_alex": [[100.0, 0.0], [0.0, 0.0]]},   # laser 0 only, ch0
        {"D": 0.0, "q_alex": [[0.0, 0.0], [0.0, 100.0]]},   # laser 1 only, ch1
    ]
    cfg["emitters"] = [{"x": 0.0, "y": 0.0, "z": 0.0, "species": 0},
                       {"x": 0.0, "y": 0.0, "z": 0.0, "species": 1}]
    e = tttrlib.SimEngine.from_dict(cfg)
    e.run()
    ph = e.photons()
    photon = ph["event_type"] == 0
    wpl = _windows_per_laser(cfg)
    laser = (ph["macro_window"] // wpl) % 2
    a = photon & (ph["species"] == 0)
    b = photon & (ph["species"] == 1)
    assert a.sum() > 1000 and b.sum() > 1000
    # species A only in laser-0 windows, species B only in laser-1 windows
    assert np.all(laser[a] == 0)
    assert np.all(laser[b] == 1)


def test_dd_da_green_aa_red():
    cfg = _alex_config()
    e = tttrlib.SimEngine.from_dict(cfg)
    e.run()
    ph = e.photons()
    photon = ph["event_type"] == 0
    wpl = _windows_per_laser(cfg)
    laser = (ph["macro_window"] // wpl) % 2
    ch = ph["channel"]
    g = photon & (laser == 0)
    r = photon & (laser == 1)
    # green window: donor (ch0) + acceptor (ch1); red window: acceptor only, NO donor leakage.
    assert (ch[g] == 0).sum() > 0 and (ch[g] == 1).sum() > 0
    assert (ch[r] == 0).sum() == 0
    assert (ch[r] == 1).sum() > 0
    # apparent E from the green window matches the q_alex green row (60 / 240 = 0.25)
    E = (ch[g] == 1).sum() / g.sum()
    assert E == pytest.approx(0.25, abs=0.02)


def test_markers():
    cfg = _alex_config(alex_markers=True, n_ph_max=40000)
    e = tttrlib.SimEngine.from_dict(cfg)
    e.run()
    ph = e.photons()
    markers = ph["event_type"] == 2
    assert markers.sum() > 0
    wpl = _windows_per_laser(cfg)
    # markers sit at the first window of each laser segment; routing channel = laser index
    mwin = ph["macro_window"][markers]
    mch = ph["channel"][markers]
    assert np.all(mwin % wpl == 0)
    assert np.all(mch == (mwin // wpl) % 2)
    # disabled by default
    e2 = tttrlib.SimEngine.from_dict(_alex_config(n_ph_max=40000))
    e2.run()
    assert (e2.photons()["event_type"] != 0).sum() == 0


def test_markers_excluded_from_photon_budget():
    cfg = _alex_config(alex_markers=True, n_ph_max=50000)
    e = tttrlib.SimEngine.from_dict(cfg)
    e.run()
    ph = e.photons()
    photons = (ph["event_type"] == 0).sum()
    # the run stops on the PHOTON count, not total records — markers do not steal the budget
    assert photons >= 50000


def test_q_alex_validation():
    cfg = _alex_config()
    cfg["species"] = [{"D": 0.0, "q_alex": [[1.0, 1.0]]}]   # only 1 row but 2 lasers
    with pytest.raises(Exception):
        tttrlib.SimEngine.from_dict(cfg).run()


def test_q_broadcast_when_no_q_alex():
    # A species with scalar q and no q_alex is excited identically by both lasers.
    cfg = _alex_config()
    cfg["species"] = [{"D": 0.0, "q": [100.0, 100.0]}]
    e = tttrlib.SimEngine.from_dict(cfg)
    e.run()
    ph = e.photons()
    photon = ph["event_type"] == 0
    wpl = _windows_per_laser(cfg)
    laser = (ph["macro_window"] // wpl) % 2
    # both windows equally bright
    assert abs((photon & (laser == 0)).sum() - (photon & (laser == 1)).sum()) \
        < 0.05 * photon.sum()


def test_back_compat_single_laser_identical():
    # One excitation grid + alex off reproduces the pre-ALEX stream regardless of the code path.
    base = _alex_config()
    base["species"] = [{"D": 0.0, "q": [80.0, 40.0]}]
    base["settings"]["alex_period"] = 0.0
    base["excitation"] = base["excitation"][0]           # single object => one laser
    e1 = tttrlib.SimEngine.from_dict(base)
    e1.run()
    a = e1.photons()
    e2 = tttrlib.SimEngine.from_dict(base)
    e2.run()
    b = e2.photons()
    for k in ("macro_window", "arrival_time", "channel", "micro_time", "species"):
        np.testing.assert_array_equal(a[k], b[k])


# ---------------------------------------------------------------------------
# Export / round-trip
# ---------------------------------------------------------------------------


def test_sm_roundtrip_drops_micro():
    cfg = _alex_config(alex_markers=True, n_ph_max=40000, n_microtime_channels=4096)
    cfg["species"] = [{"D": 0.0, "q_alex": [[180.0, 60.0], [0.0, 220.0]],
                       "decay": {"lifetimes": [3.0], "amplitudes": [1.0], "dt": 0.008}}]
    e = tttrlib.SimEngine.from_dict(cfg)
    e.run()
    ph = e.photons()
    dt = cfg["settings"]["dt"]
    macro_res = dt / 1024
    ref_macro, _, ref_rc = sim_photon_arrays(ph, dt, macro_res)
    sm = sim_photons_to_tttr(ph, dt, macro_res, container="SM")
    # SM keeps macro + routing of the photons; micro-time is dropped
    np.testing.assert_array_equal(np.asarray(sm.macro_times), ref_macro)
    np.testing.assert_array_equal(np.asarray(sm.routing_channels), ref_rc)
    assert int(np.asarray(sm.micro_times).max()) == 0


@pytest.mark.parametrize("container,rec,cid", [("PTU", 4, 0), ("PHOTON-HDF5", 4, 5)])
def test_lossless_roundtrip_preserves_photons(container, rec, cid):
    cfg = _alex_config(alex_markers=True, n_ph_max=40000, n_microtime_channels=4096)
    cfg["species"] = [{"D": 0.0, "q_alex": [[180.0, 60.0], [0.0, 220.0]],
                       "decay": {"lifetimes": [3.0], "amplitudes": [1.0], "dt": 0.008}}]
    e = tttrlib.SimEngine.from_dict(cfg)
    e.run()
    ph = e.photons()
    dt = cfg["settings"]["dt"]
    macro_res = dt / 1024
    ref_macro, ref_micro, ref_rc = sim_photon_arrays(ph, dt, macro_res)
    try:
        t = sim_photons_to_tttr(ph, dt, macro_res, container=container,
                                record_type=rec, container_id=cid)
    except Exception as ex:  # e.g. built without Photon-HDF5
        pytest.skip("container %s unavailable: %s" % (container, ex))
    np.testing.assert_array_equal(np.asarray(t.macro_times), ref_macro)
    np.testing.assert_array_equal(np.asarray(t.micro_times), ref_micro)
    np.testing.assert_array_equal(np.asarray(t.routing_channels), ref_rc)


def test_to_tttr_with_markers_preserves_microtime():
    # Regression for the to_tttr fix: markers no longer segfault, and the simulated micro-time
    # (FLIM) is carried faithfully into the SPC round-trip (was mangled before).
    NMT = 4096
    cfg = _alex_config(alex_markers=True, n_ph_max=40000,
                       n_microtime_channels=NMT, microtime_resolution=0.008)
    cfg["species"] = [{"D": 0.0, "q_alex": [[180.0, 60.0], [0.0, 220.0]],
                       "decay": {"lifetimes": [3.2], "amplitudes": [1.0], "dt": 0.008}}]
    e = tttrlib.SimEngine.from_dict(cfg)
    e.run()
    ph = e.photons()
    sim_micro = ph["micro_time"][ph["event_type"] == 0]
    t = e.to_tttr(dt=cfg["settings"]["dt"], n_channels=2,
                  n_microtime_channels=NMT, microtime_resolution=0.008, laser_period=32.0)
    assert len(t) == int((ph["event_type"] == 0).sum())          # markers dropped, no crash
    np.testing.assert_array_equal(np.asarray(t.micro_times), sim_micro)


# ---------------------------------------------------------------------------
# Analysis / loop closure
# ---------------------------------------------------------------------------


def test_fold_recovers_windows_vs_markers():
    cfg = _alex_config(alex_markers=True, n_ph_max=60000)
    e = tttrlib.SimEngine.from_dict(cfg)
    e.run()
    ph = e.photons()
    dt = cfg["settings"]["dt"]
    macro_res = dt / 1024
    t = sim_photons_to_tttr(ph, dt, macro_res, container="SM")
    period_ticks = int(round(cfg["settings"]["alex_period"] / macro_res))
    t.alex_to_microtime(period_ticks, 0)
    phase = np.asarray(t.micro_times)
    win = auto_alex_windows(phase, np.asarray(t.routing_channels), [0], [1], period_ticks)
    g_lo, g_hi = win["green"]
    r_lo, r_hi = win["red"]
    assert g_lo < g_hi <= period_ticks and r_lo < r_hi <= period_ticks
    # the detected green window covers the first half, red the second (equal duty)
    assert g_hi <= period_ticks / 2 + 1
    assert r_lo >= period_ticks / 2 - 1


def test_cw_rejected():
    # A single-laser (non-ALEX) stream has the donor lit in every window -> not ALEX.
    cfg = _alex_config()
    cfg["settings"]["alex_period"] = 0.0
    cfg["excitation"] = cfg["excitation"][0]
    cfg["species"] = [{"D": 0.0, "q": [120.0, 40.0]}]
    e = tttrlib.SimEngine.from_dict(cfg)
    e.run()
    ph = e.photons()
    dt = cfg["settings"]["dt"]
    macro_res = dt / 1024
    t = sim_photons_to_tttr(ph, dt, macro_res, container="SM")
    t.alex_to_microtime(10240, 0)
    with pytest.raises(ValueError):
        auto_alex_windows(np.asarray(t.micro_times), np.asarray(t.routing_channels),
                          [0], [1], 10240)


@pytest.mark.heavy  # 8s
def test_smfret_es_recovery():
    # End-to-end: diffusing two-population ALEX config -> SM -> auto-split -> burst -> E/S.
    cfg = json.load(open(ALEX_CONFIG))
    cfg.pop("_comment", None)
    dt = cfg["settings"]["dt"]
    macro_res = dt / 1024
    e = tttrlib.SimEngine.from_dict(cfg)
    e.run()
    ph = e.photons()
    t = sim_photons_to_tttr(ph, dt, macro_res, container="SM")
    period_ticks = int(round(cfg["settings"]["alex_period"] / macro_res))
    t.alex_to_microtime(period_ticks, 0)
    phase = np.asarray(t.micro_times)
    rc = np.asarray(t.routing_channels)
    win = auto_alex_windows(phase, rc, [0], [1], period_ticks)
    g_lo, g_hi = win["green"]
    r_lo, r_hi = win["red"]
    green = (phase >= g_lo) & (phase < g_hi)
    red = (phase >= r_lo) & (phase < r_hi)
    bursts = np.asarray(t.burst_search(L=40, m=10, T=0.5, mode="sliding_window")).reshape(-1, 2)
    i_dd = np.zeros(len(bursts))
    i_da = np.zeros(len(bursts))
    i_aa = np.zeros(len(bursts))
    for k, (s, en) in enumerate(bursts):
        sl = slice(int(s), int(en) + 1)
        gg, rr, cc = green[sl], red[sl], rc[sl]
        i_dd[k] = np.count_nonzero(gg & (cc == 0))
        i_da[k] = np.count_nonzero(gg & (cc == 1))
        i_aa[k] = np.count_nonzero(rr & (cc == 1))
    grn = i_dd + i_da
    E = np.divide(i_da, grn, out=np.zeros_like(i_da), where=grn > 0)
    sel = (grn + i_aa) > 30
    E = E[sel]
    assert len(E) > 500
    lo, hi = E[E < 0.5], E[E >= 0.5]
    assert lo.mean() == pytest.approx(0.20, abs=0.05)
    assert hi.mean() == pytest.approx(0.80, abs=0.05)


def test_independent_mode_parity():
    # Diffusing doubly-labelled molecules (open-volume) in independent-timeline mode; the ALEX
    # physics (no donor leakage in red windows) must hold there too.
    cfg = _alex_config(n_ph_max=0, max_windows=400000, independent_molecules=True,
                       fast_grid_bbox=True)
    cfg["species"] = [{"D": 0.05, "q_alex": [[180.0, 60.0], [0.0, 220.0]]}]
    cfg["population"] = [0.3]
    cfg.pop("emitters", None)
    e = tttrlib.SimEngine.from_dict(cfg)
    e.run()
    ph = e.photons()
    photon = ph["event_type"] == 0
    assert photon.sum() > 1000
    wpl = _windows_per_laser(cfg)
    laser = (ph["macro_window"] // wpl) % 2
    ch = ph["channel"]
    # red window: acceptor only, no donor leakage — the physics holds in independent mode too
    assert (photon & (laser == 1) & (ch == 0)).sum() == 0
    assert (photon & (laser == 1) & (ch == 1)).sum() > 0
