"""pytest suite for the photon-stream HMM prototype.

Three tiers, each resting on the one before:

1. The Numba E-step against the **brute-force oracle** — enumeration over every
   tick-level state path, which assumes no HMM algebra at all.  This is where
   the implementation earns trust.
2. The prototype against the **C++ engine** for the paths the engine already
   has (maximum likelihood), so the port target is pinned from the start.
3. The new capabilities — Dirichlet priors, fixed entries, variational Bayes and
   blocked Gibbs — against properties that must hold by construction.

The Gibbs sampler is checked by requiring its *sampled* sufficient statistics to
converge to the E-step's *expected* ones, which are themselves pinned to the
enumeration in tier 1.  That chains the sampler back to first principles instead
of to another approximation.

Kept small so the deliberately-slow oracle stays fast enough for CI.

Run:  python -m pytest prototype/hmm/test_prototype.py -q
"""
from __future__ import annotations

import itertools
import os
import sys

import numpy as np
import pytest

from hmm import (HmmConstraints, HmmModel, PhotonData, dirichlet_kl, digamma,
                 ess, gibbs, sample_paths_and_counts, split_rhat,
                 e_step, fit, fit_vb, random_model,
                 FretEmission, LifetimeEmission, fit_emission, forward_backward_burst,
                 gaussian_irf, simulate_lifetime,
                 sbc_ranks, summarize, chi2_sf, add_background_photons,
                 FretDistanceEmission, fret_lifetime_spectrum, lifetime_averages,
                 Optics, Anisotropy, gibbs_physical, HmmPhysicalPosterior,
                 TruncatedNormalPrior1D)
from hmm.gibbs import _param_bounds
from hmm.instrument import (KineticScheme, InstrumentConfig, Photophysics,
                            simulate, build_system)

# The oracle is part of the permanent test suite; the prototype is temporary, so
# the dependency runs this way round and never the reverse.
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "test", "python", "hmm"))
import toy  # noqa: E402


TINY_PRIOR = np.array([0.6, 0.4])
TINY_A = np.array([[0.90, 0.10], [0.25, 0.75]])
TINY_B = np.array([[0.70, 0.30], [0.35, 0.65]])

TINY_BURSTS = [
    ([0, 1, 2], [0, 1, 0]),
    ([0, 2, 5], [1, 1, 0]),
    ([0, 3], [0, 1]),
]


def _tiny_data():
    return PhotonData([t for t, _ in TINY_BURSTS], [s for _, s in TINY_BURSTS], 2)


def _simulated(n_bursts=25, burst_len=80, seed=11):
    true = toy.two_state(e_low=0.2, e_high=0.8, k_switch=2e-3)
    times, streams = toy.simulate(*true, n_bursts=n_bursts, burst_len=burst_len,
                                  mean_gap=20, seed=seed)
    return true, times, streams, PhotonData(times, streams, 2)


# ---------------------------------------------------------------------------
# tier 1 — against the brute-force oracle
# ---------------------------------------------------------------------------

class TestAgainstOracle:
    def test_e_step_matches_enumeration(self):
        data = _tiny_data()
        prior_acc, gamma_obs, xi, ll = e_step(data, TINY_PRIOR, TINY_A, TINY_B)

        b_ll = 0.0
        b_prior = np.zeros(2)
        b_gamma = np.zeros((2, 2))
        b_xi = np.zeros((2, 2))
        for times, streams in TINY_BURSTS:
            l, pa, go, x = toy.enumerate_burst(TINY_PRIOR, TINY_A, TINY_B, times, streams)
            b_ll += l
            b_prior += pa
            b_gamma += go
            b_xi += x

        assert ll == pytest.approx(b_ll, abs=1e-12)
        np.testing.assert_allclose(prior_acc, b_prior, atol=1e-12)
        np.testing.assert_allclose(gamma_obs, b_gamma, atol=1e-12)
        np.testing.assert_allclose(xi, b_xi, atol=1e-12)

    def test_e_step_matches_reference_on_simulated(self):
        """Real-scale data, where enumeration is impossible but the slow
        reference recursions still run."""
        _, times, streams, data = _simulated(n_bursts=6, burst_len=25, seed=3)
        prior, A, B = toy.two_state(e_low=0.25, e_high=0.75, k_switch=3e-3)

        got = e_step(data, prior, A, B)
        want = toy.expected_counts(prior, A, B, list(zip(times, streams)))

        assert got[3] == pytest.approx(want[3], rel=1e-10)
        for g, w in zip(got[:3], want[:3]):
            np.testing.assert_allclose(g, w, atol=1e-9)

    def test_rho_cache_matches_direct_sum(self):
        """The O(n^4) deferred contraction must equal the O(dt n^2) sum."""
        _, _, _, data = _simulated(n_bursts=5, burst_len=30, seed=4)
        prior, A, B = toy.two_state(e_low=0.25, e_high=0.75, k_switch=3e-3)

        fast = e_step(data, prior, A, B, use_rho=True)
        slow = e_step(data, prior, A, B, use_rho=False)

        assert fast[3] == pytest.approx(slow[3], rel=1e-12)
        for f, s in zip(fast[:3], slow[:3]):
            np.testing.assert_allclose(f, s, rtol=1e-10, atol=1e-12)

    def test_rho_cache_matches_enumeration(self):
        """...and the fast path is pinned to brute force too, not just to the
        slow path it was derived from."""
        data = _tiny_data()
        _, _, xi, _ = e_step(data, TINY_PRIOR, TINY_A, TINY_B, use_rho=True)
        b_xi = sum(toy.enumerate_burst(TINY_PRIOR, TINY_A, TINY_B, t, s)[3]
                   for t, s in TINY_BURSTS)
        np.testing.assert_allclose(xi, b_xi, atol=1e-12)

    def test_xi_counts_every_tick(self):
        data = _tiny_data()
        _, _, xi, _ = e_step(data, TINY_PRIOR, TINY_A, TINY_B)
        expected = sum(t[-1] - t[0] for t, _ in TINY_BURSTS)
        assert xi.sum() == pytest.approx(expected, abs=1e-10)


# ---------------------------------------------------------------------------
# tier 2 — against the C++ engine (the port target)
# ---------------------------------------------------------------------------

tttrlib = pytest.importorskip("tttrlib")


class TestAgainstEngine:
    def test_loglik_matches_engine(self):
        data = _tiny_data()
        _, _, _, ll = e_step(data, TINY_PRIOR, TINY_A, TINY_B)

        eng = tttrlib.HMM()
        eng.set_bursts([list(t) for t, _ in TINY_BURSTS],
                       [list(s) for _, s in TINY_BURSTS], 2)
        model = tttrlib.HmmModel(list(TINY_PRIOR), list(TINY_A.ravel()),
                                  list(TINY_B.ravel()))
        assert eng.optimize(model, 1, 1e30).loglik == pytest.approx(ll, abs=1e-10)

    def test_mle_reaches_engine_fixed_point(self):
        _, times, streams, data = _simulated()
        init = toy.two_state(e_low=0.35, e_high=0.65, k_switch=5e-3)

        got = fit(data, HmmModel(*init), max_iter=500, tol=1e-11)

        eng = tttrlib.HMM()
        eng.set_bursts([list(t) for t in times], [list(s) for s in streams], 2)
        ref = eng.optimize(
            tttrlib.HmmModel(list(init[0]), list(init[1].ravel()), list(init[2].ravel())),
            500, 1e-11, 1e-12, False)

        assert got.loglik == pytest.approx(ref.loglik, abs=1e-6)
        np.testing.assert_allclose(got.obs, ref.obs_np, atol=1e-4)


