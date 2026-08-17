// Timing driver for the FRET_burstML MEX likelihood (mlhDiffNTRbkg_MT, Hoffmann et al.),
// built with the shims in test/cpp/burstml_mex_shim. Same stdin layout as
// test/cpp/ab_burstml_mex_driver.cpp plus a trailing repeat count:
//   numP numEval numC numS qmax t_th N_th nBursts nPhotons
//   params (numP x numEval, column-major) LUbounds (numP x 2)
//   cumindex (nBursts+1) indexone (nBursts) times[nPhotons] colours[nPhotons] repeats
// stdout: "TIME <seconds for all numEval evaluations, best of repeats>" then numEval NLL values.
#include <chrono>
#include <cstdio>
#include <iostream>
#include <vector>
#include "mex.h"
void mexFunction(int nlhs, mxArray* plhs[], int nrhs, const mxArray* prhs[]);
int main() {
    size_t numP, numEval, numC, numS, nB, nPh; double qmax, t_th, N_th;
    std::cin >> numP >> numEval >> numC >> numS >> qmax >> t_th >> N_th >> nB >> nPh;
    mxArray P{numP, numEval, nullptr}, LU{numP, 2, nullptr}, FR{nPh, 3, nullptr}, CI{nB + 1, 1, nullptr},
            IO{nB, 1, nullptr}, DP{4, 1, nullptr}, NC{1, 1, nullptr};
    std::vector<double> p(numP * numEval), lu(numP * 2), fr(nPh * 3), ci(nB + 1), io(nB), dp{double(numS), qmax, t_th, N_th}, nc{double(numC)};
    for (auto& v : p) std::cin >> v;
    for (auto& v : lu) std::cin >> v;
    for (auto& v : ci) std::cin >> v;
    for (auto& v : io) std::cin >> v;
    for (size_t i = 0; i < nPh; ++i) std::cin >> fr[0 * nPh + i];
    for (size_t i = 0; i < nPh; ++i) std::cin >> fr[2 * nPh + i];
    unsigned repeats; std::cin >> repeats;
    P.pr = p.data(); LU.pr = lu.data(); FR.pr = fr.data(); CI.pr = ci.data(); IO.pr = io.data(); DP.pr = dp.data(); NC.pr = nc.data();
    const mxArray* prhs[7] = {&P, &LU, &FR, &CI, &IO, &DP, &NC};
    mxArray* plhs[1] = {nullptr};
    double best = 1e300;
    for (unsigned r = 0; r < repeats + 1; ++r) {
        if (plhs[0]) { std::free(plhs[0]->pr); delete plhs[0]; plhs[0] = nullptr; }
        auto t0 = std::chrono::steady_clock::now();
        mexFunction(1, plhs, 7, prhs);
        double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (r > 0 && dt < best) best = dt;
    }
    std::printf("TIME %.9g\n", best);
    for (size_t i = 0; i < plhs[0]->m * plhs[0]->n; ++i) std::printf("%.17g\n", plhs[0]->pr[i]);
    return 0;
}
