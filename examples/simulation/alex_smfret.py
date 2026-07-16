"""
ALEX smFRET simulation and E-S analysis
=======================================

Micro-second **ALEX** (alternating laser excitation) alternates a green (donor) and a red
(acceptor) laser on the macro-time / diffusion timescale — unlike PIE, which separates
excitation by micro-time. ``tttrlib``'s photon simulator models this with one excitation grid
per laser and a per-laser brightness matrix ``q_alex`` on each species, so a doubly-labelled
molecule emits DD + DA under the green laser and AA under the red laser.

This example simulates two diffusing FRET populations (``E`` ≈ 0.2 and 0.8), exports the stream
to the ISS ``SM`` container, folds the macro-time to recover the excitation windows
(:meth:`TTTR.alex_to_microtime` + an auto-split on the donor detector), burst-searches, and
builds the 2D E-S histogram. See the companion notebooks ``alex_01_simulation_basics.ipynb`` and
``alex_02_smfret_es.ipynb`` for a step-by-step walk-through.
"""
# %%
import json
import os
import tempfile

import numpy as np
import matplotlib.pyplot as plt

import tttrlib

# %%
# Build a TTTR from the simulator's ``photons()`` arrays
# ------------------------------------------------------
# The macro-time resolution is chosen so the ALEX period is an integer number of ticks. SM keeps
# macro + routing (micro-time dropped, as for a real ISS file); markers are engine-level ground
# truth and are not written as photon records.
SM_CONTAINER, SM_RECORD, TY_F8 = 7, 11, 536870920


def sim_photons_to_tttr(ph, dt, macro_res):
    photon = ph["event_type"] == 0
    mw = ph["macro_window"][photon].astype(np.float64)
    macro = np.floor((mw * dt + ph["arrival_time"][photon]) / macro_res).astype(np.uint64)
    order = np.argsort(macro, kind="stable")
    d = tttrlib.TTTR()
    d.append_events(macro[order], ph["micro_time"][photon][order].astype(np.uint16),
                    ph["channel"][photon][order].astype(np.int8),
                    np.zeros(int(photon.sum()), np.int8))
    d.header.tttr_container_type = SM_CONTAINER
    d.header.tttr_record_type = SM_RECORD
    d.header.set_tag("MeasDesc_GlobalResolution", macro_res, TY_F8)
    fn = tempfile.mktemp(suffix=".sm")
    try:
        d.write(fn)
        return tttrlib.TTTR(fn, "SM")
    finally:
        if os.path.isfile(fn):
            os.unlink(fn)


def _runs(mask):
    mask = np.asarray(mask, bool)
    n = len(mask)
    if n == 0 or not mask.any():
        return []
    if mask.all():
        return [(0, n - 1)]
    off = int(np.argmin(mask))
    rolled = np.roll(mask, -off)
    out, i = [], 0
    while i < n:
        if rolled[i]:
            j = i
            while j < n and rolled[j]:
                j += 1
            out.append(((i + off) % n, (j - 1 + off) % n))
            i = j
        else:
            i += 1
    return out


def auto_alex_windows(phase, rc, donor, acceptor, period, n_bins=200, guard=0.06, occ=0.3):
    """Green = donor-detector plateau; red = donor-dark / acceptor-lit region."""
    edges = np.linspace(0, period, n_bins + 1)
    dc, _ = np.histogram(phase[np.isin(rc, donor)], bins=edges)
    ac, _ = np.histogram(phase[np.isin(rc, acceptor)], bins=edges)
    if dc.sum() == 0 or ac.sum() == 0:
        raise ValueError("need donor and acceptor photons")

    def rs(c, r):
        return (c[r[0]:r[1] + 1].sum() if r[0] <= r[1]
                else c[r[0]:].sum() + c[:r[1] + 1].sum())

    d_on = dc > occ * np.percentile(dc[dc > 0], 75)
    green = max(_runs(d_on), key=lambda r: rs(dc, r))
    reds = _runs((~d_on) & (ac > occ * np.percentile(ac[ac > 0], 75)))
    if not reds:
        raise ValueError("no acceptor-excitation window (not ALEX?)")
    red = max(reds, key=lambda r: rs(ac, r))

    def win(run):
        s, e = run
        lo = float(edges[s])
        hi = float(edges[e + 1]) if e + 1 < len(edges) else float(period)
        m = guard * ((hi - lo) if hi > lo else (period - lo + hi))
        return (lo + m, hi - m)

    return win(green), win(red)


# %%
# Simulate, export to SM, and recover the excitation windows
# ----------------------------------------------------------
cfg = json.load(open(os.path.join(os.path.dirname(__file__), "configs", "alex.json")))
cfg.pop("_comment", None)
dt = cfg["settings"]["dt"]
macro_res = dt / 1024
period_ticks = int(round(cfg["settings"]["alex_period"] / macro_res))

eng = tttrlib.SimEngine.from_dict(cfg)
eng.run()
ph = eng.photons()

data = sim_photons_to_tttr(ph, dt, macro_res)
data.alex_to_microtime(period_ticks, 0)
phase = np.asarray(data.micro_times)
rc = np.asarray(data.routing_channels)
(g_lo, g_hi), (r_lo, r_hi) = auto_alex_windows(phase, rc, [0], [1], period_ticks)
green = (phase >= g_lo) & (phase < g_hi)
red = (phase >= r_lo) & (phase < r_hi)

# %%
# Burst search and per-burst E / S
# --------------------------------
bursts = np.asarray(
    data.burst_search(L=40, m=10, T=0.5, mode="sliding_window")).reshape(-1, 2)
i_dd = np.zeros(len(bursts))
i_da = np.zeros(len(bursts))
i_aa = np.zeros(len(bursts))
for k, (s, e) in enumerate(bursts):
    sl = slice(int(s), int(e) + 1)
    g, r, c = green[sl], red[sl], rc[sl]
    i_dd[k] = np.count_nonzero(g & (c == 0))
    i_da[k] = np.count_nonzero(g & (c == 1))
    i_aa[k] = np.count_nonzero(r & (c == 1))
grn = i_dd + i_da
E = np.divide(i_da, grn, out=np.zeros_like(i_da), where=grn > 0)
S = np.divide(grn, grn + i_aa, out=np.zeros_like(grn), where=(grn + i_aa) > 0)
sel = (grn + i_aa) > 30
E, S = E[sel], S[sel]
print("%d bursts; low-E %.3f (0.20), high-E %.3f (0.80), S %.3f"
      % (sel.sum(), E[E < 0.5].mean(), E[E >= 0.5].mean(), S.mean()))

# %%
# The 2D E-S histogram
# --------------------
fig, ax = plt.subplots(figsize=(6, 4))
h = ax.hist2d(E, S, bins=50, range=[[0, 1], [0, 1]], cmap="viridis", cmin=1)
fig.colorbar(h[3], ax=ax, label="bursts")
ax.set_xlabel("FRET efficiency  E")
ax.set_ylabel("Stoichiometry  S")
ax.set_title("Simulated ALEX E-S histogram")
plt.tight_layout()
plt.show()
