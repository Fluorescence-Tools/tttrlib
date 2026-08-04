/*!
 * \file SimVectorGrid.h
 * \brief A 3-D velocity field in physical (µm) coordinates, in µm per macro-time unit.
 *
 * Three velocity components on one lattice, with the same trilinear interpolation and
 * outside-the-grid semantics as SimGrid (sampling outside returns 0). Provides an
 * analytic uniform fast path that needs no lattice at all.
 *
 * The components are stored **interleaved** -- (vx,vy,vz) adjacent per voxel -- because
 * the field is sampled once (twice with the midpoint scheme) per molecule per window, and
 * that is the hot loop of any flow simulation. Three separate SimGrid lookups would repeat
 * the same bounds test, index arithmetic and interpolation weights three times and walk
 * three distant arrays; interleaving does the arithmetic once and puts all three
 * components of a voxel on one cache line.
 */
#ifndef TTTRLIB_SIMVECTORGRID_H
#define TTTRLIB_SIMVECTORGRID_H

#include "SimGrid.h"
#include <vector>
#include <string>

namespace tttrlib {

class SimVectorGrid {
public:
    SimVectorGrid() = default;

    /*!
     * Velocity at a physical position; zero outside the lattice (SimGrid::at semantics).
     *
     * Defined inline on purpose: this is called once (twice with the midpoint scheme)
     * per molecule per window, and an out-of-line call into another translation unit
     * cannot be inlined into the engine's step loop. The uniform branch in particular
     * collapses to three loads once it is visible to the optimiser.
     */
    inline void at(double x, double y, double z,
                   double& vx, double& vy, double& vz) const {
        if (uniform_) {
            vx = ux_; vy = uy_; vz = uz_;
            return;
        }
        vx = vy = vz = 0.0;
        if (nx_ <= 0 || ny_ <= 0 || nz_ <= 0) return;

        // Continuous grid coordinates (voxel-centre convention), as in SimGrid::at.
        const double fx = (x - x0_) / dx_;
        const double fy = (y - y0_) / dy_;
        const double fz = (z - z0_) / dz_;
        if (fx < 0 || fy < 0 || fz < 0 ||
            fx > nx_ - 1 || fy > ny_ - 1 || fz > nz_ - 1) {
            return;                                  // outside the grid: no flow
        }

        // fx,fy,fz are >= 0 here, so truncation equals floor (no std::floor needed).
        const int ix = int(fx), iy = int(fy), iz = int(fz);
        const double tx = fx - ix, ty = fy - iy, tz = fz - iz;

        // Neighbour offsets in units of *interleaved triples*; clamp at the upper edge.
        const size_t sx = (ix < nx_ - 1) ? size_t(3) : 0;
        const size_t sy = (iy < ny_ - 1) ? size_t(nx_) * 3 : 0;
        const size_t sz = (iz < nz_ - 1) ? size_t(nx_) * ny_ * 3 : 0;
        const double* d = v_.data() + ((size_t(iz) * ny_ + iy) * nx_ + ix) * 3;

        // One index computation and one bounds test for all three components; the eight
        // corners of a voxel carry their vx,vy,vz adjacent, so each corner is one cache line.
        // The nested-lerp form matches SimGrid::at exactly, so a component of this field and
        // the same component read through a SimGrid agree bit for bit.
        double out[3];
        for (int c = 0; c < 3; ++c) {
            const double c000 = d[c],           c100 = d[sx + c];
            const double c010 = d[sy + c],      c110 = d[sx + sy + c];
            const double c001 = d[sz + c],      c101 = d[sx + sz + c];
            const double c011 = d[sy + sz + c], c111 = d[sx + sy + sz + c];

            const double c00 = c000 * (1 - tx) + c100 * tx;
            const double c10 = c010 * (1 - tx) + c110 * tx;
            const double c01 = c001 * (1 - tx) + c101 * tx;
            const double c11 = c011 * (1 - tx) + c111 * tx;
            const double c0 = c00 * (1 - ty) + c10 * ty;
            const double c1 = c01 * (1 - ty) + c11 * ty;
            out[c] = c0 * (1 - tz) + c1 * tz;
        }
        vx = out[0]; vy = out[1]; vz = out[2];
    }

    /// Largest |v| anywhere in the field (the coast bound and the dt guard need it).
    double max_speed() const;

    bool empty()      const { return !uniform_ && nx_ <= 0; }
    bool is_uniform() const { return uniform_; }

    /// Physical AABB of the lattice; false for the uniform (lattice-free) field.
    bool bounds(double& x0, double& y0, double& z0,
                double& x1, double& y1, double& z1) const;

    /// Smallest voxel spacing; 0 for the uniform field.
    double min_spacing() const;

    // --- builders -------------------------------------------------------------
    /// Constant field, evaluated analytically — exact everywhere, no lattice, no guards.
    static SimVectorGrid uniform(double vx, double vy, double vz);

    /// Poiseuille (Hagen–Poiseuille) flow: v_axis = v_max·(1 − rho²/radius²) with rho the
    /// distance from the axis, clamped to 0 outside the pipe. axis: 0=x, 1=y, 2=z.
    /// Divergence-free and reproduced exactly by trilinear interpolation.
    static SimVectorGrid poiseuille(double v_max, double radius, int axis,
                                    double extent_xy, double extent_z, double spacing);

    /// Rigid-body rotation about `axis` with angular velocity omega (rad / macro-time).
    /// Divergence-free and linear, so interpolation is exact.
    static SimVectorGrid rotation(double omega, int axis,
                                  double extent_xy, double extent_z, double spacing);

    /// Arbitrary field from three same-shaped component arrays (row-major, x fastest).
    /// NOT checked for divergence: a compressible field will concentrate molecules.
    static SimVectorGrid from_components(const std::vector<double>& vx,
                                         const std::vector<double>& vy,
                                         const std::vector<double>& vz,
                                         int nx, int ny, int nz,
                                         double dx, double dy, double dz,
                                         double x0, double y0, double z0);

private:
    /// Allocate the lattice and zero it; used by the builders.
    void allocate(int nx, int ny, int nz,
                  double dx, double dy, double dz,
                  double x0, double y0, double z0);
    /// Write one voxel's velocity.
    inline void set_voxel(int ix, int iy, int iz, double vx, double vy, double vz) {
        const size_t k = ((size_t(iz) * ny_ + iy) * nx_ + ix) * 3;
        v_[k] = vx; v_[k + 1] = vy; v_[k + 2] = vz;
    }

    std::vector<double> v_;                 ///< interleaved (vx,vy,vz), 3*nx*ny*nz
    int nx_ = 0, ny_ = 0, nz_ = 0;
    double dx_ = 1, dy_ = 1, dz_ = 1;       ///< voxel spacing (µm)
    double x0_ = 0, y0_ = 0, z0_ = 0;       ///< physical centre of voxel [0,0,0] (µm)
    bool uniform_ = false;
    double ux_ = 0, uy_ = 0, uz_ = 0;
};

} // namespace tttrlib

#endif // TTTRLIB_SIMVECTORGRID_H
