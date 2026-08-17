"""A/B of Photon Distribution Analysis against PAM's PDA histogram library and
ChiSurf's three-colour physics.

* ``Pda.s1s2`` (2-channel PDA model matrix) against **PAM**
  (Schrimpf et al. 2018, ``functions/PDAFit/histogram_library/PDA_histogram.cpp``
  from ``../chisurf/junk/PAM``): the MEX source is compiled at test time
  through a 15-line ``mex.h`` shim into a stdin/stdout program and driven with
  the same P(F), p_ch1, backgrounds; single species and mixtures (PAM is one
  species per call -- the mixture is the amplitude-weighted sum). Skips when
  the PAM checkout or a compiler is absent.
* The independent NumPy transcription of Antonik et al. 2006 (binomial split,
  Poisson background convolution) already pins ``Pda.s1s2`` to 1e-14 in
  ``test_pda_reference.py`` -- cited, not duplicated -- as does the defining
  nested sum for ``PdaBurstLikelihood`` in ``test_pda_burst_likelihood.py``.
* ``channel_probabilities_3c`` against ChiSurf ``pda3c.physics.channel_probabilities``
  (excitation row x transfer x emission mixing, renormalised); ``transfer_matrix_3c``
  against a NumPy transcription of the competing-acceptor cascade
  E_ij = k_ij / (1 + sum_k k_ik), k = (R0/R)^6, propagated down the cascade,
  and against ChiSurf (the latter is already in ``test_pda3c_core.py``).
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from math import factorial

import numpy as np

import tttrlib

PAM_SRC = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "..", "..", "chisurf", "junk", "PAM", "functions", "PDAFit",
    "histogram_library", "PDA_histogram.cpp",
)

_MEX_SHIM = r"""
#pragma once
#include <cstdlib>
#include <cstdio>
#include <cstddef>
typedef size_t mwSize;
struct mxArray { double* data; size_t n; };
enum { mxDOUBLE_CLASS = 6 };
enum { mxREAL = 0 };
inline double mxGetScalar(const mxArray* a) { return a->data[0]; }
inline double* mxGetPr(const mxArray* a) { return a->data; }
inline void* mxCalloc(size_t n, size_t s) { return calloc(n, s); }
inline mxArray* mxCreateNumericMatrix(mwSize m, mwSize n, int, int) { mxArray* a = new mxArray; a->data = nullptr; a->n = m * n; return a; }
inline void mxSetData(mxArray* a, void* p) { a->data = static_cast<double*>(p); }
inline void mexErrMsgIdAndTxt(const char*, const char* m) { std::fprintf(stderr, "%s\n", m); std::exit(2); }
"""

_MAIN = r"""
#include "mex.h"
#include <vector>
#include <cstdio>
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]);
int main() {
    unsigned Nmax; if (std::scanf("%u", &Nmax) != 1) return 1;
    std::vector<double> pN(Nmax + 1);
    for (unsigned i = 0; i <= Nmax; ++i) std::scanf("%lf", &pN[i]);
    double pG, Bg, Br, use; std::scanf("%lf %lf %lf %lf", &pG, &Bg, &Br, &use);
    double nm = Nmax;
    mxArray aN{&nm, 1}, apN{pN.data(), pN.size()}, apG{&pG, 1}, aBg{&Bg, 1}, aBr{&Br, 1}, aU{&use, 1};
    const mxArray* in[6] = {&aN, &apN, &apG, &aBg, &aBr, &aU};
    mxArray* out[1];
    mexFunction(1, out, 6, in);
    for (size_t i = 0; i < out[0]->n; ++i) std::printf("%.17g\n", out[0]->data[i]);
    return 0;
}
"""

_PAM_EXE = None


def _pam_exe():
    """Compile PAM's PDA_histogram.cpp once; None when it cannot be had."""
    global _PAM_EXE
    if _PAM_EXE is not None:
        return _PAM_EXE or None
    if not os.path.exists(PAM_SRC):
        _PAM_EXE = False
        return None
    cxx = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if cxx is None:
        _PAM_EXE = False
        return None
    d = tempfile.mkdtemp(prefix="pam_pda_")
    with open(os.path.join(d, "mex.h"), "w") as f:
        f.write(_MEX_SHIM)
    with open(os.path.join(d, "matrix.h"), "w") as f:
        f.write('#pragma once\n#include "mex.h"\n')
    with open(os.path.join(d, "main.cpp"), "w") as f:
        f.write(_MAIN)
    exe = os.path.join(d, "pam_pda")
    r = subprocess.run([cxx, "-std=c++17", "-O2", "-I", d, os.path.join(d, "main.cpp"), PAM_SRC, "-o", exe],
                       capture_output=True, text=True)
    if r.returncode != 0:
        _PAM_EXE = False
        return None
    _PAM_EXE = exe
    return exe


def _pam_s1s2(nmax, pF, p_ch1, bg1, bg2):
    exe = _pam_exe()
    inp = f"{nmax}\n" + " ".join(repr(float(x)) for x in pF) + f"\n{p_ch1!r} {bg1!r} {bg2!r} 1\n"
    out = subprocess.run([exe], input=inp, capture_output=True, text=True, check=True).stdout.split()
    return np.array([float(x) for x in out]).reshape(nmax + 1, nmax + 1)


def _poisson_pf(lam, nmax):
    p = np.array([np.exp(-lam) * lam ** i / factorial(i) for i in range(nmax + 1)])
    return p / p.sum()


