/*!
 * \file SimEngine.h
 * \brief Diffusion + photophysics + emission engine — the OpenMM-style "context"
 *        of the photon simulator (PRD-005).
 *
 * Reimplements the scientific core of the legacy "Burbulator" `smdif_ov3`:
 * Brownian dynamics of single fluorophores, N-state photophysics/FRET, and
 * inhomogeneous-Poisson photon emission. Fields are grids: excitation is one
 * `SimGrid`; detection is one `SimGrid` per routing channel (offset ⇒ ISM). The
 * engine produces abstract photon records (macro-window, arrival time, channel,
 * species, molecule) which `SimMicrotimeEncoder` / `TTTR::write` turn into files.
 * The Gaussian is the corrected symmetric variant (not the legacy LP64 bug), so
 * output is validated statistically, not byte-for-byte. Additive; does not modify
 * any existing tttrlib class.
 */
#ifndef TTTRLIB_SIMENGINE_H
#define TTTRLIB_SIMENGINE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include "SimSystem.h"
#include "SimGrid.h"
#include "SimScanner.h"
#include "SimIntegrator.h"
#include "SimRandom.h"
#include "SimCounterRandom.h"
#include "SimXoshiroRandom.h"
#include "SimPcgRandom.h"
#include "SimThreadPool.h"
#include "SimZiggurat.h"
#include "SimMicrotimeEncoder.h"
#include "SimInjection.h"

namespace tttrlib {

/*!
 * \brief An immutable snapshot of the live simulation (OpenMM-style `State`).
 *
 * Captures the current macro-window, the photon count so far, and the position/state of every
 * alive molecule (parallel arrays, one entry per molecule). Obtained from
 * `SimEngine::get_state()`; useful for inspection, plotting the instantaneous configuration, or
 * checkpointing alongside the RNG state (`diffusion_state()`/`emission_state()`).
 */
struct SimState {
    uint32_t window = 0;                ///< current macro-window index (T0)
    uint64_t n_photons = 0;             ///< photons generated so far
    int n_molecules = 0;                ///< number of alive molecules (== id.size())
    std::vector<int32_t> id;            ///< molecule id per alive molecule
    std::vector<int16_t> species;       ///< current species/state index per molecule
    std::vector<double> x, y, z;        ///< positions (µm), one per alive molecule
};

/*!
 * \brief Assembles a sample, an excitation grid and per-channel detection grids,
 *        and runs the simulation, accumulating abstract photon records.
 */
class SimEngine {
public:
    /*!
     * \param sample     species/kinetics/background/box + fluorophore population.
     * \param excitation excitation intensity fields, one per ALEX laser (size == n_lasers).
     *                   A single grid ⇒ one laser (no alternation, current behavior).
     * \param detection  detection efficiency grid per channel (size == n_channels);
     *                   empty ⇒ uniform detection (efficiency 1 everywhere).
     * \param settings   time-step, seeds, stopping condition.
     */
    SimEngine(SimSystem sample, std::vector<SimGrid> excitation,
              std::vector<SimGrid> detection, SimIntegrator settings);

    /// Back-compat single-excitation-grid constructor (delegates to the vector form).
    SimEngine(SimSystem sample, SimGrid excitation,
              std::vector<SimGrid> detection, SimIntegrator settings)
        : SimEngine(std::move(sample), std::vector<SimGrid>{std::move(excitation)},
                    std::move(detection), std::move(settings)) {}

    /// Build a fully-configured engine from a JSON config string. Seeds, RNG backend
    /// and scope, species, kinetics, background, box, population, excitation and
    /// per-channel detection fields, and discrete emitters are all JSON-driven.
    /// (Large multi-channel emitter TIFFs are still loaded via set_emitter_grid.)
    /// Returns a heap-allocated engine; the caller owns it (SWIG: %newobject).
    static SimEngine* from_json(const std::string& json_config);

    /// A documented default configuration as a JSON string (template for editing).
    static std::string default_json();

    /// Advance the simulation until `n_ph_max` (or `max_windows`) is reached.
    void run();

    /// Advance exactly `n_windows` time windows (streaming/stepwise use).
    void step(uint64_t n_windows);

    /// Independent single-molecule execution over a fixed horizon of `n_windows`: each
    /// molecule's whole timeline is simulated on its own (coasting far from the focus,
    /// fine-stepping near it) and the photon streams are merged. Embarrassingly parallel
    /// across molecules. `run()` dispatches here when `independent_molecules` is set.
    void run_independent(uint64_t n_windows);

    /// Run a CLSM raster scan: for each pixel, position the fields and dwell, emitting
    /// photons + frame/line/pixel markers into the record stream (event_type 1 = marker).
    /// The resulting records build a marker-annotated TTTR consumable by CLSMImage.
    void run_scan(const SimScanner& scanner);

    /// Record all molecules' positions/state every `stride` windows (0 = off). Set before run().
    void set_trajectory_reporter(uint64_t stride) { traj_stride_ = stride; }

    // --- molecule trajectory (flat, one row per (frame, molecule)) ---------------
    const std::vector<uint32_t>& trajectory_frame() const { return traj_frame_; }
    const std::vector<int32_t>&  trajectory_id() const { return traj_id_; }
    const std::vector<int32_t>&  trajectory_species() const { return traj_species_; }
    const std::vector<double>&   trajectory_x() const { return traj_x_; }
    const std::vector<double>&   trajectory_y() const { return traj_y_; }
    const std::vector<double>&   trajectory_z() const { return traj_z_; }

    /*!
     * \brief Record the state trajectory: every species/state change, exactly when it happens.
     *
     * Complementary to `set_trajectory_reporter`, which samples every molecule on a fixed
     * stride. A stride cannot see a state that is entered and left between two samples, so it
     * biases any time-average of a state-dependent observable whenever the exchange is fast
     * compared with the stride — which is the regime that is usually interesting. This log is
     * *event-based* instead: nothing is written while a molecule sits in a state, and a
     * transition is written with the time it occurred, so occupation times are exact and the
     * cost is proportional to the number of transitions rather than to the run length.
     *
     * The log is self-contained: a birth (`from == -1`) records a molecule's initial state and
     * a death (`to == -1`) records it leaving the box, so a reader never has to guess what a
     * molecule was doing before its first transition. Off by default; set before run().
     *
     * \see state_occupancy (Python) for the time-averaged occupancy this is usually reduced to.
     */
    /// Enabling records every live molecule's current state as a birth, so the log describes the
    /// whole population from the moment it is switched on rather than only the molecules that
    /// happen to be created later.
    void set_state_log(bool on) {
        if (on && !state_log_)
            for (const auto& m : mols_) if (m.alive) log_state(T0_, 0.0, m.id, -1, m.state);
        state_log_ = on;
    }
    bool state_log() const { return state_log_; }

