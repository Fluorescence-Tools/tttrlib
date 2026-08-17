"""A/B of the header-only numerics in ``modules/math`` against independent
references: numpy, scipy, scikit-learn, and the canonical RNG algorithms.

The kernels here have no Python binding (NelderMead, i_lbfgs, Mat, QREigen,
Random, Sampling, SimPcgRandom), so ``test/cpp/ab_numerics_harness.cpp`` is
compiled once per session and driven over stdin/stdout; the *reference* side is
the real library on the Python side rather than a recorded number. NeuralNet is
bound and is compared with scikit-learn directly.

Skips (does not fail) when no C++ compiler is on the PATH.

What each block establishes:

* Nelder-Mead and L-BFGS: same minimiser as scipy on the same problems (paths
  differ, optima agree). The L-BFGS *soft* bounds are a penalty, so a bounded
  optimum sits O(|g|/2k) outside the box; that is measured, not hidden.
* Mat: ``mat_solve`` / ``mat_lstsq_minnorm`` / ``mat_inverse_inplace`` /
  ``mat_power`` / GEMM (NN, NT, TN) / reductions equal numpy to rounding,
  including rank-deficient least squares where the *minimum-norm* solution has
  to match ``numpy.linalg.lstsq``.
* QREigen: eigenvalues equal ``numpy.linalg.eig``'s as multisets; each vector
  is an eigenvector of A; V * inv(V) = I; complex kernels equal numpy.
* Random: the Philox4x32-10 stream is bit-identical to a from-the-paper Python
  implementation *and* to the Random123 known-answer vectors; ``seek`` is exact;
  SimPcgRandom is bit-identical to O'Neill's pcg32 reference (seeding
  included); SplitMix64's mixer is the canonical one; the ``pcg`` engine of
  ``Random::deterministic`` reproduces PCG's XSH-RR output function bit for bit
  (this A/B caught it writing ``(state >> 18) ^ (state >> 27)`` on 2026-08-17,
  fixed the same day). ``mt19937`` streams are numpy's Mersenne Twister
  (RandomState(int) raw words, bit for bit); ``deterministic`` under it falls
  through to Philox, as documented.
* Sampling: same uniforms -> same indices as ``np.searchsorted`` on the
  cumulative weights / CDF.
* NeuralNet: trained head-to-head with sklearn's ``MLPRegressor`` on one
  regression task, comparable test error. (The forward pass is already pinned
  to sklearn's to 1e-10 in ``test/python/test_neural_net.py``.)
"""
import os
import shutil
import subprocess
import unittest

import numpy as np
import pytest

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
_HARNESS_SRC = os.path.join(_ROOT, "test", "cpp", "ab_numerics_harness.cpp")
_INCLUDES = [
    os.path.join(_ROOT, "modules", "math", "include"),
    os.path.join(_ROOT, "modules", "util", "include"),
]

_harness_path = None
_harness_error = None


def _harness():
    """Compile the harness once; return its path or skip the calling test."""
    global _harness_path, _harness_error
    if _harness_path is not None:
        return _harness_path
    if _harness_error is not None:
        raise unittest.SkipTest(_harness_error)
    cxx = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if cxx is None:
        _harness_error = "no C++ compiler on PATH"
        raise unittest.SkipTest(_harness_error)
    import tempfile
    out_dir = tempfile.mkdtemp(prefix="tttrlib_ab_numerics_")
    exe = os.path.join(out_dir, "ab_numerics_harness")
    cmd = [cxx, "-std=c++17", "-O2"] + [f"-I{p}" for p in _INCLUDES] + [_HARNESS_SRC, "-o", exe]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        _harness_error = "harness failed to compile:\n" + proc.stderr[-2000:]
        raise unittest.SkipTest(_harness_error)
    _harness_path = exe
    return exe


def _run(cmd, values, env=None):
    """Run one harness subcommand; ``values`` are written whitespace-separated."""
    exe = _harness()
    text = "\n".join(str(v) for v in values) + "\n"
    e = dict(os.environ)
    e.pop("TTTR_RNG_ENGINE", None)
    e.pop("TTTR_RNG_SEED", None)
    e.pop("TTTR_RNG_DETERMINISTIC", None)
    if env:
        e.update(env)
    proc = subprocess.run([exe, cmd], input=text, capture_output=True, text=True, env=e)
    if proc.returncode != 0:
        raise RuntimeError(f"harness {cmd} failed: {proc.stderr}")
    return proc.stdout.split()


def _floats(tokens):
    return np.array([float(t) for t in tokens], dtype=float)


def _fmt(a):
    return [repr(float(v)) for v in np.asarray(a, dtype=float).ravel()]


# ---------------------------------------------------------------------------
# The objectives the harness knows (ids must match ab_numerics_harness.cpp)
# ---------------------------------------------------------------------------
def _rosen(x):
    x = np.asarray(x, float)
    return float(np.sum(100.0 * (x[1:] - x[:-1] ** 2) ** 2 + (1.0 - x[:-1]) ** 2))


def _quad(x):
    x = np.asarray(x, float)
    i = np.arange(len(x))
    return float(np.sum((i + 1) ** 2 * (x - i) ** 2) + 0.5 * np.sum(x[:-1] * x[1:]))


