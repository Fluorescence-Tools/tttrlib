"""FRET via routing channels (PRD-008): proximity ratios and dynamic exchange.

These validate the core claim of the routing-channel model — that FRET is encoded
purely by per-species per-channel brightness ``q`` and exchange by ``k_nrad`` — with
no dedicated engine feature. See ``doc/simulator-guide.rst``.
"""

from __future__ import annotations

import numpy as np
import pytest

tttrlib = pytest.importorskip("tttrlib")
pytestmark = pytest.mark.skipif(
    not hasattr(tttrlib, "SimEngine"), reason="tttrlib built without the Sim* simulator"
)

# detection channels are [green (donor), red (acceptor)]
_B = 80.0  # total brightness (photons / macro-time unit)


def _fret_species(E, D=3.0):
    return {"D": D, "q": [(1.0 - E) * _B, E * _B]}


def _run(cfg):
    e = tttrlib.SimEngine.from_dict(cfg)
    e.run()
    return e.photons()


@pytest.mark.slow
def test_proximity_ratio_two_static_states_give_two_E():
    """Two static FRET species (E=0.25, 0.75) recover their E as the per-species red fraction."""
    cfg = {
        # active_margin + fast_grid_bbox keep the (photon-starved, open-volume) run cheap;
        # they are exact for diffusion, so the recovered proximity ratio is unaffected.
        "settings": {"dt": 0.01, "n_ph_max": 80000, "n_channels": 2,
                     "active_margin": 1.0, "fast_grid_bbox": True},
        "species": [_fret_species(0.25), _fret_species(0.75)],
        "k_rad": [0, 0, 0, 0], "k_nrad": [0, 0, 0, 0],   # static: no exchange
        "background": [0.0, 0.0], "population": [0.6, 0.6],
        "excitation": {"type": "gaussian3d", "w0": 0.3, "z0": 2.0,
                       "extent_xy": 2.0, "extent_z": 4.0, "spacing": 0.1},
    }
    ph = _run(cfg)
    sp, ch = ph["species"], ph["channel"]
    for i, E in ((0, 0.25), (1, 0.75)):
        m = sp == i
        red_frac = (ch[m] == 1).mean()            # PR = n_red / (n_green + n_red)
        assert abs(red_frac - E) < 0.02, f"species {i}: PR={red_frac:.3f} vs E={E}"


@pytest.mark.slow
@pytest.mark.smoke
def test_dynamic_exchange_reaches_symmetric_steady_state():
    """Two equally-bright states with fast symmetric spontaneous exchange (k_nrad). Fast
    exchange (relative to the box residence time) equilibrates the born-in-state-0 population
    to 50/50, so the two states emit equal photon counts — proving k_nrad drives transitions
    and reaches its steady state. Exchange time 1/k << diffusion time, so active_margin (>
    sqrt(2D/k)=0.45 µm) stays valid for kinetics here."""
    k = 30.0                                       # symmetric rate (per ms), fast vs box residence
    cfg = {
        "settings": {"dt": 0.01, "n_ph_max": 80000, "n_channels": 2,
                     "active_margin": 1.0, "fast_grid_bbox": True},
        # equal brightness so photon counts track occupancy, not brightness
        "species": [{"D": 3.0, "q": [50.0, 50.0]}, {"D": 3.0, "q": [50.0, 50.0]}],
        "k_rad": [0, 0, 0, 0],
        "k_nrad": [0.0, k,
                   k, 0.0],
        "background": [0.0, 0.0], "population": [1.0, 0.0],   # all born in state 0
        "excitation": {"type": "gaussian3d", "w0": 0.3, "z0": 2.0,
                       "extent_xy": 2.0, "extent_z": 4.0, "spacing": 0.1},
    }
    ph = _run(cfg)
    frac0 = (ph["species"] == 0).mean()
    assert abs(frac0 - 0.5) < 0.04, f"state-0 fraction {frac0:.3f} vs symmetric 0.5"
