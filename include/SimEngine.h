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

#include <cstdint>
#include <memory>
#include <vector>
#include "SimSample.h"
#include "SimGrid.h"
#include "SimScanner.h"
#include "SimSettings.h"
#include "SimRandom.h"
#include "SimCounterRandom.h"
#include "SimXoshiroRandom.h"
#include "SimPcgRandom.h"
#include "SimThreadPool.h"
#include "SimMicrotimeEncoder.h"

namespace tttrlib {

/*!
 * \brief Assembles a sample, an excitation grid and per-channel detection grids,
 *        and runs the simulation, accumulating abstract photon records.
 */
class SimEngine {
public:
    /*!
     * \param sample     species/kinetics/background/box + fluorophore population.
     * \param excitation excitation intensity field (one grid).
     * \param detection  detection efficiency grid per channel (size == n_channels);
     *                   empty ⇒ uniform detection (efficiency 1 everywhere).
     * \param settings   time-step, seeds, stopping condition.
     */
    SimEngine(SimSample sample, SimGrid excitation,
              std::vector<SimGrid> detection, SimSettings settings);

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

    /// Write the recorded trajectory to an HDF5 file (group /trajectory). Requires HDF5.
    void write_trajectory_hdf5(const std::string& path) const;

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

    /*!
     * \brief Encode the accumulated records with `enc` (no array marshalling needed
     *        across the language boundary — convenience for the bindings).
     */
    SimEncodedRecords encode(const SimMicrotimeEncoder& enc, SimRandom& rng,
                             uint64_t mt_overflow_in = 0) const {
        return enc.encode(T_.data(), t_.data(), N_.data(), sp_.data(),
                          T_.size(), rng, mt_overflow_in);
    }

    // --- reproducible continuation ---------------------------------------------
    SimRngState diffusion_state() const { return rng_diff_.getState(); }
    SimRngState emission_state() const { return rng_emit_.getState(); }

private:
    struct Mol {
        double x, y, z; int state; bool mobile; int id; bool alive;
        double ox = 0, oy = 0, oz = 1;   // dipole orientation (unit vector) for anisotropy
    };

    /// Photons produced by one worker over its molecule range (merged after the loop).
    struct LocalBuf {
        std::vector<double> t;
        std::vector<int16_t> N, sp;
        std::vector<int32_t> mol;
        std::vector<uint16_t> micro;
        void clear() { t.clear(); N.clear(); sp.clear(); mol.clear(); micro.clear(); }
        void push(double tt, int ch, int species, int molid, uint16_t mt) {
            t.push_back(tt); N.push_back(int16_t(ch));
            sp.push_back(int16_t(species)); mol.push_back(int32_t(molid)); micro.push_back(mt);
        }
    };

    static constexpr double kEps = 1e-8;

    void seed_population();               ///< initial molecules (discrete + open-volume)
    void inject_open_volume(double windows = 1.0);  ///< surface-flux injection (over N windows)
    void init_orientation(Mol& m);        ///< random dipole orientation (anisotropy)
    bool any_molecule_in_focus() const;   ///< true if a molecule is inside the excitation grid box
    void coarse_skip();                   ///< advance one safe coarse step over empty windows
    double detection_eff(int ch, double x, double y, double z) const;
    void push_marker(int routing_channel);  ///< append a marker event at the current window
    void emit_window();                   ///< one time window: photophysics + emission + diffusion

    /// Process one molecule for the current window into `buf` using RNG backend `Rng`.
    /// The per-molecule stream is (re)positioned from (mol_base_seed_, id, counter_start),
    /// so results are independent of the number of worker threads.
    template <class Rng>
    void process_molecule(Mol& m, Rng& rng, std::vector<double>& w, LocalBuf& buf) const {
        const double dt = set_.dt;
        const int nsp = sample_.n_species();
        const int nchan = set_.n_channels;
        const auto& kr = sample_.k_rad();
        const auto& kn = sample_.k_nrad();
        const double box_xy_sq = sample_.box_xy() * sample_.box_xy();
        const double box_r_sq = box_xy_sq / sample_.box_z() / sample_.box_z();

        int i = m.state;
        const double Iex = exc_.at(m.x - beam_x_, m.y - beam_y_, m.z);  // beam-scan offset

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
                const double lambda = Iex * qtot_[i] * pf;
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
                    const auto& qi = sample_.species()[i].q;
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
                i = j;
            }
        } while (t_tr < dt);
        m.state = i;