    // --- state trajectory (flat, one row per transition; see set_state_log) ------
    // Rows are in RECORDING order, which is time order except across a coast: a molecule that
    // sleeps is caught up when it wakes, so its transitions are appended then, dated to the
    // earlier windows in which they actually happened. Sort by (window, time) if you need a
    // globally ordered stream — the Python `state_trajectory()` view already does.
    const std::vector<uint32_t>& state_window() const { return st_w_; }    ///< macro-window
    const std::vector<double>&   state_time() const { return st_t_; }      ///< within-window time
    const std::vector<int32_t>&  state_molecule() const { return st_mol_; }
    const std::vector<int16_t>&  state_from() const { return st_from_; }   ///< -1 = birth
    const std::vector<int16_t>&  state_to() const { return st_to_; }       ///< -1 = death
    uint64_t n_state_events() const { return st_w_.size(); }

    /// Write the recorded trajectory to an HDF5 file (group /trajectory). Requires HDF5.
    void write_trajectory_hdf5(const std::string& path) const;

    /// The integrator settings this engine was built with (dt, channel count, laser period, ...).
    /// Read-only: the precomputed per-species tables depend on them, so they are fixed at
    /// construction.
    const SimIntegrator& settings() const { return set_; }
    /// The simulated sample (species, kinetics, background, box).
    const SimSystem& system() const { return sample_; }

    /// Cap worker threads (0 = hardware concurrency). Set before run()/step().
    void set_num_threads(unsigned n) { num_threads_ = n; }
    /// Parallelise a window only when this many molecules are alive (0 keeps the default).
    void set_parallel_threshold(size_t n) { parallel_threshold_ = n; }

    // --- abstract photon records (parallel arrays, length n_photons) -------------
    const std::vector<uint32_t>& macro_window() const { return T_; }   ///< data_T
    const std::vector<double>&   arrival_time() const { return t_; }    ///< data_t (within window)
    const std::vector<int16_t>&  channel() const { return N_; }         ///< detector ch (or marker ch)
    const std::vector<int16_t>&  emitting_species() const { return sp_; }
    const std::vector<int32_t>&  emitting_molecule() const { return mol_; }
    const std::vector<int8_t>&   event_type() const { return et_; }     ///< 0 = photon, 1 = marker
    const std::vector<uint16_t>& micro_time() const { return micro_; }  ///< FLIM micro-time channel
    uint64_t n_photons() const { return T_.size(); }
    uint64_t current_window() const { return T0_; }
    int n_molecules() const { return int(mol_alive_); }

    // --- ALEX (alternating laser excitation) -------------------------------------
    double alex_period() const { return set_.alex_period; }   ///< macro-time units (0 = off)
    int n_lasers() const { return int(exc_.size()); }         ///< number of excitation lasers

    /// Immutable snapshot of the live molecules + counters (OpenMM-style `State`). Not meaningful
    /// in independent-molecule mode (which keeps no live pool).
    SimState get_state() const;

    /*!
     * \brief Encode the accumulated records with `enc` (no array marshalling needed
     *        across the language boundary — convenience for the bindings).
     */
    SimEncodedRecords encode(const SimMicrotimeEncoder& enc, SimRandom& rng,
                             uint64_t mt_overflow_in = 0) const {
        // Only photon events are encodable as SPC photon records; marker events (event_type != 0,
        // e.g. CLSM scan or ALEX laser-switch markers) carry a routing channel / species that is
        // not a real detector and would index out of ch_conversion / the TAC lookup (a segfault)
        // and pollute the photon stream. Skip them here; the micro-time array is passed through so
        // the encoded TAC preserves the simulated FLIM axis (usable by micro-time filters). Callers
        // needing markers should use the array->TTTR path or a lossless container.
        const size_t n = T_.size();
        bool has_marker = false;
        for (size_t i = 0; i < n; ++i) if (et_[i] != 0) { has_marker = true; break; }
        if (!has_marker)
            return enc.encode(T_.data(), t_.data(), N_.data(), sp_.data(), micro_.data(),
                              n, rng, mt_overflow_in);
        std::vector<uint32_t> T; std::vector<double> t;
        std::vector<int16_t> N, sp; std::vector<uint16_t> mi;
        T.reserve(n); t.reserve(n); N.reserve(n); sp.reserve(n); mi.reserve(n);
        for (size_t i = 0; i < n; ++i) if (et_[i] == 0) {
            T.push_back(T_[i]); t.push_back(t_[i]); N.push_back(N_[i]);
            sp.push_back(sp_[i]); mi.push_back(micro_[i]);
        }
        return enc.encode(T.data(), t.data(), N.data(), sp.data(), mi.data(),
                          T.size(), rng, mt_overflow_in);
    }

    // --- reproducible continuation ---------------------------------------------
    SimRngState diffusion_state() const { return rng_diff_.getState(); }
    SimRngState emission_state() const { return rng_emit_.getState(); }

private:
    struct Mol {
        double x, y, z; int state; bool mobile; int id; bool alive;
        double ox = 0, oy = 0, oz = 1;   // dipole orientation (unit vector) for anisotropy
        bool coasting = false;           // per-molecule skip: frozen (no diffusion/emission)
        uint32_t w_sleep = 0, w_wake = 0;  // coast span [w_sleep, w_wake) in window units
    };

    /// One recorded state change (see set_state_log). Carries its own absolute window because
    /// a transition may happen during a coast, i.e. outside the window being processed.
    struct Tr {
        uint32_t w; double t; int32_t mol; int16_t from, to;
    };