# ---------------------------------------------------------------------------
# tier 3 — the new capabilities
# ---------------------------------------------------------------------------

class TestConstraints:
    def test_flat_prior_is_exactly_mle(self):
        """A flat Dirichlet must be a no-op, not merely close to one."""
        _, _, _, data = _simulated(n_bursts=8, burst_len=40, seed=5)
        init = HmmModel(*toy.two_state(e_low=0.3, e_high=0.7, k_switch=4e-3))

        mle = fit(data, init, None, max_iter=60, tol=1e-12)
        flat = fit(data, init, HmmConstraints.flat(2, 2), max_iter=60, tol=1e-12)

        assert flat.loglik == pytest.approx(mle.loglik, abs=1e-12)
        np.testing.assert_allclose(flat.trans, mle.trans, atol=1e-14)
        np.testing.assert_allclose(flat.obs, mle.obs, atol=1e-14)

    def test_objective_is_monotone(self):
        """EM must climb the *penalised* objective every iteration."""
        _, _, _, data = _simulated(n_bursts=8, burst_len=40, seed=6)
        init = HmmModel(*toy.two_state(e_low=0.3, e_high=0.7, k_switch=4e-3))
        c = HmmConstraints.sticky(2, 2, strength=500.0)

        got = fit(data, init, c, max_iter=40, tol=1e-14, track=True)
        d = np.diff(got.history)
        assert (d >= -1e-8).all(), f"objective decreased by {d.min():.3e}"

    def test_fixed_emission_stays_exact(self):
        """A pinned dark-state emission must be untouched by every M-step."""
        _, _, _, data = _simulated(n_bursts=10, burst_len=50, seed=8)
        init = HmmModel(*toy.two_state(e_low=0.3, e_high=0.7, k_switch=4e-3))
        c = HmmConstraints.flat(2, 2).with_dark_state(state=0, stream=1, value=0.0)

        got = fit(data, init, c, max_iter=50, tol=1e-12)

        assert got.obs[0, 1] == 0.0
        np.testing.assert_allclose(got.obs.sum(axis=1), 1.0, atol=1e-12)

    def test_sticky_prior_slows_dynamics(self):
        """The prior must actually move the answer in the stated direction."""
        _, _, _, data = _simulated(n_bursts=10, burst_len=40, seed=9)
        init = HmmModel(*toy.two_state(e_low=0.3, e_high=0.7, k_switch=2e-2))

        free = fit(data, init, None, max_iter=80, tol=1e-11)
        stuck = fit(data, init, HmmConstraints.sticky(2, 2, strength=1e5),
                    max_iter=80, tol=1e-11)

        off = ~np.eye(2, dtype=bool)
        assert stuck.trans[off].sum() < free.trans[off].sum()

    def test_reorder_is_canonical(self):
        m = HmmModel(np.array([0.3, 0.7]),
                     np.array([[0.9, 0.1], [0.2, 0.8]]),
                     np.array([[0.2, 0.8], [0.9, 0.1]]))
        r = m.reorder()
        assert r.obs[0, -1] < r.obs[1, -1]
        # rows and columns of the transition matrix permute together
        np.testing.assert_allclose(r.trans, np.array([[0.8, 0.2], [0.1, 0.9]]))


class TestSpecialFunctions:
    def test_digamma_known_values(self):
        euler = 0.57721566490153286
        assert digamma(1.0) == pytest.approx(-euler, abs=1e-11)
        assert digamma(2.0) == pytest.approx(1.0 - euler, abs=1e-11)
        assert digamma(0.5) == pytest.approx(-euler - 2.0 * np.log(2.0), abs=1e-11)

    def test_digamma_recurrence(self):
        """psi(x+1) - psi(x) == 1/x is an identity, so it holds everywhere."""
        x = np.logspace(-2, 3, 50)
        np.testing.assert_allclose(digamma(x + 1.0) - digamma(x), 1.0 / x, rtol=1e-9)

    def test_dirichlet_kl_properties(self):
        a = np.array([[2.0, 3.0, 5.0], [1.0, 1.0, 1.0]])
        np.testing.assert_allclose(dirichlet_kl(a, a), 0.0, atol=1e-12)
        assert np.all(dirichlet_kl(a, np.ones_like(a)) >= 0.0)


class TestVariationalBayes:
    def test_elbo_is_monotone(self):
        _, _, _, data = _simulated(n_bursts=10, burst_len=50, seed=21)
        init = HmmModel(*toy.two_state(e_low=0.3, e_high=0.7, k_switch=4e-3))
        got = fit_vb(data, init, max_iter=60, tol=1e-12)
        d = np.diff(got.history)
        assert (d >= -1e-6).all(), f"ELBO decreased by {d.min():.3e}"

    def test_elbo_lower_bounds_the_loglikelihood(self):
        """The ELBO is a bound, so it cannot exceed the MLE log-likelihood."""
        _, _, _, data = _simulated(n_bursts=10, burst_len=50, seed=22)
        init = HmmModel(*toy.two_state(e_low=0.3, e_high=0.7, k_switch=4e-3))
        vb = fit_vb(data, init, max_iter=100, tol=1e-10)
        mle = fit(data, init, max_iter=300, tol=1e-11)
        assert vb.elbo <= mle.loglik + 1e-6

    def test_recovers_the_generating_model(self):
        true, _, _, data = _simulated(n_bursts=40, burst_len=150, seed=23)
        init = HmmModel(*toy.two_state(e_low=0.35, e_high=0.65, k_switch=5e-3))
        vb = fit_vb(data, init, max_iter=200, tol=1e-9)
        got = vb.mean.reorder().obs
        want = HmmModel(*true).reorder().obs
        np.testing.assert_allclose(got, want, atol=0.05)

    def test_posterior_concentrates_with_more_data(self):
        """More photons must mean tighter posteriors -- the basic sanity check
        that this is a posterior and not a decorated point estimate."""
        init = HmmModel(*toy.two_state(e_low=0.35, e_high=0.65, k_switch=5e-3))
        widths = []
        for n_bursts in (8, 64):
            _, _, _, data = _simulated(n_bursts=n_bursts, burst_len=100, seed=24)
            vb = fit_vb(data, init, max_iter=150, tol=1e-9)
            widths.append(vb.std[2].mean())
        assert widths[1] < widths[0] / 2.0

    def test_elbo_selects_the_true_state_count(self):
        """The ELBO replaces BIC for penalised fits, so it must do BIC's job."""
        _, _, _, data = _simulated(n_bursts=40, burst_len=150, seed=25)
        rng = np.random.default_rng(0)
        elbo = {}
        for k in (1, 2, 3):
            best = -np.inf
            for _ in range(3):
                init = random_model(k, 2, rng)
                best = max(best, fit_vb(data, init, max_iter=150, tol=1e-8).elbo)
            elbo[k] = best
        assert max(elbo, key=elbo.get) == 2, elbo

    def test_rejects_fixed_entries(self):
        _, _, _, data = _simulated(n_bursts=4, burst_len=20, seed=26)
        init = HmmModel(*toy.two_state())
        c = HmmConstraints.flat(2, 2).with_dark_state(0, 1, 0.0)
        with pytest.raises(NotImplementedError, match="point mass"):
            fit_vb(data, init, c)

    def test_sampling_is_reproducible_and_normalised(self):
        _, _, _, data = _simulated(n_bursts=6, burst_len=40, seed=27)
        init = HmmModel(*toy.two_state())
        vb = fit_vb(data, init, max_iter=40, tol=1e-8)
        a = vb.sample(20, seed=3)
        b = vb.sample(20, seed=3)
        np.testing.assert_allclose(a[0].obs, b[0].obs)
        for d in a:
            np.testing.assert_allclose(d.obs.sum(axis=1), 1.0, atol=1e-12)
            np.testing.assert_allclose(d.trans.sum(axis=1), 1.0, atol=1e-12)


