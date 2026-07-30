"""
FCS with uniform flow: simulate, correlate, and fit the analytic drift-diffusion curve.

Directed transport shows up in an FCS curve as a *shoulder* that cuts the correlation off
faster than diffusion alone can: molecules are swept out of the focus instead of wandering
out of it. The closed form for a Gaussian focus with lateral waist w0 and axial waist z0 is

    G(tau) = G0 / ((1 + 4 D tau / w0^2) sqrt(1 + 4 D tau / z0^2))
             * exp( -(v tau)^2 / (w0^2 + 4 D tau) )

Choosing v so the flow is actually visible is the part worth copying. The exponential only
matters once the transit time across the waist is comparable to the diffusion time through
it, i.e. once

    v  >~  4 D / w0

Below that threshold flow is numerically invisible against diffusion, no matter how long
the acquisition: at D = 3 um^2/ms, w0 = 0.3 um and v = 1 um/ms the flow term is 3e-4 at the
diffusion time, far under the shot noise. Here D = 0.5 and v = 10, against a threshold of
4*0.5/0.3 = 6.7 um/ms.

The concentration is set to about one molecule in the focus, the usual FCS working point:
V_eff = pi^1.5 w0^2 z0 = 0.75 um^3 against an ellipsoid box of 67 um^3, so a population of
90 gives N = 1 and an amplitude G(0) = 1/N = 1.

Speed
-----
This script runs the *tuned* configuration, because the default one is about ten times
slower for the same answer. Four settings do it, and they compose (19.0 s -> 1.8 s on a
fixed photon budget, with G(0), D and v all unchanged inside their scatter):

    independent_molecules   simulate each molecule's whole timeline and merge; no
                            per-window barrier, so it parallelises freely       ~5-6x
    active_margin           shrink the box to focus+margin at fixed concentration,
                            so molecules too far away to be seen are never stepped  ~3x
    per_molecule_skip       coast molecules that are far from the focus         ~1.3x
    "radial": true          store the cylindrically symmetric PSF on a (rho, z)
                            table instead of an x-y-z lattice                   up to 2.9x

Set FLOW_FCS_TUNED = False below to run the default configuration instead and compare.
The `radial` setting is the only one that is not universally applicable: it cannot
represent an astigmatic or otherwise azimuth-dependent focus.

Run: python flow_fcs.py
"""
import json
import time

import numpy as np
from scipy.optimize import curve_fit

import tttrlib

D_SIM, V_SIM, WR, WZ, DT = 0.5, 10.0, 0.3, 1.5, 0.001

#: Run the tuned configuration (see "Speed" above). False reproduces the slow default.
FLOW_FCS_TUNED = True

config = {
    "settings": {
        "dt": DT,
        "n_ph_max": 150000,
        "max_windows": 3000000,
        "seed_diffusion": 42,
        "seed_emission": 99,
        "n_channels": 1,
        "independent_molecules": FLOW_FCS_TUNED,
        "per_molecule_skip": FLOW_FCS_TUNED,
        "active_margin": 1.0 if FLOW_FCS_TUNED else 0.0,
    },
    "box": {"xy": 2.0, "z": 4.0},
    "species": [{"D": D_SIM, "q": [50.0]}],
    "k_rad": [0.0],
    "k_nrad": [0.0],
    "background": [0.0],
    "population": [90.0],
    "flow": {"type": "uniform", "vx": V_SIM, "vy": 0.0, "vz": 0.0},
    "excitation": {
        "type": "gaussian3d",
        "w0": WR,
        "z0": WZ,
        "extent_xy": 2.0,
        "extent_z": 4.0,
        "spacing": 0.05,
        "amplitude": 1.0,
        # A confocal focus is cylindrically symmetric, so the azimuth carries no
        # information and is not stored. Drop this for an astigmatic PSF.
        "radial": FLOW_FCS_TUNED,
    },
}