    /// Photons produced by one worker over its molecule range (merged after the loop).
    /// `tr` collects that worker's state transitions when the state log is on.
    struct LocalBuf {
        std::vector<double> t;
        std::vector<int16_t> N, sp;
        std::vector<int32_t> mol;
        std::vector<uint16_t> micro;
        std::vector<Tr> tr;
        void clear() { t.clear(); N.clear(); sp.clear(); mol.clear(); micro.clear(); tr.clear(); }
        void push(double tt, int ch, int species, int molid, uint16_t mt) {
            t.push_back(tt); N.push_back(int16_t(ch));
            sp.push_back(int16_t(species)); mol.push_back(int32_t(molid)); micro.push_back(mt);
        }
        void push_transition(uint32_t w, double tt, int molid, int from, int to) {
            tr.push_back(Tr{w, tt, int32_t(molid), int16_t(from), int16_t(to)});
        }
    };

    static constexpr double kEps = 1e-8;

    void seed_population();               ///< initial molecules (discrete + open-volume)
    void inject_open_volume(double windows = 1.0);  ///< surface-flux injection (over N windows)
    void init_orientation(Mol& m);        ///< random dipole orientation (anisotropy)
    double detection_eff(int ch, double x, double y, double z) const;
    void push_marker(int routing_channel);  ///< append a marker event at the current window
    void push_alex_marker(int laser);       ///< append an ALEX laser-switch marker at the current window
    void emit_window();                   ///< one time window: photophysics + emission + diffusion
    template <class Rng> void run_independent_impl(uint64_t W);  ///< independent-mode driver

    // --- per-molecule coasting (opt-in) ----------------------------------------
    void compute_focus_aabb();            ///< effective-focus AABB + uniform_D_/any_knrad_ (once)
    void batch_background(uint64_t n_windows);  ///< emit background over a fast-forwarded gap

    /// Active excitation laser for macro-window T0. Pure function of T0 (no RNG). ALEX is active
    /// only with >= 2 grids and a positive period; otherwise laser 0 (current behavior).
    ///
    /// The alternation is defined in whole macro-windows (exact integer arithmetic — no
    /// floating-point macro-time modulo, which drifts at window boundaries). The ALEX period is
    /// rounded to `windows_per_cycle = round(alex_period/dt)` windows, split into `n` equal-duty
    /// segments of `windows_per_cycle/n` windows each. For clean alternation choose an
    /// alex_period that is an integer multiple of dt and of n.
    inline int laser_for_window(uint32_t T0) const {
        const int n = int(exc_.size());
        if (n <= 1 || set_.alex_period <= 0.0 || set_.dt <= 0.0) return 0;
        long long wpc = (long long)std::llround(set_.alex_period / set_.dt);  // windows per cycle
        if (wpc < n) wpc = n;
        long long wpl = wpc / n;                                              // windows per laser
        if (wpl < 1) wpl = 1;
        return int((T0 / (unsigned long long)wpl) % (unsigned long long)n);
    }

    /// Distance from (x,y,z) to the effective-focus AABB (0 inside; grid extent if AABB invalid).
    inline double focus_gap(double x, double y, double z) const {
        double lx0, ly0, lz0, lx1, ly1, lz1;
        if (focus_aabb_valid_) {
            lx0 = fx0_; ly0 = fy0_; lz0 = fz0_; lx1 = fx1_; ly1 = fy1_; lz1 = fz1_;
        } else if (!exc_.empty() && exc_[0].radial_) {
            double r_max, z_max;
            exc_[0].radial_extent(r_max, z_max);
            lx0 = -r_max; ly0 = -r_max; lz0 = -z_max;
            lx1 = r_max;  ly1 = r_max;  lz1 = z_max;
        } else if (!exc_.empty() && exc_[0].nx > 0) {
            const SimGrid& g = exc_[0];
            lx0 = g.x0; ly0 = g.y0; lz0 = g.z0;
            lx1 = g.x0 + (g.nx - 1) * g.dx;
            ly1 = g.y0 + (g.ny - 1) * g.dy;
            lz1 = g.z0 + (g.nz - 1) * g.dz;
        } else {
            return 0.0;
        }
        auto gap = [](double p, double lo, double hi) {
            return p < lo ? lo - p : (p > hi ? p - hi : 0.0);
        };
        const double gx = gap(x, lx0, lx1), gy = gap(y, ly0, ly1), gz = gap(z, lz0, lz1);
        return std::sqrt(gx * gx + gy * gy + gz * gz);
    }

    /// Conservative distance from (x,y,z) to the open-volume ellipsoid surface (>=0 inside).
    /// In the metric u=(x,y,√box_r_sq·z) the boundary is the sphere |u|=box_xy; box_r_sq<=1
    /// for a z-elongated box, so |Δu| <= |Δx|, i.e. this under-estimates the true Euclidean
    /// gap — safe for bounding the coast (never lets a sleeper cross the surface).
    inline double surface_margin(double x, double y, double z) const {
        if (!open_volume_) return 1e300;
        const double bxy = sample_.box_xy(), bz = sample_.box_z();
        const double box_r_sq = (bxy * bxy) / (bz * bz);
        const double rho = std::sqrt(x * x + y * y + box_r_sq * z * z);
        return bxy - rho;
    }

    /// Advance a molecule's photophysical state over `tau` under spontaneous (k_nrad-only)
    /// kinetics — the exact dynamics outside the focus (Iex=0, no emission).
    template <class Rng>
    int evolve_spontaneous(int state, double tau, Rng& rng) const {
        if (!any_knrad_ || tau <= 0.0) return state;
        const int nsp = sample_.n_species();
        const auto& kn = sample_.k_nrad();
        int i = state; double t = 0.0;
        for (;;) {
            const double koff = koff_nrad_[i];
            if (koff <= kEps) break;
            const double hold = -std::log(rng.random0e1e()) / koff;
            if (t + hold >= tau) break;
            t += hold;
            double r = (1.0 - rng.random0i1e()) * koff;
            int j = -1;
            while (r > 0.0 && j < nsp - 1) {
                ++j;
                const double knij = (size_t(i * nsp + j) < kn.size()) ? kn[i * nsp + j] : 0.0;
                r -= knij;
            }
            if (j < 0) j = i;
            i = j;
        }
        return i;
    }

