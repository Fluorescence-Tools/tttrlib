/*!
 * \file SimSpecies.h
 * \brief One photophysical species/state of a simulated fluorophore (PRD-005).
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
    SimDecay decay;                 ///< micro-time (FLIM) decay pattern; empty = no micro-time

    // Optional anisotropy (rotational-diffusion) model; ignored when D_rot == 0.
    double r0 = 0.0;                ///< limiting anisotropy
    double l1 = 0.0, l2 = 0.0;      ///< polarisation mixing factors
    double D_rot = 0.0;             ///< rotational diffusion coefficient
};

} // namespace tttrlib

#endif // TTTRLIB_SIMSPECIES_H
