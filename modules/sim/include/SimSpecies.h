/*!
 * \file SimSpecies.h
 * \brief One photophysical species/state of a simulated fluorophore.
 *
 * In the diffusion-photon model a molecule occupies one of N species/states; it
 * emits with a per-channel brightness and may transition between states (the
 * transition-rate matrices live on `SimSystem`, being N×N across all species).
 * Optional anisotropy parameters revive the legacy rotational-diffusion model.
 * Additive; does not modify any existing tttrlib class.
 */
#ifndef TTTRLIB_SIMSPECIES_H
#define TTTRLIB_SIMSPECIES_H

#include <vector>
#include "SimDecay.h"

namespace tttrlib {

/// Photophysical properties of one species/state.
struct SimSpecies {
    double D = 0.0;                 ///< translational diffusion coefficient, length²/macro-time
                                    ///< (µm² per SimIntegrator::dt unit; the ms convention ⇒ µm²/ms).
                                    ///< 0 = immobile. See the unit contract (macro vs micro-time).
    std::vector<double> q;          ///< brightness per detection channel (photons/molecule/time)

    /// Per-laser brightness rows for ALEX: q_alex[laser][channel]. Empty (default) => the scalar
    /// `q` row is broadcast to every laser (back-compat). When non-empty it must have exactly
    /// n_lasers rows (each row per-detection-channel like `q`). Lets one doubly-labelled FRET
    /// molecule emit DD+DA under the green laser (row 0) and AA under the red laser (row 1).
    std::vector<std::vector<double>> q_alex;

    SimDecay decay;                 ///< micro-time (FLIM) decay pattern; empty = no micro-time

    // Optional anisotropy (rotational-diffusion) model; ignored when D_rot == 0.
    double r0 = 0.0;                ///< limiting anisotropy
    double l1 = 0.0, l2 = 0.0;      ///< polarisation mixing factors
    double D_rot = 0.0;             ///< rotational diffusion coefficient
    double v_scale = 1.0;           ///< coupling of this species to the global flow field
                                    ///< (0 = not advected, e.g. a surface-bound state)
};

} // namespace tttrlib

#endif // TTTRLIB_SIMSPECIES_H
