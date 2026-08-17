// Drives the FRET_burstML MEX likelihood (mlhDiffNTRbkg_MT) from stdin.
// Input: numP numEval numC numS qmax t_th N_th nBursts nPhotons
//        params (numP x numEval, column-major) LUbounds (numP x 2)
//        cumindex (nBursts+1) indexone (nBursts)
//        times[nPhotons] (100 ns units) colours[nPhotons] (1-based)
// Output: numEval values of the negative log-likelihood, one per line.
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
    // frburstdata is (nPh x n) column-major; the source reads column n-3 as time and n-1 as colour
    for (size_t i = 0; i < nPh; ++i) std::cin >> fr[0 * nPh + i];
    for (size_t i = 0; i < nPh; ++i) std::cin >> fr[2 * nPh + i];
    P.pr = p.data(); LU.pr = lu.data(); FR.pr = fr.data(); CI.pr = ci.data(); IO.pr = io.data(); DP.pr = dp.data(); NC.pr = nc.data();
    const mxArray* prhs[7] = {&P, &LU, &FR, &CI, &IO, &DP, &NC};
    mxArray* plhs[1] = {nullptr};
    mexFunction(1, plhs, 7, prhs);
    for (size_t i = 0; i < plhs[0]->m * plhs[0]->n; ++i) std::printf("%.17g\n", plhs[0]->pr[i]);
    return 0;
}