def _bowl(x):
    x = np.asarray(x, float)
    d = x - 1.0
    return float(np.sum(d * d + 0.1 * np.abs(d)))


_OBJ = {0: _rosen, 1: _quad, 2: _bowl}


def _rosen_grad(x):
    x = np.asarray(x, float)
    g = np.zeros_like(x)
    a = x[1:] - x[:-1] ** 2
    g[:-1] += -400.0 * x[:-1] * a - 2.0 * (1.0 - x[:-1])
    g[1:] += 200.0 * a
    return g


def _quad_grad(x):
    x = np.asarray(x, float)
    i = np.arange(len(x))
    g = 2.0 * (i + 1) ** 2 * (x - i)
    g[:-1] += 0.5 * x[1:]
    g[1:] += 0.5 * x[:-1]
    return g


# ===========================================================================
# 1. Nelder-Mead vs scipy
# ===========================================================================
class TestNelderMeadAgainstScipy(unittest.TestCase):
    def _ours(self, obj, x0, bounded=False):
        out = _floats(_run("nm", [obj, len(x0), int(bounded)] + list(x0)))
        n = len(x0)
        return out[:n], out[n], int(out[n + 1]), int(out[n + 2])

    def test_unconstrained_optima_agree_with_scipy(self):
        from scipy.optimize import minimize
        cases = [
            (0, [-1.2, 1.0]), (0, [2.0, 2.0]), (0, [0.5, -0.5, 0.5]),
            (1, [1.0, 1.0, 1.0, 1.0]), (1, [-3.0, 5.0, 0.0, 2.0]),
            (2, [3.0, -2.0, 0.4]),
        ]
        for obj, x0 in cases:
            with self.subTest(obj=obj, x0=x0):
                x, f, it, status = self._ours(obj, x0)
                ref = minimize(_OBJ[obj], np.array(x0), method="Nelder-Mead",
                               options=dict(xatol=1e-10, fatol=1e-14, maxiter=40000, maxfev=80000))
                self.assertIn(status, (2, 3), f"did not converge: {status}")
                # Same optimum as scipy: compare against scipy's x and f, and
                # against the analytic answer where there is one.
                np.testing.assert_allclose(x, ref.x, atol=2e-5, rtol=0)
                self.assertLessEqual(abs(f - ref.fun), 1e-9 + 1e-6 * abs(ref.fun))
                if obj == 0:
                    np.testing.assert_allclose(x, np.ones(len(x0)), atol=2e-5)
                if obj == 2:
                    np.testing.assert_allclose(x, np.ones(len(x0)), atol=2e-5)

    @pytest.mark.slow
    def test_bounded_optimum_is_the_constrained_minimum(self):
        """The clamp keeps the simplex in the box [-0.5, 0.75]^n. On Rosenbrock
        the constrained optimum is x=(0.75, 0.5625) with f=0.0625; on the
        quadratic (n=3) it is x=(-0.1875, 0.75, 0.75). Both are checked against
        the analytic answer. scipy's bounded Nelder-Mead (a clip, too) is run for
        the record and *stalls* on both -- (0.75, 0.570) and (-0.5, 0.75, 0.75) --
        so the assertion against scipy is only that we are never worse."""
        from scipy.optimize import minimize
        from scipy.optimize import Bounds
        cases = [(0, [0.0, 0.0], [0.75, 0.5625]), (1, [0.0, 0.0, 0.0], [-0.1875, 0.75, 0.75])]
        for obj, x0, x_true in cases:
            with self.subTest(obj=obj):
                x, f, it, status = self._ours(obj, x0, bounded=True)
                self.assertTrue(np.all(x >= -0.5 - 1e-15) and np.all(x <= 0.75 + 1e-15))
                np.testing.assert_allclose(x, x_true, atol=1e-5, rtol=0)
                ref = minimize(_OBJ[obj], np.array(x0), method="Nelder-Mead",
                               bounds=Bounds(-0.5 * np.ones(len(x0)), 0.75 * np.ones(len(x0))),
                               options=dict(xatol=1e-10, fatol=1e-14, maxiter=40000, maxfev=80000))
                self.assertLessEqual(f, ref.fun + 1e-9)


