"""
eSRRF prototype — Python reference implementation for photon-level reassignment.

This package provides the validated Python implementation of:
- Radial Gradient Convergence (RGC) computation
- Photon reassignment (merged and split channel modes)
- Ground-truth simulation generators
- Validation tests for the five provable properties

Run validation: python -m prototype.esrrf.validate
"""

# The phantom generators and the CLSM renderer live in examples/simulation:
# the shipped ISM and superres examples are built on them, so they cannot sit
# in a directory that never reaches main. This package is dev-only and depends
# on them -- never the other way round.
import sys as _sys
from pathlib import Path as _Path
_sim = _Path(__file__).resolve().parents[2] / "examples" / "simulation"
if str(_sim) not in _sys.path:
    _sys.path.insert(0, str(_sim))

from .esrrf_reference import rgc_map, temporal_combine
from .esrrf_photon import PhotonData, reassign_photons, reassign_photons_multichannel, intensity_from_photons
from simulate import (
    Emitter,
    CLSMScanParameters,
    isolated_point_emitters,
    two_point_pairs,
    crossing_filaments,
    ring_phantom,
    siemens_star,
    dense_random_field,
    two_colour_filaments,
)

__all__ = [
    # Reference
    "rgc_map",
    "temporal_combine",
    # Photon
    "PhotonData",
    "reassign_photons",
    "reassign_photons_multichannel",
    "intensity_from_photons",
    # Simulation
    "Emitter",
    "CLSMScanParameters",
    "isolated_point_emitters",
    "two_point_pairs",
    "crossing_filaments",
    "ring_phantom",
    "siemens_star",
    "dense_random_field",
    "two_colour_filaments",
]