    /*!
     * \brief Largest coast length, in windows, that keeps a molecule inside `d` of a boundary.
     *
     * The coast has to bound both transport mechanisms: the deterministic drift
     * `v_max*n*dt` and the diffusive spread `safety*sqrt(2*D*n*dt)`. Requiring their sum to
     * stay under `d` is a quadratic in `s = sqrt(n)`,
     *
     *     A*s^2 + B*s - d <= 0,   A = v_max*dt,  B = safety*sqrt(2*D*dt)
     *
     * so `s = (-B + sqrt(B^2 + 4*A*d)) / (2*A)`. With no flow (`A == 0`) it degenerates to
     * the original diffusion-only bound, which is taken verbatim rather than as a limit so
     * a no-flow run keeps its exact previous coast lengths.
     *
     * Shared by `maybe_sleep` (window engine) and `simulate_timeline` (independent engine),
     * which have to agree: a molecule coasting further in one than the other would put the
     * two engines on different trajectories for the same configuration.
     */
    inline uint64_t coast_windows(double d, double D) const {
        const double safety = (set_.coast_safety > 1e-3) ? set_.coast_safety : 3.0;
        const double A = flow_max_speed_ * set_.dt;
        if (A <= kEps) {
            const double sigma = d / safety;          // allowed per-step displacement std
            return uint64_t((sigma * sigma) / (2.0 * D) / set_.dt);
        }
        const double B = safety * std::sqrt(2.0 * D * set_.dt);
        const double s = (-B + std::sqrt(B * B + 4.0 * A * d)) / (2.0 * A);
        return (s > 0.0) ? uint64_t(s * s) : 0;
    }

    /*!
     * \brief Draw one open-volume entry position for species `sp` across the box surface.
     *
     * The single implementation of the surface flux, used by both the window engine
     * (`inject_open_volume`) and the independent engine (`run_independent_impl`). It used
     * to be copied into both, and the copies fell out of step: the independent one kept
     * drawing surface points uniformly with a diffusion-only depth long after the window
     * one became advection-aware. That bias does not show up in the molecule count, only
     * in where molecules enter, so nothing failed.
     *
     * Under flow the surface point is accepted with probability `w(mu)/w_max`, so the
     * upstream face admits more molecules than the downstream one, and the entry depth
     * follows the drift-diffusion law. With no flow both reduce to the historical
     * uniform-point + `random_erfc` draw, consuming the same random numbers in the same
     * order. Occluded entry points are redrawn.
     */
    template <class Rng>
    void draw_surface_entry(Rng& rng, int sp, double& mx, double& my, double& mz) const {
        const double box_xy = sample_.box_xy(), box_z = sample_.box_z();
        const double box_r_sq = (box_xy * box_xy) / (box_z * box_z);
        const double ell_f = box_z / box_xy;
        const double ell_Pzmax = (ell_f <= 0.999999) ? 1. / ell_f : 1.;

        for (int attempt = 0; ; ++attempt) {
            // Uniform point on the ellipsoid surface.
            double ze, r;
            do {
                ze = 2. * rng.random0i1e() - 1.;
                r = rng.random0i1e() * ell_Pzmax;
            } while (r * r > 1. - ze * ze * (1. - box_r_sq));
            const double r_xy = std::sqrt(1. - ze * ze) * box_xy;
            ze *= box_z;
            const double phi = rng.random0i1e() * 2. * sim_detail::kInjPi;
            const double xe = std::cos(phi) * r_xy, ye = std::sin(phi) * r_xy;

            double step_in;
            if (has_flow_) {
                // Outward normal of x^2/a^2 + y^2/a^2 + z^2/c^2 = 1 is (x/a^2, y/a^2, z/c^2).
                double nxv = xe / (box_xy * box_xy);
                double nyv = ye / (box_xy * box_xy);
                double nzv = ze / (box_z * box_z);
                const double nn = std::sqrt(nxv * nxv + nyv * nyv + nzv * nzv);
                nxv /= nn; nyv /= nn; nzv /= nn;
                double vx, vy, vz;
                sample_.flow_field().at(xe, ye, ze, vx, vy, vz);
                const double vs = sample_.species()[sp].v_scale;
                const double mu = -vs * (vx * nxv + vy * nyv + vz * nzv) * set_.dt;
                if (attempt < 1000 &&
                    rng.random0i1e() * w_max_[sp] > sim_detail::influx_weight(mu, step_[sp]))
                    continue;                                     // rejected: redraw
                step_in = sim_detail::random_entry_depth(rng, mu, step_[sp]);
            } else {
                step_in = step_[sp] * sim_detail::random_erfc(rng);
            }

            const double rnnorm = 1. / std::sqrt(r_xy * r_xy + ze * ze * box_r_sq * box_r_sq);
            mx = xe * (1. - step_in * rnnorm);
            my = ye * (1. - step_in * rnnorm);
            mz = ze * (1. - step_in * rnnorm * box_r_sq);

            if (has_occ_ && attempt < 100 && sample_.occlusion().at(mx, my, mz) > 0.5)
                continue;                                         // entered inside a wall
            return;
        }
    }

    /// After processing molecule `m` at window `T0`, decide whether it may sleep. The coast
    /// is sized by its own distance to the nearest boundary (focus or box surface), so it can
    /// reach neither while asleep.
    template <class Rng>
    void maybe_sleep(Mol& m, uint32_t T0, Rng&) {
        if (!m.mobile || !m.alive) return;
        if (has_occ_) return;                                  // a sleeper would tunnel through a wall
        // Bail on a non-uniform field FIRST: there is no closed-form catch-up for one, so
        // everything below is wasted. (This used to sit after the max_speed() call, which
        // scans every voxel of the field -- per molecule, per window. On a 51x51x91 grid
        // that is 237k operations for a decision that was always "return".)
        if (has_flow_ && !uniform_flow_) return;
        const double D = sample_.species()[m.state].D;
        if (D <= kEps) return;
        double d = focus_gap(m.x, m.y, m.z);
        if (d <= 0.0) return;                                  // inside focus: stay awake
        d = std::min(d, surface_margin(m.x, m.y, m.z));
        if (d <= 0.0) return;
        const uint64_t n = coast_windows(d, D);
        if (n < set_.min_coast_windows) return;
        m.coasting = true; m.w_sleep = T0 + 1; m.w_wake = uint32_t(T0 + 1 + n);
    }

