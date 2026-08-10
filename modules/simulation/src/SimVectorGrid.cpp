/*!
 * \file SimVectorGrid.cpp
 * \brief Vector-grid implementation (see SimVectorGrid.h).
 */
#include "SimVectorGrid.h"
#include <cmath>
#include <stdexcept>

namespace tttrlib {

void SimVectorGrid::allocate(int nx, int ny, int nz,
                             double dx, double dy, double dz,
                             double x0, double y0, double z0) {
    nx_ = nx; ny_ = ny; nz_ = nz;
    dx_ = dx; dy_ = dy; dz_ = dz;
    x0_ = x0; y0_ = y0; z0_ = z0;
    v_.assign(size_t(nx) * ny * nz * 3, 0.0);
}

double SimVectorGrid::max_speed() const {
    if (uniform_) {
        return std::sqrt(ux_ * ux_ + uy_ * uy_ + uz_ * uz_);
    }
    if (v_.empty()) return 0.0;
    double ms = 0.0;
    for (size_t k = 0; k + 2 < v_.size(); k += 3) {
        const double s = v_[k] * v_[k] + v_[k + 1] * v_[k + 1] + v_[k + 2] * v_[k + 2];
        if (s > ms) ms = s;
    }
    return std::sqrt(ms);
}

bool SimVectorGrid::bounds(double& x0, double& y0, double& z0,
                           double& x1, double& y1, double& z1) const {
    if (uniform_ || nx_ <= 0) return false;
    x0 = x0_;                       y0 = y0_;                       z0 = z0_;
    x1 = x0_ + (nx_ - 1) * dx_;     y1 = y0_ + (ny_ - 1) * dy_;     z1 = z0_ + (nz_ - 1) * dz_;
    return true;
}

double SimVectorGrid::min_spacing() const {
    if (uniform_ || nx_ <= 0) return 0.0;
    double s = dx_;
    if (dy_ < s) s = dy_;
    if (dz_ < s) s = dz_;
    return s;
}

SimVectorGrid SimVectorGrid::uniform(double vx, double vy, double vz) {
    SimVectorGrid g;
    g.uniform_ = true;
    g.ux_ = vx; g.uy_ = vy; g.uz_ = vz;
    return g;
}

SimVectorGrid SimVectorGrid::poiseuille(double v_max, double radius, int axis,
                                        double extent_xy, double extent_z,
                                        double spacing) {
    const int nxy = int(std::floor(2.0 * extent_xy / spacing)) + 1;
    const int nz = int(std::floor(2.0 * extent_z / spacing)) + 1;
    SimVectorGrid g;
    g.allocate(nxy, nxy, nz, spacing, spacing, spacing,
               -extent_xy, -extent_xy, -extent_z);
    const double rsq = radius * radius;
    for (int iz = 0; iz < nz; ++iz) {
        const double z = g.z0_ + iz * g.dz_;
        for (int iy = 0; iy < nxy; ++iy) {
            const double y = g.y0_ + iy * g.dy_;
            for (int ix = 0; ix < nxy; ++ix) {
                const double x = g.x0_ + ix * g.dx_;
                double rho2;
                if (axis == 0)      rho2 = y * y + z * z;
                else if (axis == 1) rho2 = x * x + z * z;
                else                rho2 = x * x + y * y;
                const double v = (rho2 < rsq) ? v_max * (1.0 - rho2 / rsq) : 0.0;
                if (axis == 0)      g.set_voxel(ix, iy, iz, v, 0.0, 0.0);
                else if (axis == 1) g.set_voxel(ix, iy, iz, 0.0, v, 0.0);
                else                g.set_voxel(ix, iy, iz, 0.0, 0.0, v);
            }
        }
    }
    return g;
}

SimVectorGrid SimVectorGrid::rotation(double omega, int axis,
                                      double extent_xy, double extent_z,
                                      double spacing) {
    const int nxy = int(std::floor(2.0 * extent_xy / spacing)) + 1;
    const int nz = int(std::floor(2.0 * extent_z / spacing)) + 1;
    SimVectorGrid g;
    g.allocate(nxy, nxy, nz, spacing, spacing, spacing,
               -extent_xy, -extent_xy, -extent_z);
    for (int iz = 0; iz < nz; ++iz) {
        const double z = g.z0_ + iz * g.dz_;
        for (int iy = 0; iy < nxy; ++iy) {
            const double y = g.y0_ + iy * g.dy_;
            for (int ix = 0; ix < nxy; ++ix) {
                const double x = g.x0_ + ix * g.dx_;
                // v = omega_hat x r
                if (axis == 2)      g.set_voxel(ix, iy, iz, -omega * y, omega * x, 0.0);
                else if (axis == 1) g.set_voxel(ix, iy, iz, omega * z, 0.0, -omega * x);
                else                g.set_voxel(ix, iy, iz, 0.0, -omega * z, omega * y);
            }
        }
    }
    return g;
}

SimVectorGrid SimVectorGrid::from_components(const std::vector<double>& vx,
                                             const std::vector<double>& vy,
                                             const std::vector<double>& vz,
                                             int nx, int ny, int nz,
                                             double dx, double dy, double dz,
                                             double x0, double y0, double z0) {
    const size_t expected = size_t(nx) * ny * nz;
    if (vx.size() != expected || vy.size() != expected || vz.size() != expected)
        throw std::invalid_argument(
            "SimVectorGrid::from_components: vx, vy, vz must each have nx*ny*nz elements");
    SimVectorGrid g;
    g.allocate(nx, ny, nz, dx, dy, dz, x0, y0, z0);
    for (size_t k = 0; k < expected; ++k) {
        g.v_[k * 3]     = vx[k];
        g.v_[k * 3 + 1] = vy[k];
        g.v_[k * 3 + 2] = vz[k];
    }
    return g;
}

} // namespace tttrlib
