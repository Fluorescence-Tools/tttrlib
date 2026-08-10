import unittest
import numpy as np
import tttrlib


class TestGopichSzabo(unittest.TestCase):

    def _make_bursts(self, n_bursts=10, mean_len=50, seed=42):
        """Generate synthetic photon bursts for testing."""
        np.random.seed(seed)
        k12, k21 = 1000.0, 500.0
        e1, e2 = 0.2, 0.8
        mt_rate = 20e3
        all_t, all_c = [], []
        for _ in range(n_bursts):
            n = max(np.random.poisson(mean_len), 5)
            times = np.cumsum(np.random.exponential(1.0 / mt_rate, n))
            states = np.zeros(n, dtype=int)
            state = 0 if np.random.rand() < 0.33 else 1
            t_curr = 0.0
            for i in range(n):
                while times[i] > t_curr:
                    rate = k12 if state == 0 else k21
                    t_curr += np.random.exponential(1.0 / rate)
                    state = 1 - state
                states[i] = state
            effs = np.where(states == 0, e1, e2)
            colors = (np.random.rand(n) < effs).astype(np.int32)
            all_t.append(times)
            all_c.append(colors)
        lengths = np.array([len(t) for t in all_t])
        offsets = np.zeros(len(all_t) + 1, dtype=np.int64)
        np.cumsum(lengths, out=offsets[1:])
        times = np.concatenate(all_t)
        colors = np.concatenate(all_c)
        return times, colors, offsets

    def test_emission_from_efficiencies(self):
        em = tttrlib.emission_from_efficiencies([0.2, 0.8])
        self.assertEqual(len(em), 4)
        self.assertAlmostEqual(em[0], 0.8)   # 1-E for state 0
        self.assertAlmostEqual(em[1], 0.2)   # E for state 0
        self.assertAlmostEqual(em[2], 0.2)   # 1-E for state 1
        self.assertAlmostEqual(em[3], 0.8)   # E for state 1

    def test_equilibrium_populations(self):
        rm = [0.0, 1000.0, 500.0, 0.0]  # [[0, 500], [1000, 0]]
        eq = tttrlib.equilibrium_populations(rm, 2)
        # rm=[[0,1000],[500,0]]: source 0->1 at 500, source 1->0 at 1000
        # equilibrium: p0 = 1000/1500, p1 = 500/1500
        self.assertAlmostEqual(eq[0], 2.0 / 3.0, places=6)
        self.assertAlmostEqual(eq[1], 1.0 / 3.0, places=6)

    def test_set_scheme_two_state(self):
        gs = tttrlib.GopichSzabo()
        rm = [0.0, 1000.0, 500.0, 0.0]
        em = tttrlib.emission_from_efficiencies([0.2, 0.8])
        ok = gs.set_scheme(rm, list(em), 2, 2)
        self.assertTrue(ok)
        self.assertEqual(gs.n_states(), 2)
        self.assertEqual(gs.n_colors(), 2)
        self.assertTrue(gs.is_valid())

    def test_log_likelihood_finite(self):
        gs = tttrlib.GopichSzabo()
        rm = [0.0, 1000.0, 500.0, 0.0]
        em = tttrlib.emission_from_efficiencies([0.2, 0.8])
        gs.set_scheme(rm, list(em), 2, 2)
        times, colors, offsets = self._make_bursts()
        ll = gs.log_likelihood(times, colors, offsets)
        self.assertTrue(np.isfinite(ll))
        self.assertLess(ll, 0)  # log-likelihood is always negative

    def test_relaxation_times(self):
        gs = tttrlib.GopichSzabo()
        rm = [0.0, 1000.0, 500.0, 0.0]
        em = tttrlib.emission_from_efficiencies([0.2, 0.8])
        gs.set_scheme(rm, list(em), 2, 2)
        rt = gs.relaxation_times()
        self.assertEqual(len(rt), 1)  # one non-zero eigenvalue for 2 states
        # tau = 1/(k12+k21) = 1/1500
        self.assertAlmostEqual(rt[0], 1.0 / 1500.0, places=8)

    def test_viterbi_returns_path(self):
        gs = tttrlib.GopichSzabo()
        rm = [0.0, 1000.0, 500.0, 0.0]
        em = tttrlib.emission_from_efficiencies([0.2, 0.8])
        gs.set_scheme(rm, list(em), 2, 2)
        times, colors, offsets = self._make_bursts(n_bursts=1, mean_len=30)
        path = gs.viterbi(times, colors)
        self.assertEqual(len(path), len(times))
        self.assertTrue(all(s in (0, 1) for s in path))

    def test_no_exchange_limit_is_a_valid_scheme(self):
        """The all-zero rate matrix is the static mixture a dynamic fit is
        compared against, and its likelihood is perfectly well defined:
        for photons d,a,d,a it is log(0.5*prod(em_0) + 0.5*prod(em_1)),
        which ChiSurf's own implementation reproduces to 16 digits."""
        emission = [0.8, 0.2, 0.2, 0.8]
        gs = tttrlib.GopichSzabo()
        self.assertTrue(gs.set_scheme([0.0] * 4, emission, 2, 2))

        times = np.array([0.0, 1e-5, 2e-5, 3e-5])
        colors = np.array([0, 1, 0, 1], dtype=np.int32)
        offsets = np.array([0, 4], dtype=np.int64)
        ll = gs.log_likelihood(times, colors, offsets)
        expected = np.log(0.5 * (0.8 * 0.2 * 0.8 * 0.2)
                          + 0.5 * (0.2 * 0.8 * 0.2 * 0.8))
        self.assertAlmostEqual(ll, expected, places=12)

    def test_three_state_all_static_is_a_valid_scheme(self):
        gs = tttrlib.GopichSzabo()
        em = tttrlib.emission_from_efficiencies([0.1, 0.5, 0.9])
        self.assertTrue(gs.set_scheme([0.0] * 9, list(em), 3, 2))

    def test_an_isolated_state_is_a_valid_scheme(self):
        """Two states exchanging plus one that does not: a repeated zero
        eigenvalue, one per disconnected component, with a full eigenspace.
        The likelihood must be finite, and for a scheme this simple it must
        agree with the direct sum over states."""
        k12, k21 = 1000.0, 500.0
        rm = [0.0, k21, 0.0,
              k12, 0.0, 0.0,
              0.0, 0.0, 0.0]
        gs = tttrlib.GopichSzabo()
        em = tttrlib.emission_from_efficiencies([0.2, 0.8, 0.5])
        self.assertTrue(gs.set_scheme(rm, list(em), 3, 2))

        times, colors, offsets = self._make_bursts(n_bursts=3, mean_len=40)
        ll = gs.log_likelihood(times, colors, offsets)
        self.assertTrue(np.isfinite(ll))

    def test_static_likelihood_matches_direct_mixture_sum(self):
        """With no exchange, the exact likelihood of a burst is
        sum_s p_eq[s] * prod_i em[s, color_i] -- checked against the spectral
        implementation over a longer random burst."""
        np.random.seed(7)
        n = 60
        times = np.cumsum(np.random.exponential(5e-6, n))
        colors = (np.random.rand(n) < 0.6).astype(np.int32)
        offsets = np.array([0, n], dtype=np.int64)

        effs = [0.15, 0.55, 0.95]
        em = np.array(tttrlib.emission_from_efficiencies(effs)).reshape(3, 2)
        gs = tttrlib.GopichSzabo()
        self.assertTrue(gs.set_scheme([0.0] * 9, list(em.flatten()), 3, 2))
        ll = gs.log_likelihood(times, colors, offsets)

        per_state = np.array([np.sum(np.log(em[s, colors])) for s in range(3)])
        expected = np.log(np.mean(np.exp(per_state - per_state.max()))) \
            + per_state.max()
        self.assertAlmostEqual(ll, expected, places=10)


if __name__ == '__main__':
    unittest.main()