    /// Exact catch-up of a molecule over a coast of `n` windows starting at window `w_start`:
    /// one Gaussian displacement whose variance is accumulated along the spontaneous-state
    /// (k_nrad-only) path, plus the state jump. Shared by window-mode wake and independent
    /// mode; the coast RNG is keyed by (id, w_start) so it is thread-count-independent.
    template <class Rng>
    void coast_over(Mol& m, uint32_t w_start, uint64_t n, LocalBuf* log = nullptr) const {
        const double tau = double(n) * set_.dt;
        if (tau <= 0.0) return;
        // Map an elapsed time within the coast onto (absolute window, within-window time), so a
        // transition that happens while a molecule sleeps is timed like any other event.
        auto stamp = [&](double elapsed, int from, int to) {
            if (!log) return;
            uint64_t dw = uint64_t(elapsed / set_.dt);
            if (dw >= n) dw = n - 1;          // keep the event inside the coasted span
            log->push_transition(uint32_t(w_start + dw), elapsed - double(dw) * set_.dt,
                                 m.id, from, to);
        };
        Rng crng; crng.reset(mol_base_seed_ ^ kCoastSalt, uint32_t(m.id),
                             uint64_t(w_start) * kWindowStride);
        const int nsp = sample_.n_species();
        const auto& kn = sample_.k_nrad();
        int i = m.state; double t = 0.0, var = 0.0, vs_dt = 0.0;
        for (;;) {
            const double koff = any_knrad_ ? koff_nrad_[i] : 0.0;
            const double hold = (koff > kEps) ? -std::log(crng.random0e1e()) / koff : 1e300;
            const double seg = std::min(hold, tau - t);
            var += 2.0 * sample_.species()[i].D * seg;         // piecewise-const D along the path
            // The advective displacement is accumulated the same piecewise way: v_scale is a
            // per-species property, so a molecule that changes state mid-coast is advected by
            // each state's coupling for the time it spent in it. Taking a single state's
            // v_scale for the whole coast (either endpoint) is wrong whenever v_scale differs
            // between states, and wrong silently.
            vs_dt += sample_.species()[i].v_scale * seg;
            t += seg;
            if (t >= tau - kEps || koff <= kEps) break;
            double r = (1.0 - crng.random0i1e()) * koff;
            int j = -1;
            while (r > 0.0 && j < nsp - 1) {
                ++j;
                const double knij = (size_t(i * nsp + j) < kn.size()) ? kn[i * nsp + j] : 0.0;
                r -= knij;
            }
            if (j < 0) j = i;
            stamp(t, i, j);
            i = j;
        }
        const double s = std::sqrt(var);
        double g0, g1, g2; norm3(crng, g0, g1, g2);          // ziggurat catch-up displacement
        m.x += s * g0; m.y += s * g1; m.z += s * g2;
        if (has_flow_) {
            // Only a uniform field ever reaches here (maybe_sleep refuses to sleep a molecule
            // under a non-uniform field), so the field value is position-independent and the
            // closed-form displacement v * integral(v_scale dt) is exact.
            double vx, vy, vz;
            sample_.flow_field().at(m.x, m.y, m.z, vx, vy, vz);
            m.x += vx * vs_dt; m.y += vy * vs_dt; m.z += vz * vs_dt;
        }
        m.state = i;
        const double box_xy_sq = sample_.box_xy() * sample_.box_xy();
        const double box_r_sq = box_xy_sq / sample_.box_z() / sample_.box_z();
        if (open_volume_ && m.x * m.x + m.y * m.y + box_r_sq * m.z * m.z > box_xy_sq) {
            m.alive = false;
            // The displacement is a single catch-up draw, so the crossing has no resolvable
            // time within the coast; attribute the death to its end.
            stamp(tau, i, -1);
        }
    }

    /// Wake a coasting molecule at window `T0`: exact catch-up over the elapsed coast.
    template <class Rng>
    void wake_molecule(Mol& m, uint32_t T0, LocalBuf* log = nullptr) const {
        m.coasting = false;
        coast_over<Rng>(m, m.w_sleep, uint64_t(T0 - m.w_sleep), log);
    }

    /// Simulate one molecule's whole timeline independently over [w_birth, W): coast far from
    /// the focus, fine-step (via process_molecule) near it. Photons appended to `ph`, tagged
    /// with their absolute macro-window. Reuses the per-(molecule,window) RNG keying so an
    /// awake window yields the same photons as the window engine.
    template <class Rng, class PhVec>
    void simulate_timeline(Mol m, uint32_t w_birth, uint32_t W, bool coast,
                           std::vector<double>& wv, LocalBuf& buf, PhVec& ph,
                           std::vector<Tr>* tr = nullptr) const {
        const int nchan = set_.n_channels; (void)nchan;
        const double safety = (set_.coast_safety > 1e-3) ? set_.coast_safety : 3.0;
        // `buf` is reused per window, so its transitions must be drained before it is cleared.
        auto drain = [&]() {
            if (!tr) return;
            tr->insert(tr->end(), buf.tr.begin(), buf.tr.end());
            buf.tr.clear();
        };
        uint32_t w = w_birth;
        while (w < W && m.alive) {
            // A non-uniform field has no closed-form catch-up, so it can never coast; test
            // that before any per-window work (see maybe_sleep).
            if (coast && m.mobile && !has_occ_ && !(has_flow_ && !uniform_flow_)) {
                const double gap = focus_gap(m.x, m.y, m.z);
                if (gap > 0.0) {
                    double d = std::min(gap, surface_margin(m.x, m.y, m.z));
                    const double D = sample_.species()[m.state].D;
                    if (d > 0.0 && D > kEps) {
                        {
                            uint64_t n = coast_windows(d, D);
                            if (n >= set_.min_coast_windows) {
                                if (uint64_t(w) + n > W) n = W - w;
                                if (n > 0) {
                                    coast_over<Rng>(m, w, n, tr ? &buf : nullptr);
                                    drain();
                                    w += uint32_t(n);
                                    continue;
                                }
                            }
                        }
                    }
                }
            }
            Rng rng; rng.reset(mol_base_seed_, uint32_t(m.id), uint64_t(w) * kWindowStride);
            buf.clear();
            process_molecule(m, rng, wv, buf, laser_for_window(w), w);
            for (size_t k = 0; k < buf.t.size(); ++k)
                ph.push_back(typename PhVec::value_type{
                    w, buf.t[k], buf.N[k], buf.sp[k], buf.mol[k], buf.micro[k]});
            drain();
            ++w;
        }
    }

