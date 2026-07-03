/*!
 * \file SimGrid.cpp
 * \brief 3D voxel grid implementation (see SimGrid.h, PRD-005).
 */
#include "SimGrid.h"
#include <cmath>

namespace tttrlib {

double SimGrid::at(double x, double y, double z) const {
    if (nx <= 0 || ny <= 0 || nz <= 0) return 0.0;

    // Continuous grid coordinates (voxel-centre convention).
    double fx = (x - x0) / dx;
    double fy = (y - y0) / dy;
    double fz = (z - z0) / dz;

    if (fx < 0 || fy < 0 || fz < 0 ||
        fx > nx - 1 || fy > ny - 1 || fz > nz - 1) {
        return 0.0;  // outside the grid
    }

    int ix = int(std::floor(fx)), iy = int(std::floor(fy)), iz = int(std::floor(fz));
    int ix1 = (ix < nx - 1) ? ix + 1 : ix;
    int iy1 = (iy < ny - 1) ? iy + 1 : iy;
    int iz1 = (iz < nz - 1) ? iz + 1 : iz;
    double tx = fx - ix, ty = fy - iy, tz = fz - iz;

    double c000 = at_voxel(ix,  iy,  iz),  c100 = at_voxel(ix1, iy,  iz);
    double c010 = at_voxel(ix,  iy1, iz),  c110 = at_voxel(ix1, iy1, iz);
    double c001 = at_voxel(ix,  iy,  iz1), c101 = at_voxel(ix1, iy,  iz1);
    double c011 = at_voxel(ix,  iy1, iz1), c111 = at_voxel(ix1, iy1, iz1);

    double c00 = c000 * (1 - tx) + c100 * tx;
    double c10 = c010 * (1 - tx) + c110 * tx;
    double c01 = c001 * (1 - tx) + c101 * tx;
    double c11 = c011 * (1 - tx) + c111 * tx;
    double c0 = c00 * (1 - ty) + c10 * ty;
    double c1 = c01 * (1 - ty) + c11 * ty;
    return c0 * (1 - tz) + c1 * tz;
}

SimGrid SimGrid::gaussian3d(double w0, double z0v,
                            double extent_xy, double extent_z,
                            double spacing, double amplitude) {
    int nxy = int(std::floor(2.0 * extent_xy / spacing)) + 1;
    int nz  = int(std::floor(2.0 * extent_z  / spacing)) + 1;
    SimGrid g(nxy, nxy, nz, spacing, spacing, spacing,
              -extent_xy, -extent_xy, -extent_z);
    for (int iz = 0; iz < nz; ++iz) {
        double z = g.z0 + iz * g.dz;
        for (int iy = 0; iy < nxy; ++iy) {
            double y = g.y0 + iy * g.dy;
            for (int ix = 0; ix < nxy; ++ix) {
                double x = g.x0 + ix * g.dx;
                double v = amplitude *
                    std::exp(-2.0 * ((x*x + y*y) / (w0 * w0) + z*z / (z0v * z0v)));
                g.set_voxel(ix, iy, iz, v);
            }
        }
    }
    return g;
}

SimGrid SimGrid::uniform(double value,
                         double extent_xy, double extent_z, double spacing) {
    int nxy = int(std::floor(2.0 * extent_xy / spacing)) + 1;
    int nz  = int(std::floor(2.0 * extent_z  / spacing)) + 1;
    SimGrid g(nxy, nxy, nz, spacing, spacing, spacing,
              -extent_xy, -extent_xy, -extent_z);
    for (auto& v : g.data) v = value;
    return g;
}

} // namespace tttrlib
