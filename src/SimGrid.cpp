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

    // fx,fy,fz are >= 0 here, so truncation equals floor (no std::floor needed).
    int ix = int(fx), iy = int(fy), iz = int(fz);
    double tx = fx - ix, ty = fy - iy, tz = fz - iz;

    // Neighbour offsets: clamp to the last plane at the upper edge (matches ix+1 etc.).
    const size_t sx = 1;
    const size_t sy = size_t(nx);
    const size_t sz = size_t(nx) * ny;
    size_t base = (size_t(iz) * ny + iy) * nx + ix;
    size_t ox = (ix < nx - 1) ? sx : 0;
    size_t oy = (iy < ny - 1) ? sy : 0;
    size_t oz = (iz < nz - 1) ? sz : 0;
    const double* d = data.data();

    double c000 = d[base],           c100 = d[base + ox];
    double c010 = d[base + oy],      c110 = d[base + ox + oy];
    double c001 = d[base + oz],      c101 = d[base + ox + oz];
    double c011 = d[base + oy + oz], c111 = d[base + ox + oy + oz];

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