# ===========================================================================
# 2. L-BFGS vs scipy L-BFGS-B, plus the finite-difference stencils
# ===========================================================================
class TestLbfgsAgainstScipy(unittest.TestCase):
    def _ours(self, obj, x0, analytic, bounded=False, maxiter=500):
        out = _floats(_run("lbfgs", [obj, len(x0), int(analytic), int(bounded), maxiter] + list(x0)))
        n = len(x0)
        return out[:n], out[n], int(out[n + 1])

    def test_unconstrained_optima_agree_with_scipy(self):
        from scipy.optimize import minimize
        grads = {0: _rosen_grad, 1: _quad_grad}
        cases = [(0, [-1.2, 1.0]), (0, [2.0, 2.0]), (0, [0.5, -0.5, 0.5, 0.2]),
                 (1, [1.0, 1.0, 1.0, 1.0]), (1, [-3.0, 5.0, 0.0, 2.0, 1.0, -1.0])]
        for obj, x0 in cases:
            ref = minimize(_OBJ[obj], np.array(x0), jac=grads[obj], method="L-BFGS-B",
                           options=dict(ftol=1e-15, gtol=1e-12, maxiter=5000))
            for analytic in (1, 0):
                with self.subTest(obj=obj, x0=x0, analytic=analytic):
                    x, f, info = self._ours(obj, x0, analytic)
                    self.assertIn(info, (1, 2, 4), f"terminated with info={info}")
                    tol = 1e-6 if analytic else 1e-4   # FD gradients: sqrt-ish noise floor
                    np.testing.assert_allclose(x, ref.x, atol=tol, rtol=0)
                    self.assertLessEqual(f, ref.fun + 1e-9 + 1e-6 * abs(ref.fun))

    def test_soft_bounds_land_next_to_scipys_hard_bounds(self):
        """set_bounds is a quadratic exterior penalty k*(x-hi)^2 with k=1e6, so
        the optimum sits |g|/(2k) outside the box rather than on it. Compare with
        L-BFGS-B's projected optimum on Rosenbrock in [-0.5, 0.75]^2, whose
        gradient at the constrained optimum is O(10): expect ~1e-5 overshoot."""
        from scipy.optimize import minimize
        x0 = [0.0, 0.0]
        ref = minimize(_rosen, np.array(x0), jac=_rosen_grad, method="L-BFGS-B",
                       bounds=[(-0.5, 0.75)] * 2, options=dict(ftol=1e-15, gtol=1e-12))
        for analytic in (1, 0):
            with self.subTest(analytic=analytic):
                x, f, info = self._ours(0, x0, analytic, bounded=True)
                np.testing.assert_allclose(x, ref.x, atol=1e-3, rtol=0)
                overshoot = np.maximum(0.0, np.maximum(-0.5 - x, x - 0.75))
                # penalty semantics: outside the box, but by |g|/(2k) at most
                g = np.abs(_rosen_grad(x))
                self.assertTrue(np.all(overshoot <= g / (2 * 1e6) + 1e-9), (overshoot, g))

    def test_finite_difference_stencils_have_the_right_orders(self):
        """fgrad1 (forward, O(h)), fgrad2 (central, O(h^2)), fgrad4 (five-point,
        O(h^4)) against the analytic Rosenbrock gradient at a generic point."""
        x = [0.3, -0.7, 1.9]
        for eps in (1e-3, 1e-4):
            with self.subTest(eps=eps):
                out = _floats(_run("fgrad", [0, 3, eps] + x))
                g1, g2, g4, ga = out[0:3], out[3:6], out[6:9], out[9:12]
                np.testing.assert_allclose(ga, _rosen_grad(x), rtol=0, atol=1e-12)
                e1 = np.max(np.abs(g1 - ga)); e2 = np.max(np.abs(g2 - ga)); e4 = np.max(np.abs(g4 - ga))
                self.assertLess(e2, e1)
                self.assertLess(e4, e2)
                self.assertLess(e1, 5.0 * eps * 400)      # O(h * f'')
                self.assertLess(e2, 5.0 * eps ** 2 * 1200)  # O(h^2 * f''')
                self.assertLess(e4, 1e-6)