        if (m.mobile) {
            const double step = diff_step_[i];   // precomputed sqrt(2·D·dt) per species
            double g0, g1, g2;
            norm3(rng, g0, g1, g2);
            m.x += step * g0;
            m.y += step * g1;
            m.z += step * g2;
            if (open_volume_ && m.x * m.x + m.y * m.y + box_r_sq * m.z * m.z > box_xy_sq)
                m.alive = false;
        }
    }

    /// Draw three independent standard normals (the per-step diffusion displacement).
    /// Marsaglia polar: each accepted (u,v) in the unit disc yields two normals, so two
    /// rejection loops cover three draws. Stateless (no cached spare), so a molecule's
    /// stream stays a pure function of (id, window) and results remain thread-independent.
    template <class Rng>
    static inline void norm3(Rng& rng, double& a, double& b, double& c) {
        double u, v, s;
        do { u = 2.0 * rng.random0i1e() - 1.0; v = 2.0 * rng.random0i1e() - 1.0; s = u*u + v*v; }
        while (s >= 1.0 || s <= 0.0);
        double f = std::sqrt(-2.0 * std::log(s) / s);
        a = u * f; b = v * f;
        do { u = 2.0 * rng.random0i1e() - 1.0; v = 2.0 * rng.random0i1e() - 1.0; s = u*u + v*v; }
        while (s >= 1.0 || s <= 0.0);
        c = u * std::sqrt(-2.0 * std::log(s) / s);
    }

    /// Run all molecules for the current window into bufs_ (serial or thread-pool),
    /// templated on the RNG backend so the inner loop stays monomorphic.
    template <class Rng>
    void run_molecules(uint64_t counter_start, bool parallel) {
        const size_t nmol = mols_.size();
        const int nchan = set_.n_channels;
        const bool per_thread = (set_.rng_scope == SimRngScope::PerThread);
        if (parallel) {
            pool_->parallel_for(nmol, [&](size_t b, size_t e, unsigned wi) {
                Rng rng; std::vector<double> w(nchan, 0.0);
                LocalBuf& buf = bufs_[wi];
                // PerThread: one stream per worker, seeded once (worker key = 0x8000_0000|wi).
                if (per_thread) rng.reset(mol_base_seed_, 0x80000000u | wi, counter_start);
                for (size_t k = b; k < e; ++k) {
                    Mol& m = mols_[k];
                    if (!m.alive) continue;
                    if (!per_thread) rng.reset(mol_base_seed_, uint32_t(m.id), counter_start);
                    process_molecule(m, rng, w, buf);
                }
            });
        } else {
            Rng rng; std::vector<double> w(nchan, 0.0);
            if (per_thread) rng.reset(mol_base_seed_, 0x80000000u, counter_start);
            for (size_t k = 0; k < nmol; ++k) {
                Mol& m = mols_[k];
                if (!m.alive) continue;
                if (!per_thread) rng.reset(mol_base_seed_, uint32_t(m.id), counter_start);
                process_molecule(m, rng, w, bufs_[0]);
            }
        }
    }

    SimSample sample_;
    SimGrid exc_;
    std::vector<SimGrid> det_;
    SimSettings set_;
    SimRandom rng_diff_, rng_emit_;

    std::vector<Mol> mols_;
    size_t mol_alive_ = 0;
    int next_id_ = 0;
    uint32_t T0_ = 0;

    // precomputed per-species row sums of the rate matrices
    std::vector<double> koff_rad_, koff_nrad_;
    // precomputed per-species anisotropy parameters (empty q-sum for aniso rate)
    std::vector<char> aniso_;                         // 1 if species has anisotropy
    std::vector<double> qtot_, tg_th0_, l1l2f_, rot_step_;
    std::vector<double> diff_step_;                   // precomputed sqrt(2·D·dt) per species
    bool any_aniso_ = false;
    // open-volume injection bookkeeping
    std::vector<double> step_, rate_in_, t_in_;
    bool open_volume_ = false;

    // per-channel background arrival times, carried across windows
    std::vector<double> t_bg_;
    bool t_bg_setup_ = false;

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