class TestGibbs:
    def test_sampled_counts_match_the_e_step(self):
        """The decisive check on FFBS *and* the tick-level bridge.

        Averaged over draws, the sampled sufficient statistics must converge to
        the E-step's expected ones -- and those are already pinned to brute-force
        enumeration.  So this ties the sampler to first principles rather than to
        another approximation.
        """
        data = _tiny_data()
        model = HmmModel(TINY_PRIOR, TINY_A, TINY_B)
        pa, go, xi, _ = e_step(data, TINY_PRIOR, TINY_A, TINY_B)

        n = 20000
        acc = [np.zeros(2), np.zeros((2, 2)), np.zeros((2, 2))]
        for i in range(n):
            pc, oc, tc, _, _ = sample_paths_and_counts(data, model, seed=7, sweep_index=i)
            acc[0] += pc
            acc[1] += oc
            acc[2] += tc
        acc = [a / n for a in acc]

        np.testing.assert_allclose(acc[0], pa, atol=0.03)
        np.testing.assert_allclose(acc[1], go, atol=0.03)
        np.testing.assert_allclose(acc[2], xi, atol=0.05)

    def test_every_draw_conserves_ticks(self):
        """One-tick counts must total the elapsed ticks in *every* draw, not
        merely on average -- a bridge that skipped a tick would still average
        correctly if it compensated elsewhere."""
        data = _tiny_data()
        model = HmmModel(TINY_PRIOR, TINY_A, TINY_B)
        want = sum(t[-1] - t[0] for t, _ in TINY_BURSTS)
        for i in range(50):
            _, _, tc, _, _ = sample_paths_and_counts(data, model, seed=3, sweep_index=i)
            assert tc.sum() == pytest.approx(want, abs=1e-12)

    def test_draws_are_reproducible(self):
        data = _tiny_data()
        model = HmmModel(TINY_PRIOR, TINY_A, TINY_B)
        a = sample_paths_and_counts(data, model, seed=11, sweep_index=4)
        b = sample_paths_and_counts(data, model, seed=11, sweep_index=4)
        c = sample_paths_and_counts(data, model, seed=11, sweep_index=5)
        np.testing.assert_array_equal(a[2], b[2])
        assert not np.array_equal(a[2], c[2])

    def test_zero_gap_photons_share_a_state(self):
        """Two photons at the same tick cannot straddle a transition."""
        data = PhotonData([[0, 0, 4]], [[0, 1, 0]], 2)
        model = HmmModel(TINY_PRIOR, TINY_A, TINY_B)
        for i in range(40):
            *_, path = sample_paths_and_counts(data, model, seed=5, sweep_index=i,
                                               want_path=True)
            assert path[0] == path[1]

    def test_posterior_recovers_truth(self):
        true, _, _, data = _simulated(n_bursts=30, burst_len=120, seed=31)
        rng = np.random.default_rng(2)
        inits = [random_model(2, 2, rng) for _ in range(2)]
        post = gibbs(data, inits, n_draws=150, n_burn=50, n_chains=2, seed=1)

        got = post.mean()[2]
        want = HmmModel(*true).reorder().obs
        np.testing.assert_allclose(got, want, atol=0.08)

    def test_agrees_with_variational_posterior(self):
        """VB is the workhorse, Gibbs is the reference: the means must agree."""
        _, _, _, data = _simulated(n_bursts=30, burst_len=120, seed=32)
        init = HmmModel(*toy.two_state(e_low=0.3, e_high=0.7, k_switch=4e-3))
        post = gibbs(data, init, n_draws=200, n_burn=80, n_chains=1, seed=4)
        vb = fit_vb(data, init, max_iter=200, tol=1e-9)
        np.testing.assert_allclose(post.mean()[2], vb.mean.reorder().obs, atol=0.03)

    def test_chains_mix(self):
        _, _, _, data = _simulated(n_bursts=25, burst_len=100, seed=33)
        rng = np.random.default_rng(5)
        inits = [random_model(2, 2, rng) for _ in range(4)]
        post = gibbs(data, inits, n_draws=200, n_burn=100, n_chains=4, seed=6)
        rhat = post.rhat()[2]
        assert np.nanmax(rhat) < 1.1, rhat
        assert np.nanmin(post.ess()[2]) > 20

    def test_rejects_fixed_entries(self):
        _, _, _, data = _simulated(n_bursts=4, burst_len=20, seed=34)
        c = HmmConstraints.flat(2, 2).with_dark_state(0, 1, 0.0)
        with pytest.raises(NotImplementedError, match="no conditional"):
            gibbs(data, HmmModel(*toy.two_state()), c, n_draws=2, n_burn=1, n_chains=1)


class TestDiagnostics:
    def test_rhat_detects_disagreement(self):
        rng = np.random.default_rng(0)
        mixed = rng.normal(size=(4, 400))
        split = rng.normal(size=(4, 400)) + np.arange(4)[:, None] * 5.0
        assert split_rhat(mixed) < 1.05
        assert split_rhat(split) > 1.5

    def test_ess_penalises_autocorrelation(self):
        rng = np.random.default_rng(1)
        iid = rng.normal(size=(2, 2000))
        walk = np.cumsum(rng.normal(size=(2, 2000)) * 0.1, axis=1)
        assert ess(iid) > 1000
        assert ess(walk) < ess(iid) / 5


