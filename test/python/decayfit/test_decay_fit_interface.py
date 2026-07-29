"""The decay-fit interface: construction, contract, and unchanged physics.

Every fit is reached the same way — build one by registry name, hand it a
:class:`DecayFitProblem`, get back parameters and a result vector whose columns
the registry names. These tests cover that contract, and, importantly, pin the
*numbers* against the values the previous per-model API produced, so the move to
one interface is provably a change of plumbing rather than of physics.
"""
from __future__ import division

import json
import unittest

import numpy as np

import tttrlib
from misc.compute_irf import model_irf


def _problem(n_bins, dt, irf, background=None, n_channels=2):
    """A problem sized for ``irf``, with the arrays already attached."""
    p = tttrlib.DecayFitProblem(n_channels, n_bins, dt)
    p.irf = tttrlib.VectorDouble(np.asarray(irf, dtype=float).tolist())
    if background is None:
        background = np.zeros_like(np.asarray(irf, dtype=float))
    p.background = tttrlib.VectorDouble(np.asarray(background, dtype=float).tolist())
    return p


class TestRegistryAgreement(unittest.TestCase):
    """The registry describes what the code actually does."""

    def test_every_advertised_fit_is_constructible(self):
        for name in json.loads(tttrlib.registry_category_json("fit")):
            self.assertIn(name, tttrlib.fit_names())
            fit = tttrlib.DecayFit2(name)
            self.assertEqual(fit.name(), name)

    def test_unknown_name_names_the_alternatives(self):
        with self.assertRaises(Exception) as ctx:
            tttrlib.DecayFit2("not_a_fit")
        self.assertIn("fit23", str(ctx.exception))

    def test_setup_vector_length_matches_the_schema(self):
        """The flat setup wire and the schema must agree on how many slots exist.

        They are written in different files; nothing but a test keeps them in
        step, and a silent mismatch would shift every value by one slot.
        """
        registry = json.loads(tttrlib.registry_json())
        for name, entry in registry["fit"].items():
            link = entry["setup"]
            properties = registry[link["category"]][link["name"]]["params_schema"]["properties"]
            self.assertEqual(len(tttrlib.setup_vector(name)), len(properties), name)

    def test_flat_vectors_follow_declaration_order(self):
        """Slots are laid out in *declaration* order, not sorted.

        The rule is normative and the failure is silent: a JSON library that
        sorts object keys puts ``dt`` where ``period`` belongs, and the fit still
        runs — describing a different instrument. This compares the order the
        native builder uses against the order the registry declares.
        """
        registry = json.loads(tttrlib.registry_json())
        for name, entry in registry["fit"].items():
            link = entry["setup"]
            declared = list(
                registry[link["category"]][link["name"]]["params_schema"]["properties"])
            self.assertEqual(
                list(tttrlib.decay_fit_setup_names(name)), declared,
                f"{name}: setup slots are not in declaration order")

            declared_params = []
            for prop_name, prop in entry["params_schema"]["properties"].items():
                if prop.get("type") == "array":
                    declared_params.extend(f"{prop_name}[{i}]" for i in range(2))
                else:
                    declared_params.append(prop_name)
            self.assertEqual(
                list(tttrlib.decay_fit_parameter_names(name, 2)), declared_params,
                f"{name}: parameter slots are not in declaration order")

    def test_parameter_and_result_vectors_match_the_model(self):
        for name in tttrlib.fit_names():
            n = 2 if name == "fit_nexp" else None
            fit = tttrlib.DecayFit2(name, tttrlib.setup_vector(name, **({"n_exponentials": 2} if n else {})))
            problem = _problem(16, 0.032, np.zeros(32))
            self.assertEqual(len(tttrlib.parameter_vector(name, n=n)),
                             fit.n_parameters(problem), name)
            self.assertEqual(len(tttrlib.result_names(name, n=n)),
                             fit.n_results(problem), name)

    def test_defaults_hold_the_parameters_the_registry_marks_fixed(self):
        # Only tau is free by default: scatter and anisotropy are not
        # identifiable against an auto-extracted background.
        self.assertEqual(tttrlib.default_links("fit23"), [0, -1, -1, -1])
        self.assertEqual(tttrlib.default_links("fit23", free=["gamma"]), [0, 0, -1, -1])

    def test_objective_is_selected_by_name(self):
        by_name = tttrlib.setup_vector("fit23", objective="p2s_mle")
        objectives = list(json.loads(tttrlib.registry_category_json("objective")))
        self.assertEqual(by_name[7], objectives.index("p2s_mle"))
        with self.assertRaises(ValueError):
            tttrlib.setup_vector("fit23", objective="not_an_objective")

    def test_results_are_named_and_typed(self):
        named = tttrlib.results_as_dict("fit23", [1.03, 1.0, 12.0, 0.21, 0.19])
        self.assertEqual(named["twoIstar"], 1.03)
        self.assertIs(named["converged"], True)      # boolean, not 1.0
        self.assertIsInstance(named["iterations"], int)


