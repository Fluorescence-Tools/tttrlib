"""Single-molecule FRET: burst-wise proximity-ratio histograms (PRD-008).

This example shows the full, realistic single-molecule pipeline end to end using
only tttrlib:

1. **Simulate** a dilute two-state FRET sample from a JSON config (the simulator's
   single entry point). FRET is encoded purely by the per-channel brightness ``q``
   of each species (``q = [(1-E)*b, E*b]`` on the donor/acceptor channels) and
   conformational exchange by the spontaneous rate matrix ``k_nrad`` — this is the
   *routing-channel model*, there is no dedicated FRET engine feature.
2. **Export** the photon stream to a ``tttrlib.TTTR`` with :meth:`SimEngine.to_tttr`.
3. **Detect bursts** with tttrlib's *cumulative* (CUSUM/SPRT) burst search, whose
   background rate is taken directly from the simulation.
4. **Histogram** the per-burst proximity ratio ``PR = n_red / (n_green + n_red)``.

Two configs from ``examples/simulation/configs/`` are compared: two *static* states
(E = 0.25 and 0.75) give two separated peaks; the same states *interconverting*
(``dynamic_fret.json``) bridge toward the mean.
"""
import json
import os

import matplotlib.pyplot as plt
import numpy as np
import tttrlib

HERE = os.path.dirname(__file__)
CONFIGS = os.path.join(HERE, "configs")

DONOR_CH, ACCEPTOR_CH = 0, 1        # detection channels [green, red]


def load(name):
    with open(os.path.join(CONFIGS, name)) as fh:
        cfg = json.load(fh)
    cfg.pop("_comment", None)
    return cfg


def burst_proximity_ratios(cfg):
    """Simulate, export to TTTR, run the cumulative burst search, and return the
    per-burst proximity ratio."""
    sim = tttrlib.SimEngine.from_dict(cfg)
    sim.run()

    # Export to a real TTTR object (SPC-132). ``laser_period`` must match the config
    # so the exported macro-time axis is on the correct absolute time scale.
    tttr = sim.to_tttr(dt=cfg["settings"]["dt"], n_channels=2,
                       laser_period=cfg["settings"].get("laser_period", 32.0))

    # The simulation's *known* background rate, in counts per second, handed to the
    # detector so it does not have to estimate it: sum over channels of the per-ms
    # background, times 1000 ms/s.
    background_cps = sum(cfg["background"]) * 1000.0

    # --- Cumulative (CUSUM / SPRT) burst search -----------------------------------
    # A sequential-probability-ratio test decides, photon by photon, between two
    # hypotheses on the local count rate:
    #   * background      -> rate = background_cps
    #   * inside a burst  -> rate = signal_to_background_ratio x background_cps
    # and marks a burst where the evidence for "signal" wins.
    #
    # Parameters:
    #   min_photons                 discard bursts with fewer photons (removes noise
    #                               spikes; ~20 keeps well-defined single-molecule events).
    #   background_cps              the background (baseline) count rate; here from the sim.
    #   signal_to_background_ratio  how many times brighter a burst is than background;
    #                               the contrast the test is tuned to detect.
    #   alpha                       false-positive rate (calling background a burst).
    #   beta                        false-negative rate (missing a real burst).
    # alpha = beta = 0.01 -> at most ~1% of each error type.
    starts_stops = np.asarray(tttr.burst_search_cusum_sprt(
        min_photons=20,
        background_cps=background_cps,
        signal_to_background_ratio=4.0,
        alpha=0.01,
        beta=0.01,
    ))
    starts, stops = starts_stops[0::2], starts_stops[1::2]

    routing = np.asarray(tttr.routing_channels)
    pr = []
    for a, z in zip(starts, stops):
        burst = routing[int(a):int(z) + 1]
        n_d = np.count_nonzero(burst == DONOR_CH)
        n_a = np.count_nonzero(burst == ACCEPTOR_CH)
        if n_d + n_a > 0:
            pr.append(n_a / (n_d + n_a))
    return np.asarray(pr)


fig, axes = plt.subplots(1, 2, figsize=(10, 4), sharex=True, sharey=True)
bins = np.linspace(0, 1, 31)
for ax, (name, title) in zip(
    axes,
    [("proximity_ratio.json", "Static FRET (two states)"),
     ("dynamic_fret.json", "Dynamic FRET (exchange)")],
):
    pr = burst_proximity_ratios(load(name))
    ax.hist(pr, bins=bins, color="#3b78c3", edgecolor="white")
    for E in (0.25, 0.75):
        ax.axvline(E, color="0.4", ls="--", lw=1)
    ax.set_title(f"{title}  ({len(pr)} bursts)")
    ax.set_xlabel("proximity ratio  n_red / (n_green + n_red)")
axes[0].set_ylabel("bursts")
fig.suptitle("smFRET proximity-ratio histograms via cumulative burst search")
fig.tight_layout()
plt.show()