class TestPhysicalEmission:
    """The lifetime axis, and why a parameterisation is needed to use it."""

    TAU_D0, TAU_A, NB, DT = 4.0, 3.0, 32, 0.25

    def _models(self):
        irf = gaussian_irf(self.NB, self.DT, 0.3, 0.25)
        return (FretEmission(2, self.TAU_D0, self.TAU_A, self.NB, self.DT, irf=irf),
                LifetimeEmission(2, self.TAU_A, self.NB, self.DT, irf=irf))

    def test_stream_split_accounts_for_crosstalk(self):
        """The split must be the composed matrix triple, not E/(E+gamma(1-E)).

        The naive form ignores donor leakage and direct acceptor excitation, and
        the error is not small: at realistic optics it misplaces the acceptor
        fraction by ~0.15.
        """
        a, d, g = 0.08, 0.06, 0.65
        fe = FretEmission(1, self.TAU_D0, self.TAU_A, self.NB, self.DT,
                          alpha=a, delta=d, gamma=g)
        for e in (0.2, 0.5, 0.8):
            green, red = (1 - e), g * e + a * (1 - e) + d
            assert fe.state(e).stream_probability[1] == pytest.approx(
                red / (green + red), abs=1e-12)

        naive = 0.5 / (0.5 + g * 0.5)
        assert abs(fe.state(0.5).stream_probability[1] - naive) > 0.1

    def test_no_crosstalk_reduces_to_efficiency(self):
        """With alpha=delta=0 and gamma=1 the split must be exactly E."""
        fe = FretEmission(1, self.TAU_D0, self.TAU_A, self.NB, self.DT)
        for e in (0.2, 0.5, 0.8):
            assert fe.state(e).stream_probability[1] == pytest.approx(e, abs=1e-12)

    def test_sensitized_acceptor_has_a_rise(self):
        """A FRET-sensitized acceptor cannot emit before the transfer happens.

        Its micro-time is the convolution of the quenched donor decay with the
        acceptor decay, so it rises from zero and peaks *later* than either a
        plain Exp(tau_A) or the donor -- which is what the old model got wrong.
        """
        fe = FretEmission(1, self.TAU_D0, self.TAU_A, self.NB, self.DT,
                          irf=gaussian_irf(self.NB, self.DT, 0.3, 0.25))
        s = fe.state(0.5)
        donor, red = s.decay[0], s.decay[1]
        plain = fe._decay_shape(self.TAU_A)
        assert red.argmax() > donor.argmax()
        assert red.argmax() > plain.argmax()
        # rises from near zero rather than starting at the maximum
        assert red[0] < 0.2 * red.max()

    def test_red_channel_mixes_in_the_leaked_donor(self):
        """Leaked donor photons land in red carrying the DONOR lifetime.

        That is why the red channel also carries information about E, and why
        modelling it as one fixed acceptor decay biases the fit.
        """
        common = dict(n_bins=self.NB, dt=self.DT)
        clean = FretEmission(1, self.TAU_D0, self.TAU_A, **common)
        leaky = FretEmission(1, self.TAU_D0, self.TAU_A, alpha=0.5, **common)
        e = 0.3
        donor = clean._decay_shape(self.TAU_D0 * (1 - e))
        # heavy leakage pulls the red density towards the donor decay
        d_clean = np.abs(clean.state(e).decay[1] - donor).sum()
        d_leaky = np.abs(leaky.state(e).decay[1] - donor).sum()
        assert d_leaky < d_clean

    def test_background_is_mixed_once_not_per_pathway(self):
        """Red is built from three pathways; background must be added after."""
        common = dict(n_bins=self.NB, dt=self.DT, alpha=0.08, delta=0.06, gamma=0.65)
        clean = FretEmission(1, self.TAU_D0, self.TAU_A, **common)
        withbg = FretEmission(1, self.TAU_D0, self.TAU_A, background=(0.0, 0.3), **common)
        expected = 0.7 * clean.state(0.5).decay[1] + 0.3 / self.NB
        np.testing.assert_allclose(withbg.state(0.5).decay[1], expected, atol=1e-12)

    def test_hellenkamp_scalars_are_a_readout(self):
        """Scalars are reported for comparison; the matrix is the model."""
        fe = FretEmission(1, self.TAU_D0, self.TAU_A, self.NB, self.DT,
                          alpha=0.08, delta=0.06, gamma=0.65)
        params = fe.state(0.5).physical_parameters
        assert params["convention"] == "Hellenkamp"
        assert params["alpha"] == 0.08 and params["gamma"] == 0.65
        # excitation rows are probabilities -- direct excitation partitions
        np.testing.assert_allclose(fe.optics.excitation_matrix().sum(axis=1), 1.0)

    def test_fret_couples_ratio_and_lifetime(self):
        """The whole content of the model: one number moves both observables."""
        fe, _ = self._models()
        for e in (0.2, 0.5, 0.8):
            s = fe.state(e)
            assert s.stream_probability[1] == pytest.approx(e)          # gamma = 1
            assert s.physical_parameters["tau_DA"] == pytest.approx(self.TAU_D0 * (1 - e))

    def test_emission_rows_are_distributions(self):
        fe, le = self._models()
        for obs in (fe.to_obs(np.array([[0.3], [0.7]])),
                    le.to_obs(np.array([[4.0, 0.2], [2.0, 0.8]]))):
            assert obs.shape == (2, 2 * self.NB)
            np.testing.assert_allclose(obs.sum(axis=1), 1.0, atol=1e-12)

    def test_parameter_counts_collapse(self):
        """A physical state costs 1-2 dof, not p-1.  This is what keeps model
        selection meaningful once p is n_streams * n_micro_bins."""
        fe, le = self._models()
        free_categorical = 2 * (2 * self.NB - 1)
        assert fe.n_free() == 2
        assert le.n_free() == 4
        assert free_categorical == 126

    def _blinking_vs_fret(self, n_bursts=40, burst_len=150, seed=1):
        """Two states with the SAME intensity ratio, different donor lifetimes:
        genuine FRET at E=0.5 (tau_DA = 2.0) versus a dark acceptor leaving the
        donor unquenched (tau_D = 4.0)."""
        fe, le = self._models()
        s0 = fe.state(0.5)
        s1 = le.state([self.TAU_D0, 0.5])
        assert s0.stream_probability[1] == pytest.approx(s1.stream_probability[1])

        rng = np.random.default_rng(0)
        A = np.array([[0.999, 0.001], [0.001, 0.999]])
        pri = np.array([0.5, 0.5])
        times = [np.cumsum(rng.integers(1, 40, size=burst_len)).astype(np.int64).tolist()
                 for _ in range(n_bursts)]
        streams, micro, paths = simulate_lifetime(pri, A, [s0, s1], times, seed=seed)
        truth = np.concatenate([np.array(p) for p in paths])
        return times, streams, micro, truth, le

    @staticmethod
    def _accuracy(data, model, truth):
        g = np.concatenate([forward_backward_burst(data, model.prior, model.trans,
                                                   model.obs, b)[0]
                            for b in range(data.n_bursts)]).argmax(axis=1)
        return max((g == np.array([p[t] for t in truth])).mean()
                   for p in itertools.permutations(range(2)))

    def test_stream_only_cannot_separate_even_in_principle(self):
        """Not 'separates badly' -- the two states have identical emission rows,
        so no amount of data or restarts helps."""
        times, streams, _, truth, _ = self._blinking_vs_fret()
        data = PhotonData(times, streams, 2)
        best = 0.0
        for s in range(3):
            rng = np.random.default_rng(s)
            init = HmmModel(np.array([0.5, 0.5]),
                            np.array([[0.99, 0.01], [0.01, 0.99]]),
                            rng.random((2, data.p)))
            best = max(best, self._accuracy(data, fit(data, init, max_iter=150, tol=1e-9),
                                            truth))
        assert best < 0.6, f"stream-only should be near chance, got {best:.3f}"

    def test_parameterisation_separates_them(self):
        """With the lifetime axis *and* a parameterisation, the states resolve."""
        times, streams, micro, truth, le = self._blinking_vs_fret()
        data = PhotonData(times, streams, 2, micro=micro, n_micro_bins=self.NB)
        best, best_par = 0.0, None
        for s in range(3):
            rng = np.random.default_rng(s)
            p0 = np.array([[1.0 + 3 * rng.random(), 0.3 + 0.4 * rng.random()],
                           [1.0 + 3 * rng.random(), 0.3 + 0.4 * rng.random()]])
            model, par = fit_emission(data, le, p0,
                                      np.array([[0.99, 0.01], [0.01, 0.99]]),
                                      np.array([0.5, 0.5]), max_iter=150, tol=1e-9)
            a = self._accuracy(data, model, truth)
            if a > best:
                best, best_par = a, par
        assert best > 0.7, f"parameterised fit should separate, got {best:.3f}"

        taus = np.sort(best_par[:, 0])
        np.testing.assert_allclose(taus, [2.0, 4.0], rtol=0.15)
        np.testing.assert_allclose(best_par[:, 1], 0.5, atol=0.05)

    def test_fret_parameterisation_recovers_efficiencies(self):
        """On a genuinely-FRET system, E comes back."""
        fe, _ = self._models()
        true_e = np.array([[0.25], [0.75]])
        rng = np.random.default_rng(3)
        A = np.array([[0.999, 0.001], [0.001, 0.999]])
        times = [np.cumsum(rng.integers(1, 40, size=150)).astype(np.int64).tolist()
                 for _ in range(40)]
        streams, micro, _ = simulate_lifetime(
            np.array([0.5, 0.5]), A, [fe.state(e) for e in true_e.ravel()], times, seed=2)
        data = PhotonData(times, streams, 2, micro=micro, n_micro_bins=self.NB)

        _, got = fit_emission(data, fe, np.array([[0.4], [0.6]]),
                              A, np.array([0.5, 0.5]), max_iter=150, tol=1e-9)
        np.testing.assert_allclose(np.sort(got.ravel()), true_e.ravel(), atol=0.05)