class TestFit23Reference(unittest.TestCase):
    """The published fit23 answer, unchanged by the move to the interface.

    Same IRF, same counts, same starting values as the previous per-model API;
    the expected parameters and 2I* are the numbers that API returned. If the
    port had altered the physics this is what would move.
    """

    def setUp(self):
        n_bins = 32
        period, g, l1, l2 = 32, 1.0, 0.1, 0.1
        irf_np, time_axis = model_irf(
            n_channels=n_bins, period=period,
            irf_position_p=2.0, irf_position_s=18.0, irf_width=0.25)
        self.dt = time_axis[1] - time_axis[0]
        conv_stop = min(len(time_axis) // 2 - 1, n_bins // 2)
        self.setup = tttrlib.setup_vector(
            "fit23", dt=self.dt, period=period, g_factor=g, l1=l1, l2=l2,
            convolution_stop=conv_stop, soft_bifl_scatter_flag=True)
        self.irf = irf_np
        self.data = [
            0, 0, 0, 1, 9, 7, 5, 5, 5, 2, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 2, 2, 2, 3, 0, 1, 0,
            1, 1, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        ]
        self.n_bins = n_bins

    def test_recovers_the_published_parameters(self):
        fit = tttrlib.DecayFit2("fit23", self.setup, self.irf.tolist())
        problem = _problem(self.n_bins, self.dt, self.irf)
        problem.data = tttrlib.VectorDouble([float(v) for v in self.data])

        # tau and gamma free, r0 and rho held — the same mask as before, which
        # takes the general optimiser rather than the tau-only fast path.
        constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, 0, -1, -1]))
        out = fit.fit([2.1, 0.01, 0.38, 1.2], constraints, problem)

        np.testing.assert_allclose(
            list(out.parameters), [1.79, 0.0, 0.38, 1.2], rtol=1e-2, atol=0.02)
        self.assertAlmostEqual(out.objective, 0.512, places=2)

        named = tttrlib.results_as_dict("fit23", list(out.results))
        self.assertAlmostEqual(named["twoIstar"], 0.512, places=2)
        self.assertAlmostEqual(named["r_scatter"], 0.26, places=2)
        self.assertAlmostEqual(named["r_experimental"], 0.26, places=2)

    def test_free_gamma_does_not_stall_at_its_bound(self):
        """Freed scatter must not walk onto the clamp and stop there.

        Without a restoring gradient beyond the bound the optimiser used to park
        gamma at ~0.999 — an "all background" corner that fits nothing.
        """
        fit = tttrlib.DecayFit2("fit23", self.setup, self.irf.tolist())
        problem = _problem(self.n_bins, self.dt, self.irf)
        problem.data = tttrlib.VectorDouble([float(v) for v in self.data])
        constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, 0, -1, -1]))
        out = fit.fit([2.1, 0.5, 0.38, 1.2], constraints, problem)
        self.assertLess(out.parameters[1], 0.99, "gamma stalled at the cap")
        self.assertGreaterEqual(out.objective, 0.0)


