"""A/B of the probabilistic kernels in `modules/math` against independent
references: `kalman_filter`, the `hmm_*` log-domain lattice, and the MaxEnt
engine (`tcspc_quadpr_bound`, `tcspc_run_mem`, `maxent_invert`).

Every kernel here already has a known-answer or fixture test of its own
(`test_kalman.py`, `test_hmm_lattice.py`, `decayfit/test_maxent_tcspc.py`).
What those cannot say is whether the kernel agrees with an implementation
nobody here wrote. This file says it, three ways per kernel where three exist:

* **A library that is not tttrlib and not ChiSurf.** hmmlearn for the lattice,
  filterpy for the Kalman filter, scipy.optimize for the MaxEnt minimisers.
  hmmlearn and filterpy are not test dependencies, so their answers are
  recorded once by ``gen_math_ab_probabilistic_reference.py`` (inputs stored
  with the outputs) into ``math_ab_probabilistic_reference.npz``; scipy is
  a test dependency and is compared live.
* **A textbook implementation written here from the equations**, in NumPy,
  short enough to read in one sitting -- a second independent arrangement
  that catches a shared convention error between the kernel and the library.
* **ChiSurf's implementation, live**, where the kernel is a port whose
  contract is bit-exactness (the Kalman filter). Skipped when ChiSurf is not
  checked out beside tttrlib.

MaxEnt has no external twin: ChiSurf's ``core/math/optimization/mem.py``
minimises a different functional (its objective value omits the entropy term
its gradient carries, and its ``reg_scale`` comes from a settings file), so it
is not a reference for ``run_mem``. The reference for the MaxEnt kernels is
therefore the *optimisation problem itself*: the returned point must satisfy
the KKT conditions of ``Q(p) = chi2(p) - nu/2 * S(p)`` on ``p >= min_prob``
(checked in NumPy from the header's definitions), and scipy's L-BFGS-B, given
a generous budget, must land on the same point and be unable to improve on it.
That is what "agrees with the reference" means for a convex program.
"""

import importlib.util
import os
import sys
import unittest

import numpy as np

import tttrlib

FIXTURE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..",
                       "data", "reference", "math_ab_probabilistic_reference.npz")

CHISURF_KALMAN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "..", "..", "..", "..", "chisurf", "chisurf",
                              "core", "fluorescence", "burst", "kalman.py")


def _fixture():
    if not os.path.exists(FIXTURE):
        raise unittest.SkipTest("reference fixture not present")
    return np.load(FIXTURE)


def _hmm_case_names(z):
    return sorted({k.split("/")[1] for k in z.files if k.startswith("hmm/")})


def _kalman_case_names(z):
    return sorted({k.split("/")[1] for k in z.files if k.startswith("kalman/")})


# ---------------------------------------------------------------------------
# Kalman filter
# ---------------------------------------------------------------------------

def textbook_kalman(y, x0, P0, Q, dt, r_scale):
    """The filter as the header states it: A = H = I, P_pred = P + Q,
    R = diag(r_scale * max(x, 1e-12) / dt), K = P_pred S^-1, x += K v,
    P = (I - K) P_pred, D = sqrt(v^T S^-1 v). numpy.linalg.inv throughout,
    no closed forms -- a different arrangement of the same arithmetic."""
    T, dim = y.shape
    x = np.array(x0, dtype=float)
    P = np.array(P0, dtype=float)
    I = np.eye(dim)
    xs, Ps, Ds = np.empty((T, dim)), np.empty((T, dim, dim)), np.empty(T)
    for t in range(T):
        P_pred = P + Q
        R = np.diag(r_scale * np.maximum(x, 1e-12) / dt)
        v = y[t] - x
        S = P_pred + R
        S_inv = np.linalg.inv(S)
        K = P_pred @ S_inv
        x = x + K @ v
        P = (I - K) @ P_pred
        xs[t], Ps[t] = x, P
        q = v @ S_inv @ v
        Ds[t] = np.sqrt(q) if q > 0 else 0.0
    return xs, Ps, Ds


def _rel(a, b):
    return float(np.max(np.abs(a - b) / np.maximum(np.abs(b), 1e-300)))