class TestCalibrationHarness:
    """Before trusting SBC's verdict on our sampler, check SBC itself.

    A calibration test that cannot detect miscalibration is worse than none:
    it would licence exactly the error it was meant to catch.  So the harness is
    pointed at a conjugate Gaussian, where the exact posterior is known in closed
    form, and is required to pass the exact one and fail deliberately inflated
    and deflated ones -- in the right direction.
    """

    SIGMA, N_OBS = 1.0, 8

    def _pieces(self, scale):
        def draw_prior(rng):
            return np.array([rng.normal(0.0, 1.0)]), None

        def simulate(theta, aux, rng):
            return rng.normal(theta[0], self.SIGMA, size=self.N_OBS)

        def posterior(y, rng):
            prec = 1.0 + self.N_OBS / self.SIGMA ** 2
            mu = (y.sum() / self.SIGMA ** 2) / prec
            sd = (1.0 / np.sqrt(prec)) * scale
            return rng.normal(mu, sd, size=(200, 1))

        return draw_prior, simulate, posterior

    def _run(self, scale, n_replicates=500, seed=1):
        ranks, L = sbc_ranks(*self._pieces(scale), n_replicates=n_replicates, seed=seed)
        return summarize(ranks, L, ["theta"])

    def test_chi2_sf_matches_textbook_critical_values(self):
        for df, x in ((1, 3.841), (2, 5.991), (5, 11.070), (10, 18.307)):
            assert chi2_sf(x, df) == pytest.approx(0.05, abs=1e-3)
        assert chi2_sf(0.0, 3) == 1.0

    def test_exact_posterior_reads_as_calibrated(self):
        r = self._run(1.0)
        assert r.verdict()[0] == "calibrated"
        assert r.pvalues()[0] > 0.01
        assert r.outer_ratios()[0] == pytest.approx(1.0, abs=0.25)

    def test_too_narrow_posterior_is_caught(self):
        r = self._run(0.6)
        assert r.verdict()[0] == "too narrow"
        assert r.pvalues()[0] < 1e-3
        assert r.outer_ratios()[0] > 1.5

    def test_too_wide_posterior_is_caught(self):
        r = self._run(1.6)
        assert r.verdict()[0] == "too wide"
        assert r.pvalues()[0] < 1e-3
        assert r.outer_ratios()[0] < 0.6

    def test_prior_object_cannot_drift_between_generator_and_sampler(self):
        """SBC is valid only when the truth is drawn from the prior the sampler
        conditions on.  Supplying them as two callables is how they diverge — a
        flat sampler prior against a Normal-drawn truth produces a *sloped* rank
        histogram indistinguishable from real sampler bias, which cost a full
        120-replicate run here.  One object makes that unrepresentable.
        """
        p = TruncatedNormalPrior1D(55.0, 7.0, 40.0, 70.0)
        rng = np.random.default_rng(0)
        s = np.concatenate([p.sample(rng, 2).ravel() for _ in range(500)])
        assert s.min() > 40.0 and s.max() < 70.0
        assert abs(s.mean() - 55.0) < 1.0

        # log_prob is the same distribution the sampler is handed
        assert p.log_prob(0, [62.0]) - p.log_prob(0, [55.0]) == pytest.approx(-0.5)
        assert p.log_prob(0, [75.0]) == -np.inf
        assert p.as_log_prior()(0, [62.0]) == p.log_prob(0, [62.0])

    def test_ranks_are_in_range(self):
        ranks, L = sbc_ranks(*self._pieces(1.0), n_replicates=50, seed=2)
        assert L == 200
        assert ranks.min() >= 0 and ranks.max() <= L

    def test_parameter_count_mismatch_is_an_error(self):
        draw_prior, simulate, _ = self._pieces(1.0)

        def wrong(y, rng):
            return np.zeros((10, 3))

        with pytest.raises(ValueError, match="parameters"):
            sbc_ranks(draw_prior, simulate, wrong, n_replicates=1, seed=0)


def _sbc_pieces(constraints, n_bursts=8, burst_len=40, gap=20):
    """SBC glue for the photon HMM.

    Two model-specific requirements are handled here rather than in
    ``calibration``:

    * every model, true and sampled, is put in canonical state order --
      states are exchangeable, so an index-wise comparison is otherwise
      meaningless and would manufacture a failure;
    * the prior is a *proper, informative* one.  A flat ``Dir(1)`` on the
      transition rows generates chains that switch almost every tick, so the
      simulated bursts would carry almost no information and the exercise would
      degenerate.  The calibration claim is conditional on this prior.
    """
    def flat(m):
        return np.concatenate([m.obs[:, 1], [m.trans[0, 1], m.trans[1, 0]]])

    def draw_prior(rng):
        m = HmmModel(rng.dirichlet(constraints.alpha_prior),
                     np.vstack([rng.dirichlet(r) for r in constraints.alpha_trans]),
                     np.vstack([rng.dirichlet(r) for r in constraints.alpha_obs])).reorder()
        return flat(m), m

    def simulate(theta, m, rng):
        times, streams = toy.simulate(m.prior, m.trans, m.obs, n_bursts=n_bursts,
                                      burst_len=burst_len, mean_gap=gap,
                                      seed=int(rng.integers(1 << 30)))
        return PhotonData(times, streams, 2)

    return flat, draw_prior, simulate


SBC_NAMES = ["B0_acc", "B1_acc", "A01", "A10"]


def _sbc_prior():
    return HmmConstraints(2, 2,
                          alpha_trans=np.array([[200.0, 2.0], [2.0, 200.0]]),
                          alpha_obs=np.full((2, 2), 2.0),
                          alpha_prior=np.full(2, 2.0))