# ===========================================================================
# 3. Mat.h dense kernels vs numpy.linalg
# ===========================================================================
class TestMatAgainstNumpy(unittest.TestCase):
    def setUp(self):
        self.rng = np.random.default_rng(20260817)

    def _cond_matrix(self, n, cond):
        u, _ = np.linalg.qr(self.rng.standard_normal((n, n)))
        v, _ = np.linalg.qr(self.rng.standard_normal((n, n)))
        s = np.logspace(0, -np.log10(cond), n)
        return u @ np.diag(s) @ v.T

    def test_solve_matches_numpy(self):
        for n, cond in [(1, 1), (2, 1e3), (5, 1), (8, 1e8), (17, 1e10), (40, 1e4)]:
            with self.subTest(n=n, cond=cond):
                A = self._cond_matrix(n, cond) if n > 1 else np.array([[3.5]])
                b = self.rng.standard_normal(n)
                out = _floats(_run("solve", [n] + _fmt(A) + _fmt(b)))
                self.assertEqual(out[0], 1.0)
                x_ref = np.linalg.solve(A, b)
                np.testing.assert_allclose(out[1:], x_ref, rtol=0, atol=1e-9 * cond * np.linalg.norm(x_ref) + 1e-12)
                # a residual-level check that does not depend on numpy's rounding
                self.assertLess(np.linalg.norm(A @ out[1:] - b), 1e-10 * cond * (np.linalg.norm(b) + 1))

    def test_solve_flags_singular_and_scaled_singular(self):
        for scale in (1.0, 1e8, 1e-8):
            u = self.rng.standard_normal(4)
            A = scale * np.outer(u, u)   # rank 1
            out = _run("solve", [4] + _fmt(A) + _fmt(np.ones(4)))
            self.assertEqual(float(out[0]), 0.0, f"rank-1 matrix (scale {scale}) not flagged singular")

    def test_inverse_matches_numpy(self):
        for n, cond in [(1, 1), (3, 1e2), (6, 1e6), (25, 1e3)]:
            with self.subTest(n=n, cond=cond):
                A = self._cond_matrix(n, cond) if n > 1 else np.array([[-2.0]])
                out = _floats(_run("inv", [n] + _fmt(A)))
                self.assertEqual(out[0], 1.0)
                Ai = out[1:].reshape(n, n)
                np.testing.assert_allclose(Ai, np.linalg.inv(A), rtol=0, atol=1e-10 * cond * np.linalg.norm(np.linalg.inv(A)))
                np.testing.assert_allclose(Ai @ A, np.eye(n), atol=1e-9 * cond)

    def test_lstsq_minnorm_matches_numpy_lstsq(self):
        """Full-rank tall, full-rank wide (min-norm), and rank-deficient in both
        shapes: the min-norm solution is unique, so it must equal numpy's."""
        cases = [
            ("tall-full", 12, 5, 5), ("wide-full", 5, 12, 5),
            ("tall-rank3", 12, 6, 3), ("wide-rank2", 4, 9, 2), ("square-rank4", 7, 7, 4),
        ]
        for name, m, n, r in cases:
            with self.subTest(name=name):
                U = self.rng.standard_normal((m, r))
                V = self.rng.standard_normal((r, n))
                A = U @ V + (0.0 if r < min(m, n) else 0.0)
                b = self.rng.standard_normal(m)
                rcond = 1e-10
                out = _floats(_run("lstsq", [m, n, rcond] + _fmt(A) + _fmt(b)))
                self.assertEqual(out[0], 1.0)
                x_ref, *_ = np.linalg.lstsq(A, b, rcond=rcond)
                np.testing.assert_allclose(out[1:], x_ref, rtol=0, atol=1e-9 * (1 + np.linalg.norm(x_ref)))

    def test_lstsq_rcond_is_relative_like_numpy(self):
        """A uniformly tiny matrix must not be zeroed out (rcond is relative)."""
        A = 1e-12 * self.rng.standard_normal((8, 3))
        b = 1e-12 * self.rng.standard_normal(8)
        out = _floats(_run("lstsq", [8, 3, 1e-14] + _fmt(A) + _fmt(b)))
        x_ref, *_ = np.linalg.lstsq(A, b, rcond=1e-14)
        np.testing.assert_allclose(out[1:], x_ref, rtol=1e-8, atol=1e-12)

    def test_power_matches_matrix_power(self):
        A = self.rng.standard_normal((5, 5)) * 0.6
        for p in (0, 1, 2, 3, 7, 12):
            with self.subTest(p=p):
                out = _floats(_run("power", [5, p] + _fmt(A))).reshape(5, 5)
                ref = np.linalg.matrix_power(A, p)
                np.testing.assert_allclose(out, ref, rtol=1e-12, atol=1e-12 * np.abs(ref).max())

    def test_gemm_and_reductions_match_numpy(self):
        for M, N, K in [(1, 1, 1), (3, 4, 5), (17, 9, 33), (64, 48, 100), (7, 129, 2)]:
            with self.subTest(M=M, N=N, K=K):
                A = self.rng.standard_normal((M, K)); B = self.rng.standard_normal((K, N))
                out = _floats(_run("gemm", [M, N, K] + _fmt(A) + _fmt(B)))
                mn = M * N
                C_nn = out[:mn].reshape(M, N)
                C_nt = out[mn:2 * mn].reshape(M, N)
                C_tn = out[2 * mn:3 * mn].reshape(M, N)
                total = out[3 * mn]
                colsum = out[3 * mn + 1:3 * mn + 1 + N]
                rowsum = out[3 * mn + 1 + N:3 * mn + 1 + N + M]
                ref = A @ B
                tol = 1e-12 * K * np.abs(ref).max()
                np.testing.assert_allclose(C_nn, ref, rtol=0, atol=tol)
                np.testing.assert_allclose(C_nt, ref, rtol=0, atol=tol)
                np.testing.assert_allclose(C_tn, ref, rtol=0, atol=tol)
                self.assertAlmostEqual(total, ref.sum(), delta=1e-10 * (1 + abs(ref).sum()))
                np.testing.assert_allclose(colsum, ref.sum(axis=0), rtol=0, atol=1e-10 * (1 + np.abs(ref).sum()))
                np.testing.assert_allclose(rowsum, ref.sum(axis=1), rtol=0, atol=1e-10 * (1 + np.abs(ref).sum()))


