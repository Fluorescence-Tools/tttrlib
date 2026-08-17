"""A/B of the CTMC utilities and the Gopich-Szabo likelihood against scipy and
an independent NumPy transcription of the published likelihood.

* ``generator_from_rate_matrix``, ``equilibrium_populations``,
  ``rate_matrix_from_rates`` / ``rates_from_rate_matrix``: against a NumPy
  transcription of the column-sum-zero convention, ``scipy.linalg.null_space``
  and ``scipy.linalg.expm(Q t) p0 -> p_eq`` as t -> inf.
* ``GopichSzabo.log_likelihood``: against a direct evaluation of Gopich & Szabo
  (J. Phys. Chem. B 2009, 113, 10965, eq. 3) with ``scipy.linalg.expm`` for
  every inter-photon propagator, no diagonalisation, no scaling trick beyond a
  running log -- a different algorithm for the same number. Also against
  ChiSurf's own ``PhotonBursts``/``log_likelihood`` where importable (it now
  delegates to this engine, so that is a wiring check, not independent).
* ``GopichSzabo.viterbi``: against a NumPy max-product recursion on the same
  ``expm`` propagators, incl. the per-burst restart when offsets are given.
* ``relaxation_times``: against ``-1/Re(eig(Q))``.
* ``emission_from_efficiencies``: against the definition.
* ``sim_occupation_fractions`` / ``sim_state_at_times`` (SimKinetics.h, the
  simulation-side companions): mean occupation against
  ``(1/T) int_0^T expm(Q t) p0 dt`` and the state distribution at t against
  ``expm(Q t) p0``, statistically over many seeds.
"""
import unittest

import numpy as np
from scipy.linalg import expm, null_space

import tttrlib


def _rate_matrix(rng, n, scale=1000.0, zero=()):
    """[target, source] rates in Hz, row-major flat as tttrlib takes it."""
    M = rng.uniform(0.2, 1.0, size=(n, n)) * scale
    np.fill_diagonal(M, 0.0)
    for t, s in zero:
        M[t, s] = 0.0
    return M


def _generator(M):
    Q = M.copy()
    np.fill_diagonal(Q, 0.0)
    np.fill_diagonal(Q, -Q.sum(axis=0))
    return Q


def _p_eq(Q):
    v = null_space(Q)
    assert v.shape[1] == 1, "generator has a repeated zero eigenvalue"
    p = v[:, 0]
    p = p / p.sum()
    return p


def _simulate(rng, M, emission, n_bursts, n_phot, rate_phot):
    """Gillespie the chain, colour photons by the state's emission row."""
    n = M.shape[0]
    Q = _generator(M)
    p_eq = _p_eq(Q)
    times, colors, offsets = [], [], [0]
    for _ in range(n_bursts):
        s = rng.choice(n, p=p_eq)
        t = 0.0
        next_jump = t + rng.exponential(1.0 / -Q[s, s])
        for _ in range(n_phot):
            t += rng.exponential(1.0 / rate_phot)
            while next_jump < t:
                out = M[:, s].copy()
                s = rng.choice(n, p=out / out.sum())
                next_jump += rng.exponential(1.0 / -Q[s, s])
            times.append(t)
            colors.append(rng.choice(emission.shape[1], p=emission[s]))
        offsets.append(len(times))
    return (np.asarray(times), np.asarray(colors, dtype=np.int32), np.asarray(offsets, dtype=np.int64))


def _loglik_expm(M, emission, times, colors, offsets):
    """1^T F(c_N) e^{Q dt_N} ... F(c_2) e^{Q dt_2} F(c_1) p_eq, per burst, logged."""
    Q = _generator(M)
    p_eq = _p_eq(Q)
    total = 0.0
    for b in range(len(offsets) - 1):
        t = times[offsets[b]:offsets[b + 1]]
        c = colors[offsets[b]:offsets[b + 1]]
        v = emission[:, c[0]] * p_eq
        s = v.sum(); ll = np.log(s); v = v / s
        for k in range(1, len(t)):
            v = emission[:, c[k]] * (expm(Q * (t[k] - t[k - 1])) @ v)
            s = v.sum(); ll += np.log(s); v = v / s
        total += ll
    return total


def _viterbi_expm(M, emission, times, colors):
    Q = _generator(M)
    p_eq = _p_eq(Q)
    n = M.shape[0]
    N = len(times)
    logd = np.log(emission[:, colors[0]] * p_eq)
    back = np.zeros((N, n), dtype=int)
    for k in range(1, N):
        P = expm(Q * (times[k] - times[k - 1]))  # P[target, source]
        cand = logd[None, :] + np.log(P)  # [target, source]
        back[k] = np.argmax(cand, axis=1)
        logd = cand[np.arange(n), back[k]] + np.log(emission[:, colors[k]])
    path = np.empty(N, dtype=int)
    path[-1] = int(np.argmax(logd))
    for k in range(N - 1, 0, -1):
        path[k - 1] = back[k, path[k]]
    return path