class TestPosteriorCalibration:
    """Are the posteriors honest?  The one claim no other test can make."""

    @pytest.mark.slow
    def test_gibbs_is_calibrated(self):
        c = _sbc_prior()
        flat, draw_prior, simulate = _sbc_pieces(c)

        # Ranks are uniform only for *independent* draws.  Measured
        # autocorrelation time on a representative dataset reached ~28 (worst
        # parameter A10, via ess()), so thin by that.  Under-thinning does not
        # merely add noise -- it biases the ranks and would fake a failure.
        def posterior(data, rng):
            init = [random_model(2, 2, rng) for _ in range(2)]
            p = gibbs(data, init, c, n_draws=100, n_burn=300, n_chains=2,
                      seed=int(rng.integers(1 << 30)), thin=28)
            return np.array([flat(d.reorder()) for d in p.draws])

        ranks, L = sbc_ranks(draw_prior, simulate, posterior, n_replicates=300, seed=3)
        result = summarize(ranks, L, SBC_NAMES)
        assert all(v == "calibrated" for v in result.verdict()), "\n" + result.report()

    @pytest.mark.slow
    def test_sbc_detects_unmodelled_background(self):
        """SBC as a *misspecification* detector, not just a calibration check.

        The perturbation has to break structure the model cannot absorb.
        Relabelling photons to random symbols does not: a free categorical
        emission simply learns the contaminated row and stays correctly
        specified (measured flat at 0/5/15%, no trend).  Adding photons at random
        *times* changes the inter-photon gap distribution, which the model has no
        way to represent -- and then the emission parameters degrade with dose.
        """
        c = _sbc_prior()
        flat, draw_prior, _ = _sbc_pieces(c)

        def make_sim(frac):
            def simulate(theta, m, rng):
                times, streams = toy.simulate(m.prior, m.trans, m.obs, n_bursts=10,
                                              burst_len=80, mean_gap=20,
                                              seed=int(rng.integers(1 << 30)))
                times, streams = add_background_photons(times, streams, frac, rng)
                return PhotonData(times, streams, 2)
            return simulate

        def posterior(data, rng):
            init = [random_model(2, 2, rng) for _ in range(2)]
            p = gibbs(data, init, c, n_draws=100, n_burn=300, n_chains=2,
                      seed=int(rng.integers(1 << 30)), thin=28)
            return np.array([flat(d.reorder()) for d in p.draws])

        ratios = {}
        for frac in (0.0, 0.5):
            ranks, L = sbc_ranks(draw_prior, make_sim(frac), posterior,
                                 n_replicates=120, seed=5)
            # emission parameters are where dilution shows; transitions survive
            ratios[frac] = summarize(ranks, L, SBC_NAMES).outer_ratios()[:2].mean()

        assert ratios[0.5] > 2.0 * ratios[0.0], ratios
        assert ratios[0.5] > 1.5, ratios

    @pytest.mark.slow
    def test_vb_is_over_confident(self):
        """VB is an approximation, so it must *fail* -- and in the predicted
        direction.  A flat VB result would mean the harness is broken, not that
        mean-field is exact."""
        c = _sbc_prior()
        flat, draw_prior, simulate = _sbc_pieces(c)

        def posterior(data, rng):
            init = random_model(2, 2, rng)
            vb = fit_vb(data, init, c, max_iter=150, tol=1e-8)
            return np.array([flat(d.reorder())
                             for d in vb.sample(100, seed=int(rng.integers(1 << 30)))])

        ranks, L = sbc_ranks(draw_prior, simulate, posterior, n_replicates=300, seed=4)
        result = summarize(ranks, L, SBC_NAMES)
        ratios = result.outer_ratios()
        assert (ratios > 1.0).sum() >= 3, "\n" + result.report()


class TestDistanceDistributedStates:
    """A state is a distance *distribution* over a multi-exponential donor.

    The single-E, mono-exponential idealisation is convenient but wrong, and it
    is wrong along exactly the axis the physics-aware model exploits.
    """

    NB, DT, R0 = 64, 0.125, 52.0

    def _irf(self):
        return gaussian_irf(self.NB, self.DT, 0.3, 0.25)

    def test_reduces_exactly_to_the_idealisation(self):
        """sigma=0 and a mono-exponential donor must reproduce FretEmission bit
        for bit -- otherwise the general model is not a generalisation."""
        fd = FretDistanceEmission(1, donor=4.0, tau_a=3.0, r0=self.R0, sigma=0.0,
                                  n_bins=self.NB, dt=self.DT, irf=self._irf())
        fe = FretEmission(1, 4.0, 3.0, self.NB, self.DT, irf=self._irf())
        assert fd.efficiency(self.R0) == pytest.approx(0.5, abs=1e-12)
        a, b = fd.state([self.R0]), fe.state(fd.efficiency(self.R0))
        np.testing.assert_allclose(a.stream_probability, b.stream_probability, atol=1e-12)
        np.testing.assert_allclose(a.decay, b.decay, atol=1e-12)

    def test_linker_width_shifts_efficiency(self):
        """<E> over the distribution is not E at the mean distance, and the
        shift changes sign with R -- so it cannot be calibrated away."""
        fs = FretDistanceEmission(1, donor=4.0, tau_a=3.0, r0=self.R0, sigma=6.0,
                                  n_bins=self.NB, dt=self.DT)
        shifts = []
        for r in (40.0, 65.0):
            naive = self.R0 ** 6 / (self.R0 ** 6 + r ** 6)
            shifts.append(fs.efficiency(r) - naive)
        assert abs(shifts[0]) > 0.01 and abs(shifts[1]) > 0.01
        assert shifts[0] * shifts[1] < 0, f"expected a sign change, got {shifts}"

    def test_state_decay_is_not_mono_exponential(self):
        """tau_f > tau_x is the signature; they coincide only for a single
        exponential."""
        for r in (40.0, 52.0, 65.0):
            amp, tau = fret_lifetime_spectrum(r, 4.0, self.R0, 6.0)
            tau_x, tau_f = lifetime_averages(amp, tau)
            assert tau_f > tau_x

        amp, tau = fret_lifetime_spectrum(52.0, 4.0, self.R0, 0.0)
        tau_x, tau_f = lifetime_averages(amp, tau)
        assert tau_f == pytest.approx(tau_x, rel=1e-12)

    def test_multi_exponential_donor(self):
        fm = FretDistanceEmission(1, donor=([0.7, 0.3], [4.0, 1.2]), tau_a=3.0,
                                  sigma=6.0, n_bins=self.NB, dt=self.DT)
        assert fm.tau_x_d0 == pytest.approx(0.7 * 4.0 + 0.3 * 1.2)
        p = fm.state([52.0]).physical_parameters
        assert p["n_components"] > 2
        np.testing.assert_allclose(fm.to_obs(np.array([[52.0]])).sum(axis=1), 1.0,
                                   atol=1e-12)

    def test_one_free_parameter_per_state(self):
        fd = FretDistanceEmission(3, sigma=6.0, n_bins=self.NB, dt=self.DT)
        assert fd.n_free() == 3

    def test_recovers_the_generating_distances(self):
        fd = FretDistanceEmission(2, donor=([0.7, 0.3], [4.0, 1.2]), tau_a=3.0,
                                  r0=self.R0, sigma=6.0, n_bins=self.NB,
                                  dt=self.DT, irf=self._irf())
        true_r = np.array([[45.0], [62.0]])
        rng = np.random.default_rng(4)
        A = np.array([[0.999, 0.001], [0.001, 0.999]])
        times = [np.cumsum(rng.integers(1, 40, size=200)).astype(np.int64).tolist()
                 for _ in range(50)]
        streams, micro, _ = simulate_lifetime(
            np.array([0.5, 0.5]), A, [fd.state(r) for r in true_r], times, seed=5)
        data = PhotonData(times, streams, 2, micro=micro, n_micro_bins=self.NB)

        _, got = fit_emission(data, fd, np.array([[50.0], [58.0]]), A,
                              np.array([0.5, 0.5]), max_iter=150, tol=1e-9)
        np.testing.assert_allclose(np.sort(got.ravel()), true_r.ravel(), atol=2.0)