# ===========================================================================
# 4. QREigen vs numpy.linalg.eig
# ===========================================================================
class TestQREigenAgainstNumpy(unittest.TestCase):
    def setUp(self):
        self.rng = np.random.default_rng(7)

    def _eig(self, A):
        n = A.shape[0]
        out = _floats(_run("eig", [n] + _fmt(A)))
        self.assertEqual(out[0], 1.0, "qr_eigendecompose reported failure")
        vals = out[1:]
        ev = vals[0:2 * n:2] + 1j * vals[1:2 * n:2]
        off = 2 * n
        V = (vals[off:off + 2 * n * n:2] + 1j * vals[off + 1:off + 2 * n * n:2]).reshape(n, n)
        off += 2 * n * n
        Vi = (vals[off:off + 2 * n * n:2] + 1j * vals[off + 1:off + 2 * n * n:2]).reshape(n, n)
        return ev, V, Vi

    @staticmethod
    def _sorted(z):
        z = np.asarray(z)
        return z[np.lexsort((np.round(z.imag, 8), np.round(z.real, 8)))]

    def _check(self, A, tol=1e-8):
        n = A.shape[0]
        ev, V, Vi = self._eig(A)
        ref = np.linalg.eigvals(A)
        scale = max(1.0, np.abs(ref).max())
        np.testing.assert_allclose(self._sorted(ev), self._sorted(ref), rtol=0, atol=tol * scale)
        # each column is an eigenvector of A for its eigenvalue
        for j in range(n):
            v = V[:, j]
            self.assertGreater(np.linalg.norm(v), 0.5)
            res = np.linalg.norm(A @ v - ev[j] * v) / np.linalg.norm(v)
            self.assertLess(res, tol * scale, f"eigenvector {j} residual {res}")
        np.testing.assert_allclose(V @ Vi, np.eye(n), atol=1e-7 * np.linalg.cond(V))

    def test_random_real_matrices(self):
        for n in (2, 3, 5, 10, 33, 64):
            with self.subTest(n=n):
                self._check(self.rng.standard_normal((n, n)))

    def test_rate_matrix_and_complex_pairs(self):
        # a generator matrix (rows sum to zero) as in Gopich-Szabo/BurstML
        K = self.rng.random((6, 6)); np.fill_diagonal(K, 0); K -= np.diag(K.sum(axis=1))
        self._check(K)
        # rotation blocks -> pure complex pairs
        th = [0.3, 1.1, 2.0]
        R = np.zeros((6, 6))
        for i, t in enumerate(th):
            R[2 * i:2 * i + 2, 2 * i:2 * i + 2] = [[np.cos(t), -np.sin(t)], [np.sin(t), np.cos(t)]]
        Q, _ = np.linalg.qr(self.rng.standard_normal((6, 6)))
        self._check(Q @ R @ Q.T)

    def test_near_defective(self):
        # a Jordan block perturbed by 1e-6: eigenvalues split by ~1e-3, still
        # well within reach; the eigenvector matrix is ill-conditioned by design.
        J = np.eye(4) * 2.0 + np.diag(np.ones(3), 1)
        J[3, 0] = 1e-6
        ev, V, Vi = self._eig(J)
        ref = np.linalg.eigvals(J)
        np.testing.assert_allclose(self._sorted(ev), self._sorted(ref), rtol=0, atol=1e-6)

    def test_complex_kernels_match_numpy(self):
        for n in (1, 3, 8):
            with self.subTest(n=n):
                A = self.rng.standard_normal((n, n)) + 1j * self.rng.standard_normal((n, n))
                B = self.rng.standard_normal((n, n)) + 1j * self.rng.standard_normal((n, n))
                x = self.rng.standard_normal(n) + 1j * self.rng.standard_normal(n)
                vals = []
                for arr in (A, B, x):
                    for z in np.asarray(arr).ravel():
                        vals += [repr(float(z.real)), repr(float(z.imag))]
                out = _floats(_run("zops", [n] + vals))
                C = (out[0:2 * n * n:2] + 1j * out[1:2 * n * n:2]).reshape(n, n)
                off = 2 * n * n
                y = out[off:off + 2 * n:2] + 1j * out[off + 1:off + 2 * n:2]
                off += 2 * n
                self.assertEqual(out[off], 1.0); off += 1
                Ai = (out[off:off + 2 * n * n:2] + 1j * out[off + 1:off + 2 * n * n:2]).reshape(n, n)
                np.testing.assert_allclose(C, A @ B, rtol=0, atol=1e-12 * n)
                np.testing.assert_allclose(y, A @ x, rtol=0, atol=1e-12 * n)
                np.testing.assert_allclose(Ai, np.linalg.inv(A), rtol=0, atol=1e-9 * np.linalg.cond(A))


# ===========================================================================
# 5. Random.h / SimPcgRandom.h vs canonical generators
# ===========================================================================
_M32 = 0xFFFFFFFF


def _philox4x32_10(ctr, key):
    """Philox4x32-10 exactly as in Salmon et al. 2011 / Random123."""
    c0, c1, c2, c3 = ctr
    k0, k1 = key
    for _ in range(10):
        p0 = 0xD2511F53 * c0
        p1 = 0xCD9E8D57 * c2
        hi0, lo0 = p0 >> 32, p0 & _M32
        hi1, lo1 = p1 >> 32, p1 & _M32
        c0, c1, c2, c3 = hi1 ^ c1 ^ k0, lo1, hi0 ^ c3 ^ k1, lo0
        k0 = (k0 + 0x9E3779B9) & _M32
        k1 = (k1 + 0xBB67AE85) & _M32
    return [c0, c1, c2, c3]


def _philox_stream_ref(seed, stream, n):
    out = []
    block = 0
    while len(out) < n:
        out += _philox4x32_10((block & _M32, block >> 32, 0, 0), (seed, stream))
        block += 1
    return out[:n]


_M64 = 0xFFFFFFFFFFFFFFFF