class TestCrossLanguageReference(unittest.TestCase):
    """The published reference values for all four Fit2x models.

    These numbers were the cross-language contract: the same IRF, counts and
    starting values fitted through Python, R and Java had to agree to 1e-3. They
    are reproduced here through the new interface, which is what makes "the port
    changed the plumbing, not the physics" a checkable claim rather than a hope.
    """

    FN = 32
    DT = 0.5
    DATA = [
        0, 0, 0, 1, 9, 7, 5, 5, 5, 2, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 2, 2, 2, 3, 0, 1, 0,
        1, 1, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    ]

    def setUp(self):
        fn = self.FN
        self.irf = np.zeros(2 * fn)
        for half in (0, fn):
            for i in range(fn):
                self.irf[half + i] = np.exp(-((i - 8.0) ** 2) / (2 * 0.5 * 0.5))
        self.corrections = dict(period=2.0 * fn, g_factor=1.0, l1=0.1, l2=0.1,
                                convolution_stop=fn // 2 - 1)

    def _problem(self, background_level):
        p = _problem(self.FN, self.DT, self.irf,
                     np.zeros(2 * self.FN) + background_level)
        p.data = tttrlib.VectorDouble([float(v) for v in self.DATA])
        return p

    def _fit(self, name):
        setup = tttrlib.setup_vector(
            name, dt=self.DT, soft_bifl_scatter_flag=True, **self.corrections)
        return tttrlib.DecayFit2(name, setup, self.irf.tolist())

    def test_fit23_matches_the_reference(self):
        # Start at 1.0, not 2.1. This 58-photon reference decay is so sparse that
        # the objective falls monotonically as tau grows — a lifetime far longer
        # than the recorded window describes it better than any real one — so the
        # historical answer is a *local* minimum whose basin runs from about 0.5
        # to 1.2. Started outside it the fit converges, reports success, and
        # returns tau in the tens of thousands. Pinning the reference therefore
        # means starting inside the basin and saying so, rather than relying on
        # the descent path from 2.1 happening to fall into it.
        out = self._fit("fit23").fit(
            [1.0, 0.01, 0.38, 1.2],
            tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, 0, -1, -1])),
            self._problem(0.0))
        self.assertAlmostEqual(out.objective, 23.802337, places=3)
        self.assertAlmostEqual(out.parameters[0], 0.74219, places=3)
        named = tttrlib.results_as_dict("fit23", list(out.results))
        self.assertAlmostEqual(named["r_experimental"], 0.25974, places=3)

    def test_a_uniform_prior_bounds_the_single_row_fit(self):
        """A bound that is stored but not enforced is worse than no bound.

        "Bounds are priors" is the documented contract, and this decay is the
        sharpest possible test of it: unbounded, the lifetime runs to tens of
        thousands, so a bound that holds is unmistakable from one that does not.

        Both internal paths are checked. fit23 specialises to a 1-D Brent search
        when only the lifetime is free, and a specialisation that ignored the
        bound would make a fit's answer depend on which path it happened to take
        — which is not an optimisation, it is a second model.
        """
        box = '{"kind": "uniform", "lb": 0.001, "ub": 8.0}'
        for codes in ([0, 0, -1, -1], [0, -1, -1, -1]):
            constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32(codes))
            constraints.set_prior_json(0, box)
            out = self._fit("fit23").fit([2.1, 0.01, 0.38, 1.2], constraints,
                                         self._problem(0.0))
            self.assertLessEqual(out.parameters[0], 8.001,
                                 "prior ignored for links %s" % codes)
            self.assertGreaterEqual(out.parameters[0], 0.0009)

    def test_a_sparse_decay_runs_the_lifetime_away_silently(self):
        """The failure mode the reference start had to be moved to avoid.

        With 58 photons the likelihood barely distinguishes a real lifetime from
        one far longer than the recorded window, and the objective falls all the
        way to tau -> infinity. Started outside the local minimum's basin the fit
        converges to a lifetime in the tens of thousands and *reports success* —
        no exception, no convergence flag, and a 2I* that looks better than the
        right answer's because it is. Nothing in the result says the number is
        nonsense, which is why it is pinned here: the guard is knowing the
        behaviour exists, and comparing the lifetime against the excitation
        period is what catches it in real use.
        """
        out = self._fit("fit23").fit(
            [2.1, 0.01, 0.38, 1.2],
            tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, 0, -1, -1])),
            self._problem(0.0))
        period = self.corrections["period"]
        self.assertGreater(out.parameters[0], 100.0 * period)
        # and it looks *better* than the physical answer by the objective alone
        self.assertLess(out.objective, 23.802337)

    def test_fit24_matches_the_reference(self):
        out = self._fit("fit24").fit(
            [3.8, 0.02, 0.4, 0.8, 1.0],
            tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, 0, 0, 0, 0])),
            self._problem(0.2))
        self.assertAlmostEqual(out.objective, 2.41049, places=3)

    def test_fit25_matches_the_reference_and_names_the_winner(self):
        out = self._fit("fit25").fit(
            [0.5, 1.0, 2.0, 4.0, 0.02, 0.38],
            tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, 0, 0, 0, -1, -1])),
            self._problem(0.2))
        self.assertAlmostEqual(out.objective, 4.738831, places=3)
        self.assertAlmostEqual(out.parameters[0], 0.5, places=3)
        # The winning candidate is now reported directly rather than having to be
        # recovered by matching the returned lifetime against the four inputs.
        named = tttrlib.results_as_dict("fit25", list(out.results))
        self.assertEqual(named["selected_index"], 0)

    def test_fit26_matches_the_reference(self):
        out = self._fit("fit26").fit(
            [0.5],
            tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0])),
            self._problem(0.2))
        self.assertAlmostEqual(out.objective, 2.218772, places=3)


