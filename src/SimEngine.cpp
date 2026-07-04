/*!
 * \file SimEngine.cpp
 * \brief Diffusion + photophysics + emission engine (see SimEngine.h, PRD-005).
 *
 * Reimplements the legacy `smdif_ov3` scientific core over grid-based fields:
 * excitation `Iex(pos)` drives emission at rate `Iex·Σ_j q_j·det_j(pos)`, with a
 * photon assigned to channel j ∝ `q_j·det_j(pos)` (separate per-channel detection
 * ⇒ ISM). N-state photophysics uses competing exponential clocks (`k_rad` scaled by
 * `Iex`, `k_nrad` spontaneous). Diffusion uses the corrected symmetric Gaussian.
 * Open-volume (FCS) population uses the legacy surface-flux injection (ported here).
 */
#include "SimEngine.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>
#ifdef BUILD_PHOTON_HDF
#include <highfive/H5File.hpp>
#include <highfive/H5Group.hpp>
#include <highfive/H5DataSet.hpp>
#include <highfive/H5Attribute.hpp>
#endif

namespace tttrlib {

namespace {
constexpr double kEps = 1e-8;
constexpr double kPi = 3.14159265358979;

// Integral of the standard normal from x to infinity (ccmath qnorm), for random_erfc.
double qnorm(double x) {
    double y, ro, f, t; int k, nf;
    if (x < 0.) { x = -x; nf = 0; } else nf = 1;
    y = x * x; ro = std::exp(-y / 2.) / 2.506628274631;
    if (x < 3.) { f = t = 1.;
        for (k = 1; t > 1.e-14;) { t *= y / (k += 2); f += t; }
        f = .5 - x * ro * f; }
    else { f = x; k = int(std::ceil(250. / y)); if (k < 3) k = 3;
        for (; k > 0;) f = x + (k--) / f;
        f = ro / f; }
    return nf ? f : 1. - f;
}

// Draw from Poisson(mean): Knuth for small mean, normal approximation for large.
template <class Rng>
long poisson_draw(double mean, Rng& rng) {
    if (mean <= 0.0) return 0;
    if (mean < 30.0) {
        double L = std::exp(-mean), p = 1.0; long k = 0;
        do { ++k; p *= rng.random0i1e(); } while (p > L);
        return k - 1;
    }
    long n = long(std::llround(mean + std::sqrt(mean) * sim_randn(rng)));
    return n < 0 ? 0 : n;
}

// Random number with p(x) ~ erf(x/sqrt(2)); the legacy surface penetration depth.
template <class Rng>
double random_erfc(Rng& rng) {
    const double sqrt_pi_half = 1.2533141373155;
    double v, x, yv;
    do {
        v = rng.random0e1e();
        x = -std::log(v) * sqrt_pi_half;
        yv = v * rng.random0i1e();
    } while (yv > 2. * qnorm(x));
    return x;
}
} // namespace

SimEngine::SimEngine(SimSystem sample, SimGrid excitation,
                     std::vector<SimGrid> detection, SimIntegrator settings)
    : sample_(std::move(sample)), exc_(std::move(excitation)),
      det_(std::move(detection)), set_(std::move(settings)),
      rng_diff_(settings.seed_diffusion), rng_emit_(settings.seed_emission) {

    int nsp = sample_.n_species();
    const auto& kr = sample_.k_rad();
    const auto& kn = sample_.k_nrad();
    koff_rad_.assign(nsp, 0.0);
    koff_nrad_.assign(nsp, 0.0);
    for (int i = 0; i < nsp; ++i)
        for (int j = 0; j < nsp; ++j) {
            if (size_t(i * nsp + j) < kr.size()) koff_rad_[i] += kr[i * nsp + j];
            if (size_t(i * nsp + j) < kn.size()) koff_nrad_[i] += kn[i * nsp + j];
        }

    // Precompute per-species anisotropy parameters (legacy rotdiff model).
    aniso_.assign(nsp, 0);
    qtot_.assign(nsp, 0.0); tg_th0_.assign(nsp, 0.0);
    l1l2f_.assign(nsp, 1.0); rot_step_.assign(nsp, 0.0);
    diff_step_.assign(nsp, 0.0);
    for (int i = 0; i < nsp; ++i) {
        const SimSpecies& s = sample_.species()[i];
        diff_step_[i] = std::sqrt(2.0 * s.D * set_.dt);
        double qsum = 0.0; for (double v : s.q) qsum += v;
        qtot_[i] = qsum;
        bool a = (s.r0 > 0.0 || s.D_rot > 0.0 || s.l1 > 0.0 || s.l2 > 0.0);
        aniso_[i] = a ? 1 : 0;
        if (a) any_aniso_ = true;
        double arg = (5.0 * s.r0 + 1.0) / 3.0;
        if (arg < 0.0) arg = 0.0; else if (arg > 1.0) arg = 1.0;
        tg_th0_[i] = std::tan(std::acos(std::sqrt(arg)));
        l1l2f_[i] = (s.l1 > s.l2) ? 1.0 / (1.0 - s.l2 + s.l1) : 1.0 / (1.0 - s.l1 + s.l2);
        rot_step_[i] = std::sqrt(2.0 * s.D_rot * set_.dt);
    }

    const auto& pop = sample_.population();
    for (double m : pop) if (m > kEps) open_volume_ = true;

    mol_base_seed_ = set_.seed_emission ^ (set_.seed_diffusion * 2654435761u);

    // Coasting precompute: diffusion decouples from state iff all D equal; spontaneous
    // kinetics exist iff any k_nrad row sum > 0.
    uniform_D_ = true;
    for (int i = 1; i < nsp; ++i)
        if (std::fabs(sample_.species()[i].D - sample_.species()[0].D) > kEps) uniform_D_ = false;
    any_knrad_ = false;
    for (int i = 0; i < nsp; ++i) if (koff_nrad_[i] > kEps) { any_knrad_ = true; break; }
    compute_focus_aabb();

    // Two-step field lookup: precompute a cheap reject box per grid (opt-in).
    if (set_.fast_grid_bbox) {
        exc_.build_bbox(set_.focus_threshold);
        for (auto& d : det_) d.build_bbox(set_.focus_threshold);
    }

    // Active-domain clipping: shrink the open-volume box to focus+margin, holding concentration
    // fixed, so far-from-focus molecules (which emit nothing) are never simulated. Exact once
    // the margin exceeds a few diffusion lengths. Applied before seeding so all downstream
    // seeding/injection/killing uses the smaller box.
    if (set_.active_margin > 0.0 && open_volume_ && focus_aabb_valid_) {
        const double obxy = sample_.box_xy(), obz = sample_.box_z();
        double axy = std::max(std::max(std::fabs(fx0_), std::fabs(fx1_)),
                              std::max(std::fabs(fy0_), std::fabs(fy1_))) + set_.active_margin;
        double az  = std::max(std::fabs(fz0_), std::fabs(fz1_)) + set_.active_margin;
        if (axy < obxy && az < obz && axy > 0.0 && az > 0.0) {
            const double vol_ratio = (axy * axy * az) / (obxy * obxy * obz);  // ∝ ellipsoid volume
            sample_.set_box(axy, az);
            const auto pop = sample_.population();                            // copy before rescale
            for (size_t i = 0; i < pop.size(); ++i)
                sample_.set_population(int(i), pop[i] * vol_ratio);           // preserve concentration
        }
    }

    seed_population();
}

void SimEngine::compute_focus_aabb() {
    // Effective focus = tight AABB over excitation voxels exceeding focus_threshold·peak,
    // independent of the (possibly box-spanning) grid extent, so a coasting molecule's
    // distance to the focus is measured against where excitation is actually significant.
    focus_aabb_valid_ = false;
    if (exc_.analytic_) {
        // A·exp(-2(r²/w0² + z²/z0²)) > thr·A  ⇔  r²/w0² + z²/z0² < -ln(thr)/2.
        const double thr = (set_.focus_threshold > 0.0) ? set_.focus_threshold : 1e-3;
        const double L = -0.5 * std::log(thr);                 // >0
        if (L <= 0.0 || exc_.an_cxy_ >= 0.0 || exc_.an_cz_ >= 0.0) return;
        const double w0 = std::sqrt(-2.0 / exc_.an_cxy_), z0 = std::sqrt(-2.0 / exc_.an_cz_);
        const double rxy = w0 * std::sqrt(L), rz = z0 * std::sqrt(L);
        fx0_ = -rxy; fx1_ = rxy; fy0_ = -rxy; fy1_ = rxy; fz0_ = -rz; fz1_ = rz;
        focus_aabb_valid_ = true;
        return;
    }
    if (exc_.nx <= 0 || exc_.data.empty()) return;
    double vmax = 0.0;
    for (double v : exc_.data) if (v > vmax) vmax = v;
    if (vmax <= 0.0) return;
    const double thr = ((set_.focus_threshold > 0.0) ? set_.focus_threshold : 1e-3) * vmax;
    int ix0 = exc_.nx, iy0 = exc_.ny, iz0 = exc_.nz, ix1 = -1, iy1 = -1, iz1 = -1;
    for (int iz = 0; iz < exc_.nz; ++iz)
        for (int iy = 0; iy < exc_.ny; ++iy)
            for (int ix = 0; ix < exc_.nx; ++ix)
                if (exc_.data[exc_.index(ix, iy, iz)] > thr) {
                    if (ix < ix0) ix0 = ix; if (ix > ix1) ix1 = ix;
                    if (iy < iy0) iy0 = iy; if (iy > iy1) iy1 = iy;
                    if (iz < iz0) iz0 = iz; if (iz > iz1) iz1 = iz;
                }
    if (ix1 < 0) return;
    fx0_ = exc_.x0 + (ix0 - 1) * exc_.dx; fx1_ = exc_.x0 + (ix1 + 1) * exc_.dx;
    fy0_ = exc_.y0 + (iy0 - 1) * exc_.dy; fy1_ = exc_.y0 + (iy1 + 1) * exc_.dy;
    fz0_ = exc_.z0 + (iz0 - 1) * exc_.dz; fz1_ = exc_.z0 + (iz1 + 1) * exc_.dz;
    focus_aabb_valid_ = true;
}

void SimEngine::init_orientation(Mol& m) {
    // Uniform random dipole on the unit sphere (only when anisotropy is active).
    if (!any_aniso_) return;
    double z = 2.0 * rng_diff_.random0i1e() - 1.0;
    double phi = 2.0 * kPi * rng_diff_.random0i1e();
    double r = std::sqrt(std::max(0.0, 1.0 - z * z));
    m.ox = std::cos(phi) * r; m.oy = std::sin(phi) * r; m.oz = z;
}

double SimEngine::detection_eff(int ch, double x, double y, double z) const {
    if (det_.empty()) return 1.0;                       // uniform detection
    if (ch < 0 || ch >= int(det_.size())) return 0.0;
    return det_[ch].at(x - beam_x_, y - beam_y_, z);    // beam-scan offset
}

void SimEngine::push_marker(int routing_channel) {
    T_.push_back(T0_); t_.push_back(0.0);
    N_.push_back(int16_t(routing_channel));
    sp_.push_back(int16_t(-1)); mol_.push_back(int32_t(-1));
    et_.push_back(int8_t(marker_event_type_));
    micro_.push_back(0);
}

void SimEngine::seed_population() {
    // Discrete / grid emitters: fixed instances from the sample.
    for (const auto& e : sample_.emitters()) {
        Mol m{e.x, e.y, e.z, e.species, e.mobile, next_id_++, true};
        init_orientation(m);
        mols_.push_back(m);
    }

    if (open_volume_) {
        const double box_xy = sample_.box_xy(), box_z = sample_.box_z();
        int nsp = sample_.n_species();
        const auto& pop = sample_.population();

        // Ellipsoid surface/volume factors and per-species injection setup.
        double ell_f = box_z / box_xy;
        double ell_e = std::sqrt(std::fabs(ell_f * ell_f - 1.)) / ell_f;
        double ell_S_V;
        if (ell_f <= 0.999999)      ell_S_V = 3. / 2. / box_z * (1. + ell_f / ell_e * std::log(ell_e + 1. / ell_f));
        else if (ell_f >= 1.000001) ell_S_V = 3. / 2. / box_z * (1. + ell_f / ell_e * std::asin(ell_e));
        else                        ell_S_V = 3. / box_z;
        const double sqrt_2pi = std::sqrt(2. * kPi);

        step_.assign(nsp, 0.0); rate_in_.assign(nsp, 0.0); t_in_.assign(nsp, 1e60);
        for (int i = 0; i < nsp; ++i) {
            double D = sample_.species()[i].D;
            step_[i] = std::sqrt(2. * D * set_.dt);
            double M = (i < int(pop.size())) ? pop[i] : 0.0;
            rate_in_[i] = M * step_[i] * ell_S_V / sqrt_2pi;
            if (rate_in_[i] > kEps) t_in_[i] = -std::log(rng_diff_.random0e1e()) / rate_in_[i];
        }

        // Initial Poisson placement uniformly in the ellipsoid.
        for (int i = 0; i < nsp; ++i) {
            double M = (i < int(pop.size())) ? pop[i] : 0.0;
            if (M < kEps) continue;
            double t = 0.;
            while ((t -= std::log(rng_diff_.random0e1e()) / M) < 1.) {
                double x, y, z;
                do {
                    x = 2. * rng_diff_.random0i1e() - 1.;
                    y = 2. * rng_diff_.random0i1e() - 1.;
                    z = 2. * rng_diff_.random0i1e() - 1.;
                } while (x * x + y * y + z * z > 1.);
                Mol m{x * box_xy, y * box_xy, z * box_z, i, true, next_id_++, true};
                init_orientation(m);
                mols_.push_back(m);
            }
        }
    }

    mol_alive_ = mols_.size();
    t_bg_setup_ = false;
}

void SimEngine::inject_open_volume(double windows) {
    const double box_xy = sample_.box_xy(), box_z = sample_.box_z();
    const double box_xy_sq = box_xy * box_xy;
    const double box_r_sq = box_xy_sq / box_z / box_z;
    double ell_f = box_z / box_xy;
    double ell_Pzmax = (ell_f <= 0.999999) ? 1. / ell_f : 1.;
    int nsp = sample_.n_species();

    for (int i = 0; i < nsp; ++i) {
        if (rate_in_[i] < kEps) continue;
        while (t_in_[i] <= windows) {
            double ze, r;
            do {
                ze = 2. * rng_diff_.random0i1e() - 1.;
                r = rng_diff_.random0i1e() * ell_Pzmax;
            } while (r * r > 1. - ze * ze * (1. - box_r_sq));
            double r_xy = std::sqrt(1. - ze * ze) * box_xy;
            ze *= box_z;
            double phi = rng_diff_.random0i1e() * 2. * kPi;
            double xe = std::cos(phi) * r_xy, ye = std::sin(phi) * r_xy;
            double step_in = step_[i] * random_erfc(rng_diff_);
            double rnnorm = 1. / std::sqrt(r_xy * r_xy + ze * ze * box_r_sq * box_r_sq);
            Mol m{xe * (1. - step_in * rnnorm),
                  ye * (1. - step_in * rnnorm),
                  ze * (1. - step_in * rnnorm * box_r_sq),
                  i, true, next_id_++, true};
            init_orientation(m);
            mols_.push_back(m);
            mol_alive_++;
            t_in_[i] -= std::log(rng_diff_.random0e1e()) / rate_in_[i];
        }
        t_in_[i] -= windows;
    }
}

void SimEngine::emit_window() {
    const int nchan = set_.n_channels;
    const int nsp = sample_.n_species();
    const auto& qbg = sample_.background();
    if (!t_bg_setup_) { t_bg_.assign(nchan, 0.0); t_bg_setup_ = true; }
    const uint64_t counter_start = uint64_t(T0_) * kWindowStride;

    // Trajectory snapshot at this window's start positions.
    if (traj_stride_ && (T0_ % traj_stride_) == 0) {
        for (const auto& m : mols_) if (m.alive) {
            traj_frame_.push_back(T0_); traj_id_.push_back(m.id);
            traj_species_.push_back(m.state);
            traj_x_.push_back(m.x); traj_y_.push_back(m.y); traj_z_.push_back(m.z);
        }
    }

    // Decide serial vs parallel (parallelise only with many live molecules).
    bool parallel = mol_alive_ > parallel_threshold_;
    if (parallel) {
        if (!pool_) pool_.reset(new SimThreadPool(num_threads_));
        if (pool_->size() < 2) parallel = false;
    }

    unsigned nbuf = (parallel && pool_) ? pool_->size() : 1u;
    if (bufs_.size() < nbuf) bufs_.resize(nbuf);
    for (auto& b : bufs_) b.clear();

    switch (set_.rng_kind) {
        case SimRngKind::Pcg:
            run_molecules<SimPcgRandom>(counter_start, parallel); break;
        case SimRngKind::Philox:
            run_molecules<SimCounterRandom>(counter_start, parallel); break;
        case SimRngKind::Mt19937:
            run_molecules<SimRandom>(counter_start, parallel); break;
        case SimRngKind::Xoshiro:
        default:
            run_molecules<SimXoshiroRandom>(counter_start, parallel); break;
    }

    if (open_volume_) {   // deletions happened during the (possibly parallel) loop
        size_t a = 0; for (auto& m : mols_) if (m.alive) ++a; mol_alive_ = a;
    }

    // Background: independent Poisson per channel (serial, carried across windows).
    // Micro-time drawn from the per-channel background decay pattern if configured.
    LocalBuf bg;
    const auto& bgd = sample_.background_decays();
    for (int j = 0; j < nchan; ++j) {
        double rate = (j < int(qbg.size())) ? qbg[j] : 0.0;
        if (rate < kEps) continue;
        const SimDecay* dec = nullptr;
        if (bgd.size() == 1) dec = &bgd[0];
        else if (j < int(bgd.size())) dec = &bgd[j];
        while (t_bg_[j] < set_.dt) {
            uint16_t micro = 0;
            if (dec && !dec->empty()) {
                double mns = std::fmod(dec->sample_ns(rng_emit_), set_.laser_period);
                if (mns < 0.0) mns += set_.laser_period;
                int ch = int(mns / set_.microtime_resolution);
                if (ch < 0) ch = 0;
                else if (ch >= set_.n_microtime_channels) ch = set_.n_microtime_channels - 1;
                micro = uint16_t(ch);
            }
            bg.push(t_bg_[j], j, nsp, 0, micro);   // emitting_species = n_species
            t_bg_[j] -= std::log(rng_emit_.random0e1e()) / rate;
        }
        t_bg_[j] -= set_.dt;
    }

    // Merge all buffers, sort by (t, molecule, channel) for thread-independent order.
    struct Ph { double t; int16_t N; int16_t sp; int32_t mol; uint16_t micro; };
    size_t total = bg.t.size(); for (auto& b : bufs_) total += b.t.size();
    std::vector<Ph> all; all.reserve(total);
    auto add = [&](LocalBuf& b) {
        for (size_t k = 0; k < b.t.size(); ++k)
            all.push_back(Ph{b.t[k], b.N[k], b.sp[k], b.mol[k], b.micro[k]});
    };
    for (auto& b : bufs_) add(b); add(bg);
    std::sort(all.begin(), all.end(), [](const Ph& a, const Ph& b) {
        if (a.t != b.t) return a.t < b.t;
        if (a.mol != b.mol) return a.mol < b.mol;
        return a.N < b.N;
    });
    for (const auto& p : all) {
        T_.push_back(T0_); t_.push_back(p.t);
        N_.push_back(p.N); sp_.push_back(p.sp); mol_.push_back(p.mol);
        et_.push_back(0);          // photon
        micro_.push_back(p.micro);
    }

    if (open_volume_) {
        inject_open_volume();
        if (mols_.size() > 4 * (mol_alive_ + 16)) {
            mols_.erase(std::remove_if(mols_.begin(), mols_.end(),
                        [](const Mol& m) { return !m.alive; }), mols_.end());
        }
    }
    ++T0_;
}

void SimEngine::step(uint64_t n_windows) {
    for (uint64_t k = 0; k < n_windows; ++k) emit_window();
}

SimState SimEngine::get_state() const {
    SimState s;
    s.window = T0_;
    s.n_photons = T_.size();
    s.id.reserve(mol_alive_); s.species.reserve(mol_alive_);
    s.x.reserve(mol_alive_); s.y.reserve(mol_alive_); s.z.reserve(mol_alive_);
    for (const auto& m : mols_) {
        if (!m.alive) continue;
        s.id.push_back(int32_t(m.id)); s.species.push_back(int16_t(m.state));
        s.x.push_back(m.x); s.y.push_back(m.y); s.z.push_back(m.z);
    }
    s.n_molecules = int(s.id.size());
    return s;
}

void SimEngine::batch_background(uint64_t n_windows) {
    // Emit background over a fast-forwarded gap of n_windows, placing each photon at its
    // correct macro-window (identical statistics to per-window emission, just batched).
    if (n_windows == 0) return;
    const int nchan = set_.n_channels, nsp = sample_.n_species();
    const auto& qbg = sample_.background();
    const auto& bgd = sample_.background_decays();
    if (!t_bg_setup_) { t_bg_.assign(nchan, 0.0); t_bg_setup_ = true; }
    const double gap = double(n_windows) * set_.dt;
    struct BgPh { uint32_t win; double t; int16_t ch; uint16_t micro; };
    std::vector<BgPh> bgph;
    for (int j = 0; j < nchan; ++j) {
        double rate = (j < int(qbg.size())) ? qbg[j] : 0.0;
        if (rate < kEps) continue;
        const SimDecay* dec = (bgd.size() == 1) ? &bgd[0]
                            : (j < int(bgd.size()) ? &bgd[j] : nullptr);
        while (t_bg_[j] < gap) {
            uint64_t w = uint64_t(t_bg_[j] / set_.dt);
            double arr = t_bg_[j] - double(w) * set_.dt;
            uint16_t micro = 0;
            if (dec && !dec->empty()) {
                double mns = std::fmod(dec->sample_ns(rng_emit_), set_.laser_period);
                if (mns < 0.0) mns += set_.laser_period;
                int ch = int(mns / set_.microtime_resolution);
                if (ch < 0) ch = 0; else if (ch >= set_.n_microtime_channels) ch = set_.n_microtime_channels - 1;
                micro = uint16_t(ch);
            }
            bgph.push_back(BgPh{uint32_t(T0_ + w), arr, int16_t(j), micro});
            t_bg_[j] -= std::log(rng_emit_.random0e1e()) / rate;
        }
        t_bg_[j] -= gap;
    }
    std::sort(bgph.begin(), bgph.end(), [](const BgPh& a, const BgPh& b) {
        return a.win != b.win ? a.win < b.win : a.t < b.t;
    });
    for (const auto& p : bgph) {
        T_.push_back(p.win); t_.push_back(p.t); N_.push_back(p.ch);
        sp_.push_back(int16_t(nsp)); mol_.push_back(0); et_.push_back(0); micro_.push_back(p.micro);
    }
}

void SimEngine::run() {
    if (set_.independent_molecules && set_.max_windows > 0) {
        run_independent(set_.max_windows);
        return;
    }
    const bool coast = set_.per_molecule_skip;
    while (n_photons() < set_.n_ph_max) {
        // Fast-forward when every alive molecule is asleep and not yet due to wake: nothing
        // can enter the focus during the gap, so only background is emitted. Surface flux
        // injection still runs over the whole gap, so the open-volume population stays balanced.
        if (coast && mol_alive_ > 0) {
            uint32_t earliest = UINT32_MAX;
            bool all_asleep = true;
            for (const auto& m : mols_) {
                if (!m.alive) continue;
                if (!m.coasting || m.w_wake <= T0_) { all_asleep = false; break; }
                if (m.w_wake < earliest) earliest = m.w_wake;
            }
            if (all_asleep && earliest > T0_ && earliest != UINT32_MAX) {
                uint64_t G = uint64_t(earliest - T0_);
                if (set_.max_windows && T0_ + G > set_.max_windows)
                    G = set_.max_windows - T0_;
                if (G >= 1) {
                    batch_background(G);
                    if (open_volume_) inject_open_volume(double(G));
                    T0_ += uint32_t(G);
                    if (set_.max_windows && T0_ >= set_.max_windows) break;
                    continue;
                }
            }
        }
        emit_window();
        if (set_.max_windows && T0_ >= set_.max_windows) break;
    }
}

// One photon record in independent mode (carries its absolute macro-window).
namespace { struct IndPh { uint32_t w; double t; int16_t N, sp; int32_t mol; uint16_t micro;
    typedef IndPh value_type; }; }

template <class Rng>
void SimEngine::run_independent_impl(uint64_t W) {
    const bool coast = set_.per_molecule_skip;
    const int nchan = set_.n_channels, nsp = sample_.n_species();

    // --- 1) Molecule count. Initial population (present at t=0) + open-volume injections. ----
    // Over [0,W) the number of injections of species i is Poisson(rate_in·W), and — by the
    // Poisson conditional-uniformity property — each injection's start time is i.i.d. uniform
    // on [0,W). So every molecule (its random start time, entry point, orientation and whole
    // trajectory) is a fully independent, id-keyed work unit: no coordinated birth phase, no
    // shared clock. Photon streams are merged afterwards, each shifted by its start window.
    const double box_xy = sample_.box_xy(), box_z = sample_.box_z();
    const double box_r_sq = (box_xy * box_xy) / (box_z * box_z);
    const double ell_f = box_z / box_xy;
    const double ell_Pzmax = (ell_f <= 0.999999) ? 1. / ell_f : 1.;
    const bool aniso = any_aniso_;

    const size_t init = mols_.size();
    std::vector<uint32_t> sp_off(nsp + 1, 0);     // injected-molecule index ranges per species
    if (open_volume_)
        for (int i = 0; i < nsp; ++i)
            sp_off[i + 1] = sp_off[i] +
                uint32_t((rate_in_[i] > kEps) ? poisson_draw(rate_in_[i] * double(W), rng_diff_) : 0);
    const size_t M = sp_off[nsp];
    const size_t nmol = init + M;

    bool parallel = nmol > parallel_threshold_;
    if (parallel) { if (!pool_) pool_.reset(new SimThreadPool(num_threads_)); if (pool_->size() < 2) parallel = false; }
    const unsigned nbuf = (parallel && pool_) ? pool_->size() : 1u;
    std::vector<std::vector<IndPh>> perbuf(nbuf);

    auto cmp = [](const IndPh& a, const IndPh& b) {
        if (a.w != b.w) return a.w < b.w;
        if (a.t != b.t) return a.t < b.t;
        if (a.mol != b.mol) return a.mol < b.mol;
        return a.N < b.N;
    };
    auto species_of = [&](size_t j) { int i = 0; while (i + 1 < nsp && sp_off[i + 1] <= j) ++i; return i; };

    // --- 2) Simulate every molecule's whole timeline; each worker sorts its own buffer. ------
    auto do_range = [&](size_t b, size_t e, unsigned wi) {
        std::vector<double> wv(nchan, 0.0); LocalBuf lb;
        std::vector<IndPh>& out = perbuf[wi];
        for (size_t k = b; k < e; ++k) {
            Mol m; uint32_t w_birth;
            if (k < init) { m = mols_[k]; w_birth = 0; }                 // present at t=0
            else {                                                        // injected: self-derived
                Rng br; br.reset(mol_base_seed_ ^ kBirthSalt, uint32_t(k), 0);
                int i = species_of(k - init);
                w_birth = uint32_t(br.random0i1e() * double(W));          // uniform start time
                double ze, r;
                do { ze = 2. * br.random0i1e() - 1.; r = br.random0i1e() * ell_Pzmax; }
                while (r * r > 1. - ze * ze * (1. - box_r_sq));
                double r_xy = std::sqrt(1. - ze * ze) * box_xy; ze *= box_z;
                double phi = br.random0i1e() * 2. * kPi;
                double xe = std::cos(phi) * r_xy, ye = std::sin(phi) * r_xy;
                double step_in = step_[i] * random_erfc(br);
                double rnnorm = 1. / std::sqrt(r_xy * r_xy + ze * ze * box_r_sq * box_r_sq);
                m = Mol{xe * (1. - step_in * rnnorm), ye * (1. - step_in * rnnorm),
                        ze * (1. - step_in * rnnorm * box_r_sq), i, true, int(k), true};
                if (aniso) {
                    double z = 2. * br.random0i1e() - 1., ph = 2. * kPi * br.random0i1e();
                    double rr = std::sqrt(std::max(0., 1. - z * z));
                    m.ox = std::cos(ph) * rr; m.oy = std::sin(ph) * rr; m.oz = z;
                }
            }
            simulate_timeline<Rng>(m, w_birth, uint32_t(W), coast, wv, lb, out);
        }
        std::sort(out.begin(), out.end(), cmp);   // per-worker sort — parallel across workers
    };
    if (parallel) pool_->parallel_for(nmol, do_range);
    else do_range(0, nmol, 0);

    // --- 3) Background (serial) into its own sorted run. ------------------------------------
    std::vector<IndPh> bg;
    const auto& qbg = sample_.background();
    const auto& bgd = sample_.background_decays();
    if (!t_bg_setup_) { t_bg_.assign(nchan, 0.0); t_bg_setup_ = true; }
    const double gap = double(W) * set_.dt;
    for (int j = 0; j < nchan; ++j) {
        double rate = (j < int(qbg.size())) ? qbg[j] : 0.0;
        if (rate < kEps) continue;
        const SimDecay* dec = (bgd.size() == 1) ? &bgd[0] : (j < int(bgd.size()) ? &bgd[j] : nullptr);
        while (t_bg_[j] < gap) {
            uint64_t w = uint64_t(t_bg_[j] / set_.dt);
            double arr = t_bg_[j] - double(w) * set_.dt;
            uint16_t micro = 0;
            if (dec && !dec->empty()) {
                double mns = std::fmod(dec->sample_ns(rng_emit_), set_.laser_period);
                if (mns < 0.0) mns += set_.laser_period;
                int ch = int(mns / set_.microtime_resolution);
                if (ch < 0) ch = 0; else if (ch >= set_.n_microtime_channels) ch = set_.n_microtime_channels - 1;
                micro = uint16_t(ch);
            }
            bg.push_back(IndPh{uint32_t(w), arr, int16_t(j), int16_t(nsp), 0, micro});
            t_bg_[j] -= std::log(rng_emit_.random0e1e()) / rate;
        }
    }
    if (!bg.empty()) std::sort(bg.begin(), bg.end(), cmp);

    // --- 4) k-way merge of the pre-sorted per-worker runs + background (O(P log R), R small). -
    struct Cur { const std::vector<IndPh>* run; size_t pos; };
    std::vector<Cur> runs;
    for (auto& b : perbuf) if (!b.empty()) runs.push_back({&b, 0});
    if (!bg.empty()) runs.push_back({&bg, 0});
    size_t total = 0; for (auto& c : runs) total += c.run->size();
    T_.reserve(T_.size() + total); t_.reserve(t_.size() + total); N_.reserve(N_.size() + total);
    sp_.reserve(sp_.size() + total); mol_.reserve(mol_.size() + total);
    et_.reserve(et_.size() + total); micro_.reserve(micro_.size() + total);

    auto hgt = [&](int a, int b) {   // min-heap on the runs' current fronts
        return cmp((*runs[b].run)[runs[b].pos], (*runs[a].run)[runs[a].pos]);
    };
    std::vector<int> heap;
    for (int i = 0; i < int(runs.size()); ++i) heap.push_back(i);
    std::make_heap(heap.begin(), heap.end(), hgt);
    while (!heap.empty()) {
        std::pop_heap(heap.begin(), heap.end(), hgt);
        int r = heap.back(); heap.pop_back();
        const IndPh& p = (*runs[r].run)[runs[r].pos++];
        T_.push_back(p.w); t_.push_back(p.t); N_.push_back(p.N);
        sp_.push_back(p.sp); mol_.push_back(p.mol); et_.push_back(0); micro_.push_back(p.micro);
        if (runs[r].pos < runs[r].run->size()) { heap.push_back(r); std::push_heap(heap.begin(), heap.end(), hgt); }
    }
    T0_ = uint32_t(W);
    mol_alive_ = 0;   // independent mode does not maintain a live-molecule pool
}

void SimEngine::run_independent(uint64_t n_windows) {
    switch (set_.rng_kind) {
        case SimRngKind::Pcg:     run_independent_impl<SimPcgRandom>(n_windows); break;
        case SimRngKind::Philox:  run_independent_impl<SimCounterRandom>(n_windows); break;
        case SimRngKind::Mt19937: run_independent_impl<SimRandom>(n_windows); break;
        case SimRngKind::Xoshiro:
        default:                  run_independent_impl<SimXoshiroRandom>(n_windows); break;
    }
}

void SimEngine::write_trajectory_hdf5(const std::string& path) const {
#ifdef BUILD_PHOTON_HDF
    HighFive::File f(path, HighFive::File::Overwrite);
    HighFive::Group g = f.createGroup("trajectory");
    g.createDataSet("frame", traj_frame_);
    g.createDataSet("id", traj_id_);
    g.createDataSet("species", traj_species_);
    g.createDataSet("x", traj_x_);
    g.createDataSet("y", traj_y_);
    g.createDataSet("z", traj_z_);
    g.createAttribute("stride", traj_stride_);
    g.createAttribute("dt", set_.dt);
#else
    (void)path;
    throw std::runtime_error(
        "tttrlib built without HDF5 (BUILD_PHOTON_HDF); cannot write trajectory.");
#endif
}

void SimEngine::run_scan(const SimScanner& sc) {
    if (!sc.enabled) { run(); return; }
    marker_event_type_ = sc.markers.marker_event_type;

    for (int fm : sc.markers.marker_frame) push_marker(fm);   // frame marker(s) at scan start
    for (int iy = 0; iy < sc.ny; ++iy) {
        const bool rev = sc.bidirectional && (iy & 1);
        push_marker(sc.markers.marker_line_start);
        for (int p = 0; p < sc.nx; ++p) {
            const int ix = rev ? (sc.nx - 1 - p) : p;
            beam_x_ = sc.origin_x + ix * sc.pixel_dx;
            beam_y_ = sc.origin_y + iy * sc.pixel_dy;
            if (sc.markers.emit_pixel_markers) push_marker(sc.markers.marker_pixel);
            double dwell = sc.dwell[size_t(iy) * sc.nx + ix];
            uint64_t nwin = uint64_t(std::llround(dwell / set_.dt));
            if (nwin < 1) nwin = 1;
            for (uint64_t w = 0; w < nwin; ++w) emit_window();
        }
        if (sc.markers.emit_line_stop) push_marker(sc.markers.marker_line_stop);
    }
    beam_x_ = beam_y_ = 0.0;
}

// --- JSON configuration -----------------------------------------------------------
namespace {
using nlohmann::json;

SimRngKind rng_kind_from(const std::string& s) {
    if (s == "pcg") return SimRngKind::Pcg;
    if (s == "philox") return SimRngKind::Philox;
    if (s == "mt19937" || s == "mt") return SimRngKind::Mt19937;
    return SimRngKind::Xoshiro;
}
SimRngScope rng_scope_from(const std::string& s) {
    return (s == "per_thread") ? SimRngScope::PerThread : SimRngScope::PerMolecule;
}

// Build a SimGrid excitation/detection field from a JSON spec. Supported "type"s:
//   "gaussian3d"           — separable 3D Gaussian (w0, z0); "analytic":true ⇒ grid-free eval.
//   "analytic_gaussian3d"  — same, always grid-free.
//   "gaussian_lorentzian"  — confocal MDF with z-expanding waist (w0, zR).
//   "radial"               — numeric/measured radially-symmetric PSF: inline "rz" array
//                            (row-major [iz*nr+ir]) with nr, nz, r_step, z_step.
//   "uniform"              — constant "value" (default; e.g. a flat detection/CEF grid).
SimGrid grid_from(const json& g) {
    double ext_xy = g.value("extent_xy", 2.0);
    double ext_z  = g.value("extent_z", 4.0);
    double sp     = g.value("spacing", 0.1);
    std::string t = g.value("type", std::string("uniform"));
    if (t == "analytic_gaussian3d" || (t == "gaussian3d" && g.value("analytic", false)))
        return SimGrid::analytic_gaussian3d(g.value("w0", 0.3), g.value("z0", 2.0),
                                            g.value("amplitude", 1.0));
    if (t == "gaussian3d")
        return SimGrid::gaussian3d(g.value("w0", 0.3), g.value("z0", 2.0),
                                   ext_xy, ext_z, sp, g.value("amplitude", 1.0));
    if (t == "gaussian_lorentzian")
        return SimGrid::gaussian_lorentzian(g.value("w0", 0.3), g.value("zR", 1.0),
                                            ext_xy, ext_z, sp, g.value("amplitude", 1.0));
    if (t == "radial" && g.contains("rz"))
        return SimGrid::from_radial(g["rz"].get<std::vector<double>>(),
                                    g.value("nr", 0), g.value("nz", 0),
                                    g.value("r_step", 0.05), g.value("z_step", 0.05),
                                    ext_xy, ext_z, sp, g.value("amplitude", 1.0));
    return SimGrid::uniform(g.value("value", 1.0), ext_xy, ext_z, sp);
}
} // namespace

SimEngine* SimEngine::from_json(const std::string& json_config) {
    json cfg = json::parse(json_config);

    SimIntegrator st;
    if (cfg.contains("settings")) {
        const json& s = cfg["settings"];
        st.dt = s.value("dt", st.dt);
        st.n_ph_max = s.value("n_ph_max", st.n_ph_max);
        st.max_windows = s.value("max_windows", st.max_windows);
        st.seed_diffusion = s.value("seed_diffusion", st.seed_diffusion);
        st.seed_emission = s.value("seed_emission", st.seed_emission);
        st.n_channels = s.value("n_channels", st.n_channels);
        st.n_microtime_channels = s.value("n_microtime_channels", st.n_microtime_channels);
        st.microtime_resolution = s.value("microtime_resolution", st.microtime_resolution);
        st.laser_period = s.value("laser_period", st.laser_period);
        st.rng_kind = rng_kind_from(s.value("rng_kind", std::string("xoshiro")));
        st.rng_scope = rng_scope_from(s.value("rng_scope", std::string("per_molecule")));
        st.fast_grid_bbox = s.value("fast_grid_bbox", st.fast_grid_bbox);
        st.focus_threshold = s.value("focus_threshold", st.focus_threshold);
        st.per_molecule_skip = s.value("per_molecule_skip", st.per_molecule_skip);
        st.coast_safety = s.value("coast_safety", st.coast_safety);
        st.min_coast_windows = s.value("min_coast_windows", st.min_coast_windows);
        st.independent_molecules = s.value("independent_molecules", st.independent_molecules);
        st.active_margin = s.value("active_margin", st.active_margin);
    }

    SimSystem sample;
    if (cfg.contains("species")) {
        for (const json& sp : cfg["species"]) {
            SimSpecies s;
            s.D = sp.value("D", 0.0);
            if (sp.contains("q")) s.q = sp["q"].get<std::vector<double>>();
            s.r0 = sp.value("r0", 0.0); s.l1 = sp.value("l1", 0.0);
            s.l2 = sp.value("l2", 0.0); s.D_rot = sp.value("D_rot", 0.0);
            if (sp.contains("decay")) {   // micro-time decay: arbitrary pattern (primary) or a model helper
                const json& d = sp["decay"];
                double ddt = d.value("dt", 0.008), dt0 = d.value("t0", 0.0);
                if (d.contains("pattern")) {
                    s.decay = SimDecay::from_pattern(d["pattern"].get<std::vector<double>>(), ddt, dt0);
                } else if (d.contains("lifetimes")) {
                    auto amps = d.value("amplitudes", std::vector<double>{1.0});
                    auto taus = d["lifetimes"].get<std::vector<double>>();
                    int nb = d.value("n_bins", 4096);
                    if (d.contains("irf"))
                        s.decay = SimDecay::multi_exponential_with_irf(
                            amps, taus, nb, ddt, d["irf"].get<std::vector<double>>(), dt0);
                    else
                        s.decay = SimDecay::multi_exponential(amps, taus, nb, ddt, dt0);
                }
            }
            sample.add_species(s);
        }
    }
    if (cfg.contains("k_rad") && cfg.contains("k_nrad"))
        sample.set_rate_matrices(cfg["k_rad"].get<std::vector<double>>(),
                                 cfg["k_nrad"].get<std::vector<double>>());
    if (cfg.contains("background"))
        sample.set_background(cfg["background"].get<std::vector<double>>());
    if (cfg.contains("background_decay")) {   // micro-time pattern for background (G1)
        const json& d = cfg["background_decay"];
        double ddt = d.value("dt", 0.008), dt0 = d.value("t0", 0.0);
        if (d.contains("pattern"))
            sample.set_background_decay(
                SimDecay::from_pattern(d["pattern"].get<std::vector<double>>(), ddt, dt0));
    }
    if (cfg.contains("box"))
        sample.set_box(cfg["box"].value("xy", 2.0), cfg["box"].value("z", 4.0));
    if (cfg.contains("population")) {
        const json& pop = cfg["population"];
        for (size_t i = 0; i < pop.size(); ++i) sample.set_population(int(i), pop[i].get<double>());
    }
    if (cfg.contains("emitters")) {
        for (const json& e : cfg["emitters"])
            sample.add_fluorophore(e.value("x", 0.0), e.value("y", 0.0), e.value("z", 0.0),
                                   e.value("species", 0), e.value("mobile", false));
    }

    SimGrid excitation = cfg.contains("excitation") ? grid_from(cfg["excitation"])
                                                    : SimGrid::uniform(1.0, 2.0, 4.0, 0.1);
    std::vector<SimGrid> detection;
    if (cfg.contains("detection"))
        for (const json& d : cfg["detection"]) detection.push_back(grid_from(d));

    return new SimEngine(std::move(sample), std::move(excitation), std::move(detection), st);
}

std::string SimEngine::default_json() {
    return R"({
  "settings": {
    "dt": 0.01, "n_ph_max": 1000000, "max_windows": 0,
    "seed_diffusion": 12345, "seed_emission": 54321, "n_channels": 2,
    "n_microtime_channels": 4096, "microtime_resolution": 0.008, "laser_period": 32.0,
    "rng_kind": "xoshiro", "rng_scope": "per_molecule",
    "per_molecule_skip": false, "fast_grid_bbox": false,
    "independent_molecules": false, "active_margin": 0.0
  },
  "box": {"xy": 2.0, "z": 4.0},
  "species": [{"D": 3.0, "q": [50.0, 50.0], "r0": 0.0, "l1": 0.0, "l2": 0.0, "D_rot": 0.0}],
  "k_rad": [0.0], "k_nrad": [0.0],
  "background": [0.0, 0.0],
  "population": [5.0],
  "emitters": [],
  "excitation": {"type": "gaussian3d", "w0": 0.3, "z0": 2.0,
                 "extent_xy": 2.0, "extent_z": 4.0, "spacing": 0.1, "amplitude": 1.0},
  "detection": []
})";
}

} // namespace tttrlib
