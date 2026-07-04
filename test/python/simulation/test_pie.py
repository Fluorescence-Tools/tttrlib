"""PIE via micro-time windows (PRD-008).

Pulsed-interleaved excitation is modelled with routing channels + a per-species
``SimDecay.t0`` that places emission in the prompt (donor-excitation) or the
delayed (acceptor-excitation) half of the laser period — no engine PIE feature.
See ``doc/simulator-guide.rst``.
"""

from __future__ import annotations

import numpy as np
import pytest

tttrlib = pytest.importorskip("tttrlib")
pytestmark = pytest.mark.skipif(
    not hasattr(tttrlib, "SimEngine"), reason="tttrlib built without the Sim* simulator"
)


def test_pie_prompt_and_delay_windows_separate():
    """A prompt species (t0=0) emits in [0, P/2); a delayed species (t0=P/2) in [P/2, P)."""
    P = 32.0            # laser period (ns)
    res = 0.008         # ns per micro-time channel
    tau = 3.0           # short vs P/2 so decays stay inside their window
    cfg = {
        "settings": {"dt": 0.01, "n_ph_max": 200000, "n_channels": 2,
                     "n_microtime_channels": 4096, "microtime_resolution": res,
                     "laser_period": P},
        "species": [
            {"D": 0.0, "q": [50.0, 10.0],                         # prompt (donor-excited)
             "decay": {"lifetimes": [tau], "amplitudes": [1.0], "dt": res, "t0": 0.0}},
            {"D": 0.0, "q": [0.0, 40.0],                          # delayed (acceptor-excited)
             "decay": {"lifetimes": [tau], "amplitudes": [1.0], "dt": res, "t0": P / 2.0}},
        ],
        "k_rad": [0, 0, 0, 0], "k_nrad": [0, 0, 0, 0],
        "background": [0.0, 0.0],
        "emitters": [{"x": 0.0, "y": 0.0, "z": 0.0, "species": 0},
                     {"x": 0.0, "y": 0.0, "z": 0.0, "species": 1}],
        "excitation": {"type": "gaussian3d", "w0": 0.3, "z0": 2.0,
                       "extent_xy": 2.0, "extent_z": 4.0, "spacing": 0.1},
    }
    e = tttrlib.SimEngine.from_dict(cfg)
    e.run()
    ph = e.photons()
    micro_ns = ph["micro_time"].astype(float) * res
    half = P / 2.0
    prompt = ph["species"] == 0
    delayed = ph["species"] == 1
    # each population lands almost entirely in its own half of the period
    assert (micro_ns[prompt] < half).mean() > 0.95
    assert (micro_ns[delayed] >= half).mean() > 0.95
    # and gating the micro-time window cleanly separates the two excitation sources
    in_prompt_window = micro_ns < half
    assert (ph["species"][in_prompt_window] == 0).mean() > 0.95
