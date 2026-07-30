/*!
 * \file SimVectorGrid.h
 * \brief A 3-D velocity field in physical (µm) coordinates, in µm per macro-time unit.
 *
 * Three SimGrid components on one lattice, using SimGrid::at for trilinear
 * interpolation and the same outside-the-grid semantics. Provides an analytic
 * uniform fast path that needs no lattice at all.
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

    /// Velocity at a physical position. Zero outside the lattice (SimGrid::at semantics).
    void at(double x, double y, double z, double& vx, double& vy, double& vz) const;

    /// Largest |v| anywhere in the field (the coast bound and the dt guard need it).
    double max_speed() const;

    bool empty()      const { return !uniform_ && vx_.nx <= 0; }
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
    SimGrid vx_, vy_, vz_;
    bool uniform_ = false;
    double ux_ = 0, uy_ = 0, uz_ = 0;
};

} // namespace tttrlib

#endif // TTTRLIB_SIMVECTORGRID_H