    /// Process one molecule for the current window into `buf` using RNG backend `Rng`.
    /// The per-molecule stream is (re)positioned from (mol_base_seed_, id, counter_start),
    /// so results are independent of the number of worker threads.
    template <class Rng>
    void process_molecule(Mol& m, Rng& rng, std::vector<double>& w, LocalBuf& buf,
                          int laser, uint32_t window = 0) const {
        const double dt = set_.dt;
        const int nsp = sample_.n_species();
        const int nchan = set_.n_channels;
        const auto& kr = sample_.k_rad();
        const auto& kn = sample_.k_nrad();
        const double box_xy_sq = sample_.box_xy() * sample_.box_xy();
        const double box_r_sq = box_xy_sq / sample_.box_z() / sample_.box_z();

        int i = m.state;
        const double Iex = exc_[laser].at(m.x - beam_x_, m.y - beam_y_, m.z);  // active ALEX laser

        // Micro-time (FLIM) draw from a state's decay pattern, wrapped + quantised.
        auto sample_micro = [&](int st) -> uint16_t {
            const SimDecay& dec = sample_.species()[st].decay;
            if (dec.empty()) return 0;
            double mns = std::fmod(dec.sample_ns(rng), set_.laser_period);
            if (mns < 0.0) mns += set_.laser_period;
            int ch = int(mns / set_.microtime_resolution);
            if (ch < 0) ch = 0;
            else if (ch >= set_.n_microtime_channels) ch = set_.n_microtime_channels - 1;
            return uint16_t(ch);
        };

        double t_tr = 0.0, t_shift;
        do {
            t_shift = t_tr;
            double koff = Iex * koff_rad_[i] + koff_nrad_[i];
            double tau_off = (koff > kEps) ? 1.0 / koff : 0.0;
            t_tr = (koff > kEps) ? t_shift - std::log(rng.random0e1e()) * tau_off : dt + 1.0;

            if (aniso_[i]) {
                // Anisotropy (rotdiff): x-polarised photoselection (∝ 3·ox²); the emission
                // dipole is the absorption dipole tilted by the intrinsic r0 cone AND rotated
                // by rotational diffusion over the excited-state (micro-time) delay — so the
                // anisotropy depolarises with the fluorescence lifetime (Perrin). The l1/l2
                // factors give the parallel(ch0)/perp(ch1) split. D_rot is in rad²/ns.
                const double pf = 3.0 * m.ox * m.ox;
                const double lambda = Iex * qtot_by_laser_[laser][i] * pf;
                if (lambda > kEps) {
                    const double l1 = sample_.species()[i].l1, l2 = sample_.species()[i].l2;
                    const double f = l1l2f_[i], tg = tg_th0_[i];
                    const double Drot = sample_.species()[i].D_rot;
                    const SimDecay& dec = sample_.species()[i].decay;
                    const double ox = m.ox, oy = m.oy, oz = m.oz;
                    double xn1, yn1, zn1;
                    if (std::fabs(oz) < 0.9) { xn1 = oy; yn1 = -ox; zn1 = 0.0; }
                    else { xn1 = 0.0; yn1 = oz; zn1 = -oy; }
                    double inv = 1.0 / std::sqrt(xn1*xn1 + yn1*yn1 + zn1*zn1);
                    xn1 *= inv; yn1 *= inv; zn1 *= inv;
                    const double xn2 = oy*zn1 - oz*yn1, yn2 = oz*xn1 - ox*zn1, zn2 = ox*yn1 - oy*xn1;
                    const double two_pi = 6.28318530717959;
                    double t = t_shift - std::log(rng.random0e1e()) / lambda;
                    while (t < t_tr && t < dt) {
                        double delay = dec.empty() ? 0.0 : dec.sample_ns(rng);  // excited-state lifetime
                        double phi = rng.random0i1e() * two_pi, cp = std::cos(phi), sp2 = std::sin(phi);
                        double xe = ox + tg*(cp*xn1 + sp2*xn2);
                        double ye = oy + tg*(cp*yn1 + sp2*yn2);
                        double ze = oz + tg*(cp*zn1 + sp2*zn2);
                        double ne = 1.0 / std::sqrt(xe*xe + ye*ye + ze*ze);
                        xe *= ne; ye *= ne; ze *= ne;
                        if (Drot > 0.0 && delay > 0.0) {   // rotate during the excited state
                            double sr = std::sqrt(2.0 * Drot * delay);
                            xe += sr * rng.randomNorm(); ye += sr * rng.randomNorm(); ze += sr * rng.randomNorm();
                            double nr = 1.0 / std::sqrt(xe*xe + ye*ye + ze*ze);
                            xe *= nr; ye *= nr; ze *= nr;
                        }
                        double pp = f * ((1.0 - l1) * xe*xe + l1 * ye*ye);   // parallel (ch 0)
                        double ps = f * (l2 * xe*xe + (1.0 - l2) * ye*ye);   // perpendicular (ch 1)
                        double r = rng.random0e1e();
                        int j = (r < pp) ? 0 : (r < pp + ps ? 1 : -1);
                        if (j >= 0) {
                            uint16_t micro = 0;
                            if (delay > 0.0) {
                                double mns = std::fmod(delay, set_.laser_period);
                                if (mns < 0.0) mns += set_.laser_period;
                                int ch = int(mns / set_.microtime_resolution);
                                if (ch < 0) ch = 0;
                                else if (ch >= set_.n_microtime_channels) ch = set_.n_microtime_channels - 1;
                                micro = uint16_t(ch);
                            }
                            buf.push(t, j, i, m.id, micro);
                        }
                        t -= std::log(rng.random0e1e()) / lambda;
                    }
                }
            } else {
                // Detection weights depend only on position (fixed within the window) and
                // are used only for emission; when the excitation field is zero (molecule
                // outside the focus/grid) no photon can be produced, so skip the lookups.
                double sumw = 0.0;
                if (Iex > 0.0) {
                    const auto& qi = q_by_laser_[laser][i];
                    for (int j = 0; j < nchan; ++j) {
                        double qij = (j < int(qi.size())) ? qi[j] : 0.0;
                        w[j] = qij * detection_eff(j, m.x, m.y, m.z);
                        sumw += w[j];
                    }
                }
                double lambda = Iex * sumw;
                if (lambda > kEps) {
                    double t = t_shift - std::log(rng.random0e1e()) / lambda;
                    while (t < t_tr && t < dt) {
                        double r = (1.0 - rng.random0i1e()) * sumw;
                        int j = -1;
                        while (r > 0.0 && j < nchan - 1) { ++j; r -= w[j]; }
                        buf.push(t, j, i, m.id, sample_micro(i));
                        t -= std::log(rng.random0e1e()) / lambda;
                    }
                }
            }
            if (t_tr < dt && koff > kEps) {   // photophysical transition
                double r = 1.0 - rng.random0i1e();
                int j = -1;
                while (r > 0.0 && j < nsp - 1) {
                    ++j;
                    double krij = (size_t(i * nsp + j) < kr.size()) ? kr[i * nsp + j] : 0.0;
                    double knij = (size_t(i * nsp + j) < kn.size()) ? kn[i * nsp + j] : 0.0;
                    r -= tau_off * (Iex * krij + knij);
                }
                if (state_log_ && j != i) buf.push_transition(window, t_tr, m.id, i, j);
                i = j;
            }
        } while (t_tr < dt);
        m.state = i;

        if (m.mobile) {
            double g0, g1, g2;
            norm3(rng, g0, g1, g2);                 // draw FIRST: keeps the RNG stream
                                                     // aligned with the no-flow build
            double nx = m.x, ny = m.y, nz = m.z;
            if (has_flow_) {
                const double a = flow_dt_[i];       // v_scale[i] * dt
                double vx, vy, vz;
                if (uniform_flow_) {
                    // The overwhelmingly common case, and the one every closed-loop test
                    // uses. Reading the cached constant here keeps the whole drift to
                    // three fused multiply-adds with no field lookup at all.
                    vx = flow_ux_; vy = flow_uy_; vz = flow_uz_;
                } else {
                    sample_.flow_field().at(nx, ny, nz, vx, vy, vz);
                }
                if (drift_midpoint_) {
                    // Explicit midpoint for the drift. Plain Euler is EXACT for a uniform
                    // field but not for one with shear or rotation: the Euler map of a rigid
                    // rotation is I + omega*dt*A, whose determinant is 1 + (omega*dt)^2 > 1,
                    // so it inflates volume on every step and molecules spiral outward until
                    // the absorbing boundary eats them. Measured at omega*dt = 0.002: the
                    // radius grows 1.82x over 3e5 windows (predicted 1.822x), draining an
                    // open volume by a third. Midpoint drops the per-step volume error from
                    // (omega*dt)^2 to (omega*dt)^4/4 -- a factor of 1e6 at that step size --
                    // and costs one extra field lookup, which the uniform path never pays.
                    const double hx = nx + vx * 0.5 * a;
                    const double hy = ny + vy * 0.5 * a;
                    const double hz = nz + vz * 0.5 * a;
                    sample_.flow_field().at(hx, hy, hz, vx, vy, vz);
                }
                nx += vx * a; ny += vy * a; nz += vz * a;
            }
            const double step = diff_step_[i];      // sqrt(2·D·dt)
            nx += step * g0; ny += step * g1; nz += step * g2;
            if (has_occ_) {
                const double occ = sample_.occlusion().at(nx, ny, nz);
                if (occ > 0.0 && (occ >= 1.0 || rng.random0i1e() < occ)) {
                    nx = m.x; ny = m.y; nz = m.z;   // blocked
                }
            }
            m.x = nx; m.y = ny; m.z = nz;
            if (open_volume_ && m.x * m.x + m.y * m.y + box_r_sq * m.z * m.z > box_xy_sq) {
                m.alive = false;
                if (state_log_) buf.push_transition(window, dt, m.id, i, -1);
            }
        }
    }

