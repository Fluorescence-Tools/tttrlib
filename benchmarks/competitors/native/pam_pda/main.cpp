// Timing driver for PAM's PDA_histogram.cpp (Schrimpf et al. 2018).
// stdin: Nmax  pN[Nmax+1]  n_species  (amp pG)*n_species  Bg Br  repeats
// stdout: "TIME <seconds for one mixture, best of repeats>" then the (Nmax+1)^2 mixture matrix.
#include "mex.h"
#include <chrono>
#include <cstdio>
#include <vector>
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);
int main() {
    unsigned Nmax; if (std::scanf("%u", &Nmax) != 1) return 1;
    std::vector<double> pN(Nmax + 1);
    for (unsigned i = 0; i <= Nmax; ++i) std::scanf("%lf", &pN[i]);
    unsigned ns; std::scanf("%u", &ns);
    std::vector<double> amp(ns), pG(ns);
    for (unsigned s = 0; s < ns; ++s) std::scanf("%lf %lf", &amp[s], &pG[s]);
    double Bg, Br; unsigned repeats; std::scanf("%lf %lf %u", &Bg, &Br, &repeats);
    double nm = Nmax, use = 1.0;
    const size_t n2 = (size_t)(Nmax + 1) * (Nmax + 1);
    std::vector<double> mix(n2);
    double best = 1e300;
    for (unsigned r = 0; r < repeats + 1; ++r) {          // first pass is the warm-up
        auto t0 = std::chrono::steady_clock::now();
        std::fill(mix.begin(), mix.end(), 0.0);
        for (unsigned s = 0; s < ns; ++s) {
            mxArray aN{&nm, 1}, apN{pN.data(), pN.size()}, apG{&pG[s], 1}, aBg{&Bg, 1}, aBr{&Br, 1}, aU{&use, 1};
            const mxArray* in[6] = {&aN, &apN, &apG, &aBg, &aBr, &aU};
            mxArray* out[1];
            mexFunction(1, out, 6, in);
            for (size_t i = 0; i < n2; ++i) mix[i] += amp[s] * out[0]->data[i];
            std::free(out[0]->data); delete out[0];
        }
        double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (r > 0 && dt < best) best = dt;
    }
    std::printf("TIME %.9g\n", best);
    for (size_t i = 0; i < n2; ++i) std::printf("%.17g\n", mix[i]);
    return 0;
}
