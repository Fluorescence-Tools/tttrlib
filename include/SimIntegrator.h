/*!
 * \file SimSettings.h
 * \brief Time-base and RNG settings for the photon-simulation engine (PRD-005).
 *
 * Record/TAC encoding lives on SimMicrotimeEncoder; this holds only what the
 * diffusion/emission engine needs. Additive; does not modify existing tttrlib.
 */
#ifndef TTTRLIB_SIMSETTINGS_H
#define TTTRLIB_SIMSETTINGS_H

#include <cstdint>

namespace tttrlib {

/// Per-molecule RNG backend for the parallel engine. All are thread-count-independent
/// (each stream is keyed by molecule id + window, not by thread).
enum class SimRngKind {
    Xoshiro,   ///< xoshiro256++ (default): fast, high quality, cheap splitmix64 seeding
    Pcg,       ///< PCG32 (O'Neill): high quality, cheap seeding, built-in stream selection
    Philox,    ///< Philox4x32-10: counter-based, stateless, no seeding cost
    Mt19937    ///< Mersenne Twister: high quality but expensive per-molecule reseed (not recommended)
};

/// RNG stream granularity (see SimSettings::rng_scope).
enum class SimRngScope {
    PerMolecule,  ///< one substream per (molecule, window): reproducible across thread counts
    PerThread     ///< one stream per worker (seeded per window): faster, thread-count-dependent
};

/// Engine time-step, stopping condition, RNG seeds, and RNG backend.
struct SimSettings {
    double dt = 0.01;                    ///< diffusion/emission time-window length
    uint64_t n_ph_max = 1000000;         ///< stop after this many photons
    uint64_t max_windows = 0;            ///< optional hard cap on windows (0 = unlimited)
    uint32_t seed_diffusion = 12345;     ///< RNG stream: diffusion, geometry, state transitions
    uint32_t seed_emission = 54321;      ///< RNG stream: emission, channel choice, background
    int n_channels = 2;                  ///< number of detection channels

    // Micro-time (FLIM) axis — photons are sampled from each species' decay pattern
    // and quantised onto this instrument micro-time channel axis.
    int n_microtime_channels = 4096;     ///< number of micro-time channels
    double microtime_resolution = 0.008; ///< ns per micro-time channel
    double laser_period = 32.0;          ///< pulsed excitation period (ns); micro-time wraps modulo this

    SimRngKind rng_kind = SimRngKind::Xoshiro;  ///< RNG backend
    /// RNG stream granularity. PerMolecule (default): reproducible regardless of thread
    /// count (each molecule keyed by id+window). PerThread: one stream per worker, seeded
    /// once per window — faster/simpler but results depend on thread count and partition.
    SimRngScope rng_scope = SimRngScope::PerMolecule;

    /// Per-molecule coasting (opt-in throughput, PRD-007 G2). A molecule far from BOTH the
    /// focus and the box surface sleeps — its diffusion/state/emission are skipped — and is
    /// caught up exactly on wake. The coast is bounded by the molecule's own distance to the
    /// nearest boundary (÷ coast_safety), so a sleeper can reach neither the focus (no missed
    /// photons) nor the surface (no missed open-volume deaths); surface flux injection is
    /// unchanged (still per-window), so the population stays balanced. When every molecule is
    /// asleep the engine fast-forwards to the earliest wake, batching background over the gap.
    /// Changes the exact RNG draw pattern (validated statistically). No effect during a scan.
    bool per_molecule_skip = false;
    double coast_safety = 3.0;            ///< coast step std ≤ (distance-to-boundary / coast_safety)
    uint64_t min_coast_windows = 8;       ///< don't sleep for fewer than this many windows
    double focus_threshold = 1e-3;        ///< fraction of peak excitation defining the focus AABB
};

} // namespace tttrlib

#endif // TTTRLIB_SIMSETTINGS_H