class TestContract(unittest.TestCase):
    """Behaviour the interface guarantees for every model."""

    def _round_trip_problem(self, n_bins=128, dt=0.032):
        irf = np.zeros(2 * n_bins)
        irf[0] = 1.0
        irf[n_bins] = 1.0
        # The period bounds the lifetime search, so it must comfortably exceed
        # the lifetime being recovered — 128 bins at 32 ps is 4.1 ns against a
        # 2.1 ns lifetime. At 64 bins the period *is* 2.048 ns and every fit
        # rails to that ceiling.
        setup = tttrlib.setup_vector(
            "fit23", dt=dt, period=n_bins * dt, soft_bifl_scatter_flag=False)
        fit = tttrlib.DecayFit2("fit23", setup, irf.tolist())
        problem = _problem(n_bins, dt, irf)
        seed = np.concatenate([1000 * np.exp(-np.arange(n_bins) * dt / 2.1) + 1] * 2)
        problem.data = tttrlib.VectorDouble(seed.tolist())
        fit.evaluate([2.1, 0.0, 0.0, 1.0], problem)
        model = np.asarray(problem.model)
        model = model / model.sum() * 2.0e5
        counts = np.random.default_rng(20260728).poisson(model).astype(float)
        problem.data = tttrlib.VectorDouble(counts.tolist())
        return fit, problem

    def test_recovers_a_simulated_lifetime(self):
        fit, problem = self._round_trip_problem()
        constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, -1, -1, -1]))
        out = fit.fit([0.6, 0.0, 0.0, 1.0], constraints, problem)
        self.assertAlmostEqual(out.parameters[0], 2.1, delta=0.05)
        self.assertTrue(tttrlib.results_as_dict("fit23", list(out.results))["converged"])

    def test_held_parameters_come_back_unchanged(self):
        fit, problem = self._round_trip_problem()
        constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, -1, -1, -1]))
        out = fit.fit([0.6, 0.123, 0.0, 1.7], constraints, problem)
        self.assertEqual(out.parameters[1], 0.123)
        self.assertEqual(out.parameters[3], 1.7)

    def test_batch_reproduces_the_scalar_fit(self):
        """A batch is many scalar fits, not an approximation of them."""
        fit, problem = self._round_trip_problem()
        constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, -1, -1, -1]))
        scalar = fit.fit([0.6, 0.0, 0.0, 1.0], constraints, problem)

        rows = 6
        matrix = np.tile(np.asarray(problem.data), rows)
        batch = fit.fit_many(problem, matrix.tolist(), rows,
                             int(problem.total_size()), [0.6, 0.0, 0.0, 1.0],
                             constraints)
        n_par = fit.n_parameters(problem)
        for r in range(rows):
            self.assertAlmostEqual(batch.objective[r], scalar.objective, places=12)
            self.assertAlmostEqual(batch.parameters[r * n_par], scalar.parameters[0],
                                   places=12)

    def test_a_decay_longer_than_the_irf_is_refused(self):
        """Sizes that would read past the end of the IRF must fail loudly.

        This used to be a crash, then a sentinel return; the problem validates
        instead, so the caller learns what is wrong rather than what happened.
        """
        n_bins = 64
        irf = np.zeros(2 * n_bins)
        fit = tttrlib.DecayFit2("fit23", tttrlib.setup_vector("fit23"), irf.tolist())
        problem = tttrlib.DecayFitProblem(2, n_bins, 0.032)
        problem.irf = tttrlib.VectorDouble(np.zeros(2 * (n_bins - 8)).tolist())
        problem.background = tttrlib.VectorDouble(np.zeros(2 * n_bins).tolist())
        constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, -1, -1, -1]))
        with self.assertRaises(Exception):
            fit.fit([2.0, 0.0, 0.0, 1.0], constraints, problem)

    def test_a_lifetime_longer_than_the_period_rails_to_the_period(self):
        """A lifetime beyond the excitation period cannot be measured.

        The decay is convolved over one period, so the search is bounded by it
        and anything longer stops at that ceiling. Pinned here because the
        failure is *quiet*: the fit still reports ``converged`` and a plausible
        2I*, so the only clue is a lifetime sitting exactly on the period. A
        caller seeing that should lengthen the period, not believe the number.
        """
        n_bins, dt = 64, 0.032
        period = n_bins * dt                      # 2.048 ns
        irf = np.zeros(2 * n_bins)
        irf[0] = 1.0
        irf[n_bins] = 1.0
        setup = tttrlib.setup_vector(
            "fit23", dt=dt, period=period, soft_bifl_scatter_flag=False)
        fit = tttrlib.DecayFit2("fit23", setup, irf.tolist())
        problem = _problem(n_bins, dt, irf)
        # Ask for a 5 ns lifetime against a 2.048 ns period.
        problem.data = tttrlib.VectorDouble(
            np.concatenate([1000 * np.exp(-np.arange(n_bins) * dt / 5.0) + 1] * 2).tolist())
        fit.evaluate([5.0, 0.0, 0.0, 1.0], problem)
        model = np.asarray(problem.model)
        problem.data = tttrlib.VectorDouble((model / model.sum() * 1.0e6).tolist())

        constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, -1, -1, -1]))
        out = fit.fit([0.6, 0.0, 0.0, 1.0], constraints, problem)
        self.assertAlmostEqual(out.parameters[0], period, delta=0.01)

    def test_empty_data_does_not_crash(self):
        n_bins = 32
        irf = np.zeros(2 * n_bins)
        irf[0] = 1.0
        irf[n_bins] = 1.0
        fit = tttrlib.DecayFit2("fit23", tttrlib.setup_vector("fit23"), irf.tolist())
        problem = _problem(n_bins, 0.032, irf)
        constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, -1, -1, -1]))
        out = fit.fit([2.0, 0.0, 0.0, 1.0], constraints, problem)
        self.assertEqual(len(out.parameters), 4)