engine = tttrlib.SimEngine.from_json(json.dumps(config))
started = time.perf_counter()
if FLOW_FCS_TUNED:
    # Independent-molecule mode needs a fixed horizon and parallelises across molecules.
    engine.set_parallel_threshold(0)
    engine.run_independent(config["settings"]["max_windows"])
else:
    engine.run()
elapsed = time.perf_counter() - started

print(f"tuned       : {FLOW_FCS_TUNED}  ({elapsed:.2f} s)")
print(f"photons     : {engine.n_photons()}")
print(f"windows     : {engine.current_window()}")
if FLOW_FCS_TUNED:
    # Independent-molecule mode keeps no live-molecule pool -- it draws the whole horizon's
    # births from a Poisson up front -- so n_molecules() is 0 by construction and says
    # nothing. The concentration shows up in the fitted amplitude below instead: G(0) = 1/N.
    print("molecules   : n/a in independent mode (see the fitted G(0) = 1/N below)")
else:
    # A molecule crosses this box in 0.2 ms under a 10 um/ms flow, so the standing
    # population is worth checking: it is held there by the advection-aware surface influx,
    # and a diffusion-only influx would have drained the box long before the photon budget.
    print(f"molecules   : {engine.n_molecules()} (population asked for: 90)")

photons = engine.photons()
macro = np.ascontiguousarray(np.asarray(photons["macro_window"], dtype=np.uint64))

correlator = tttrlib.Correlator()
correlator.n_bins = 8
correlator.n_casc = 20
weights = np.ones(macro.size, dtype=float)
correlator.set_macrotimes(macro, macro)
correlator.set_weights(weights, weights)
correlator.run()

tau = np.asarray(correlator.get_x_axis(), dtype=float) * DT
g = np.asarray(correlator.get_corr_normalized(), dtype=float) - 1.0
# Drop the multi-tau tail: its last cascades average a handful of photon pairs and are pure
# noise, swinging to G = -12 and flattening the whole decay into a sliver on a linear axis.
# 1 ms is already ~100x the correlation time here.
keep = (tau > 0) & (tau <= 1.0) & np.isfinite(g)
tau, g = tau[keep], g[keep]


def drift_diffusion(x, g0, d, v):
    """Normalised FCS correlation for 3-D diffusion with a uniform drift."""
    return (g0 / ((1 + 4 * d * x / WR ** 2) * np.sqrt(1 + 4 * d * x / WZ ** 2))
            * np.exp(-(v * x) ** 2 / (WR ** 2 + 4 * d * x)))


popt, _ = curve_fit(drift_diffusion, tau, g, p0=[g[0], D_SIM, V_SIM],
                    bounds=([0, 0.01, 0], [100, 50, 200]), maxfev=20000)
g0_fit, d_fit, v_fit = popt
print(f"fitted G(0) : {g0_fit:.3f}   (expected 1/N = 1.0)")
print(f"fitted D    : {d_fit:.3f} um^2/ms   (simulated {D_SIM})")
print(f"fitted v    : {v_fit:.2f} um/ms     (simulated {V_SIM})")

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

fig, ax = plt.subplots(figsize=(6, 4))
ax.semilogx(tau, g, ".", ms=4, label="simulated")
ax.semilogx(tau, drift_diffusion(tau, *popt), "-", lw=2, label="fit (D, v)")
# The same fit with the flow switched off, to show what the shoulder is worth.
ax.semilogx(tau, drift_diffusion(tau, g0_fit, d_fit, 0.0), "--", lw=1.5,
            label="same D, no flow")
ax.set_xlabel(r"$\tau$ (ms)")
ax.set_ylabel(r"$G(\tau)$")
ax.set_ylim(-0.1, 1.15 * g0_fit)
ax.set_title(f"FCS with uniform flow: v = {V_SIM} um/ms, D = {D_SIM} um$^2$/ms")
ax.legend()
fig.tight_layout()
fig.savefig("flow_fcs.png", dpi=130)
print("Saved flow_fcs.png")
