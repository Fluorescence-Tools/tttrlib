#!/usr/bin/env python3
"""Smoke tests for the HMM gallery examples.

Runs ``examples/single_molecule/plot_hmm_analysis.py`` end-to-end (headless) and
checks that the state-count scan / BIC selection / Viterbi decoding recover the
simulated three-state ground truth. This keeps the tutorial from silently
breaking when the :class:`tttrlib.HMM` API changes.
"""
import runpy
import unittest
from pathlib import Path

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

_EXAMPLES = Path(__file__).resolve().parents[3] / "examples" / "single_molecule"
EXAMPLE = _EXAMPLES / "plot_hmm_analysis.py"
LIFETIME_EXAMPLE = _EXAMPLES / "plot_hmm_lifetime_posterior.py"
DISTANCE_EXAMPLE = _EXAMPLES / "plot_hmm_distance_refinement.py"
PIPELINE_EXAMPLE = _EXAMPLES / "plot_hmm_burst_pipeline.py"
BLINKING_EXAMPLE = _EXAMPLES / "plot_hmm_blinking_acceptor.py"
COINCIDENCE_EXAMPLE = _EXAMPLES / "plot_hmm_coincidence.py"
PHASOR_EXAMPLE = _EXAMPLES / "plot_hmm_phasor_diagnostic.py"
BOOTSTRAP_EXAMPLE = _EXAMPLES / "plot_hmm_bootstrap.py"


def _run(path):
    """Run a gallery script headless and hand back its namespace."""
    _show = plt.show
    plt.show = lambda *a, **k: None       # keep the script from blocking
    try:
        return runpy.run_path(str(path))
    finally:
        plt.show = _show
        plt.close("all")


class TestHmmExample(unittest.TestCase):
    def test_example_runs_and_recovers_states(self):
        self.assertTrue(EXAMPLE.exists(), f"missing example: {EXAMPLE}")
        _show = plt.show
        plt.show = lambda *a, **k: None  # keep the gallery script from blocking
        try:
            ns = runpy.run_path(str(EXAMPLE))
        finally:
            plt.show = _show
            plt.close("all")

        # BIC selected the true three-state model.
        self.assertEqual(ns["best_k"], 3)
        # Three separated, ascending FRET states near the ground truth.
        E = ns["E_sorted"]
        self.assertEqual(len(E), 3)
        self.assertTrue(E[0] < E[1] < E[2])
        self.assertLess(abs(E[0] - 0.15), 0.1)
        self.assertLess(abs(E[2] - 0.80), 0.1)
        # Dwells and transitions were extracted from the Viterbi path.
        self.assertGreater(ns["dwell_state"].size, 0)
        self.assertGreater(len(ns["trans_from"]), 0)


class TestLifetimeExample(unittest.TestCase):
    """The dark-state-vs-FRET example, which is a negative control end to end.

    Its whole claim is that the *lifetime* carries the separation, so both halves
    are asserted: that micro-time decodes well above chance, and that the same
    photons on streams alone decode AT chance. Checking only the first would pass
    even if the two states had accidentally differed in intensity, which would
    make the example prove nothing.
    """

    def test_lifetimes_recovered_and_only_micro_time_separates(self):
        self.assertTrue(LIFETIME_EXAMPLE.exists(), f"missing: {LIFETIME_EXAMPLE}")
        ns = _run(LIFETIME_EXAMPLE)

        # Recovered from a start wrong by 2x in both directions (8.0 / 0.8 ns).
        tau = ns["tau_fit"]
        self.assertAlmostEqual(tau[0], ns["TAU_DARK"], delta=0.4)
        self.assertAlmostEqual(tau[1], ns["TAU_FRET"], delta=0.4)

        self.assertGreater(ns["accuracy"], 0.70)      # micro-time separates
        self.assertLess(ns["acc_stream"], 0.60)       # streams alone do not


class TestDistanceRefinementExample(unittest.TestCase):
    """Physics outside, photon recursions inside — refined against the data.

    The example's claim is that a *physical* parameter can be refined through
    `HMM.evaluate` without the engine knowing any physics, so the test checks
    what that requires: distances recovered from a wrong start, kinetics
    recovered in the same loop, and a monotone log-likelihood — the last being
    what distinguishes a genuine EM step from a heuristic run alongside one.
    """

    def test_distances_and_kinetics_are_recovered(self):
        self.assertTrue(DISTANCE_EXAMPLE.exists(), f"missing: {DISTANCE_EXAMPLE}")
        ns = _run(DISTANCE_EXAMPLE)

        # Started at 35 / 55 / 75 against a truth of 42 / 52 / 64.
        np.testing.assert_allclose(ns["R"], ns["R_TRUE"], atol=1.5)

        # Kinetics refined in the same loop, from a flat 1e-3 start.
        A, K = ns["A"], ns["K"]
        self.assertAlmostEqual(A[0, 1], K[0, 1], delta=0.0005)
        self.assertAlmostEqual(A[1, 2], K[1, 2], delta=0.0005)

    def test_the_loglikelihood_increases_monotonically(self):
        """A physical M-step that is a real EM step cannot decrease it."""
        ns = _run(DISTANCE_EXAMPLE)
        ll = np.array([h[1] for h in ns["history"]])
        self.assertTrue(np.all(np.diff(ll) > -1e-6), ll)


