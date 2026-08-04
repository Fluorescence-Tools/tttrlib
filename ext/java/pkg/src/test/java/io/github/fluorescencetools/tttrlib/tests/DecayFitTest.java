// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.tests;

import io.github.fluorescencetools.tttrlib.*;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * JUnit port of test/java/DecayFitTest.java (PRD-001 cross-language reference).
 */
public class DecayFitTest {

    static final int FN = 32;
    static final double DT = 0.5;

    static final double REF_TWO_ISTAR = 23.802337;
    static final double REF_FIT_TAU = 0.74219;
    static final double REF_FIT_RS = 0.25974;
    static final double REF_FIT24_TI = 2.41049;
    static final double REF_FIT25_TI = 4.738831;
    static final double REF_FIT25_BEST_TAU = 0.5;
    static final double REF_FIT26_TI = 2.218772;

    static final int[] DATA = {
        0, 0, 0, 1, 9, 7, 5, 5, 5, 2, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 2, 2, 2, 3, 0, 1, 0,
        1, 1, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };


    static VectorDouble vd(double... v) {
        VectorDouble r = new VectorDouble();
        for (double x : v) r.add(x);
        return r;
    }

    static VectorInt32 vi(int... v) {
        VectorInt32 r = new VectorInt32();
        for (int x : v) r.add(x);
        return r;
    }

    /** The deterministic IRF shared by every language's copy of this test. */
    static VectorDouble irf() {
        VectorDouble r = new VectorDouble();
        for (int half = 0; half < 2 * FN; half += FN)
            for (int i = 0; i < FN; i++)
                r.add(Math.exp(-Math.pow(i - 8.0, 2) / (2 * 0.5 * 0.5)));
        return r;
    }

    static final String SETUP_JSON =
        "{\"dt\": " + DT + ", \"period\": " + (2.0 * FN) + ", \"g_factor\": 1.0,"
        + " \"l1\": 0.1, \"l2\": 0.1, \"convolution_stop\": " + (FN / 2 - 1) + ","
        + " \"soft_bifl_scatter_flag\": true}";

    static DecayFitProblem problem(double bgLevel) {
        DecayFitProblem p = new DecayFitProblem(2, FN, DT);
        p.setIrf(irf());
        VectorDouble bg = new VectorDouble();
        for (int i = 0; i < 2 * FN; i++) bg.add(bgLevel);
        p.setBackground(bg);
        VectorDouble data = new VectorDouble();
        for (int v : DATA) data.add((double) v);
        p.setData(data);
        return p;
    }

    static DecayFitOutcome runFit(String name, VectorDouble start,
                                  VectorInt32 link, double bgLevel) {
        DecayFit2 fit = new DecayFit2(
            name, tttrlib.decay_fit_setup_vector(name, SETUP_JSON), irf());
        return fit.fit(start, new DecayFitConstraints(link), problem(bgLevel));
    }

    /** A result column by name, because the registry names them. */
    static double resultOf(String model, DecayFitOutcome out, String column) {
        VectorString names = tttrlib.decay_fit_result_names(model, 0);
        for (int i = 0; i < names.size(); i++) {
            if (names.get(i).equals(column)) return out.getResults().get(i);
        }
        throw new IllegalArgumentException(model + " has no result '" + column + "'");
    }

    @Test
    public void referenceValuesMatch() throws Exception {
        // fit23: tau and gamma free, r0/rho held (link -1 = fixed).
        // Start at 1.0: this 58-photon reference is sparse enough that the objective
        // falls monotonically with tau, so the historical answer is a local minimum
        // whose basin is roughly 0.5 to 1.2. Outside it the fit "succeeds" with tau
        // in the tens of thousands. See the Python reference test for the full note.
        DecayFitOutcome r23 = runFit("fit23", vd(1.0, 0.01, 0.38, 1.2),
                                     vi(0, 0, -1, -1), 0.0);
        assertTrue(Math.abs(r23.getObjective() - REF_TWO_ISTAR) < 1e-3,
              "fit23 twoIstar == " + REF_TWO_ISTAR + " (got " + r23.getObjective() + ")");
        assertTrue(Math.abs(r23.getParameters().get(0) - REF_FIT_TAU) < 1e-3,
              "fit23 tau == " + REF_FIT_TAU + " (got " + r23.getParameters().get(0) + ")");
        assertTrue(Math.abs(resultOf("fit23", r23, "r_experimental") - REF_FIT_RS) < 1e-3,
              "fit23 r_experimental == " + REF_FIT_RS);

        // fit24 / fit25 / fit26 (bg = 0.2 keeps the MLE finite).
        DecayFitOutcome r24 = runFit("fit24", vd(3.8, 0.02, 0.4, 0.8, 1.0),
                                     vi(0, 0, 0, 0, 0), 0.2);
        assertTrue(Math.abs(r24.getObjective() - REF_FIT24_TI) < 1e-3,
              "fit24 twoIstar == " + REF_FIT24_TI + " (got " + r24.getObjective() + ")");

        DecayFitOutcome r25 = runFit("fit25", vd(0.5, 1.0, 2.0, 4.0, 0.02, 0.38),
                                     vi(0, 0, 0, 0, -1, -1), 0.2);
        assertTrue(Math.abs(r25.getObjective() - REF_FIT25_TI) < 1e-3,
              "fit25 twoIstar == " + REF_FIT25_TI + " (got " + r25.getObjective() + ")");
        assertTrue(Math.abs(r25.getParameters().get(0) - REF_FIT25_BEST_TAU) < 1e-3,
              "fit25 best tau == " + REF_FIT25_BEST_TAU);
        // fit25 classifies rather than measures, so which candidate won is the answer.
        assertTrue(resultOf("fit25", r25, "selected_index") == 0.0,
              "fit25 selected_index == 0");

        DecayFitOutcome r26 = runFit("fit26", vd(0.5), vi(0), 0.2);
        assertTrue(Math.abs(r26.getObjective() - REF_FIT26_TI) < 1e-3,
              "fit26 twoIstar == " + REF_FIT26_TI + " (got " + r26.getObjective() + ")");

        // The registry describes what the code does.
        assertTrue(tttrlib.decay_fit_setup_names("fit23").size()
              == tttrlib.decay_fit_setup_vector("fit23", "{}").size(),
              "setup names and setup vector must have the same length");

        System.out.println("Java decay-fit cross-language reference test (fit23-26): PASS");
    }
}