class TestLinkedFit(unittest.TestCase):
    """Fitting rows *together*, with parameters shared between them.

    This is what separates a linked batch from an independent one: in
    ``fit_many`` the rows never meet, so a link group spanning them does
    nothing. Here one parameter vector covers every row and a group ties slots
    across them — the reason to fit several measurements jointly at all.
    """

    N_BINS = 128
    DT = 0.032
    PERIOD = 32.0
    TRUE_TAUS = (1.4, 2.1, 3.2)
    TRUE_RHO = 1.5

    def setUp(self):
        n = self.N_BINS
        self.irf = np.zeros(2 * n)
        self.irf[0] = 1.0
        self.irf[n] = 1.0
        setup = tttrlib.setup_vector(
            "fit23", dt=self.DT, period=self.PERIOD, soft_bifl_scatter_flag=False)
        self.fit = tttrlib.DecayFit2("fit23", setup, self.irf.tolist())
        self.problem = _problem(n, self.DT, self.irf)

        # Several measurements that differ in lifetime but share a rotational
        # correlation time — a property of the sample, not of each acquisition.
        rows = []
        for i, tau in enumerate(self.TRUE_TAUS):
            seed = 1e3 * np.exp(-(np.arange(2 * n) % n) * self.DT / tau) + 1
            self.problem.data = tttrlib.VectorDouble(seed.tolist())
            curve = np.asarray(
                self.fit.model_curve([tau, 0.0, 0.3, self.TRUE_RHO], self.problem))
            expected = curve / curve.sum() * 3.0e5
            rows.append(np.random.default_rng(i).poisson(expected).astype(float))
        self.matrix = np.vstack(rows)
        self.n_rows = len(self.TRUE_TAUS)

    def _link(self, rho_group):
        """tau free per row, gamma/r0 held, rho tied by ``rho_group``."""
        codes = []
        for _ in range(self.n_rows):
            codes += [0, -1, -1, rho_group]
        return tttrlib.DecayFitConstraints(tttrlib.VectorInt32(codes))

    def _start(self):
        return [v for _ in range(self.n_rows) for v in (1.0, 0.0, 0.3, 0.8)]

    def test_a_linked_group_collapses_the_search(self):
        out = self.fit.fit_linked(
            self.problem, self.matrix.ravel().tolist(), self.n_rows,
            2 * self.N_BINS, self._start(), self._link(1))
        # 12 slots -> 3 free lifetimes + 1 shared rho.
        self.assertEqual(out.n_variables, self.n_rows + 1)

    def test_linked_slots_come_back_identical(self):
        """Not merely close: they are one value, written into every row."""
        out = self.fit.fit_linked(
            self.problem, self.matrix.ravel().tolist(), self.n_rows,
            2 * self.N_BINS, self._start(), self._link(1))
        rho = np.asarray(out.parameters).reshape(self.n_rows, 4)[:, 3]
        self.assertTrue(np.all(rho == rho[0]), f"rho differs across rows: {rho}")
        self.assertAlmostEqual(rho[0], self.TRUE_RHO, delta=0.15)

    def test_each_row_keeps_its_own_free_parameter(self):
        out = self.fit.fit_linked(
            self.problem, self.matrix.ravel().tolist(), self.n_rows,
            2 * self.N_BINS, self._start(), self._link(1))
        taus = np.asarray(out.parameters).reshape(self.n_rows, 4)[:, 0]
        for recovered, truth in zip(taus, self.TRUE_TAUS):
            self.assertAlmostEqual(recovered, truth, delta=0.05)

    def test_linking_actually_constrains(self):
        """Untied, the same fit is free to give each row a different rho.

        If it did not, the linked result would be indistinguishable from the
        unlinked one and the group would be doing nothing.
        """
        untied = self.fit.fit_linked(
            self.problem, self.matrix.ravel().tolist(), self.n_rows,
            2 * self.N_BINS, self._start(), self._link(0))     # 0 = free per row
        self.assertEqual(untied.n_variables, 2 * self.n_rows)  # tau and rho each
        rho = np.asarray(untied.parameters).reshape(self.n_rows, 4)[:, 3]
        self.assertFalse(np.all(rho == rho[0]),
                         "rho is identical without a link group, so the link "
                         "group cannot be shown to do anything")

    def test_reports_a_joint_objective_and_per_row_scores(self):
        out = self.fit.fit_linked(
            self.problem, self.matrix.ravel().tolist(), self.n_rows,
            2 * self.N_BINS, self._start(), self._link(1))
        self.assertEqual(len(out.row_objective), self.n_rows)
        self.assertAlmostEqual(out.objective, sum(out.row_objective), places=9)
        self.assertGreater(out.iterations, 0)
        self.assertEqual(len(out.results),
                         self.n_rows * self.fit.n_results(self.problem))

    def test_holding_everything_searches_nothing(self):
        held = tttrlib.DecayFitConstraints(
            tttrlib.VectorInt32([-1] * (4 * self.n_rows)))
        out = self.fit.fit_linked(
            self.problem, self.matrix.ravel().tolist(), self.n_rows,
            2 * self.N_BINS, self._start(), held)
        self.assertEqual(out.n_variables, 0)
        # The supplied values come back untouched rather than being optimised.
        taus = np.asarray(out.parameters).reshape(self.n_rows, 4)[:, 0]
        self.assertTrue(np.all(taus == 1.0))


