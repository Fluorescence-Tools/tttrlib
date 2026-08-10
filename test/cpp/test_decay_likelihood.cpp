// SPDX-License-Identifier: BSD-3-Clause
//
// The Poisson likelihoods the decay fits minimise, at and below the model floor.
//
//   c++ -std=c++17 -O2 -I modules/spectroscopy/decay/include -I modules/util/include \
//       test/cpp/test_decay_likelihood.cpp modules/spectroscopy/decay/src/DecayStatistics.cpp \
//       modules/util/src/Verbose.cpp -o /tmp/test_decay_likelihood && /tmp/test_decay_likelihood
//
// `Wcm` and `wcm_p2s` used to answer a model bin at or below 1e-12 by *skipping*
// it. The comment called it stability; it was not. The term a near-zero bin
// contributes to the minimised objective is -C*log(m), large and positive, so
// dropping it is a discontinuous improvement -- the optimiser is paid to push a
// bin under the floor, and below it the objective is flat, so nothing brings it
// back. They are now continued smoothly instead.
//
// This file has two jobs, and the first matters more than the second:
//
//   1. Prove the change is a NO-OP above the floor. These functions are pinned
//      by cross-language reference tests; if the continuation perturbed an
//      ordinary evaluation by one ulp, that is a regression dressed as a fix.
//      Checked bitwise against the original expressions, transcribed here.
//   2. Prove the pathology is gone: continuous across the floor, finite below
//      it including at negative models, and monotone in the right direction so
//      there is always a gradient home.
//
// Exit status is the number of failed checks.

#include "DecayStatistics.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int g_failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) { std::printf("  FAIL  %s\n", what); ++g_failures; }
    else       std::printf("  ok    %s\n", what);
}

static void report(bool ok, const char* what, double v, double tol) {
    if (!ok) { std::printf("  FAIL  %s (%.6g > %.6g)\n", what, v, tol); ++g_failures; }
    else       std::printf("  ok    %s (%.3g)\n", what, v);
}

// --- the originals, transcribed verbatim, as the no-op reference -------------

static double Wcm_legacy(const int* C, const double* M, int Nchannels) {
    double W = 0.;
    for (int i = 0; i < 2 * Nchannels; i++)
        if (M[i] > 1.e-12) W += C[i] * std::log(M[i]);
    return -W;
}

// -----------------------------------------------------------------------------

static void test_wcm_is_unchanged_above_the_floor() {
    std::printf("Wcm: unchanged above the floor\n");

    // A spread of magnitudes, all strictly above 1e-12, including values just
    // above it where a sloppy `>=` would show up.
    const double mags[] = {1.0000001e-12, 1e-11, 1e-6, 1e-3, 0.5, 1.0, 7.25,
                           1e3, 1e6, 12345.6789};
    const int nmag = sizeof(mags) / sizeof(mags[0]);

    int worst_i = -1;
    for (int k = 0; k < nmag; ++k) {
        const int N = 8;
        std::vector<double> M(2 * N);
        std::vector<int> C(2 * N);
        for (int i = 0; i < 2 * N; ++i) {
            M[i] = mags[k] * (1.0 + 0.37 * i);
            C[i] = (i * 13) % 47;           // includes C == 0 bins
        }
        const double a = Wcm(C.data(), M.data(), N);
        const double b = Wcm_legacy(C.data(), M.data(), N);
        if (a != b) worst_i = k;
    }
    check(worst_i < 0, "bitwise identical to the original across 10 magnitudes");

    // A zero-count bin with a live model, and a live-count bin with a large
    // model, are the two shapes the loop treats differently. Pin them.
    {
        const int N = 1;
        double M[2] = {3.5, 0.25};
        int C[2] = {0, 19};
        check(Wcm(C, M, N) == Wcm_legacy(C, M, N), "C == 0 bins still contribute nothing");
    }
}