    /// Draw three independent standard normals (the per-step diffusion displacement)
    /// with the ziggurat sampler — the hottest RNG path, ~2.5x cheaper than randomNorm
    /// and ~2x cheaper than Marsaglia polar (no per-draw log/sqrt on the common path).
    /// Stateless, so a molecule's stream stays a pure function of (id, window) and
    /// results remain thread-count-independent.
    template <class Rng>
    static inline void norm3(Rng& rng, double& a, double& b, double& c) {
        a = sim_randn(rng); b = sim_randn(rng); c = sim_randn(rng);
    }

    /// Run all molecules for the current window into bufs_ (serial or thread-pool),
    /// templated on the RNG backend so the inner loop stays monomorphic.
    template <class Rng>
    void run_molecules(uint64_t counter_start, bool parallel) {
        const size_t nmol = mols_.size();
        const int nchan = set_.n_channels;
        const bool per_thread = (set_.rng_scope == SimRngScope::PerThread);
        const bool coast = set_.per_molecule_skip;
        const uint32_t T0 = uint32_t(counter_start / kWindowStride);
        const int laser = laser_for_window(T0);
        auto handle = [&](Mol& m, Rng& rng, std::vector<double>& w, LocalBuf& buf) {
            if (coast && m.coasting) {
                if (T0 < m.w_wake) return;               // still asleep: skip entirely
                wake_molecule<Rng>(m, T0, state_log_ ? &buf : nullptr);   // exact catch-up
                if (!m.alive) return;                    // left the box on wake
            }
            if (!per_thread) rng.reset(mol_base_seed_, uint32_t(m.id), counter_start);
            process_molecule(m, rng, w, buf, laser, T0);
            if (coast) maybe_sleep<Rng>(m, T0, rng);
        };
        if (parallel) {
            pool_->parallel_for(nmol, [&](size_t b, size_t e, unsigned wi) {
                Rng rng; std::vector<double> w(nchan, 0.0);
                LocalBuf& buf = bufs_[wi];
                // PerThread: one stream per worker, seeded once (worker key = 0x8000_0000|wi).
                if (per_thread) rng.reset(mol_base_seed_, 0x80000000u | wi, counter_start);
                for (size_t k = b; k < e; ++k) {
                    Mol& m = mols_[k];
                    if (!m.alive) continue;
                    handle(m, rng, w, buf);
                }
            });
        } else {
            Rng rng; std::vector<double> w(nchan, 0.0);
            if (per_thread) rng.reset(mol_base_seed_, 0x80000000u, counter_start);
            for (size_t k = 0; k < nmol; ++k) {
                Mol& m = mols_[k];
                if (!m.alive) continue;
                handle(m, rng, w, bufs_[0]);
            }
        }
    }

