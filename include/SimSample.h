/*!
 * \file SimSample.h
 * \brief The simulated sample: species/states, transition kinetics, background, box
 *        geometry, and the fluorophore population (PRD-005).
 *
 * Fluorophores may be provided three ways (combinable): a multi-channel integer
 * emitter grid (primary, from a TIFF read in Python), explicit discrete emitters,
 * or an open-volume expected population (stationary-focus FCS, molecules injected
 * across the box surface). Per-particle static/mobile is a flag; the species D
 * governs the step size when mobile. Additive; does not modify existing tttrlib.
 */
#ifndef TTTRLIB_SIMSAMPLE_H
#define TTTRLIB_SIMSAMPLE_H

#include <cstdint>
#include <vector>
#include "SimSpecies.h"

namespace tttrlib {

/// A single point fluorophore instance.
struct SimEmitter {
    double x = 0, y = 0, z = 0;    ///< position (µm)
    int species = 0;               ///< species/state index
    bool mobile = false;           ///< diffuses (with species D) when true
};

/*!
 * \brief Defines what is in the simulated volume.
 *
 * State/species transitions use N×N (row-major, i→j) rate matrices: `k_rad` is
 * intensity-dependent (scaled by excitation at runtime — FRET/photo-induced) and
 * `k_nrad` is spontaneous. `n_species()` must equal the matrix dimension.
 */
class SimSample {
public:
    // --- species / kinetics -----------------------------------------------------
    int add_species(const SimSpecies& s) { species_.push_back(s); return int(species_.size()) - 1; }
    int n_species() const { return int(species_.size()); }
    const std::vector<SimSpecies>& species() const { return species_; }

    /// N×N transition rate matrices (row-major, i→j). Sizes must be n_species²·.
    void set_rate_matrices(std::vector<double> k_rad, std::vector<double> k_nrad) {
        k_rad_ = std::move(k_rad); k_nrad_ = std::move(k_nrad);
    }
    const std::vector<double>& k_rad() const { return k_rad_; }
    const std::vector<double>& k_nrad() const { return k_nrad_; }

    /// Background count rate per detection channel.
    void set_background(std::vector<double> q_bg) { q_bg_ = std::move(q_bg); }
    const std::vector<double>& background() const { return q_bg_; }

    /// Micro-time (FLIM) decay pattern for background photons (e.g. scatter/IRF or a
    /// flat "dark" distribution) so background carries a realistic micro-time instead
    /// of 0. `set_background_decay` broadcasts one pattern to all channels;
    /// `set_background_decays` gives one per channel. Empty vector => background micro-time 0.
    void set_background_decay(const SimDecay& d) { bg_decays_.assign(1, d); }
    void set_background_decays(std::vector<SimDecay> d) { bg_decays_ = std::move(d); }
    const std::vector<SimDecay>& background_decays() const { return bg_decays_; }

    // --- geometry ---------------------------------------------------------------
    void set_box(double box_xy, double box_z) { box_xy_ = box_xy; box_z_ = box_z; }
    double box_xy() const { return box_xy_; }
    double box_z() const { return box_z_; }

    // --- fluorophore population -------------------------------------------------
    /// Open-volume FCS: expected number of molecules of a species (surface-flux injection).
    void set_population(int species, double expected_count);
    const std::vector<double>& population() const { return population_; }

    /// Add one explicit discrete emitter.
    void add_fluorophore(double x, double y, double z, int species, bool mobile = false) {
        emitters_.push_back(SimEmitter{x, y, z, species, mobile});
    }
    /// Add many discrete emitters (xyz length 3n; species/mobile length n, mobile may be null).
    void set_positions(const double* xyz, int n, const int* species, const uint8_t* mobile);

    /*!
     * \brief Populate discrete emitters from a multi-channel INT grid ("single emitters").
     *
     * Channel semantics: `ch0` = emitter count in the voxel, `ch1` = species index
     * (default 0 if absent), `ch2` = mobile flag (default 0/static if absent).
     * Voxels expand into `count` point emitters at the voxel centre; the grid
     * spacing/origin set the µm frame. Data is row-major with x fastest within a
     * channel, channels outermost: `data[(c*nz + iz)*ny*nx + iy*nx + ix]`.
     */
    void set_emitter_grid(const int* data, int nch, int nz, int ny, int nx,
                          double dx, double dy, double dz,
                          double x0, double y0, double z0);

    /// Binding-friendly overload taking a flat (channel-outermost) int vector.
    void set_emitter_grid(const std::vector<int>& data, int nch, int nz, int ny, int nx,
                          double dx, double dy, double dz,
                          double x0, double y0, double z0) {
        set_emitter_grid(data.data(), nch, nz, ny, nx, dx, dy, dz, x0, y0, z0);
    }

    const std::vector<SimEmitter>& emitters() const { return emitters_; }

private:
    std::vector<SimSpecies> species_;
    std::vector<double> k_rad_, k_nrad_, q_bg_, population_;
    std::vector<SimDecay> bg_decays_;
    std::vector<SimEmitter> emitters_;
    double box_xy_ = 2.0, box_z_ = 4.0;
};

} // namespace tttrlib

#endif // TTTRLIB_SIMSAMPLE_H