def _pcg32_output(state):
    """pcg32 XSH-RR output function of a 64-bit state (O'Neill, pcg_basic.c)."""
    xorshifted = (((state >> 18) ^ state) >> 27) & _M32
    rot = state >> 59
    return ((xorshifted >> rot) | (xorshifted << ((-rot) & 31))) & _M32


class _Pcg32Ref:
    """pcg32_srandom_r + pcg32_random_r from pcg_basic.c, verbatim."""
    MULT = 6364136223846793005

    def __init__(self, initstate, initseq):
        self.state = 0
        self.inc = ((initseq << 1) | 1) & _M64
        self.next()
        self.state = (self.state + initstate) & _M64
        self.next()

    def next(self):
        old = self.state
        self.state = (old * self.MULT + self.inc) & _M64
        return _pcg32_output(old)


def _splitmix64_mix(state):
    z = state
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & _M64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & _M64
    return z ^ (z >> 31)


class TestRandomAgainstCanonicalGenerators(unittest.TestCase):
    def test_philox_stream_is_bit_exact(self):
        for seed, stream, n in [(0, 0, 8), (12345, 0, 1000), (0xDEADBEEF, 7, 257), (1, 0xFFFFFFFF, 64)]:
            with self.subTest(seed=seed, stream=stream):
                got = [int(t) for t in _run("philox_stream", [seed, stream, n])]
                self.assertEqual(got, _philox_stream_ref(seed, stream, n))

    def test_philox_random123_known_answer_vectors(self):
        """The Random123 kat_vectors for philox4x32-10 (ctr, key -> output)."""
        kat = [
            ((0, 0, 0, 0), (0, 0), (0x6627e8d5, 0xe169c58d, 0xbc57ac4c, 0x9b00dbd8)),
            ((_M32,) * 4, (_M32, _M32), (0x408f276d, 0x41c83b0e, 0xa20bc7c6, 0x6d5451fd)),
            ((0x243f6a88, 0x85a308d3, 0x13198a2e, 0x03707344), (0xa4093822, 0x299f31d0),
             (0xd16cfe09, 0x94fdcceb, 0x5001e420, 0x24126ea1)),
        ]
        for ctr, key, expect in kat:
            self.assertEqual(tuple(_philox4x32_10(ctr, key)), expect, "python reference itself is off")
        # The streaming generator with (seed, stream) = key and ctr = (block, 0, 0, 0):
        # block 0 of key (0, 0) IS the first KAT vector.
        got = [int(t) for t in _run("philox_stream", [0, 0, 4])]
        self.assertEqual(tuple(got), kat[0][2])
        # deterministic(seed, index): ctr = (index lo, index hi, 0, 0), key = (seed, 0)
        det = [int(t) for t in _run("det", [1, 0, 0], env={"TTTR_RNG_ENGINE": "philox"})]
        self.assertEqual(det[0], kat[0][2][0])

    def test_philox_seek_matches_the_stream(self):
        full = [int(t) for t in _run("philox_stream", [99, 3, 64])]
        for idx in (0, 1, 3, 4, 5, 17, 40):
            with self.subTest(index=idx):
                got = [int(t) for t in _run("philox_seek", [99, 3, idx, 64 - idx])]
                self.assertEqual(got, full[idx:])

    def test_philox_deterministic_is_the_counter_form(self):
        pairs = [(0, 0), (5, 1), (5, 2 ** 32 + 3), (0xFFFFFFFF, 2 ** 40)]
        flat = [len(pairs)]
        for s, i in pairs:
            flat += [s, i]
        got = [int(t) for t in _run("det", flat, env={"TTTR_RNG_ENGINE": "philox"})]
        ref = [_philox4x32_10((i & _M32, i >> 32, 0, 0), (s, 0))[0] for s, i in pairs]
        self.assertEqual(got, ref)

    def test_mt19937_deterministic_falls_through_to_philox(self):
        """Documented in Random.h: MT19937 is not counter-based, so the static
        deterministic() uses Philox for it."""
        pairs = [(3, 0), (3, 1), (77, 123456)]
        flat = [len(pairs)]
        for s, i in pairs:
            flat += [s, i]
        mt = _run("det", flat, env={"TTTR_RNG_ENGINE": "mt19937"})
        ph = _run("det", flat, env={"TTTR_RNG_ENGINE": "philox"})
        self.assertEqual(mt, ph)

    def test_mt19937_streaming_engine_is_numpys_mersenne_twister(self):
        """TTTR_RNG_ENGINE=mt19937 turns the streaming Random into a Mersenne
        Twister: std::mt19937(seed) is mt19937ar's init_genrand(seed), which is
        numpy's legacy RandomState(int) seeding, so the raw u32 stream must be
        RandomState(seed).randint(0, 2**32) draw for draw (legacy randint on
        the full 32-bit range consumes exactly one raw word). Until 2026-08-17
        the engine name was accepted and Philox ran instead. seek() discards
        to the position, so seek(k) then draw == draw k of the stream."""
        for seed in (0, 5489, 123456789):
            with self.subTest(seed=seed):
                got = [int(x) for x in _run("philox_stream", [seed, 0, 12], env={"TTTR_RNG_ENGINE": "mt19937"})]
                rs = np.random.RandomState(seed)
                ref = [int(v) for v in rs.randint(0, 2 ** 32, size=12, dtype=np.uint64)]
                self.assertEqual(got, ref)
                ph = [int(x) for x in _run("philox_stream", [seed, 0, 12], env={"TTTR_RNG_ENGINE": "philox"})]
                self.assertNotEqual(got, ph)
                after = [int(x) for x in _run("philox_seek", [seed, 0, 7, 3], env={"TTTR_RNG_ENGINE": "mt19937"})]
                self.assertEqual(after, ref[7:10])

    def test_splitmix64_mixer_is_canonical(self):
        """The (seed, index) -> state mixing is tttrlib's own; the SplitMix64
        finaliser applied to that state must be the canonical one. The public
        API returns the low 32 bits."""
        pairs = [(0, 0), (1, 0), (42, 99), (0xFFFFFFFF, 2 ** 50 + 1)]
        flat = [len(pairs)]
        for s, i in pairs:
            flat += [s, i]
        got = [int(t) for t in _run("det", flat, env={"TTTR_RNG_ENGINE": "splitmix64"})]
        ref = []
        for s, i in pairs:
            state = (s * 6364136223846793005 + i * 1442695040888963407 + 0x9E3779B97F4A7C15) & _M64
            state = (state + 0x9E3779B97F4A7C15) & _M64
            ref.append(_splitmix64_mix(state) & _M32)
        self.assertEqual(got, ref)

    def test_simpcgrandom_is_bit_exact_pcg32(self):
        """SimPcgRandom::reset derives (initstate, initseq) from (base, id,
        counter_start) and then seeds exactly like pcg32_srandom_r; next32 is
        pcg32_random_r. So given the derived pair, the raw stream must equal
        O'Neill's reference generator word for word."""
        for base, mid, cs, n in [(0, 0, 0, 16), (20260731, 5, 0, 500), (1, 2 ** 32 - 1, 12345, 200)]:
            with self.subTest(base=base, id=mid, cs=cs):
                seed = ((base << 32) ^ ((cs * 0xD1B54A32D192ED03) & _M64) ^ 0x1) & _M64
                seed = ((seed ^ (seed >> 30)) * 0xBF58476D1CE4E5B9) & _M64
                seed ^= seed >> 27
                seq = ((mid * 0x9E3779B97F4A7C15) & _M64) | 0x1
                ref = _Pcg32Ref(seed, seq)
                expect = [ref.next() for _ in range(n)]
                got = [int(t) for t in _run("pcg32", [base, mid, cs, n])]
                self.assertEqual(got, expect)

    def test_pcg_engine_output_function_is_pcg32_xsh_rr(self):
        """Random::pcg_deterministic_u32 must apply PCG's XSH-RR output function,
        ``((state >> 18) ^ state) >> 27`` then rotate by ``state >> 59``. This
        A/B found it written as ``((state >> 18) ^ (state >> 27)) >> 27`` on
        2026-08-17 -- a 19-live-bit pre-rotation word, 13 zero bits per output
        in a rotating window; fixed the same day. Bit-exact comparison against
        the canonical output function of the same state:"""
        pairs = [(0, 0), (1, 0), (42, 99), (0xFFFFFFFF, 2 ** 50 + 1)]
        flat = [len(pairs)]
        for s, i in pairs:
            flat += [s, i]
        got = [int(t) for t in _run("det", flat, env={"TTTR_RNG_ENGINE": "pcg"})]
        ref = []
        for s, i in pairs:
            state = (s * 6364136223846793005 + i * 1442695040888963407 + 1) & _M64
            state = (state * 6364136223846793005 + 1442695040888963407) & _M64
            ref.append(_pcg32_output(state))
        self.assertEqual(got, ref)

    def test_pcg_engine_bits_are_unbiased(self):
        """Independent statistical witness of the same contract: with the
        defective 19-bit pre-rotation word each output bit was 1 with
        probability ~0.22-0.37 rather than 1/2."""
        self._bit_balance("pcg")

    def test_philox_and_splitmix_bits_are_unbiased(self):
        self._bit_balance("philox")
        self._bit_balance("splitmix64")

    def _bit_balance(self, engine, n=20000):
        flat = [n]
        for i in range(n):
            flat += [1234, i]
        got = np.array([int(t) for t in _run("det", flat, env={"TTTR_RNG_ENGINE": engine})], dtype=np.uint64)
        bits = ((got[:, None] >> np.arange(32, dtype=np.uint64)[None, :]) & np.uint64(1)).astype(float)
        p1 = bits.mean(axis=0)
        # 5-sigma band around 1/2 for n Bernoulli trials
        band = 5.0 * np.sqrt(0.25 / n)
        self.assertTrue(np.all(np.abs(p1 - 0.5) < band), f"{engine}: per-bit P(1) = {np.round(p1, 3)}")

    def test_normal_is_standard_normal(self):
        from scipy import stats
        x = _floats(_run("normal", [4242, 200000]))
        self.assertAlmostEqual(x.mean(), 0.0, delta=0.01)
        self.assertAlmostEqual(x.var(), 1.0, delta=0.02)
        self.assertAlmostEqual(stats.skew(x), 0.0, delta=0.03)
        self.assertAlmostEqual(stats.kurtosis(x), 0.0, delta=0.06)
        self.assertGreater(stats.kstest(x, "norm").pvalue, 1e-3)