    SimSystem sample_;
    std::vector<SimGrid> exc_;                         ///< one excitation grid per ALEX laser
    std::vector<SimGrid> det_;
    SimIntegrator set_;
    SimRandom rng_diff_, rng_emit_;

    std::vector<Mol> mols_;
    size_t mol_alive_ = 0;
    int next_id_ = 0;
    uint32_t T0_ = 0;
    uint64_t n_markers_ = 0;   ///< marker events pushed (excluded from the n_ph_max photon budget)

    // precomputed per-species row sums of the rate matrices
    std::vector<double> koff_rad_, koff_nrad_;
    // precomputed per-species anisotropy parameters (empty q-sum for aniso rate)
    std::vector<char> aniso_;                         // 1 if species has anisotropy
    std::vector<double> qtot_, tg_th0_, l1l2f_, rot_step_;
    std::vector<double> diff_step_;                   // precomputed sqrt(2·D·dt) per species
    std::vector<double> flow_dt_;                     // v_scale[i] * dt, precomputed per species
    bool has_flow_ = false;
    bool uniform_flow_ = false;    // constant field: Euler integrates the drift exactly
    bool drift_midpoint_ = false;  // midpoint drift step (non-uniform fields, opt-out)
    double flow_max_speed_ = 0.0;  // cached: SimVectorGrid::max_speed() scans every voxel
    double flow_ux_ = 0.0, flow_uy_ = 0.0, flow_uz_ = 0.0;  // cached uniform velocity
    bool has_occ_  = false;
    // Per-laser emission weights for ALEX: q_by_laser_[laser][species] is the per-channel row
    // used under that laser (species' q_alex row, or the scalar q broadcast). qtot_by_laser_ is
    // its row-sum (used by the anisotropy rate). One laser => identical to qtot_/species q.
    std::vector<std::vector<std::vector<double>>> q_by_laser_;   // [laser][species][channel]
    std::vector<std::vector<double>> qtot_by_laser_;             // [laser][species]
    bool any_aniso_ = false;

    // effective-focus AABB (voxels where Iex > focus_threshold·peak) + coasting precompute
    double fx0_ = 0, fy0_ = 0, fz0_ = 0, fx1_ = 0, fy1_ = 0, fz1_ = 0;
    bool focus_aabb_valid_ = false;
    bool uniform_D_ = false;      ///< all species share D within kEps
    bool any_knrad_ = false;      ///< any spontaneous (k_nrad) transition possible
    static constexpr uint32_t kCoastSalt = 0x00C0A57u;  ///< salt for the per-molecule coast RNG
    static constexpr uint32_t kBirthSalt = 0x0B1A7Du;   ///< salt for the independent-mode birth RNG

    // open-volume injection bookkeeping
    std::vector<double> step_, rate_in_, t_in_;
    std::vector<double> w_in_, w_max_;        // cached per-species influx weight and max
    bool open_volume_ = false;

    // per-channel background arrival times, carried across windows
    std::vector<double> t_bg_;
    bool t_bg_setup_ = false;

    // state trajectory (see set_state_log): one row per birth / transition / death
    bool state_log_ = false;
    std::vector<uint32_t> st_w_;
    std::vector<double> st_t_;
    std::vector<int32_t> st_mol_;
    std::vector<int16_t> st_from_, st_to_;

    /// Mean influx weight over the ellipsoid surface for species sp (flow-aware).
    double mean_influx_weight(int sp) const;
    /// Max influx weight over the ellipsoid surface for species sp (acceptance sampling).
    double max_influx_weight(int sp) const;

    /// Append one recorded state change to the engine-level log (serial contexts only).
    void log_state(uint32_t w, double t, int mol, int from, int to) {
        st_w_.push_back(w); st_t_.push_back(t); st_mol_.push_back(int32_t(mol));
        st_from_.push_back(int16_t(from)); st_to_.push_back(int16_t(to));
    }

    // parallelism
    std::unique_ptr<SimThreadPool> pool_;   // lazily created on first parallel window
    std::vector<LocalBuf> bufs_;            // one per worker, reused
    unsigned num_threads_ = 0;              // 0 => hardware concurrency
    size_t parallel_threshold_ = 2048;      // min alive molecules to go parallel
    uint32_t mol_base_seed_ = 0;            // counter-RNG base key for per-molecule streams
    static constexpr uint64_t kWindowStride = 1ull << 20;  // counter budget per molecule/window

    // molecule trajectory reporter
    uint64_t traj_stride_ = 0;
    std::vector<uint32_t> traj_frame_;
    std::vector<int32_t> traj_id_, traj_species_;
    std::vector<double> traj_x_, traj_y_, traj_z_;

    // beam-scan position (0,0 = stationary focus)
    double beam_x_ = 0.0, beam_y_ = 0.0;
    int marker_event_type_ = 1;   ///< value written to et_ for markers during a scan

    // output records
    std::vector<uint32_t> T_;
    std::vector<double> t_;
    std::vector<int16_t> N_, sp_;
    std::vector<int32_t> mol_;
    std::vector<int8_t> et_;      ///< 0 = photon, 1 = marker
    std::vector<uint16_t> micro_; ///< micro-time channel per event (0 for markers)
};

} // namespace tttrlib

#endif // TTTRLIB_SIMENGINE_H
