// SPDX-License-Identifier: BSD-3-Clause
//
// The masked-lattice diffusion solver and its adjoint
// (modules/math/include/LatticeDiffusion.h).
//
//   c++ -std=c++17 -O2 -I modules/math/include test/cpp/test_lattice_diffusion.cpp \
//       -o /tmp/test_lattice_diffusion && /tmp/test_lattice_diffusion
//
// The forward is checked for the two things it promises -- population is
// conserved without decay, and decays as exp(-k t) with a uniform rate -- and
// the adjoint by the dot-product identity against the forward, which shares no
// code with the reverse sweep: <dL/dtheta, v> from the adjoint must equal a
// central difference of the forward along a random direction v, for the
// mobility, the decay factor and the initial density, both flux forms,
// checkpoint lengths that do and do not divide n_steps, and a domain that
// touches the shell (which the imp.bff wrapper forbids, but the kernel is
// exact for it). A wrong transpose, a missed bounds factor, a mis-seeded
// report or a checkpoint restart off by one all fail this at 1e-3 or worse;
// the agreement is 1e-8 to 1e-12.
//
// Exit status is the number of failed checks.

#include "LatticeDiffusion.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace tttrlib;

static int g_failures = 0;

static void report(bool ok, const char* what, double value, double tol) {
    if (!ok) { std::printf("  FAIL  %s (%.3g > %.3g)\n", what, value, tol); ++g_failures; }
    else       std::printf("  ok    %s (%.3g)\n", what, value);
}

struct Lcg {
    std::uint64_t s;
    double u() {  // in [0, 1)
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>((s >> 11) & ((1ULL << 53) - 1)) / static_cast<double>(1ULL << 53);
    }
};

static double dot(const std::vector<double>& a, const std::vector<double>& b) {
    double s = 0; for (size_t i = 0; i < a.size(); ++i) s += a[i] * b[i]; return s;
}

/// A sphere with a corner missing, inside the shell (or touching it).
static void make_domain(int ng, bool touch_shell, Lcg& rng,
                        std::vector<double>& cur, std::vector<double>& d,
                        std::vector<double>& decay, std::vector<double>& b) {
    const size_t nv = static_cast<size_t>(ng) * ng * ng;
    cur.assign(nv, 0); d.assign(nv, 0); decay.assign(nv, 0); b.assign(nv, 0);
    const double c0 = (ng - 1) / 2.0;
    for (int i = 0; i < ng; ++i) for (int j = 0; j < ng; ++j) for (int k = 0; k < ng; ++k) {
        const size_t c = (static_cast<size_t>(i) * ng + j) * ng + k;
        const double r2 = (i - c0) * (i - c0) + (j - c0) * (j - c0) + (k - c0) * (k - c0);
        const bool interior = i > 0 && j > 0 && k > 0 && i < ng - 1 && j < ng - 1 && k < ng - 1;
        b[c] = ((interior || touch_shell) && r2 < 4.5 * 4.5 && !(i < 5 && j < 5)) ? 1.0 : 0.0;
        cur[c] = b[c] * (0.5 + rng.u());
        d[c] = 0.02 + 0.08 * rng.u();               // D dt/dg^2, well under the 1/6 limit
        decay[c] = std::exp(-0.05 * rng.u() * 3.0);
    }
    double s = 0; for (double v : cur) s += v; for (double& v : cur) v /= s;
}

// --------------------------------------------------------------------------
// 1. Forward: conservation and a uniform rate
// --------------------------------------------------------------------------
static void test_forward() {
    std::printf("forward: conservation, uniform decay\n");
    Lcg rng{3};
    std::vector<double> cur, d, decay, b;
    make_domain(13, false, rng, cur, d, decay, b);
    for (int ff = 0; ff < 2; ++ff) {
        std::vector<double> ones(cur.size(), 1.0), F;
        lattice_propagate(cur, d, ones, b, 13, ff, 200, 20, F);
        double worst = 0; for (double f : F) worst = std::max(worst, std::abs(f - 1.0));
        report(worst < 1e-12, ff == 0 ? "smoluchowski conserves population" : "ito conserves population", worst, 1e-12);
        std::vector<double> dec(cur.size(), std::exp(-0.01));
        lattice_propagate(cur, d, dec, b, 13, ff, 200, 20, F);
        worst = 0;
        for (size_t k = 0; k < F.size(); ++k) worst = std::max(worst, std::abs(F[k] - std::exp(-0.01 * 20.0 * k)));
        report(worst < 1e-12, "uniform rate decays as exp(-k t)", worst, 1e-12);
    }
}