class TestBurstPipelineExample(unittest.TestCase):
    """The full pipeline: SimEngine -> TTTR -> BurstFilter -> selection -> HMM.

    Its claim is that photon-wise modelling resolves states a burst-wise
    histogram cannot, so both halves are asserted. Checking only that the HMM
    recovers three states would pass even if the histogram had shown them too,
    in which case the example would be demonstrating nothing.
    """

    def test_pipeline_runs_and_resolves_what_the_histogram_cannot(self):
        self.assertTrue(PIPELINE_EXAMPLE.exists(), f"missing: {PIPELINE_EXAMPLE}")
        ns = _run(PIPELINE_EXAMPLE)

        # The pipeline actually ran: bursts found, then narrowed by selection.
        self.assertGreater(len(ns["bursts"]), 100)
        self.assertLess(len(ns["selected"]), len(ns["bursts"]))
        self.assertGreater(ns["eng"].get_n_photons(), 5000)

        # Burst-wise: one averaged population, far narrower than the state spread.
        E_burst = ns["E_burst"]
        spread = max(ns["E_STATES"]) - min(ns["E_STATES"])
        self.assertLess(E_burst.std(), spread / 3.0,
                        "bursts should average -- exchange is faster than diffusion")

        # Photon-wise: the three states come back.
        np.testing.assert_allclose(ns["E_fit"], sorted(ns["E_STATES"]), atol=0.09)


class TestBlinkingAcceptorExample(unittest.TestCase):
    """A blinking acceptor against a distant one -- the hardest confusion here.

    The example's claim is that a dark acceptor and a low-FRET state are the
    *same* observation by intensity and differ only in donor lifetime, so both
    halves are asserted: that the two really are indistinguishable in E, and
    that decoding separates them anyway. Checking only the recovery would pass
    even if the dark state had accidentally been distinguishable by intensity,
    which is exactly the case the example exists to rule out.
    """

    def test_blinking_and_distances_are_recovered(self):
        self.assertTrue(BLINKING_EXAMPLE.exists(), f"missing: {BLINKING_EXAMPLE}")
        ns = _run(BLINKING_EXAMPLE)

        # Blinking rate, from a 10 us truth. This is the headline number: the
        # acceptor toggles several times within a burst.
        self.assertAlmostEqual(ns["A"][0, 3], ns["K_BLINK_OFF"], delta=0.015)

        # Distances refined from a clustered 45 / 50 / 58 start. The tolerance is
        # asymmetric on purpose -- see the example's closing note: at 65 A the
        # quenching is weak, so that state and the dark one genuinely converge,
        # and it comes back short. Tightening this would be asserting past the
        # information in the data.
        R, truth = ns["R"], np.asarray(ns["R_STATES"])
        np.testing.assert_array_less(np.abs(R[:2] - truth[:2]), 2.0)
        np.testing.assert_array_less(abs(R[2] - truth[2]), 6.0)
        self.assertTrue(R[0] < R[1] < R[2])

    def test_the_dark_state_is_invisible_to_intensity_alone(self):
        """The negative half: E cannot tell the dark state from R = 65 A."""
        ns = _run(BLINKING_EXAMPLE)

        # By intensity the two low-FRET states are close, and both are far from
        # the high-FRET state -- so E alone puts them in the same place.
        E = ns["E_TRUE"]
        self.assertLess(abs(E[2] - E[3]), 0.25)
        self.assertGreater(abs(E[0] - E[2]), 0.5)

        # By donor lifetime they are not: the dark acceptor is the slowest, and
        # the gap is what the decoding runs on.
        tau = ns["TAU_TRUE"]
        self.assertGreater(tau[3], tau[2])
        self.assertGreater(tau[3] - tau[2], 0.5)

        # And the decoding does find it, despite the confusion being one-sided.
        self.assertGreater(ns["recall"], 0.75)
        self.assertGreater(ns["precision"], 0.55)