class TestAgainstPam(unittest.TestCase):

    def setUp(self):
        if _pam_exe() is None:
            self.skipTest("PAM PDA_histogram.cpp not available (needs ../chisurf/junk/PAM and a C++ compiler)")

    def test_single_species(self):
        nmax = 14
        pf = _poisson_pf(5.0, nmax)
        for p1 in (0.05, 0.3, 0.5, 0.95):
            for bg1, bg2 in ((0.0, 0.0), (1.5, 0.8), (3.0, 3.0), (0.0, 2.2)):
                with self.subTest(p_ch1=p1, bg=(bg1, bg2)):
                    pda = tttrlib.Pda(hist2d_nmax=nmax, hist2d_nmin=0,
                                      background_ch1=bg1, background_ch2=bg2, pF=pf.tolist())
                    pda.append(1.0, p1)
                    ref = _pam_s1s2(nmax, pf, p1, bg1, bg2)
                    np.testing.assert_allclose(pda.s1s2, ref, rtol=0, atol=1e-15)

    def test_mixture_is_the_weighted_sum_of_pam_species(self):
        nmax = 16
        pf = _poisson_pf(6.0, nmax)
        amps, probs = [0.5, 0.3, 0.2], [0.2, 0.55, 0.9]
        for bg1, bg2 in ((0.0, 0.0), (2.0, 1.0)):
            with self.subTest(bg=(bg1, bg2)):
                pda = tttrlib.Pda(hist2d_nmax=nmax, hist2d_nmin=0,
                                  background_ch1=bg1, background_ch2=bg2, pF=pf.tolist())
                for a, p in zip(amps, probs):
                    pda.append(a, p)
                ref = sum(a * _pam_s1s2(nmax, pf, p, bg1, bg2) for a, p in zip(amps, probs))
                np.testing.assert_allclose(pda.s1s2, ref, rtol=0, atol=1e-15)

    def test_a_non_poisson_pf(self):
        # a bimodal P(F): PAM takes it as given, so must the library
        nmax = 12
        pf = np.zeros(nmax + 1)
        pf[3] = 0.4; pf[9] = 0.6
        pda = tttrlib.Pda(hist2d_nmax=nmax, hist2d_nmin=0, background_ch1=0.7, background_ch2=0.4, pF=pf.tolist())
        pda.append(1.0, 0.35)
        np.testing.assert_allclose(pda.s1s2, _pam_s1s2(nmax, pf, 0.35, 0.7, 0.4), rtol=0, atol=1e-15)


class TestThreeColourPhysics(unittest.TestCase):

    def _chisurf(self):
        try:
            sys.path.insert(0, "/Users/tpeulen/dev/chisurf")
            from chisurf.core.fluorescence.pda3c import physics
            return physics
        except Exception as exc:  # pragma: no cover
            self.skipTest(f"chisurf pda3c not importable: {exc}")

    @staticmethod
    def _cascade_transfer(dist, r0):
        """Cascade E for K dyes ordered blue -> green -> red: an excited dye i
        transfers to j>i with k_ij = (R0_ij / R_ij)^6 competing against its
        own decay (rate 1); what reaches j cascades on. Row i = fate of an
        excitation on dye i (fraction emitted by each dye)."""
        K = len(r0.shape) and r0.shape[0]
        T = np.zeros((K, K))
        for i in range(K):
            # excitation of dye i propagates forward
            occupancy = np.zeros(K); occupancy[i] = 1.0
            for a in range(i, K):
                if occupancy[a] == 0.0:
                    continue
                k = np.array([(r0[a, b] / dist[a, b]) ** 6 if b > a else 0.0 for b in range(K)])
                denom = 1.0 + k.sum()
                T[i, a] += occupancy[a] / denom          # emitted by a
                for b in range(a + 1, K):
                    occupancy[b] += occupancy[a] * k[b] / denom
        return T

    def test_transfer_matrix_against_the_cascade_formula(self):
        rng = np.random.default_rng(3)
        for trial in range(20):
            with self.subTest(trial=trial):
                d = rng.uniform(25.0, 90.0, size=3)  # d01, d02, d12
                r0 = rng.uniform(40.0, 60.0, size=3)
                D = np.zeros((3, 3)); D[0, 1] = D[1, 0] = d[0]; D[0, 2] = D[2, 0] = d[1]; D[1, 2] = D[2, 1] = d[2]
                R = np.zeros((3, 3)); R[0, 1] = R[1, 0] = r0[0]; R[0, 2] = R[2, 0] = r0[1]; R[1, 2] = R[2, 1] = r0[2]
                got = np.asarray(tttrlib.transfer_matrix_3c(d.tolist(), r0.tolist(), 3)).reshape(3, 3)
                np.testing.assert_allclose(got, self._cascade_transfer(D, R), rtol=1e-12, atol=1e-14)
                np.testing.assert_allclose(got.sum(axis=1), 1.0, atol=1e-12)

    def test_channel_probabilities_against_chisurf(self):
        physics = self._chisurf()
        setup = physics.ThreeColorSetup.from_scalars(r0_bg=47.0, r0_br=52.0, r0_gr=58.0)
        rng = np.random.default_rng(4)
        for trial in range(10):
            with self.subTest(trial=trial):
                d = rng.uniform(30.0, 80.0, size=3)
                ref = physics.channel_probabilities(physics.distances_to_matrix(d, 3), setup, laser=0)[0]
                T = tttrlib.transfer_matrix_3c(d.tolist(), [47.0, 52.0, 58.0], 3)
                got = np.asarray(tttrlib.channel_probabilities_3c(
                    list(T), setup.excitation[0].tolist(), setup.emission.flatten().tolist(),
                    3, setup.emission.shape[1]))
                np.testing.assert_allclose(got, ref, rtol=1e-10, atol=1e-12)
                self.assertAlmostEqual(got.sum(), 1.0, places=12)


if __name__ == "__main__":
    unittest.main()
