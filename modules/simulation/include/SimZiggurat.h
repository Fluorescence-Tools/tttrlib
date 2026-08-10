/*!
 * \file SimZiggurat.h
 * \brief Ziggurat standard-normal sampler (Marsaglia & Tsang 2000) for the simulator.
 *
 * The per-step diffusion displacement needs three standard normals per mobile
 * molecule per window — by far the hottest RNG consumer in the engine. The ziggurat
 * method returns a normal with a single 32-bit draw, one table lookup and one compare
 * in ~98% of calls, hitting a `log`/`sqrt` path only in the rare tail/edge case. That
 * makes it ~2.5x faster than the ratio-of-uniforms `randomNorm` used elsewhere, while
 * remaining a pure function of the RNG stream — so a molecule's displacement stays a
 * deterministic function of (id, window) and results remain thread-count-independent.
 *
 * The 128-layer tables are read-only after construction (a Meyers singleton), so they
 * are shared safely across worker threads. Header-only; additive.
 */
#ifndef TTTRLIB_SIMZIGGURAT_H
#define TTTRLIB_SIMZIGGURAT_H

#include <cstdint>
#include <cmath>

namespace tttrlib {

struct SimZigguratTables {
    uint32_t kn[128];
    double wn[128], fn[128];
    SimZigguratTables() {
        const double m1 = 2147483648.0;                 // 2^31 (int32 magnitude range)
        double dn = 3.442619855899, tn = dn, vn = 9.91256303526217e-3;
        double q = vn / std::exp(-0.5 * dn * dn);
        kn[0] = uint32_t((dn / q) * m1); kn[1] = 0;
        wn[0] = q / m1; wn[127] = dn / m1;
        fn[0] = 1.0;    fn[127] = std::exp(-0.5 * dn * dn);
        for (int i = 126; i >= 1; --i) {
            dn = std::sqrt(-2.0 * std::log(vn / dn + std::exp(-0.5 * dn * dn)));
            kn[i + 1] = uint32_t((dn / tn) * m1); tn = dn;
            fn[i] = std::exp(-0.5 * dn * dn); wn[i] = dn / m1;
        }
    }
};

inline const SimZigguratTables& sim_ziggurat_tables() {
    static const SimZigguratTables t;
    return t;
}

/// Draw one standard normal from `rng` via the ziggurat method.
template <class Rng>
inline double sim_randn(Rng& rng) {
    const SimZigguratTables& z = sim_ziggurat_tables();
    for (;;) {
        int32_t hz = int32_t(rng.next_u32());
        uint32_t iz = uint32_t(hz) & 127u;
        uint32_t az = (hz < 0) ? uint32_t(-(int64_t)hz) : uint32_t(hz);
        if (az < z.kn[iz]) return hz * z.wn[iz];        // fast path (~98%)
        double x = hz * z.wn[iz];
        if (iz == 0) {                                   // tail
            const double r = 3.442620;
            double xx, yy;
            do { xx = -std::log(rng.random0i1e()) / r; yy = -std::log(rng.random0i1e()); }
            while (yy + yy < xx * xx);
            return (hz > 0) ? r + xx : -r - xx;
        }
        if (z.fn[iz] + rng.random0i1e() * (z.fn[iz - 1] - z.fn[iz]) < std::exp(-0.5 * x * x))
            return x;                                    // wedge accept
    }
}

} // namespace tttrlib

#endif // TTTRLIB_SIMZIGGURAT_H
