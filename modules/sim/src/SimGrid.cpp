/*!
 * \file SimGrid.cpp
 * \brief 3D voxel grid implementation (see SimGrid.h, PRD-005).
 */
#include "SimGrid.h"
#include <cmath>

namespace tttrlib {

void SimGrid::build_bbox(double threshold_frac) {
    clear_bbox();
    if (radial_) return;   // the (rho,z) table is already the tight support
    if (nx <= 0 || ny <= 0 || nz <= 0 || data.empty() || threshold_frac >= 1.0) return;
    double peak = 0.0;
    for (double v : data) { double a = std::fabs(v); if (a > peak) peak = a; }
    if (peak <= 0.0) return;
    const double thr = (threshold_frac > 0.0) ? threshold_frac * peak : 0.0;
    int ix0 = nx, iy0 = ny, iz0 = nz, ix1 = -1, iy1 = -1, iz1 = -1;
    for (int iz = 0; iz < nz; ++iz)
        for (int iy = 0; iy < ny; ++iy)
            for (int ix = 0; ix < nx; ++ix)
                if (std::fabs(data[index(ix, iy, iz)]) > thr) {
                    if (ix < ix0) ix0 = ix; if (ix > ix1) ix1 = ix;
                    if (iy < iy0) iy0 = iy; if (iy > iy1) iy1 = iy;
                    if (iz < iz0) iz0 = iz; if (iz > iz1) iz1 = iz;
                }
    if (ix1 < 0) return;                       // no voxel above threshold
    // Pad by one voxel so trilinear interpolation just inside the box edge is unaffected.
    bb_x0_ = x0 + (ix0 - 1) * dx; bb_x1_ = x0 + (ix1 + 1) * dx;
    bb_y0_ = y0 + (iy0 - 1) * dy; bb_y1_ = y0 + (iy1 + 1) * dy;
    bb_z0_ = z0 + (iz0 - 1) * dz; bb_z1_ = z0 + (iz1 + 1) * dz;
}

SimGrid SimGrid::analytic_gaussian3d(double w0, double z0v, double amplitude) {
    SimGrid g;
    g.analytic_ = true;
    g.an_amp_ = amplitude;
    g.an_cxy_ = (w0  > 0.0) ? -2.0 / (w0  * w0)  : 0.0;
    g.an_cz_  = (z0v > 0.0) ? -2.0 / (z0v * z0v) : 0.0;
    return g;
}

double SimGrid::at(double x, double y, double z) const {
    if (analytic_) return an_amp_ * std::exp(an_cxy_ * (x * x + y * y) + an_cz_ * z * z);
    if (radial_) {
        // Bilinear in (rho, z): the azimuth carries no information for a cylindrically
        // symmetric field, so it is not stored and not interpolated over.
        const double fr = std::sqrt(x * x + y * y) / dr_;
        const double fz = (z - zr0_) / dzr_;
        if (fr > nr_ - 1 || fz < 0.0 || fz > nzr_ - 1) return 0.0;
        const int ir = int(fr), iz = int(fz);
        const double tr = fr - ir, tz = fz - iz;
        const size_t sr = (ir < nr_ - 1) ? 1 : 0;
        const size_t sz = (iz < nzr_ - 1) ? size_t(nr_) : 0;
        const double* d = rz_.data() + size_t(iz) * nr_ + ir;
        const double c0 = d[0] * (1 - tr) + d[sr] * tr;
        const double c1 = d[sz] * (1 - tr) + d[sz + sr] * tr;
        return c0 * (1 - tz) + c1 * tz;
    }
    if (nx <= 0 || ny <= 0 || nz <= 0) return 0.0;

    // Two-step lookup: cheap bounding-box reject before the trilinear (when active).
    if (bb_x1_ >= bb_x0_ &&
        (x < bb_x0_ || x > bb_x1_ || y < bb_y0_ || y > bb_y1_ || z < bb_z0_ || z > bb_z1_))
        return 0.0;

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

SimGrid SimGrid::gaussian_lorentzian(double w0, double zR,
                                     double extent_xy, double extent_z,
                                     double spacing, double amplitude) {
    int nxy = int(std::floor(2.0 * extent_xy / spacing)) + 1;
    int nz  = int(std::floor(2.0 * extent_z  / spacing)) + 1;
    SimGrid g(nxy, nxy, nz, spacing, spacing, spacing,
              -extent_xy, -extent_xy, -extent_z);
    const double w0sq = w0 * w0;
    for (int iz = 0; iz < nz; ++iz) {
        double z = g.z0 + iz * g.dz;
        // Beam waist expands along z (Lorentzian): w(z)² = w0²·(1 + (z/zR)²).
        double wz2 = (zR > 0.0) ? w0sq * (1.0 + (z / zR) * (z / zR)) : w0sq;
        double pref = amplitude * w0sq / wz2;               // (w0/w(z))²
        for (int iy = 0; iy < nxy; ++iy) {
            double y = g.y0 + iy * g.dy;
            for (int ix = 0; ix < nxy; ++ix) {
                double x = g.x0 + ix * g.dx;
                g.set_voxel(ix, iy, iz, pref * std::exp(-2.0 * (x*x + y*y) / wz2));
            }
        }
    }
    return g;
}

SimGrid SimGrid::from_radial(const std::vector<double>& rz, int nr, int nz_in,
                             double r_step, double z_step,
                             double extent_xy, double extent_z,
                             double spacing, double amplitude) {
    int nxy = int(std::floor(2.0 * extent_xy / spacing)) + 1;
    int nz  = int(std::floor(2.0 * extent_z  / spacing)) + 1;
    SimGrid g(nxy, nxy, nz, spacing, spacing, spacing,
              -extent_xy, -extent_xy, -extent_z);
    if (nr < 2 || nz_in < 2 || r_step <= 0.0 || z_step <= 0.0 ||
        rz.size() < size_t(nr) * nz_in)
        return g;                                            // ill-formed table ⇒ empty grid
    const double z_lo = -0.5 * (nz_in - 1) * z_step;         // radial table is z-centred
    // Bilinear interpolation of the (r,z) half-plane; clamp outside the table to 0.
    auto sample = [&](double r, double z) -> double {
        double fr = r / r_step, fz = (z - z_lo) / z_step;
        if (fr < 0.0 || fz < 0.0 || fr > nr - 1 || fz > nz_in - 1) return 0.0;
        int ir = int(fr), iz2 = int(fz);
        int ir1 = (ir < nr - 1) ? ir + 1 : ir, iz1 = (iz2 < nz_in - 1) ? iz2 + 1 : iz2;
        double tr = fr - ir, tz = fz - iz2;
        double v00 = rz[size_t(iz2) * nr + ir],  v10 = rz[size_t(iz2) * nr + ir1];
        double v01 = rz[size_t(iz1) * nr + ir],  v11 = rz[size_t(iz1) * nr + ir1];
        double v0 = v00 * (1 - tr) + v10 * tr, v1 = v01 * (1 - tr) + v11 * tr;
        return v0 * (1 - tz) + v1 * tz;
    };
    for (int iz = 0; iz < nz; ++iz) {
        double z = g.z0 + iz * g.dz;
        for (int iy = 0; iy < nxy; ++iy) {
            double y = g.y0 + iy * g.dy;
            for (int ix = 0; ix < nxy; ++ix) {
                double x = g.x0 + ix * g.dx;
                g.set_voxel(ix, iy, iz, amplitude * sample(std::sqrt(x*x + y*y), z));
            }
        }
    }
    return g;
}

SimGrid SimGrid::gaussian3d_radial(double w0, double z0v,
                                   double extent_xy, double extent_z,
                                   double spacing, double amplitude) {
    const double w0sq = w0 * w0, z0sq = z0v * z0v;
    return radial_from([&](double r, double z) {
        return amplitude * std::exp(-2.0 * (r * r / w0sq + z * z / z0sq));
    }, extent_xy, extent_z, spacing);
}

SimGrid SimGrid::gaussian_lorentzian_radial(double w0, double zR,
                                            double extent_xy, double extent_z,
                                            double spacing, double amplitude) {
    const double w0sq = w0 * w0;
    return radial_from([&](double r, double z) {
        const double wz2 = (zR > 0.0) ? w0sq * (1.0 + (z / zR) * (z / zR)) : w0sq;
        return amplitude * (w0sq / wz2) * std::exp(-2.0 * r * r / wz2);
    }, extent_xy, extent_z, spacing);
}

SimGrid SimGrid::from_radial_table(const std::vector<double>& rz, int nr, int nz_in,
                                   double r_step, double z_step,
                                   double extent_xy, double extent_z,
                                   double spacing, double amplitude) {
    if (nr < 2 || nz_in < 2 || r_step <= 0.0 || z_step <= 0.0 ||
        rz.size() < size_t(nr) * nz_in)
        return SimGrid();
    const double z_lo = -0.5 * (nz_in - 1) * z_step;
    auto sample = [&](double r, double z) -> double {
        double fr = r / r_step, fz = (z - z_lo) / z_step;
        if (fr < 0.0 || fz < 0.0 || fr > nr - 1 || fz > nz_in - 1) return 0.0;
        int ir = int(fr), iz2 = int(fz);
        int ir1 = (ir < nr - 1) ? ir + 1 : ir, iz1 = (iz2 < nz_in - 1) ? iz2 + 1 : iz2;
        double tr = fr - ir, tz = fz - iz2;
        double v00 = rz[size_t(iz2) * nr + ir],  v10 = rz[size_t(iz2) * nr + ir1];
        double v01 = rz[size_t(iz1) * nr + ir],  v11 = rz[size_t(iz1) * nr + ir1];
        double v0 = v00 * (1 - tr) + v10 * tr, v1 = v01 * (1 - tr) + v11 * tr;
        return v0 * (1 - tz) + v1 * tz;
    };
    return radial_from([&](double r, double z) { return amplitude * sample(r, z); },
                       extent_xy, extent_z, spacing);
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
