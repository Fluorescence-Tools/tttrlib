// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.tests;

import io.github.fluorescencetools.tttrlib.*;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * JUnit port of test/java/PdaTest.java (PRD-001 cross-language reference).
 */
public class PdaTest {

    static final int NMAX = 30;
    static final int REF_N = (NMAX + 1) * (NMAX + 1);
    static final double REF_S1S2_SUM = 1.0;
    static final double REF_S1S2_MAX = 0.01800533;
    static final double REF_HIST1D_SUM = 0.92940452;


    @Test
    public void referenceValuesMatch() throws Exception {
        // deterministic Poisson pF (lambda = 10)
        double[] pF = new double[NMAX + 1];
        pF[0] = Math.exp(-10.0);
        for (int i = 1; i <= NMAX; i++) pF[i] = pF[i - 1] * 10.0 / i;
        double s = 0; for (double v : pF) s += v;
        for (int i = 0; i <= NMAX; i++) pF[i] /= s;
        VectorDouble pfv = new VectorDouble(); for (double v : pF) pfv.add(v);

        Pda pda = new Pda(NMAX, 5, 0.0, 0.0, pfv);   // background 0; PDA_DEFAULT
        pda.append(0.5, 0.3);
        pda.append(0.5, 0.7);
        pda.evaluate();

        double[] mat = new double[REF_N];
        int n = pda.get_S1S2_matrix_into(mat);
        double sum = 0, max = 0;
        for (int i = 0; i < n; i++) { sum += mat[i]; if (mat[i] > max) max = mat[i]; }

        assertTrue(n == REF_N, "S1S2 cells == " + REF_N + " (got " + n + ")");
        assertTrue(Math.abs(sum - REF_S1S2_SUM) < 1e-5, "S1S2 sum == " + REF_S1S2_SUM + " (got " + sum + ")");
        assertTrue(Math.abs(max - REF_S1S2_MAX) < 1e-6, "S1S2 max == " + REF_S1S2_MAX + " (got " + max + ")");

        // 1-D histogram (over the current S1S2 model) via the get_1dhistogram_y_into accessor
        double[] hy = new double[81];
        int nb = pda.get_1dhistogram_y_into(hy, 1000.0, 0.01, 81, true);
        double hsum = 0; for (int i = 0; i < nb; i++) hsum += hy[i];
        assertTrue(Math.abs(hsum - REF_HIST1D_SUM) < 1e-5, "1d histogram sum == " + REF_HIST1D_SUM + " (got " + hsum + ")");

        System.out.println("Java PDA cross-language reference test: PASS");
    }
}
