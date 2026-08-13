"""Recording the state trajectory of an exchanging molecule.

A simulated molecule hops between photophysical states — a FRET pair breathing between
a compact and an extended conformation, a fluorophore blinking, a binding site filling
and emptying. Two outputs can tell you what it was doing.

``set_trajectory_reporter(stride)`` samples every molecule every ``stride`` windows. It
is a snapshot: a state entered and left between two samples leaves no trace at all, so
any time-average taken from it is biased exactly when the exchange is interesting.

``set_state_log(True)`` records the transitions themselves — nothing while a molecule
sits in a state, one row when it moves. Occupation times are then exact, and the cost
scales with the number of transitions rather than with the length of the run.

This script simulates a two-state molecule at three exchange rates and shows what each
output can say about it, then checks the recorded occupancy against the analytic
equilibrium populations.
"""
import matplotlib.pyplot as plt
import numpy as np
import tttrlib

# Two states exchanging spontaneously (no light needed) at k01 and k10, per macro-time
# unit. Equilibrium occupancy of state 0 is k10 / (k01 + k10).
k01, k10 = 40.0, 25.0
p0_expected = k10 / (k01 + k10)

# One macro-window is dt macro-time units; we simulate n_windows of them.
dt, n_windows = 0.01, 100000


def build_engine(k_forward, k_backward):
    """A single immobile molecule exchanging between two dark states."""
    sample = tttrlib.SimSystem()
    for _ in range(2):
        species = tttrlib.SimSpecies()
        species.D = 0.0                                  # immobile: no diffusion
        species.q = tttrlib.VectorDouble([0.0])          # no photons; we watch the state
        sample.add_species(species)
    # Rate matrices are row-major i->j. k_rad scales with the excitation intensity
    # (photo-induced processes); k_nrad is spontaneous, which is what we want here.
    sample.set_rate_matrices(
        tttrlib.VectorDouble([0.0, 0.0, 0.0, 0.0]),
        tttrlib.VectorDouble([0.0, k_forward, k_backward, 0.0]))
    sample.set_background(tttrlib.VectorDouble([0.0]))
    sample.set_box(50.0, 50.0)
    sample.add_fluorophore(0.0, 0.0, 0.0, 0, False)      # species 0, immobile

    settings = tttrlib.SimIntegrator()
    settings.dt = dt
    settings.n_channels = 1
    settings.n_ph_max = 10 ** 12                         # stop on windows, not photons
    settings.max_windows = n_windows
    return tttrlib.SimEngine(
        sample,
        tttrlib.SimGrid.gaussian3d(0.3, 2.0, 4.0, 8.0, 0.2, 1.0),
        tttrlib.VectorSimGrid([]),
        settings)


# --- the log itself ----------------------------------------------------------------
engine = build_engine(k01, k10)
engine.set_state_log(True)                               # must be set before run()
engine.run()

trajectory = engine.state_trajectory()
print("recorded events:", engine.n_state_events())
print("first rows (macro_time, from, to):")
for i in range(5):
    print(f"  {trajectory['macro_time'][i]:8.4f}  "
          f"{trajectory['from'][i]:3d} -> {trajectory['to'][i]:3d}")

# The first row is a birth: from == -1 carries the molecule's initial state, so the log
# describes the whole run without needing to ask the engine what it started as.

# Consecutive events bracket a dwell in one state. Holding times are exponential with
# the rate out of that state, so their mean is 1/k.
dwell = np.diff(trajectory["macro_time"])
state = trajectory["to"][:-1]
print(f"mean dwell in state 0: {dwell[state == 0].mean():.5f}  (expected {1 / k01:.5f})")
print(f"mean dwell in state 1: {dwell[state == 1].mean():.5f}  (expected {1 / k10:.5f})")

# --- occupancy, which is what most analyses actually consume ------------------------
# state_occupancy reduces the log to the fraction of each time bin spent in each state.
# Over the whole run that is the equilibrium population.
_, whole_run = engine.state_occupancy(windows_per_bin=n_windows)
print(f"occupancy of state 0: {whole_run[0, 0, 0]:.4f}  (expected {p0_expected:.4f})")

# --- what a stride cannot see -------------------------------------------------------
# Sweep the exchange rate at a fixed bin width. Slow exchange leaves each bin nearly
# pure; fast exchange averages every bin towards the equilibrium fraction.
bin_windows = 100                                        # bin = 1.0 macro-time unit
rates = np.array([0.2, 1.0, 5.0, 25.0, 125.0, 625.0])
occupancies = []
for k in rates:
    eng = build_engine(k, k)                             # symmetric: equilibrium is 0.5
    eng.set_state_log(True)
    eng.run()
    _, frac = eng.state_occupancy(windows_per_bin=bin_windows)
    occupancies.append(frac[0, :, 0])

# A snapshot taken once per bin can only ever report 0 or 1 for the same molecule.
eng = build_engine(rates[-1], rates[-1])
eng.set_state_log(True)
eng.set_trajectory_reporter(bin_windows)
eng.run()
snapshot = np.asarray(eng.trajectory_species())
_, resolved = eng.state_occupancy(windows_per_bin=bin_windows)

fig, (ax0, ax1) = plt.subplots(1, 2, figsize=(11, 4))

for k, occ in zip(rates, occupancies):
    ax0.hist(occ, bins=np.linspace(0, 1, 41), histtype="step", density=True,
             label=f"k = {k:g}")
ax0.set_xlabel("fraction of bin spent in state 0")
ax0.set_ylabel("probability density")
ax0.set_title(f"exchange averaged over a bin of {bin_windows * dt:g}")
ax0.legend(fontsize=8)

ax1.hist(1 - snapshot, bins=np.linspace(0, 1, 41), histtype="stepfilled",
         alpha=0.4, density=True, label="stride snapshot")
ax1.hist(resolved[0, :, 0], bins=np.linspace(0, 1, 41), histtype="step",
         density=True, lw=2, label="state log")
ax1.axvline(0.5, color="k", ls=":", lw=1)
ax1.set_xlabel("fraction of bin spent in state 0")
ax1.set_title(f"fast exchange (k = {rates[-1]:g}): snapshot vs log")
ax1.legend(fontsize=8)

fig.tight_layout()
plt.show()
