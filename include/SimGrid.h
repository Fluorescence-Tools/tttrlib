/*!
 * \file SimGrid.h
 * \brief 3D voxel grid with trilinear interpolation — the field representation for
 *        excitation and detection profiles in the photon simulator (PRD-005).
 *
 * The simulator is fully grid-based: excitation and each detector's detection
 * profile are `SimGrid`s sampled at continuous molecule positions by trilinear
 * interpolation. Analytic PSF/CEF shapes exist only as **static fillers** that
 * build a `SimGrid`; they are never evaluated at runtime. Additive; does not
 * modify any existing tttrlib class.
 */
#ifndef TTTRLIB_SIMGRID_H
#define TTTRLIB_SIMGRID_H

#include <cstddef>
#include <vector>

namespace tttrlib {

/*!
 * \brief A regular 3D scalar grid in physical (µm) coordinates.
 *
 * Storage is row-major with x fastest: `data[((iz*ny)+iy)*nx + ix]`. `(x0,y0,z0)`
 * is the physical position of voxel-centre `[0,0,0]`; `(dx,dy,dz)` the spacing.
 * Sampling outside the grid returns 0.
 */
class SimGrid {
public:
    std::vector<double> data;
    int nx = 0, ny = 0, nz = 0;
    double dx = 1, dy = 1, dz = 1;   ///< voxel spacing (µm)
    double x0 = 0, y0 = 0, z0 = 0;   ///< physical centre of voxel [0,0,0] (µm)

    SimGrid() = default;
    SimGrid(int nx_, int ny_, int nz_,
            double dx_, double dy_, double dz_,
            double x0_, double y0_, double z0_)
        : data(size_t(nx_) * ny_ * nz_, 0.0),
          nx(nx_), ny(ny_), nz(nz_), dx(dx_), dy(dy_), dz(dz_),
          x0(x0_), y0(y0_), z0(z0_) {}

    inline size_t index(int ix, int iy, int iz) const {
        return (size_t(iz) * ny + iy) * nx + ix;
    }
    inline double at_voxel(int ix, int iy, int iz) const { return data[index(ix, iy, iz)]; }
    inline void set_voxel(int ix, int iy, int iz, double v) { data[index(ix, iy, iz)] = v; }

    /// Trilinearly interpolated value at physical position (x,y,z); 0 outside the grid.
    double at(double x, double y, double z) const;

    /*!
     * \brief Fill a grid from a 3D-Gaussian intensity `A·exp(-2((x²+y²)/w0² + z²/z0²))`.
     *
     * Convenience only (matches the legacy `focus_3dgauss` shape, amplitude `A`). The
     * grid spans ±extent about the origin at the given isotropic `spacing`.
     */
    static SimGrid gaussian3d(double w0, double z0,
                              double extent_xy, double extent_z,
                              double spacing, double amplitude = 1.0);

    /// A grid of constant value (e.g. a uniform detection profile / CEF = 1).
    static SimGrid uniform(double value,
                           double extent_xy, double extent_z, double spacing);
};

} // namespace tttrlib

#endif // TTTRLIB_SIMGRID_H