class TestKalmanAgainstTheTextbook(unittest.TestCase):
    """Random Poisson traces in one, two and three channels against the
    NumPy statement of the recursion above. Only the arithmetic differs
    (closed-form vs LAPACK inverse, fused vs plain dot products), so
    agreement at ~1e-12 relative is what a correct port looks like; a wrong
    convention (R from the wrong state, K from P instead of P_pred, the
    Mahalanobis on the wrong innovation) is a 1e-2 event."""

    def _trace(self, rng, dim, T):
        rates = rng.uniform(2e3, 1e5, size=dim)
        jump = rng.uniform(0.3, 3.0, size=dim)
        true = np.where(np.arange(T)[:, None] < T // 2, rates, rates * jump)
        dt = 1e-3
        y = rng.poisson(true * dt).astype(np.float64) / dt
        return y, rates.copy(), np.eye(dim) * 1e6, np.eye(dim) * 100.0, dt

    def test_one_channel_is_deterministic_and_dims_beyond_four_work(self):
        """Regression: the general `K = P_pred S^-1` branch was written for
        dim == 4 (strides of 4, four terms), so dim == 1 read past its
        1-element vectors -- undefined behaviour that usually met zeroed heap
        slack and now and then did not, an intermittent one-channel failure of
        the test below -- and dim >= 5 was refused. Interleaving shapes is what
        made the stale heap show; 20 interleaved repetitions must be identical
        and every dim must match the textbook."""
        rng = np.random.default_rng(23)
        cases = []
        for dim in (1, 2, 3, 4, 5, 6):
            y, x0, P0, Q, dt = self._trace(rng, dim, 120)
            cases.append((dim, y, x0, P0, Q, dt, 1.0))
        first = {}
        for _ in range(20):
            for dim, y, x0, P0, Q, dt, r_scale in cases:
                got = [np.array(g) for g in tttrlib.kalman_filter(y, x0, P0, Q, dt, r_scale)]
                if dim in first:
                    for a, b in zip(got, first[dim]):
                        np.testing.assert_array_equal(a, b)
                else:
                    first[dim] = got
                    ref = textbook_kalman(y, x0, P0, Q, dt, r_scale)
                    for g, r, name in zip(got, ref, ("x", "P", "D")):
                        self.assertLess(_rel(g, r), 1e-11, f"dim={dim} {name}")

    def test_one_two_and_three_channels(self):
        rng = np.random.default_rng(11)
        for dim in (1, 2, 3):
            for _ in range(5):
                y, x0, P0, Q, dt = self._trace(rng, dim, int(rng.integers(50, 500)))
                r_scale = float(rng.uniform(0.5, 2.0))
                got = tttrlib.kalman_filter(y, x0, P0, Q, dt, r_scale)
                ref = textbook_kalman(y, x0, P0, Q, dt, r_scale)
                for g, r, name in zip(got, ref, ("x", "P", "D")):
                    self.assertLess(_rel(g, r), 1e-11, f"dim={dim} {name}")


class TestKalmanAgainstFilterpy(unittest.TestCase):
    """The recorded filterpy answers. filterpy updates the covariance in the
    Joseph form (I-KH) P (I-KH)^T + K R K^T, which is algebraically the same
    matrix and numerically a different one, so this is not a transcription
    check -- see the generator's docstring."""

    def test_recorded_cases(self):
        z = _fixture()
        names = _kalman_case_names(z)
        self.assertGreaterEqual(len(names), 3)
        for n in names:
            g = lambda s: z[f"kalman/{n}/{s}"]
            got = tttrlib.kalman_filter(g("y"), g("x0"), g("P0"), g("Q"),
                                        float(g("dt")), float(g("r_scale")))
            for got_i, key in zip(got, ("x_filt", "P_filt", "D")):
                self.assertLess(_rel(got_i, g(key)), 1e-11, f"{n} {key}")


class TestKalmanAgainstChiSurfLive(unittest.TestCase):
    """The port's contract is bit-exactness with ChiSurf's loop for two
    channels. `test_kalman.py` pins one recorded trace; this runs the two
    side by side on fifty random traces when ChiSurf is checked out beside
    tttrlib, and is a skip otherwise (a fixture cannot age, a live check can
    notice ChiSurf moving)."""

    @classmethod
    def setUpClass(cls):
        if not os.path.exists(CHISURF_KALMAN):
            raise unittest.SkipTest("chisurf not checked out beside tttrlib")
        spec = importlib.util.spec_from_file_location("_cs_kalman_ab", CHISURF_KALMAN)
        mod = importlib.util.module_from_spec(spec)
        sys.modules["_cs_kalman_ab"] = mod
        try:
            spec.loader.exec_module(mod)
        except Exception as e:  # pragma: no cover - environment dependent
            raise unittest.SkipTest(f"chisurf kalman.py not importable: {e}")
        if not hasattr(mod, "_kalman_filter_loop"):
            raise unittest.SkipTest("chisurf no longer has _kalman_filter_loop")
        cls.loop = staticmethod(mod._kalman_filter_loop)

    def test_fifty_random_two_channel_traces_are_bit_identical(self):
        rng = np.random.default_rng(1)
        for _ in range(50):
            T = int(rng.integers(50, 600))
            rates = rng.uniform(1e3, 1e5, 2)
            dt = 1e-3
            y = rng.poisson(rates * dt, size=(T, 2)).astype(float) / dt
            x0 = rates + rng.normal(0, 100, 2)
            P0 = np.eye(2) * rng.uniform(1e3, 1e7)
            Q = np.eye(2) * rng.uniform(1, 500)
            r_scale = float(rng.uniform(0.5, 2))
            got = tttrlib.kalman_filter(y, x0, P0, Q, dt, r_scale)
            ref = self.loop(y, x0.copy(), P0.copy(), Q.copy(), dt, r_scale)
            for g, r in zip(got, ref):
                np.testing.assert_array_equal(g, r)


# ---------------------------------------------------------------------------
# HMM lattice
# ---------------------------------------------------------------------------

def _lse(a, axis=None):
    from scipy.special import logsumexp
    with np.errstate(divide="ignore", invalid="ignore"):
        return logsumexp(a, axis=axis)


def textbook_forward_backward(log_start, log_trans, log_frame):
    """Forward-backward straight from the recursions, one logsumexp per
    cell, plus posteriors and the summed xi. Returns
    (log_prob, fwd, bwd, posteriors, xi_sum)."""
    T, K = log_frame.shape
    fwd = np.empty((T, K))
    fwd[0] = log_start + log_frame[0]
    for t in range(1, T):
        fwd[t] = _lse(fwd[t - 1][:, None] + log_trans, axis=0) + log_frame[t]
    log_prob = _lse(fwd[-1])
    bwd = np.zeros((T, K))
    for t in range(T - 2, -1, -1):
        bwd[t] = _lse(log_trans + (log_frame[t + 1] + bwd[t + 1])[None, :], axis=1)
    post = np.exp(fwd + bwd - log_prob)
    xi = np.zeros((K, K))
    for t in range(T - 1):
        xi += np.exp(fwd[t][:, None] + log_trans + (log_frame[t + 1] + bwd[t + 1])[None, :]
                     - log_prob)
    return log_prob, fwd, bwd, post, xi


def textbook_viterbi(log_start, log_trans, log_frame):
    T, K = log_frame.shape
    delta = log_start + log_frame[0]
    back = np.zeros((T, K), dtype=np.int64)
    for t in range(1, T):
        cand = delta[:, None] + log_trans           # (from, to)
        back[t] = np.argmax(cand, axis=0)           # first max = lowest index
        delta = cand[back[t], np.arange(K)] + log_frame[t]
    path = np.empty(T, dtype=np.int64)
    path[-1] = int(np.argmax(delta))
    for t in range(T - 1, 0, -1):
        path[t - 1] = back[t, path[t]]
    return float(np.max(delta)), path


def _run_lattice(log_start, log_trans, log_frame):
    fwd = np.empty_like(log_frame)
    lp = tttrlib.hmm_forward_log(log_start, log_trans, log_frame, fwd)
    bwd = np.empty_like(log_frame)
    tttrlib.hmm_backward_log(log_trans, log_frame, bwd)
    post = np.empty_like(log_frame)
    xi = np.zeros((log_frame.shape[1],) * 2)
    tttrlib.hmm_backward_posteriors_xi(log_trans, log_frame, fwd, lp, post, xi)
    states = np.empty(log_frame.shape[0], dtype=np.int64)
    vs = tttrlib.hmm_viterbi_log(log_start, log_trans, log_frame, states)
    return lp, fwd, bwd, post, xi, vs, states


class TestHmmLatticeAgainstHmmlearn(unittest.TestCase):
    """Recorded from hmmlearn's `_hmmc` (forward_log, backward_log,
    compute_log_xi_sum, viterbi) on random 2/3/5-state models, a T=1
    sequence, a forbidden transition and a frame one state cannot explain.
    The two lattices are the same recursion in the same order, so forward and
    backward are expected identical to the ulp; posteriors and xi are one
    fused sweep here against three passes there, so those carry
    accumulation-order noise (~1e-14 / ~1e-13 observed) and are compared at
    1e-12 / 1e-11."""

    def test_recorded_cases(self):
        z = _fixture()
        names = _hmm_case_names(z)
        self.assertGreaterEqual(len(names), 5)
        for n in names:
            g = lambda s: z[f"hmm/{n}/{s}"]
            frame = np.ascontiguousarray(g("log_frameprob"))
            lp, fwd, bwd, post, xi, vs, states = _run_lattice(
                g("log_startprob"), g("log_transmat"), frame)
            self.assertAlmostEqual(lp, float(g("log_prob")), places=12, msg=n)
            np.testing.assert_allclose(fwd, g("fwd"), rtol=0, atol=1e-12, err_msg=n)
            np.testing.assert_allclose(bwd, g("bwd"), rtol=0, atol=1e-12, err_msg=n)
            np.testing.assert_allclose(post, g("posteriors"), rtol=0, atol=1e-12, err_msg=n)
            np.testing.assert_allclose(xi, g("xi_sum"), rtol=1e-11, atol=1e-11, err_msg=n)
            self.assertAlmostEqual(vs, float(g("viterbi_score")), places=12, msg=n)
            np.testing.assert_array_equal(states, g("viterbi_path"), err_msg=n)

    def test_estep_over_the_concatenated_cases_is_the_sum(self):
        """`hmm_estep_log` on the recorded sequences laid end to end must give
        hmmlearn's per-sequence log-likelihoods, and a xi_sum that is the sum
        of hmmlearn's per-sequence xi -- which is exactly how hmmlearn's own
        E-step accumulates over `lengths`."""
        z = _fixture()
        names = [n for n in _hmm_case_names(z)
                 if z[f"hmm/{n}/log_frameprob"].shape[1] == 3]
        self.assertGreaterEqual(len(names), 2)
        start = z[f"hmm/{names[0]}/log_startprob"]
        trans = z[f"hmm/{names[0]}/log_transmat"]
        # one model, several sequences: re-score every 3-state sequence under
        # the first case's parameters with hmmlearn's answer recomputed here
        # by the textbook code (hmmlearn is not importable in-process).
        frames = [np.ascontiguousarray(z[f"hmm/{n}/log_frameprob"]) for n in names]
        frame = np.ascontiguousarray(np.vstack(frames))
        lengths = np.asarray([f.shape[0] for f in frames], dtype=np.int64)
        fwd = np.empty_like(frame)
        post = np.empty_like(frame)
        xi = np.zeros((3, 3))
        per_seq = np.empty(len(frames))
        total = tttrlib.hmm_estep_log(start, trans, frame, lengths, fwd, post, xi, per_seq)
        exp_total, exp_xi = 0.0, np.zeros((3, 3))
        for i, f in enumerate(frames):
            lp, _, _, _, xi_i = textbook_forward_backward(start, trans, f)
            exp_total += lp
            exp_xi += xi_i
            self.assertAlmostEqual(per_seq[i], lp, places=10)
        self.assertAlmostEqual(total, exp_total, places=9)
        np.testing.assert_allclose(xi, exp_xi, rtol=1e-10, atol=1e-11)


class TestHmmLatticeAgainstTheTextbook(unittest.TestCase):
    """The recursions written out in NumPy with scipy's logsumexp, on random
    models up to eight states -- an independent arrangement that would
    disagree with the fused sweep if the fusion dropped a term."""

    def test_random_models(self):
        rng = np.random.default_rng(5)
        for K, T in ((2, 40), (3, 90), (5, 120), (8, 60)):
            start = np.log(rng.dirichlet(np.ones(K)))
            trans = np.log(rng.dirichlet(np.ones(K), size=K))
            frame = np.ascontiguousarray(np.log(rng.random((T, K)) * 0.95 + 0.05))
            lp, fwd, bwd, post, xi, vs, states = _run_lattice(start, trans, frame)
            rlp, rfwd, rbwd, rpost, rxi = textbook_forward_backward(start, trans, frame)
            self.assertAlmostEqual(lp, rlp, places=10)
            np.testing.assert_allclose(fwd, rfwd, rtol=1e-12, atol=1e-10)
            np.testing.assert_allclose(bwd, rbwd, rtol=1e-12, atol=1e-10)
            np.testing.assert_allclose(post, rpost, rtol=1e-10, atol=1e-12)
            np.testing.assert_allclose(xi, rxi, rtol=1e-10, atol=1e-11)
            rvs, rpath = textbook_viterbi(start, trans, frame)
            self.assertAlmostEqual(vs, rvs, places=10)
            np.testing.assert_array_equal(states, rpath)

    def test_logsumexp_matches_scipy_including_minus_infinity(self):
        from scipy.special import logsumexp
        rng = np.random.default_rng(2)
        for v in (rng.normal(size=7) * 50, np.array([-np.inf, -3.0, -1000.0]),
                  np.full(4, -np.inf), np.array([700.0, 700.0])):
            got = tttrlib.hmm_logsumexp(np.ascontiguousarray(v))
            with np.errstate(divide="ignore"):
                ref = float(logsumexp(v))
            if np.isinf(ref):
                self.assertEqual(got, ref)
            else:
                self.assertAlmostEqual(got, ref, places=12)


# ---------------------------------------------------------------------------
# MaxEnt: the bounded QP and the Skilling-Bryan iteration
# ---------------------------------------------------------------------------

def _qp(C, d):
    return (lambda x: 0.5 * x @ C @ x + d @ x), (lambda x: C @ x + d)


def _scipy_bounded_min(f, jac, x0, lb, **opts):
    from scipy.optimize import minimize
    options = dict(ftol=1e-16, gtol=1e-14, maxiter=200000, maxfun=2000000)
    options.update(opts)
    return minimize(f, x0, jac=jac, bounds=[(lb, None)] * len(x0),
                    method="L-BFGS-B", options=options)


class TestQuadprBoundAgainstScipy(unittest.TestCase):
    """`quadpr_bound` against L-BFGS-B on the same bounded quadratic.

    The header is explicit that the active-set *sweep* is not a KKT-correct
    QP solver: it never releases a clamped variable and never checks the
    multiplier sign. So the honest statement is in two parts. Whenever its
    answer *is* a KKT point -- always when no bound binds, and on many
    problems where some do -- it must coincide with scipy's minimiser. When
    it is not, it must still be feasible, must not beat scipy (scipy's value
    is the true minimum, so anything lower is a bug in the check), and the
    gap is bounded and reported. On the problems here that gap is ~1e-2
    relative at worst; the MEM outer loop re-solves the QP each step, which
    is why the sweep is adequate there and why it is not exposed as a
    general solver."""

    def _cases(self):
        rng = np.random.default_rng(3)
        for n, lb, jitter in ((10, 0.0, 1.0), (30, 1e-4, 3.0), (20, 0.5, 3.0),
                              (15, -2.0, 1.0), (40, 0.0, 2.0)):
            A = rng.standard_normal((n, n))
            C = A @ A.T + 5 * np.eye(n)
            d = rng.standard_normal(n) * jitter
            yield n, lb, C, d

    def test_free_optimum_and_kkt_points_coincide_with_scipy(self):
        checked_kkt = 0
        for n, lb, C, d in self._cases():
            x = np.asarray(tttrlib.tcspc_quadpr_bound(C.ravel(), d, lb))
            f, jac = _qp(C, d)
            r = _scipy_bounded_min(f, jac, np.full(n, max(lb, 0.0) + 1.0), lb)
            self.assertTrue(np.all(x >= lb - 1e-12))
            self.assertGreaterEqual(f(x), r.fun - 1e-9 * max(1.0, abs(r.fun)),
                                    "the sweep may not beat the true minimum")
            g = jac(x)
            free = x > lb + 1e-9
            is_kkt = (np.abs(g[free]).max(initial=0.0) < 1e-8 * (1 + np.abs(g).max())
                      and np.all(g[~free] >= -1e-8))
            if is_kkt:
                checked_kkt += 1
                np.testing.assert_allclose(x, r.x, rtol=1e-6, atol=1e-8)
                self.assertAlmostEqual(f(x), r.fun, delta=1e-8 * max(1.0, abs(r.fun)))
            else:
                # documented non-optimality: bounded, and reported here
                gap = (f(x) - r.fun) / max(1.0, abs(r.fun))
                self.assertLess(gap, 5e-2)
        self.assertGreaterEqual(checked_kkt, 2, "no KKT case exercised the equality")

    def test_the_two_by_two_with_an_active_bound_is_exact(self):
        """Small enough for the sweep to be exact and for the answer to be
        written down: x1 clamps at the bound, x2 solves its 1-D problem."""
        C = np.array([[2.0, 0.5], [0.5, 1.0]])
        d = np.array([3.0, -1.0])       # x1 wants to go negative
        lb = 0.0
        x = np.asarray(tttrlib.tcspc_quadpr_bound(C.ravel(), d, lb))
        f, jac = _qp(C, d)
        r = _scipy_bounded_min(f, jac, np.ones(2), lb)
        np.testing.assert_allclose(x, r.x, rtol=1e-8, atol=1e-10)


def _mem_problem(seed=3, n_rows=200, n=25, sigma=0.01):
    """A two-lifetime decay on a log-spaced tau grid, weighted normal
    equations in run_mem's `1/2 p^T H p - g0^T p + const` form."""
    rng = np.random.default_rng(seed)
    tau = np.geomspace(0.1, 10, n)
    t = np.linspace(0, 20, n_rows)
    A = np.exp(-t[:, None] / tau[None, :])
    p_true = np.zeros(n)
    p_true[8] = 1.0
    p_true[17] = 0.5
    b = A @ p_true + rng.standard_normal(n_rows) * sigma
    w = np.full(n_rows, 1.0 / sigma ** 2)
    H = 2 * (A * w[:, None]).T @ A
    g0 = 2 * A.T @ (w * b)
    c = float(w @ (b * b))
    return A, b, tau, p_true, H, g0, c


def _mem_Q(H, g0, c, m, nu):
    """run_mem's objective, from the header: Q = chi2 - nu/2 * S with
    S = sum(p - p log(p/m)) - sum(m). And its gradient."""
    def Q(p):
        with np.errstate(divide="ignore", invalid="ignore"):
            S = np.sum((1.0 - np.log(p / m)) * p) - m.sum()
        return 0.5 * p @ H @ p - g0 @ p + c - 0.5 * nu * S

    def dQ(p):
        with np.errstate(divide="ignore", invalid="ignore"):
            return H @ p - g0 + 0.5 * nu * np.log(p / m)
    return Q, dQ


class TestRunMemAgainstScipy(unittest.TestCase):
    """`run_mem`'s fixed point is the bound-constrained minimiser of Q.

    Three checks, none of which imports the kernel's own arithmetic: (i) the
    KKT conditions of Q at the returned p, evaluated in NumPy from the
    header's formulas -- free coordinates stationary, clamped coordinates with
    a non-negative gradient; (ii) L-BFGS-B started at the prior with a
    generous budget lands on the same p; (iii) L-BFGS-B started *at* p cannot
    lower Q. Q is convex (H is PSD, -S is convex on p > 0), so KKT is
    sufficient and there is one answer to agree on."""

    @classmethod
    def setUpClass(cls):
        cls.A, cls.b, cls.tau, cls.p_true, cls.H, cls.g0, cls.c = _mem_problem()
        cls.m = np.full(cls.tau.size, 0.05)

    def _solve(self, nu, tol=1e-8, max_iter=5000):
        r = tttrlib.tcspc_run_mem(self.H.ravel(), self.g0, self.m, self.c, nu,
                                  max_iter, tol, 1e-12)
        return np.array(list(r.p), dtype=float), r

    def test_kkt_of_the_returned_point(self):
        for nu in (0.1, 1.0, 10.0, 100.0):
            p, _ = self._solve(nu)
            Q, dQ = _mem_Q(self.H, self.g0, self.c, self.m, nu)
            g = dQ(p)
            scale = np.abs(self.H @ p).max() + np.abs(self.g0).max()
            free = p > 1e-9
            self.assertTrue(free.any())
            self.assertLess(np.abs(g[free]).max() / scale, 1e-9, f"nu={nu}")
            if (~free).any():
                self.assertGreater(g[~free].min(), 0.0, f"nu={nu}: a clamped "
                                   "coordinate wants to move up")

    def test_lbfgsb_from_the_prior_lands_on_the_same_point(self):
        for nu in (0.1, 1.0, 10.0, 100.0):
            p, _ = self._solve(nu)
            Q, dQ = _mem_Q(self.H, self.g0, self.c, self.m, nu)
            r = _scipy_bounded_min(Q, dQ, self.m.copy(), 1e-12)
            # L-BFGS-B stalls a few 1e-8 relative above the optimum on the
            # cases with clamped coordinates (a projected quasi-Newton is
            # weak exactly there); the kernel may be lower, never higher.
            self.assertLessEqual(Q(p), r.fun + 1e-9 * abs(r.fun), f"nu={nu}")
            self.assertAlmostEqual(Q(p), r.fun, delta=1e-6 * abs(r.fun), msg=f"nu={nu}")
            # Q is flat along the clamped directions, so where L-BFGS-B stops
            # in p is loose (~1e-3 of the peak) even when its Q is 1e-8 off;
            # the tight statement about p is the KKT test above.
            np.testing.assert_allclose(p, r.x, rtol=0, atol=5e-3 * p.max(),
                                       err_msg=f"nu={nu}")

    def test_lbfgsb_cannot_improve_on_the_returned_point(self):
        for nu in (0.1, 1.0, 10.0, 100.0):
            p, _ = self._solve(nu)
            Q, dQ = _mem_Q(self.H, self.g0, self.c, self.m, nu)
            r = _scipy_bounded_min(Q, dQ, p.copy(), 1e-12)
            self.assertGreaterEqual(r.fun, Q(p) - 1e-9 * abs(Q(p)), f"nu={nu}")

    def test_the_two_lifetimes_come_back(self):
        """Known answer, independent of any solver: at a weak prior pull the
        mass sits on the two planted grid points -- within one grid step,
        which is what a lifetime spectrum resolves at this noise."""
        p, _ = self._solve(0.1)
        top = np.argsort(p)[-2:]
        self.assertEqual(set(top.tolist()), {8, 17})
        self.assertAlmostEqual(p[7:10].sum(), 1.0, delta=0.1)
        self.assertAlmostEqual(p[16:19].sum(), 0.5, delta=0.1)
        self.assertLess(p[np.r_[0:7, 10:16, 19:25]].sum(), 0.1)


class TestMaxentInvertAgainstScipy(unittest.TestCase):
    """`maxent_invert(A, b, nu)` documents its objective as
    ||Ax - b||^2 - nu^2 S(x) with a uniform prior of ones. Same three checks
    against that objective, on the documented functional rather than on the
    engine's internal (H, g0) form -- so this also checks the translation
    (H = 2 A^T A, nu_run = 2 nu^2) that MaxEnt.cpp does on the way in."""

    @classmethod
    def setUpClass(cls):
        cls.A, cls.b, cls.tau, cls.p_true, *_ = _mem_problem(seed=9, sigma=0.02)

    def _objective(self, nu):
        A, b = self.A, self.b
        m = np.ones(A.shape[1])

        def Q(x):
            with np.errstate(divide="ignore", invalid="ignore"):
                S = np.sum((1.0 - np.log(x / m)) * x) - m.sum()
            r = A @ x - b
            return r @ r - nu * nu * S

        def dQ(x):
            with np.errstate(divide="ignore", invalid="ignore"):
                return 2 * A.T @ (A @ x - b) + nu * nu * np.log(x / m)
        return Q, dQ

    def test_agrees_with_lbfgsb_on_the_documented_objective(self):
        n_rows, n = self.A.shape
        for nu in (0.05, 0.3, 1.0):
            x = np.array(list(tttrlib.maxent_invert(self.A.ravel(), self.b, nu,
                                                    n_rows, n, 20000, 1e-9)), dtype=float)
            Q, dQ = self._objective(nu)
            self.assertTrue(np.all(x >= 1e-12))
            g = dQ(x)
            free = x > 1e-9
            scale = np.abs(2 * self.A.T @ (self.A @ x)).max() + 1.0
            self.assertLess(np.abs(g[free]).max() / scale, 1e-8, f"nu={nu}")
            r = _scipy_bounded_min(Q, dQ, np.ones(n), 1e-12)
            self.assertAlmostEqual(Q(x), r.fun, delta=1e-7 * max(1.0, abs(r.fun)),
                                   msg=f"nu={nu}")
            r2 = _scipy_bounded_min(Q, dQ, x.copy(), 1e-12)
            self.assertGreaterEqual(r2.fun, Q(x) - 1e-8 * max(1.0, abs(Q(x))))


if __name__ == "__main__":
    unittest.main()
