"""
eSRRF prototype — Python reference implementation for photon-level reassignment.

This package provides the validated Python implementation of:
- Radial Gradient Convergence (RGC) computation
- Photon reassignment (merged and split channel modes)
- Ground-truth simulation generators
- Validation tests for the five provable properties

Run validation: python -m prototype.esrrf.validate
"""

from .esrrf_reference import rgc_map, temporal_combine
from .esrrf_photon import PhotonData, reassign_photons, reassign_photons_multichannel, intensity_from_photons
from .simulate import (
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
