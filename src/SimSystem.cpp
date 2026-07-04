/*!
 * \file SimSample.cpp
 * \brief SimSample implementation — population helpers and emitter-grid expansion
 *        (see SimSample.h, PRD-005).
 */
#include "SimSample.h"

namespace tttrlib {

void SimSample::set_population(int species, double expected_count) {
    if (species < 0) return;
    if (int(population_.size()) <= species) population_.resize(species + 1, 0.0);
    population_[species] = expected_count;
}

void SimSample::set_positions(const double* xyz, int n,
                              const int* species, const uint8_t* mobile) {
    emitters_.reserve(emitters_.size() + size_t(n));
    for (int i = 0; i < n; ++i) {
        emitters_.push_back(SimEmitter{
            xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2],
            species ? species[i] : 0,
            mobile ? (mobile[i] != 0) : false});
    }
}

void SimSample::set_emitter_grid(const int* data, int nch, int nz, int ny, int nx,
                                 double dx, double dy, double dz,
                                 double x0, double y0, double z0) {
    const size_t plane = size_t(ny) * nx;
    const size_t vol = size_t(nz) * plane;   // voxels per channel
    auto chan = [&](int c, int iz, int iy, int ix) -> int {
        return data[size_t(c) * vol + size_t(iz) * plane + size_t(iy) * nx + ix];
    };
    for (int iz = 0; iz < nz; ++iz) {
        double z = z0 + iz * dz;
        for (int iy = 0; iy < ny; ++iy) {
            double y = y0 + iy * dy;
            for (int ix = 0; ix < nx; ++ix) {
                int count = chan(0, iz, iy, ix);
                if (count <= 0) continue;
                int sp = (nch > 1) ? chan(1, iz, iy, ix) : 0;
                bool mob = (nch > 2) ? (chan(2, iz, iy, ix) != 0) : false;
                double x = x0 + ix * dx;
                for (int k = 0; k < count; ++k)
                    emitters_.push_back(SimEmitter{x, y, z, sp, mob});
            }
        }
    }
}

} // namespace tttrlib