class TestNExp(unittest.TestCase):
    """The variable-arity model, and with it the ``count_from`` link."""

    def _fit(self, n_exp):
        n_bins, dt = 128, 0.032
        irf = np.zeros(n_bins)
        irf[0] = 1.0
        setup = tttrlib.setup_vector("fit_nexp", dt=dt, n_exponentials=n_exp)
        fit = tttrlib.DecayFit2("fit_nexp", setup, irf.tolist())
        problem = tttrlib.DecayFitProblem(1, n_bins, dt)
        problem.irf = tttrlib.VectorDouble(irf.tolist())
        problem.background = tttrlib.VectorDouble(np.zeros(n_bins).tolist())
        return fit, problem, n_bins, dt

    def test_arity_follows_the_component_count(self):
        for n_exp in (1, 2, 3):
            fit, problem, _, _ = self._fit(n_exp)
            self.assertEqual(fit.n_parameters(problem), 2 * n_exp)
            self.assertEqual(len(tttrlib.parameter_vector("fit_nexp", n=n_exp)), 2 * n_exp)

    def test_recovers_a_single_lifetime(self):
        """Recovery is tested against data drawn from the model itself.

        Sampling an analytic ``exp(-t/tau)`` instead would test my idea of the
        model's discretisation rather than the fit, and shows a ~5% offset for
        exactly that reason.
        """
        fit, problem, n_bins, dt = self._fit(1)
        true_tau = 3.0
        problem.data = tttrlib.VectorDouble(
            (1.0e3 * np.exp(-np.arange(n_bins) * dt / true_tau) + 1).tolist())
        fit.evaluate([true_tau, 1.0], problem)
        model = np.asarray(problem.model)
        model = model / model.sum() * 5.0e5
        problem.data = tttrlib.VectorDouble(
            np.random.default_rng(11).poisson(model).astype(float).tolist())
        constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, -1]))
        out = fit.fit([1.0, 1.0], constraints, problem)
        self.assertAlmostEqual(out.parameters[0], true_tau, delta=0.05)
        named = tttrlib.results_as_dict("fit_nexp", list(out.results))
        self.assertGreater(named["photon_count"], 0.0)


class TestPriors(unittest.TestCase):
    """Bounds are priors, and a prior crosses the boundary losslessly."""

    def test_prior_json_round_trips(self):
        for state in (
            {"kind": "normal", "mu": 2.0, "sigma": 0.5},
            {"kind": "uniform", "lb": 0.1, "ub": 5.0},
            {"kind": "lognormal", "mu": 0.7, "sigma": 0.3},
            {"kind": "gamma", "alpha": 2.0, "beta": 1.5, "loc": 0.0},
        ):
            constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0]))
            constraints.set_prior_json(0, json.dumps(state))
            restored = json.loads(constraints.get_json())["priors"][0]
            self.assertEqual(restored["kind"], state["kind"])
            for key, value in state.items():
                if key != "kind":
                    self.assertAlmostEqual(restored[key], value, places=12)

    def test_a_callable_prior_is_refused_rather_than_approximated(self):
        """A Python callback cannot be evaluated natively, and silently

        substituting a flat prior would change the posterior without saying so.
        """
        constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0]))
        with self.assertRaises(Exception):
            constraints.set_prior_json(0, json.dumps({"kind": "callable", "label": "f"}))


if __name__ == "__main__":
    unittest.main()