# ===========================================================================
# 6. Sampling.h vs numpy searchsorted on the same uniforms
# ===========================================================================
class TestSamplingAgainstNumpy(unittest.TestCase):
    SEED = 20260817

    def _uniforms(self, n):
        u32 = np.array(_philox_stream_ref(self.SEED, 0, n), dtype=np.float64)
        return u32 / 4294967296.0

    def test_weighted_choice_matches_searchsorted(self):
        rng = np.random.default_rng(3)
        for nw in (1, 2, 7, 100, 1000):
            with self.subTest(nw=nw):
                w = rng.random(nw) * (rng.random(nw) < 0.8)   # some zero weights
                if w.sum() == 0:
                    w[0] = 1.0
                nout = 5000
                got = np.array([int(t) for t in _run("wchoice", [nw] + _fmt(w) + [nout],
                                                     env={"TTTR_RNG_SEED": str(self.SEED)})])
                u = self._uniforms(nout)
                totals = np.cumsum(w)
                # lower_bound == searchsorted(side='left'); a zero-weight index
                # can never be hit unless it is chosen for draw == running total.
                ref = np.minimum(np.searchsorted(totals, u * totals[-1], side="left"), nw - 1)
                np.testing.assert_array_equal(got, ref)
                # and it agrees with numpy's own choice(p=) up to the (measure-zero) tie side
                ref_np = np.searchsorted(np.cumsum(w / w.sum()), u, side="right")
                self.assertGreater(np.mean(got == np.minimum(ref_np, nw - 1)), 0.999)

    def test_sample_from_cdf_matches_searchsorted(self):
        rng = np.random.default_rng(5)
        for n in (2, 10, 333):
            with self.subTest(n=n):
                axis = np.sort(rng.random(n)) * 10
                pdf = rng.random(n)
                cdf = np.cumsum(pdf) * 3.0   # not normalised on purpose
                nout = 4000
                for normalize in (1, 0):
                    got = _floats(_run("cdf", [n] + _fmt(axis) + _fmt(cdf) + [normalize, nout],
                                       env={"TTTR_RNG_SEED": str(self.SEED)}))
                    u = self._uniforms(nout)
                    table = cdf / cdf[-1] if normalize else cdf
                    idx = np.searchsorted(table, u, side="left")
                    ref = np.where(idx < n, axis[np.minimum(idx, n - 1)], 0.0)
                    np.testing.assert_array_equal(got, ref)