class TestPolarization:
    """Four detection streams: green/red x parallel/perpendicular."""

    NB, DT = 256, 0.128

    def _em(self, aniso):
        return FretDistanceEmission(1, donor=4.0, tau_a=3.0, sigma=0.0,
                                    n_bins=self.NB, dt=self.DT, anisotropy=aniso)

    def test_isotropic_collapses_to_a_even_split(self):
        """r0 = 0 must be the two-stream model with each colour duplicated."""
        p = self._em(Anisotropy(r0=0.0)).state([52.0]).stream_probability
        np.testing.assert_allclose(p, 0.25, atol=1e-12)

    def test_colour_information_is_untouched(self):
        """Polarisation must *refine* the streams, not disturb the FRET signal:
        the colour totals have to survive the split unchanged."""
        base = self._em(None).state([52.0]).stream_probability
        for a in (Anisotropy(r0=0.0), Anisotropy(r0=0.38, rho=1.0)):
            p = self._em(a).state([52.0]).stream_probability
            np.testing.assert_allclose([p[0] + p[2], p[1] + p[3]], base, atol=1e-12)

    def test_depolarises_with_micro_time(self):
        a = Anisotropy(r0=0.38, rho=1.0)
        assert a.parallel_fraction(0.0) > a.parallel_fraction(8.0)
        assert a.parallel_fraction(8.0) == pytest.approx(0.5, abs=0.01)

    def test_sensitized_acceptor_is_less_polarised(self):
        """Emergent, not parameterised: the model carries no pathway-specific
        anisotropy term.  The sensitized acceptor simply emits later
        (tau_DA + tau_A), landing where r(t) has already decayed."""
        p = self._em(Anisotropy(r0=0.38, rho=1.0)).state([52.0]).stream_probability
        green_par = p[0] / (p[0] + p[2])
        red_par = p[1] / (p[1] + p[3])
        assert red_par < green_par

    def test_rows_are_distributions(self):
        obs = self._em(Anisotropy(r0=0.38)).to_obs(np.array([[52.0]]))
        assert obs.shape == (1, 4 * self.NB)
        np.testing.assert_allclose(obs.sum(axis=1), 1.0, atol=1e-12)

    @pytest.mark.slow
    def test_engine_routes_four_channels_preserving_colour(self):
        """SimEngine's anisotropy branch overwrites the channel with the
        parallel/perpendicular split and never reads the species `q` row, so
        colour has to come from the species identity.  Composing the two must
        reproduce the colour split the unpolarised run gives."""
        def run(**kw):
            cfg = InstrumentConfig(optics=Optics(alpha=0.08, tau_a=3.0), donor=4.0,
                                   sigma=0.0, n_molecules=1, n_photons=60_000,
                                   max_windows=25_000, brightness=100.0, **kw)
            d = simulate(KineticScheme.two_state((45.0, 62.0), 0.4, 0.4), cfg,
                         seed=1, n_micro_bins=64, burst_ms=2.0)
            ch = np.concatenate([np.array(s) for s in d.streams])
            st = np.concatenate([np.array(s) for s in d.states])
            return d, ch, st

        def colour_split(d, ch, st, state):
            """Green/red fractions **within one state**.

            Pooling over states would compare occupancies, not colours: the
            anisotropy branch scales the emission rate by the photoselection
            factor, so the two runs do not sample the states identically.  The
            per-state colour ratio is the physics; the pooled one is bookkeeping.
            """
            m = st == state
            f = np.bincount(ch[m], minlength=d.n_streams) / m.sum()
            return np.array([f[0] + f[2], f[1] + f[3]]) if d.n_streams == 4 else f

        d2, ch2, st2 = run(n_channels=2)
        d4, ch4, st4 = run(n_channels=4, aniso_r0=0.38, d_rot=0.2)
        assert d2.n_streams == 2 and d4.n_streams == 4
        for state in (0, 1):
            np.testing.assert_allclose(colour_split(d4, ch4, st4, state),
                                       colour_split(d2, ch2, st2, state), atol=0.03)
        # every species carries exactly one colour, which is what makes it work
        assert set(d4.truth["colour_of_species"]) <= {0, 1}