class TestCtmcUtilities(unittest.TestCase):

    def test_generator_and_equilibrium_against_scipy(self):
        rng = np.random.default_rng(1)
        for n in (2, 3, 4, 6):
            for trial in range(3):
                with self.subTest(n=n, trial=trial):
                    M = _rate_matrix(rng, n)
                    Q_ref = _generator(M)
                    Q = np.asarray(tttrlib.generator_from_rate_matrix(M.ravel().tolist(), n)).reshape(n, n)
                    np.testing.assert_allclose(Q, Q_ref, rtol=1e-14, atol=1e-12)
                    p = np.asarray(tttrlib.equilibrium_populations(M.ravel().tolist(), n))
                    np.testing.assert_allclose(p, _p_eq(Q_ref), rtol=1e-9, atol=1e-12)
                    # and it is where expm(Q t) p0 goes
                    p_inf = expm(Q_ref * 1.0) @ np.full(n, 1.0 / n)
                    np.testing.assert_allclose(p, p_inf, rtol=1e-8, atol=1e-10)

    def test_equilibrium_with_a_one_way_and_a_zero_rate(self):
        # 0 -> 1 only, 1 -> 0 at 0: all population ends in 1
        M = np.array([[0.0, 0.0], [300.0, 0.0]])
        p = np.asarray(tttrlib.equilibrium_populations(M.ravel().tolist(), 2))
        np.testing.assert_allclose(p, [0.0, 1.0], atol=1e-12)

    def test_flat_rate_round_trip(self):
        rng = np.random.default_rng(2)
        for n in (2, 3, 5):
            M = _rate_matrix(rng, n)
            flat = np.asarray(tttrlib.rates_from_rate_matrix(M.ravel().tolist(), n))
            # documented order: source-major, skipping the diagonal
            ref = [M[t, s] for s in range(n) for t in range(n) if t != s]
            np.testing.assert_allclose(flat, ref, rtol=0, atol=0)
            back = np.asarray(tttrlib.rate_matrix_from_rates(flat.tolist(), n)).reshape(n, n)
            np.testing.assert_allclose(back, M, rtol=0, atol=0)

    def test_emission_from_efficiencies(self):
        E = [0.1, 0.55, 0.9]
        em = np.asarray(tttrlib.emission_from_efficiencies(E)).reshape(3, 2)
        np.testing.assert_allclose(em[:, 0], 1 - np.asarray(E))
        np.testing.assert_allclose(em[:, 1], E)