class TestCoincidenceExample(unittest.TestCase):
    """Multi-molecule coincidence: a negative result, so it needs both halves.

    The example claims coincidence manufactures dynamics AND that no burst
    statistic can find it. Asserting only the second would pass on data where
    coincidence did no damage, making the negative result vacuous -- so the
    damage is asserted too, on species that are static by construction.
    """

    def test_static_molecules_appear_to_switch(self):
        self.assertTrue(COINCIDENCE_EXAMPLE.exists(), f"missing: {COINCIDENCE_EXAMPLE}")
        ns = _run(COINCIDENCE_EXAMPLE)

        # Nothing in the simulation ever changes state, so every one of these is
        # an artifact, and the crowded sample must show more of it.
        sw = ns["switching"]
        pops = list(ns["POPULATIONS"])
        self.assertGreater(sw[pops[-1]], sw[pops[0]])
        self.assertGreater(sw[pops[-1]], 1e-4)

        # Coincidence rises with occupancy -- the lever the example recommends.
        frac = {p: np.mean([r["label"].mean() for r in ns["runs"][p]]) for p in pops}
        self.assertGreater(frac[pops[-1]], frac[pops[0]])
        self.assertGreater(frac[pops[-1]], 0.10)

    def test_no_burst_statistic_detects_it(self):
        """The negative half, pooled over seeds -- one run would test the seed."""
        ns = _run(COINCIDENCE_EXAMPLE)
        auc, runs = ns["auc"], ns["runs"]
        for p in (0.25, 1.0):
            label = np.concatenate([r["label"] for r in runs[p]])
            for stat in ns["STATS"]:
                score = np.concatenate([r[stat] for r in runs[p]])
                a = auc(score, label)
                self.assertLess(a, 0.70,
                                f"{stat} at population {p} reached AUC {a:.3f} -- "
                                f"a detector became possible and the example is stale")

    def test_the_example_uses_independent_seeds(self):
        """Its own headline lesson, enforced.

        `SimEngine` is deterministic, so unseeded replicates are copies and the
        spread the example reports would be meaningless.
        """
        ns = _run(COINCIDENCE_EXAMPLE)
        self.assertGreater(len(ns["SEEDS"]), 1)
        a, b = ns["runs"][0.25][0]["duration"], ns["runs"][0.25][1]["duration"]
        self.assertFalse(len(a) == len(b) and np.allclose(a, b),
                         "replicates are identical -- seeds are not reaching SimEngine")


class TestPhasorDiagnosticExample(unittest.TestCase):
    """A model-free check that catches what E does not.

    The claim has two halves and both are asserted: the misspecified fit must
    look FINE on the headline number, and the phasor must separate it anyway.
    Checking only the separation would pass on a fit that was visibly broken,
    where no diagnostic would have been needed.
    """

    def test_phasor_separates_models_that_E_cannot(self):
        self.assertTrue(PHASOR_EXAMPLE.exists(), f"missing: {PHASOR_EXAMPLE}")
        ns = _run(PHASOR_EXAMPLE)

        # Half one: the wrong model recovers E as well as the right one, so the
        # usual output gives no warning at all.
        truth = np.asarray(ns["E_TRUE"])
        err_bad = np.abs(np.asarray(ns["E_bad"]) - truth).max()
        err_good = np.abs(np.asarray(ns["E_good"]) - truth).max()
        self.assertLess(err_bad, 0.02, "the misspecified fit should still look fine in E")
        self.assertLess(abs(err_bad - err_good), 0.02,
                        "if E already separated them, the diagnostic proves nothing")

        # Half two: the phasor distance does separate them, with no overlap
        # across replicate datasets.
        self.assertGreater(ns["m_bad"], 2.0 * ns["m_good"])
        self.assertLess(ns["good_all"].max(), ns["bad_all"].min(),
                        "correct and misspecified distances must not overlap")

    def test_the_likelihood_agrees_but_says_less(self):
        """The companion claim: the likelihood ranks the models correctly.

        If it did not, the example would be recommending a diagnostic over a
        broken likelihood rather than beside a working one.
        """
        ns = _run(PHASOR_EXAMPLE)
        self.assertGreater(ns["ll_good"], ns["ll_bad"])


class TestBootstrapExample(unittest.TestCase):
    """Burst resampling as an error bar for the maximum-likelihood path.

    The claim is that the interval is honest and that the cheap analytic width
    is not, so both are asserted. Checking only that the bootstrap covers would
    pass even if the analytic width covered equally well, in which case the
    extra cost would buy nothing.
    """

    def test_the_bootstrap_interval_covers_and_beats_the_analytic_one(self):
        self.assertTrue(BOOTSTRAP_EXAMPLE.exists(), f"missing: {BOOTSTRAP_EXAMPLE}")
        ns = _run(BOOTSTRAP_EXAMPLE)

        # Tolerant on the low side: the example runs a small budget on purpose,
        # where the coverage estimate itself carries a standard error of a few
        # percent. A larger run measures 94.4% +- 1.8%.
        self.assertGreater(ns["tot_b"], 0.82,
                           "bootstrap coverage collapsed well below nominal 95%")
        self.assertLessEqual(ns["tot_b"], 1.0)

        # The analytic width is a documented LOWER bound, so it must under-cover
        # by a clear margin -- otherwise the bootstrap is not worth its cost.
        self.assertLess(ns["tot_a"], ns["tot_b"] - 0.10,
                        "analytic width should under-cover markedly")

    def test_states_are_ordered_before_summarising(self):
        """Guards the trap the example warns about.

        Without canonical ordering the replicate spread measures label
        switching, which inflates every interval. If that regressed, the
        intervals would balloon and the low-FRET/high-FRET means would collide.
        """
        ns = _run(BOOTSTRAP_EXAMPLE)
        draws = ns["draws"]
        self.assertLess(draws["B0"].mean(), draws["B1"].mean())
        # A label-switched chain would show a bimodal, near-full-range spread.
        self.assertLess(draws["B1"].std(), 0.05)


if __name__ == "__main__":
    unittest.main()