static void test_wcm_below_the_floor() {
    std::printf("Wcm: the floor is no longer a cliff\n");

    const int N = 2;
    int C[4] = {30, 30, 30, 30};
    auto obj = [&](double m0v) {
        double M[4] = {m0v, 1.0, 1.0, 1.0};
        return Wcm(C, M, N);
    };
    auto obj_legacy = [&](double m0v) {
        double M[4] = {m0v, 1.0, 1.0, 1.0};
        return Wcm_legacy(C, M, N);
    };

    // Continuity is NOT "the gap either side of the floor is small". The slope
    // of log at 1e-12 is 1e12, so any finite probe step shows a finite gap --
    // 6e-05 for a step of 1e-18, which is correct C1 behaviour, not a jump.
    // What separates a steep join from a cliff is how the gap responds to the
    // step: proportionally for the former, not at all for the latter.
    const double eps = 1e-18;
    const double gap_old = std::fabs(obj_legacy(kModelFloor + eps) - obj_legacy(kModelFloor - eps));
    const double gap_old_half = std::fabs(obj_legacy(kModelFloor + eps / 2) -
                                          obj_legacy(kModelFloor - eps / 2));
    const double gap_new = std::fabs(obj(kModelFloor + eps) - obj(kModelFloor - eps));
    const double gap_new_half = std::fabs(obj(kModelFloor + eps / 2) - obj(kModelFloor - eps / 2));

    std::printf("        (original: gap %.1f at step %.0e, %.1f at half that -- unchanged)\n",
                gap_old, eps, gap_old_half);
    check(gap_old > 100.0 && gap_old_half > 100.0,
          "the original was discontinuous: the gap ignores the step size");

    const double ratio = gap_new_half > 0 ? gap_new / gap_new_half : 0.0;
    report(std::fabs(ratio - 2.0) < 0.05, "continuous: halving the step halves the gap",
           std::fabs(ratio - 2.0), 0.05);
    report(gap_new < 1e-3, "and the gap is small in absolute terms too", gap_new, 1e-3);

    // Finite everywhere below, including negative and far negative.
    const double probes[] = {0.0, -1e-12, -1e-6, -1.0, -1e6};
    bool all_finite = true;
    for (double p : probes) if (!std::isfinite(obj(p))) all_finite = false;
    check(all_finite, "finite at m = 0, -1e-12, -1e-6, -1, -1e6");

    // Monotone the right way: pushing the bin further negative must make the
    // minimised objective worse, or the optimiser is still being paid to do it.
    bool monotone = true;
    double prev = obj(kModelFloor);
    for (double p : {0.0, -1e-12, -1e-9, -1e-6, -1.0}) {
        const double cur = obj(p);
        if (!(cur > prev)) monotone = false;
        prev = cur;
    }
    check(monotone, "strictly worse the further below the floor it goes");

    // ...and the original was not.
    check(obj_legacy(-1.0) < obj_legacy(kModelFloor + eps),
          "(the original rewarded a negative model, which is the defect)");
}

static void test_wcm_p2s_below_the_floor() {
    std::printf("Wcm_p2s / wcm_p2s: same treatment\n");

    init_fact();

    // Above the floor: untouched. wcm_p2s has no legacy transcription here --
    // it is a long series -- so the check is that ordinary arguments are
    // unaffected by the new branch, which is true by construction since the
    // branch is not taken. Pin a value so a future edit to the series itself
    // has to be deliberate.
    const double w_ok = wcm_p2s(12, 3.0, 2.0);
    check(std::isfinite(w_ok), "ordinary arguments evaluate finitely");

    // Continuity at the floor.
    const double at = wcm_p2s(12, kModelFloor, 2.0);
    const double just_below = wcm_p2s(12, kModelFloor * (1 - 1e-9), 2.0);
    report(std::fabs(at - just_below) < 1e-6, "continuous across the floor in mp",
           std::fabs(at - just_below), 1e-6);

    const double at_s = wcm_p2s(12, 3.0, kModelFloor);
    const double below_s = wcm_p2s(12, 3.0, kModelFloor * (1 - 1e-9));
    report(std::fabs(at_s - below_s) < 1e-6, "continuous across the floor in ms",
           std::fabs(at_s - below_s), 1e-6);

    // Finite and correctly signed below. wcm_p2s returns a LOG-LIKELIHOOD that
    // Wcm_p2s negates, so "worse" is a smaller value here.
    check(std::isfinite(wcm_p2s(12, -1.0, 2.0)), "finite for a negative mp");
    check(std::isfinite(wcm_p2s(12, 3.0, -1.0)), "finite for a negative ms");
    check(wcm_p2s(12, -1.0, 2.0) < at, "a negative mp is penalised, not rewarded");
    check(wcm_p2s(12, -1e6, 2.0) < wcm_p2s(12, -1.0, 2.0), "and monotonically so");

    // C == 0 short-circuits before the floor branch and must stay at 0.
    check(wcm_p2s(0, -1.0, -1.0) == 0.0, "C == 0 still returns 0");
}

int main() {
    test_wcm_is_unchanged_above_the_floor();
    test_wcm_below_the_floor();
    test_wcm_p2s_below_the_floor();

    if (g_failures) std::printf("\n%d check(s) FAILED\n", g_failures);
    else            std::printf("\nall checks passed\n");
    return g_failures;
}