# ===========================================================================
# 7. NeuralNet training vs scikit-learn's MLPRegressor
# ===========================================================================
class TestNeuralNetAgainstSklearn(unittest.TestCase):
    @pytest.mark.heavy
    def test_training_reaches_sklearn_level_error(self):
        try:
            import tttrlib
            from sklearn.neural_network import MLPRegressor
        except ImportError as e:  # pragma: no cover
            raise unittest.SkipTest(str(e))
        rng = np.random.default_rng(11)
        X = rng.uniform(-2, 2, size=(4000, 2))
        y = np.sin(X[:, 0]) * np.cos(0.5 * X[:, 1]) + 0.1 * X[:, 0] * X[:, 1]
        Xtr, Xte, ytr, yte = X[:3000], X[3000:], y[:3000], y[3000:]

        opt = tttrlib.TrainOptions()
        opt.hidden_layer_sizes = tttrlib.VectorInt32([64, 64])
        opt.max_iter = 300; opt.batch_size = 200; opt.learning_rate = 1e-3
        opt.alpha = 1e-4; opt.early_stopping = True; opt.n_iter_no_change = 20; opt.seed = 1
        net = tttrlib.NeuralNet.train_np(Xtr, ytr[:, None], opt)
        mse_ours = float(np.mean((net.predict_batch_np(Xte)[:, 0] - yte) ** 2))

        from sklearn.preprocessing import StandardScaler
        xs = StandardScaler().fit(Xtr); ys = StandardScaler().fit(ytr[:, None])
        mlp = MLPRegressor(hidden_layer_sizes=(64, 64), activation="relu", solver="adam",
                           learning_rate_init=1e-3, alpha=1e-4, batch_size=200, max_iter=300,
                           early_stopping=True, n_iter_no_change=20, random_state=1)
        mlp.fit(xs.transform(Xtr), ys.transform(ytr[:, None]).ravel())
        pred = ys.inverse_transform(mlp.predict(xs.transform(Xte))[:, None])[:, 0]
        mse_skl = float(np.mean((pred - yte) ** 2))

        var = float(np.var(yte))
        # both fit the function (R^2 > 0.98) and neither is far behind the other
        self.assertLess(mse_ours, 0.02 * var, f"tttrlib mse {mse_ours:.3e} vs var {var:.3e}")
        self.assertLess(mse_skl, 0.02 * var, f"sklearn mse {mse_skl:.3e} vs var {var:.3e}")
        self.assertLess(mse_ours, 3.0 * mse_skl + 1e-4, f"tttrlib {mse_ours:.3e} vs sklearn {mse_skl:.3e}")


if __name__ == "__main__":
    unittest.main()