class TestGopichSzaboAgainstExpm(unittest.TestCase):

    def _cases(self):
        rng = np.random.default_rng(7)
        cases = []
        # two-state, two colours
        M = np.array([[0.0, 800.0], [400.0, 0.0]])
        em = np.asarray(tttrlib.emission_from_efficiencies([0.2, 0.8])).reshape(2, 2)
        cases.append(("two_state", M, em, _simulate(rng, M, em, 12, 60, 3e4)))
        # three-state cycle, non-reversible (complex eigenvalues)
        M = np.array([[0.0, 50.0, 2000.0], [2000.0, 0.0, 50.0], [50.0, 2000.0, 0.0]])
        em = np.array([[0.9, 0.1], [0.5, 0.5], [0.1, 0.9]])
        cases.append(("cycle", M, em, _simulate(rng, M, em, 8, 80, 5e4)))
        # four states, three colours
        M = _rate_matrix(rng, 4, scale=3000.0)
        em = rng.dirichlet(np.ones(3), size=4)
        cases.append(("four_state_3col", M, em, _simulate(rng, M, em, 6, 50, 4e4)))
        # slow exchange: long gaps relative to kinetics
        M = np.array([[0.0, 5.0], [5.0, 0.0]])
        em = np.asarray(tttrlib.emission_from_efficiencies([0.3, 0.7])).reshape(2, 2)
        cases.append(("slow", M, em, _simulate(rng, M, em, 10, 40, 1e3)))
        return cases

    def test_log_likelihood(self):
        for name, M, em, (t, c, off) in self._cases():
            with self.subTest(case=name):
                gs = tttrlib.GopichSzabo()
                self.assertTrue(gs.set_scheme(M.ravel().tolist(), em.ravel().tolist(), M.shape[0], em.shape[1]))
                ll = gs.log_likelihood(t.tolist(), c.tolist(), off.tolist())
                ref = _loglik_expm(M, em, t, c, off)
                self.assertAlmostEqual(ll, ref, delta=1e-9 * abs(ref))

    def test_log_likelihood_matches_chisurf_wiring(self):
        try:
            import sys
            sys.path.insert(0, "/Users/tpeulen/dev/chisurf")
            from chisurf.core.fluorescence.burst import gopich_szabo as gsm
        except Exception as exc:  # pragma: no cover
            self.skipTest(f"chisurf gopich_szabo not importable: {exc}")
        name, M, em, (t, c, off) = self._cases()[0]
        bursts = gsm.PhotonBursts.from_lists(
            [t[off[b]:off[b + 1]] for b in range(len(off) - 1)],
            [c[off[b]:off[b + 1]] for b in range(len(off) - 1)])
        ll_c = gsm.log_likelihood(bursts, M, em)
        gs = tttrlib.GopichSzabo(); gs.set_scheme(M.ravel().tolist(), em.ravel().tolist(), 2, 2)
        self.assertAlmostEqual(ll_c, gs.log_likelihood(t.tolist(), c.tolist(), off.tolist()), delta=1e-9)

    def test_viterbi(self):
        for name, M, em, (t, c, off) in self._cases():
            with self.subTest(case=name):
                gs = tttrlib.GopichSzabo()
                gs.set_scheme(M.ravel().tolist(), em.ravel().tolist(), M.shape[0], em.shape[1])
                path = np.asarray(gs.viterbi(t.tolist(), c.tolist(), off.tolist()))
                ref = np.concatenate([_viterbi_expm(M, em, t[off[b]:off[b + 1]], c[off[b]:off[b + 1]])
                                      for b in range(len(off) - 1)])
                # ties in the max-product are possible in principle; require near-total agreement
                agree = np.mean(path == ref)
                self.assertGreaterEqual(agree, 0.999, f"{name}: agreement {agree}")
                # the single-burst overload equals the offsets form on burst 0
                p0 = np.asarray(gs.viterbi(t[off[0]:off[1]].tolist(), c[off[0]:off[1]].tolist()))
                np.testing.assert_array_equal(p0, path[off[0]:off[1]])

    def test_relaxation_times_against_eig(self):
        for name, M, em, _ in self._cases():
            with self.subTest(case=name):
                gs = tttrlib.GopichSzabo()
                gs.set_scheme(M.ravel().tolist(), em.ravel().tolist(), M.shape[0], em.shape[1])
                lam = np.linalg.eigvals(_generator(M))
                ref = sorted(-1.0 / lam.real[np.abs(lam) > 1e-9 * np.abs(lam).max()])
                got = sorted(gs.relaxation_times())
                np.testing.assert_allclose(got, ref, rtol=1e-8)


class TestSimKineticsAgainstExpm(unittest.TestCase):

    def test_state_distribution_at_a_time(self):
        M = np.array([[0.0, 300.0, 50.0], [100.0, 0.0, 400.0], [200.0, 150.0, 0.0]])
        Q = _generator(M)
        # SimKinetics takes row-major source->target: transpose of [target, source]
        k = M.T.ravel().tolist()
        p0 = [1.0, 0.0, 0.0]
        ts = [0.0005, 0.002, 0.01]
        n_rep = 4000
        counts = np.zeros((len(ts), 3))
        for seed in range(n_rep):
            st = np.asarray(tttrlib.sim_state_at_times(k, 3, ts, p0, seed))
            for i, s in enumerate(st):
                counts[i, s] += 1
        emp = counts / n_rep
        for i, t in enumerate(ts):
            ref = expm(Q * t) @ np.asarray(p0)
            se = np.sqrt(ref * (1 - ref) / n_rep) + 1e-9
            self.assertTrue(np.all(np.abs(emp[i] - ref) < 5 * se), f"t={t}: {emp[i]} vs {ref}")

    def test_mean_occupation_over_a_window(self):
        M = np.array([[0.0, 500.0], [200.0, 0.0]])
        Q = _generator(M)
        k = M.T.ravel().tolist()
        T = 0.004
        n = 3000
        fr = np.asarray(tttrlib.sim_occupation_fractions(k, 2, T, n, [1.0, 0.0], 3)).reshape(n, 2)
        # (1/T) int_0^T expm(Q t) p0 dt by fine quadrature
        tt = np.linspace(0, T, 2001)
        ref = np.trapz(np.array([expm(Q * t) @ np.array([1.0, 0.0]) for t in tt]), tt, axis=0) / T
        se = fr.std(axis=0) / np.sqrt(n)
        self.assertTrue(np.all(np.abs(fr.mean(axis=0) - ref) < 5 * se), f"{fr.mean(axis=0)} vs {ref}")


if __name__ == "__main__":
    unittest.main()
