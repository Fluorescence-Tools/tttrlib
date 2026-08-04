// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.tests;

import io.github.fluorescencetools.tttrlib.*;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * JUnit port of test/java/LifetimeTest.java (PRD-001 cross-language reference).
 */
public class LifetimeTest {

    static double meanNonzero(double[] a) {
        double s = 0; int c = 0;
        for (double v : a) if (v > 0) { s += v; c++; }
        return c > 0 ? s / c : 0;
    }

    @Test
    public void referenceValuesMatch() throws Exception {
        String root = System.getenv().getOrDefault("TTTRLIB_DATA", "tttr-data");
        TTTR t = new TTTR(root + "/imaging/pq/ht3/pq_ht3_clsm.ht3");
        VectorInt32 ch = new VectorInt32(); ch.add(0);
        CLSMImage img = new CLSMImage(t, new CLSMSettings(), null, true, ch);

        double offset = img.get_decay_irf_offset();
        int n = img.getN_frames() * img.getN_lines() * img.getN_pixel();
        double[] raw = new double[n], corr = new double[n];
        img.get_mean_micro_time_into(raw, -1.0, 2, false);
        img.get_mean_micro_time_into(corr, -1.0, 2, true);
        double mr = meanNonzero(raw), mc = meanNonzero(corr);

        System.out.printf("IRF offset=%.3g  FastLifetime mean raw=%.3g corrected=%.3g%n",
                offset, mr, mc);
        boolean ok = offset > 0 && mr > 0 && mc >= 0 && mc < mr
                     && Math.abs((mr - mc) - offset) < offset * 0.01;
        assertTrue(ok, "Lifetime IRF-correction test: FAIL");
        System.out.println("Lifetime IRF-correction test: PASS");
    }
}