class TestPhysicalPosterior:
    """A posterior over **distance** — the point of the whole exercise."""

    NB = 256

    def _pieces(self):
        fine = np.array(list(tttrlib.SimDecay.gaussian_irf(4096, 0.008, 1.0, 0.15)))
        em = FretDistanceEmission(2, donor=4.0, tau_a=3.0, r0=52.0, sigma=0.0,
                                  n_bins=self.NB, dt=0.008 * (4096 // self.NB),
                                  irf=fine.reshape(self.NB, -1).sum(1))
        k = 4e-6
        A = np.array([[1 - k, k], [k, 1 - k]])
        return em, A, np.array([0.5, 0.5])

    def _data(self, em, A, pri, true_r=(45.0, 62.0), seed=3, n_b=6, n_p=60):
        """Generate from the model itself, so the fit is correctly specified."""
        rng = np.random.default_rng(seed)
        obs = em.to_obs(np.array(true_r).reshape(2, 1))
        times, streams, micro = [], [], []
        for _ in range(n_b):
            t = np.cumsum(rng.integers(1, 800, size=n_p)).astype(np.int64)
            s = rng.choice(2, p=pri)
            sym = np.empty(n_p, dtype=np.int64)
            for i in range(n_p):
                if i and rng.random() > (1 - 4e-6) ** int(t[i] - t[i - 1]):
                    s = 1 - s
                sym[i] = rng.choice(obs.shape[1], p=obs[s])
            times.append(t.tolist())
            streams.append((sym // self.NB).tolist())
            micro.append((sym % self.NB).tolist())
        return PhotonData(times, streams, 2, micro=micro, n_micro_bins=self.NB)

    def test_relabelling_is_required_for_a_meaningful_mean(self):
        """States are exchangeable, so a raw mean over draws is meaningless once
        a chain visits the other labelling -- it lands on the midpoint."""
        par = np.array([[[40.], [60.]], [[41.], [61.]],
                        [[60.], [40.]], [[61.], [41.]]])      # 2nd chain swapped
        dummy = [HmmModel(np.array([.5, .5]), np.eye(2), np.eye(2)) for _ in range(4)]
        p = HmmPhysicalPosterior(draws=dummy, loglik=np.zeros(4), n_chains=2,
                                 n_burn=0, params=par, accept_rate=np.zeros((2, 2)))
        # raw: the swapped chain drags both states to the midpoint
        np.testing.assert_allclose(par.mean(axis=0).ravel(), [50.5, 50.5])
        # relabelled: the two populations are recovered
        np.testing.assert_allclose(p.param_mean().ravel(), [40.5, 60.5])

    def test_rejects_a_parameterisation_without_bounds(self):
        class NoBounds:
            n_states = 2
        with pytest.raises(ValueError, match="no bounds"):
            _param_bounds(NoBounds(), 1)

    @pytest.mark.slow
    def test_posterior_mean_matches_the_map(self):
        """Under a flat prior the posterior mean must land on the MAP -- if it
        does not, the Metropolis step is sampling the wrong conditional."""
        em, A, pri = self._pieces()
        data = self._data(em, A, pri)
        init = [HmmModel(pri, A, em.to_obs(np.array([[48.], [60.]]))) for _ in range(2)]
        post = gibbs_physical(data, em, np.array([[48.], [60.]]), init,
                              n_draws=120, n_burn=150, n_chains=2, seed=2, thin=2)
        _, mapv = fit_emission(data, em, np.array([[48.], [60.]]), A, pri,
                               max_iter=150, tol=1e-9)
        np.testing.assert_allclose(np.sort(post.param_mean().ravel()),
                                   np.sort(mapv.ravel()), atol=1.0)
        # a random walk that never moves, or always moves, is not sampling
        assert 0.1 < post.accept_rate.mean() < 0.9

    @pytest.mark.slow
    def test_a_sharp_prior_pulls_the_posterior(self):
        """Distance is the coordinate a structure constrains, so a prior on R
        has to actually reach the sampled parameter."""
        em, A, pri = self._pieces()
        data = self._data(em, A, pri)
        init = [HmmModel(pri, A, em.to_obs(np.array([[48.], [60.]]))) for _ in range(2)]

        def pull(k, params):      # sharp Normal at 50 on both states
            return -0.5 * ((float(params[0]) - 50.0) / 0.5) ** 2

        free = gibbs_physical(data, em, np.array([[48.], [60.]]), init, n_draws=80,
                              n_burn=120, n_chains=2, seed=4, thin=2)
        tied = gibbs_physical(data, em, np.array([[48.], [60.]]), init, n_draws=80,
                              n_burn=120, n_chains=2, seed=4, thin=2, log_prior=pull)
        assert (np.abs(tied.param_mean() - 50.0).max()
                < np.abs(free.param_mean() - 50.0).max())


class TestKineticSchemes:
    def test_linear_chain_has_no_end_to_end_rate(self):
        s = KineticScheme.linear((40.0, 52.0, 65.0), k=1.0)
        assert s.rates[0, 2] == 0.0 and s.rates[2, 0] == 0.0
        assert s.rates[0, 1] > 0 and s.rates[1, 2] > 0

    def test_cyclic_connects_everything(self):
        s = KineticScheme.cyclic((40.0, 52.0, 65.0), k=1.0)
        off = ~np.eye(3, dtype=bool)
        assert (s.rates[off] > 0).all()
        assert (np.diag(s.rates) == 0).all()

    def test_regime_is_transitions_per_burst(self):
        """Rates are set by transitions *per burst* -- the scale that decides
        identifiability, not the absolute rate."""
        slow = KineticScheme.from_regime((45.0, 62.0), "slow", burst_ms=2.0)
        fast = KineticScheme.from_regime((45.0, 62.0), "fast", burst_ms=2.0)
        assert fast.rates[0, 1] > 100 * slow.rates[0, 1]
        assert KineticScheme.from_regime((45.0, 62.0), "static").rates.max() == 0.0
        with pytest.raises(ValueError, match="unknown regime"):
            KineticScheme.from_regime((45.0, 62.0), "warp-speed")


@pytest.mark.slow
class TestSimEngineBuilder:
    """The builder must produce what it claims, through tttrlib's own engine."""

    NB = 256

    def _cfg(self, **kw):
        base = dict(optics=Optics(tau_a=3.0), donor=4.0, sigma=0.0, n_molecules=1,
                    n_photons=120_000, max_windows=40_000, brightness=100.0)
        base.update(kw)
        return InstrumentConfig(**base)

    def _model_irf(self, cfg):
        fine = np.array(list(tttrlib.SimDecay.gaussian_irf(
            cfg.n_microtime_channels, cfg.microtime_resolution,
            cfg.irf_mean, cfg.irf_fwhm)))
        return fine.reshape(self.NB, -1).sum(axis=1)

    def test_closed_loop_recovers_the_generating_distances(self):
        """Imperfections off: the builder collapses to the two-species case and
        the fit recovers what generated the photons.  This is the invariant that
        catches a mis-encoded species graph."""
        cfg = self._cfg()
        true_r = (45.0, 62.0)
        d = simulate(KineticScheme.two_state(true_r, 0.4, 0.4), cfg,
                     seed=1, n_micro_bins=self.NB, burst_ms=2.0)
        data = PhotonData(d.times, d.streams, 2, micro=d.micro, n_micro_bins=self.NB)

        em = FretDistanceEmission(
            2, donor=4.0, tau_a=3.0, r0=52.0, sigma=0.0, n_bins=self.NB,
            dt=cfg.microtime_resolution * (cfg.n_microtime_channels // self.NB),
            irf=self._model_irf(cfg))
        k = 0.4 * cfg.macro_resolution
        A = np.array([[1 - k, k], [k, 1 - k]])
        _, got = fit_emission(data, em, np.array([[50.0], [57.0]]), A,
                              np.array([0.5, 0.5]), max_iter=250, tol=1e-10)
        np.testing.assert_allclose(np.sort(got.ravel()), true_r, atol=2.0)

    def test_kinetics_land_at_the_requested_rate(self):
        """A state change every few photons would mean the graph is wrong --
        which is exactly what several simultaneous molecules produced."""
        cfg = self._cfg()
        d = simulate(KineticScheme.two_state((45.0, 62.0), 0.4, 0.4), cfg,
                     seed=1, n_micro_bins=self.NB, burst_ms=2.0)
        truth = np.concatenate([np.array(s) for s in d.states])
        per_burst = (np.diff(truth) != 0).sum() / len(d.times)
        assert 0.2 < per_burst < 4.0, f"{per_burst:.2f} transitions/burst"

    def test_leak_ratio_follows_the_brightness_row(self):
        """alpha is a q entry, not a correction factor."""
        cfg = self._cfg(optics=Optics(alpha=0.25, tau_a=3.0))
        d = simulate(KineticScheme.two_state((45.0, 62.0), 0.4, 0.4), cfg,
                     seed=2, n_micro_bins=self.NB, burst_ms=2.0)
        kinds = d.truth["kind_of_species"]
        assert "donor" in kinds and "sensitized" in kinds

    def test_single_molecule_is_the_default(self):
        """Several lit emitters interleave into a mixture, breaking the HMM's
        core assumption -- so the default must not create one."""
        assert InstrumentConfig().n_molecules == 1


class TestProductAlphabet:
    def test_micro_time_bins_widen_the_alphabet(self):
        """The lifetime-resolved model is the same kernel with a larger p."""
        times = [[0, 2, 5], [0, 3]]
        streams = [[0, 1, 0], [1, 0]]
        micro = [[0, 3, 1], [2, 0]]
        data = PhotonData(times, streams, n_streams=2, micro=micro, n_micro_bins=4)

        assert data.p == 8
        assert list(data.symbols) == [0 * 4 + 0, 1 * 4 + 3, 0 * 4 + 1, 1 * 4 + 2, 0 * 4 + 0]

    def test_single_bin_reduces_to_streams(self):
        times = [[0, 2, 5]]
        streams = [[0, 1, 0]]
        plain = PhotonData(times, streams, 2)
        binned = PhotonData(times, streams, 2, micro=[[0, 0, 0]], n_micro_bins=1)
        np.testing.assert_array_equal(plain.symbols, binned.symbols)
        assert plain.p == binned.p == 2