// --------------------------------------------------------------------------
// 2. Adjoint: the dot-product identity against the forward
// --------------------------------------------------------------------------
static double loss(const std::vector<double>& cur, const std::vector<double>& d, const std::vector<double>& decay,
                   const std::vector<double>& b, int ng, int ff, int ns, int no, const std::vector<double>& w) {
    std::vector<double> F; lattice_propagate(cur, d, decay, b, ng, ff, ns, no, F); return dot(w, F);
}

static void test_adjoint(int ff, bool touch_shell, int ns, int no, unsigned seed) {
    const int ng = touch_shell ? 9 : 13;
    std::printf("adjoint: flux=%s n_steps=%d n_out=%d %s\n", ff == 0 ? "smoluchowski" : "ito", ns, no,
                touch_shell ? "(domain touches the shell)" : "");
    Lcg rng{seed};
    std::vector<double> cur, d, decay, b;
    make_domain(ng, touch_shell, rng, cur, d, decay, b);
    const size_t nv = cur.size();
    const int nr = ns / no + 1;
    std::vector<double> w(nr); for (auto& v : w) v = rng.u() - 0.5;
    std::vector<double> gd, gdec, gcur;
    lattice_propagate_adjoint(cur, d, decay, b, ng, ff, ns, no, w, gd, gdec, gcur);
    const double h = 1e-5;
    const char* names[3] = {"dL/dd", "dL/ddecay", "dL/dcur"};
    for (int which = 0; which < 3; ++which) {
        std::vector<double> v(nv);
        for (auto& x : v) x = (which == 1 ? 0.1 : 1.0) * (rng.u() - 0.5);
        std::vector<double> cp = cur, cm = cur, dp = d, dm = d, kp = decay, km = decay;
        std::vector<double>* P = which == 0 ? &dp : which == 1 ? &kp : &cp;
        std::vector<double>* M = which == 0 ? &dm : which == 1 ? &km : &cm;
        for (size_t c = 0; c < nv; ++c) { (*P)[c] += h * v[c]; (*M)[c] -= h * v[c]; }
        const double fd = (loss(cp, dp, kp, b, ng, ff, ns, no, w) - loss(cm, dm, km, b, ng, ff, ns, no, w)) / (2 * h);
        const std::vector<double>& g = which == 0 ? gd : which == 1 ? gdec : gcur;
        const double adj = dot(g, v);
        const double rel = std::abs(adj - fd) / (std::abs(fd) + 1e-30);
        report(rel < 1e-7, names[which], rel, 1e-7);
    }
    // n_steps == 0: only the initial report, gradient is dL_dF[0] everywhere on cur
    {
        std::vector<double> w0(1, 0.7), a, bb, cc;
        lattice_propagate_adjoint(cur, d, decay, b, ng, ff, 0, no, w0, a, bb, cc);
        double worst = 0; for (double x : cc) worst = std::max(worst, std::abs(x - 0.7));
        for (double x : a) worst = std::max(worst, std::abs(x));
        report(worst == 0.0, "n_steps = 0 seeds only the initial report", worst, 0.0);
    }
}

int main() {
    test_forward();
    test_adjoint(LATTICE_FLUX_SMOLUCHOWSKI, false, 47, 5, 7);
    test_adjoint(LATTICE_FLUX_SMOLUCHOWSKI, true, 100, 10, 17);
    test_adjoint(LATTICE_FLUX_ITO, false, 47, 5, 8);
    test_adjoint(LATTICE_FLUX_ITO, true, 100, 10, 18);
    bool threw = false;
    try {
        std::vector<double> a(27, 0), b, c, dd;
        lattice_propagate_adjoint(a, a, a, a, 3, 0, 10, 5, std::vector<double>(2), b, c, dd);
    } catch (const std::invalid_argument&) { threw = true; }
    report(threw, "wrong dL_dF length throws", 0, 0);
    std::printf("%d failure(s)\n", g_failures);
    return g_failures;
}
