"""
Simulate FCS with uniform flow, correlate and compare to the analytic curve.

Run: python flow_fcs.py
"""
import json
import numpy as np

import tttrlib

# Open-volume simulation with uniform flow v=(1,0,0) µm/macro-time.
config = {
    "settings": {
        "dt": 0.001,
        "n_ph_max": 200000,
        "seed_diffusion": 42,
        "seed_emission": 99,
        "n_channels": 1,
        "per_molecule_skip": False,
    },
    "box": {"xy": 5.0, "z": 10.0},
    "species": [{"D": 3.0, "q": [50.0]}],
    "k_rad": [0.0],
    "k_nrad": [0.0],
    "background": [0.0],
    "population": [5.0],
    "flow": {"type": "uniform", "vx": 1.0, "vy": 0.0, "vz": 0.0},
    "excitation": {
        "type": "analytic_gaussian3d",
        "w0": 0.3,
        "z0": 2.0,
        "amplitude": 1.0,
    },
}

e = tttrlib.SimEngine.from_json(json.dumps(config))
e.run()
ph = e.photons()
T = np.asarray(ph["macro_window"], dtype=np.float64) * config["settings"]["dt"]

corr = tttrlib.Correlator()
corr.append(T)
tau, G = np.asarray(corr.correlation[0]), np.asarray(corr.correlation[1])
m = tau > 0
tau, G = tau[m], G[m]

wr, wz = 0.3, 2.0
D_sim, v_sim = 3.0, 1.0
G_analytic = (1.0 / ((1 + 4*D_sim*tau/wr**2) * np.sqrt(1 + 4*D_sim*tau/wz**2))
              * np.exp(-(v_sim*tau)**2 / (wr**2 + 4*D_sim*tau)))

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

fig, ax = plt.subplots()
ax.loglog(tau, G, ".", label="simulated")
ax.loglog(tau, G_analytic, "-", label="analytic")
ax.set_xlabel("tau (macro-time)")
ax.set_ylabel("G(tau)")
ax.legend()
ax.set_title("FCS with uniform flow v=1 µm/ms, D=3 µm²/ms")
fig.tight_layout()
fig.savefig("flow_fcs.png")
print("Saved flow_fcs.png")
