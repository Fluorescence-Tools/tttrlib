/*!
 * \file SimIntegrator.h
 * \brief Time-base and RNG settings for the photon-simulation engine.
 *
 * Record/TAC encoding lives on SimMicrotimeEncoder; this holds only what the
 * diffusion/emission engine needs. Additive; does not modify existing tttrlib.
 */
#ifndef TTTRLIB_SIMINTEGRATOR_H
#define TTTRLIB_SIMINTEGRATOR_H

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

/// RNG stream granularity (see SimIntegrator::rng_scope).
enum class SimRngScope {
    PerMolecule,  ///< one substream per (molecule, window): reproducible across thread counts
    PerThread     ///< one stream per worker (seeded per window): faster, thread-count-dependent
};

/// Engine time-step, stopping condition, RNG seeds, and RNG backend.
struct SimIntegrator {
    double dt = 0.01;                    ///< diffusion/emission time-window length, in SECONDS (rate
                                          ///< matrices are per second; the micro-time fields below
                                          ///< are in ns — mixed on purpose, matching TCSPC practice)
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

    // ALEX (alternating laser excitation) — a MACRO-time laser alternation, orthogonal to
    // laser_period (the ns TCSPC pulse). With >= 2 excitation grids and alex_period > 0 each
    // macro-window is assigned to laser floor(fmod(T0*dt, alex_period)/(alex_period/n_lasers)),
    // an equal-duty round-robin. 0 = ALEX off (single laser, index always 0 = current behavior).
    double alex_period = 0.0;            ///< ALEX alternation period in macro-time units (same as dt)
    bool alex_markers = false;           ///< emit a marker event at each laser switch (ground truth)
    int alex_marker_event_type = 2;      ///< event_type written for ALEX laser-switch markers (scan uses 1)

    SimRngKind rng_kind = SimRngKind::Xoshiro;  ///< RNG backend
    /// RNG stream granularity. PerMolecule (default): reproducible regardless of thread
    /// count (each molecule keyed by id+window). PerThread: one stream per worker, seeded
    /// once per window — faster/simpler but results depend on thread count and partition.
    SimRngScope rng_scope = SimRngScope::PerMolecule;

    /// Per-molecule coasting (opt-in throughput). A molecule far from BOTH the
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

    /// Active-domain clipping (opt-in throughput; open-volume only). When > 0, the simulation
    /// box is shrunk to the effective-focus AABB expanded by this margin (µm), and the
    /// population is rescaled to hold the configured concentration. Molecules far from the
    /// focus contribute no photons, so restricting diffusion/injection/killing to focus+margin
    /// cuts the wasted far-field work ∝ domain² (measured ~7× at box 10→3µm). It is a
    /// SPEED/ACCURACY KNOB, not free:
    ///   • Pure diffusion / FCS: exact once the margin exceeds the spatial decorrelation
    ///     length (~1–2 µm for typical confocal smFRET; count rate preserved to <1%).
    ///   • Internal kinetics (blinking / FRET-state dynamics): a molecule is born in a fixed
    ///     state and must diffuse far enough to reach its stationary-state distribution BEFORE
    ///     entering the focus. If the margin is smaller than the diffusion length over the
    ///     state-relaxation time (√(2·D/k)), freshly-injected molecules reach the focus
    ///     under-equilibrated and brightness is biased (measured up to +6% count rate for a
    ///     ~2–3 ms⁻¹ blinker at margin 1.5µm). Use a margin ≳ √(2·D/k_min), or leave off for
    ///     kinetics-sensitive observables. Default 0 = no clip (exact).
    double active_margin = 0.0;

    /*!
     * Integrate the flow drift with an explicit midpoint step (default) instead of plain
     * Euler. Costs **one extra field lookup per step**, which on a grid field is the
     * dominant cost: measured 9.7 s vs 6.9 s for a 400k-window Poiseuille run, i.e. about
     * 30 % of the run time.
     *
     * Only non-uniform fields are affected. A uniform field is a constant drift, which
     * Euler integrates exactly, so it never takes the midpoint path and never pays.
     *
     * When it is safe to turn off. The concern is whether the drift map preserves
     * phase-space volume; if it does not, the molecule density drifts and an open volume
     * slowly gains or loses molecules. For a velocity Jacobian `J = grad v`, the Euler map
     * `I + J·dt` has determinant `1 - tr(J^2)·dt^2/2 + ...` for a divergence-free field,
     * and `tr(J^2) = ||S||^2 - ||W||^2` splits into its symmetric (strain) and
     * antisymmetric (vorticity) parts. So:
     *
     *   • **Pure shear — safe to disable.** `Poiseuille` has a nilpotent Jacobian
     *     (`v_x` depends only on y and z), so `tr(J^2) = 0` and Euler is volume-exact.
     *     Its trajectory is exact too, since the transverse coordinates never change.
     *   • **Vorticity — keep it on.** `rotation` has `S = 0`, so the determinant is
     *     `1 + (omega·dt)^2 > 1`: Euler inflates volume every step and molecules spiral
     *     outward. The error is systematic, so it accumulates linearly in time rather than
     *     averaging away — at `omega·dt = 0.002` the radius grows 1.82x over 300k windows.
     *   • **Pure strain — keep it on.** Note that zero vorticity is *not* sufficient:
     *     a strain-only field has `tr(J^2) > 0`, so Euler contracts.
     *
     * In short, disable it for shear-like transport (the usual pCF/flow-profile case) and
     * leave it on for anything that rotates or strains, or when unsure.
     */
    bool drift_midpoint = true;

    /// Two-step field lookup (opt-in throughput). When true, the engine builds a cheap
    /// bounding box (at `focus_threshold`·peak) around each excitation/detection grid so
    /// `SimGrid::at` rejects far-from-focus queries with 6 comparisons before the
    /// trilinear interpolation. Drops the sub-threshold field tail — a speed/accuracy
    /// knob like `focus_threshold`; leave false to keep the fixed-dt path exact.
    bool fast_grid_bbox = false;

    /// Independent single-molecule execution (opt-in). Instead of stepping all molecules
    /// window-by-window, simulate each molecule's whole timeline on its own — coasting far
    /// from the focus, fine-stepping near it — then merge the photon streams. Molecules do
    /// not interact, so this is exact up to the coast approximation, and removes the O(N)
    /// per-window sleeper bookkeeping, so the speedup grows with dilution. It is also
    /// embarrassingly parallel across molecules (no per-window barrier). Requires a fixed
    /// observation window (`max_windows > 0`) and a stationary focus (ignored during a scan);
    /// implies coasting. Reuses the same per-(molecule,window) RNG keying, so output is
    /// thread-count-independent and statistically equivalent to the window engine.
    bool independent_molecules = false;
};

} // namespace tttrlib

#endif // TTTRLIB_SIMINTEGRATOR_H
