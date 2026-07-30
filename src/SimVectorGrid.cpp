/*!
 * \file SimVectorGrid.cpp
 * \brief Vector-grid implementation (see SimVectorGrid.h).
 */
#include "SimVectorGrid.h"
#include <cmath>
#include <stdexcept>

namespace tttrlib {

void SimVectorGrid::at(double x, double y, double z,
                        double& vx, double& vy, double& vz) const {
    if (uniform_) {
        vx = ux_; vy = uy_; vz = uz_;
        return;
    }
    vx = vx_.at(x, y, z);
    vy = vy_.at(x, y, z);
    vz = vz_.at(x, y, z);
}

double SimVectorGrid::max_speed() const {
    if (uniform_) {
        return std::sqrt(ux_ * ux_ + uy_ * uy_ + uz_ * uz_);
    }
    if (vx_.nx <= 0 || vx_.data.empty()) return 0.0;
    double ms = 0.0;
    const size_t n = vx_.data.size();
    for (size_t k = 0; k < n; ++k) {
        double s = vx_.data[k] * vx_.data[k]
                 + vy_.data[k] * vy_.data[k]
                 + vz_.data[k] * vz_.data[k];
        if (s > ms) ms = s;
    }
    return std::sqrt(ms);
}

bool SimVectorGrid::bounds(double& x0, double& y0, double& z0,
                            double& x1, double& y1, double& z1) const {
    if (uniform_ || vx_.nx <= 0) return false;
    x0 = vx_.x0;            y0 = vx_.y0;            z0 = vx_.z0;
    x1 = vx_.x0 + (vx_.nx - 1) * vx_.dx;
    y1 = vx_.y0 + (vx_.ny - 1) * vx_.dy;
    z1 = vx_.z0 + (vx_.nz - 1) * vx_.dz;
    return true;
}

double SimVectorGrid::min_spacing() const {
    if (uniform_ || vx_.nx <= 0) return 0.0;
    double s = vx_.dx;
    if (vx_.dy < s) s = vx_.dy;
    if (vx_.dz < s) s = vx_.dz;
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
    int nxy = int(std::floor(2.0 * extent_xy / spacing)) + 1;
    int nz  = int(std::floor(2.0 * extent_z  / spacing)) + 1;
    SimVectorGrid g;
    g.vx_ = SimGrid(nxy, nxy, nz, spacing, spacing, spacing,
                    -extent_xy, -extent_xy, -extent_z);
    g.vy_ = SimGrid(nxy, nxy, nz, spacing, spacing, spacing,
                    -extent_xy, -extent_xy, -extent_z);
    g.vz_ = SimGrid(nxy, nxy, nz, spacing, spacing, spacing,
                    -extent_xy, -extent_xy, -extent_z);
    const double rsq = radius * radius;
    for (int iz = 0; iz < nz; ++iz) {
        double z = g.vz_.z0 + iz * g.vz_.dz;
        for (int iy = 0; iy < nxy; ++iy) {
            double y = g.vy_.y0 + iy * g.vy_.dy;
            for (int ix = 0; ix < nxy; ++ix) {
                double x = g.vx_.x0 + ix * g.vx_.dx;
                double rho2 = 0.0;
                if (axis == 0)      rho2 = y*y + z*z;
                else if (axis == 1) rho2 = x*x + z*z;
                else                rho2 = x*x + y*y;
                double v = (rho2 < rsq) ? v_max * (1.0 - rho2 / rsq) : 0.0;
                if (axis == 0) {
                    g.vx_.set_voxel(ix, iy, iz, v);
                } else if (axis == 1) {
                    g.vy_.set_voxel(ix, iy, iz, v);
                } else {
                    g.vz_.set_voxel(ix, iy, iz, v);
                }
            }
        }
    }
    return g;
}

SimVectorGrid SimVectorGrid::rotation(double omega, int axis,
                                       double extent_xy, double extent_z,
                                       double spacing) {
    int nxy = int(std::floor(2.0 * extent_xy / spacing)) + 1;
    int nz  = int(std::floor(2.0 * extent_z  / spacing)) + 1;
    SimVectorGrid g;
    g.vx_ = SimGrid(nxy, nxy, nz, spacing, spacing, spacing,
                    -extent_xy, -extent_xy, -extent_z);
    g.vy_ = SimGrid(nxy, nxy, nz, spacing, spacing, spacing,
                    -extent_xy, -extent_xy, -extent_z);
    g.vz_ = SimGrid(nxy, nxy, nz, spacing, spacing, spacing,
                    -extent_xy, -extent_xy, -extent_z);
    for (int iz = 0; iz < nz; ++iz) {
        double z = g.vz_.z0 + iz * g.vz_.dz;
        for (int iy = 0; iy < nxy; ++iy) {
            double y = g.vy_.y0 + iy * g.vy_.dy;
            for (int ix = 0; ix < nxy; ++ix) {
                double x = g.vx_.x0 + ix * g.vx_.dx;
                // v = omega × r: v = ω·(−y, x, 0) for rotation about z, etc.
                if (axis == 2) {
                    g.vx_.set_voxel(ix, iy, iz, -omega * y);
                    g.vy_.set_voxel(ix, iy, iz,  omega * x);
                } else if (axis == 1) {
                    g.vx_.set_voxel(ix, iy, iz,  omega * z);
                    g.vz_.set_voxel(ix, iy, iz, -omega * x);
                } else {
                    g.vy_.set_voxel(ix, iy, iz, -omega * z);
                    g.vz_.set_voxel(ix, iy, iz,  omega * y);
                }
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
    size_t expected = size_t(nx) * ny * nz;
    if (vx.size() != expected || vy.size() != expected || vz.size() != expected)
        throw std::invalid_argument(
            "SimVectorGrid::from_components: vx, vy, vz must each have nx*ny*nz elements");
    SimVectorGrid g;
    g.vx_ = SimGrid(nx, ny, nz, dx, dy, dz, x0, y0, z0);
    g.vy_ = SimGrid(nx, ny, nz, dx, dy, dz, x0, y0, z0);
    g.vz_ = SimGrid(nx, ny, nz, dx, dy, dz, x0, y0, z0);
    g.vx_.data = vx;
    g.vy_.data = vy;
    g.vz_.data = vz;
    return g;
}

} // namespace tttrlib
