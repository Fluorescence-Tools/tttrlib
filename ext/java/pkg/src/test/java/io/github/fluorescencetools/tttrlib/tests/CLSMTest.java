// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.tests;

import io.github.fluorescencetools.tttrlib.*;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * JUnit port of test/java/CLSMTest.java (PRD-001 cross-language reference).
 */
public class CLSMTest {

    static final int REF_FRAMES = 40, REF_LINES = 256, REF_PIXEL = 256;
    static final long REF_SUM = 3364714L;
    static final int REF_MAX = 26;
    // FastLifetime (mean micro time) and phasor pixel counts (default params)
    static final int REF_MMT_NONZERO = 610553;
    static final int REF_PHASOR_VALID = 412275;

    @Test
    public void referenceValuesMatch() throws Exception {
        String root = System.getenv().getOrDefault("TTTRLIB_DATA", "tttr-data");
        TTTR tttr = new TTTR(root + "/imaging/pq/ht3/pq_ht3_clsm.ht3");

        VectorInt32 channels = new VectorInt32();
        channels.add(0);
        CLSMImage img = new CLSMImage(tttr, new CLSMSettings(), null, true, channels);

        int nf = img.getN_frames(), nl = img.getN_lines(), np = img.getN_pixel();
        int[] flat = new int[nf * nl * np];
        img.get_intensity_into(flat);

        long sum = 0; int max = 0;
        for (int v : flat) { sum += v; if (v > max) max = v; }

        // FastLifetime (mean micro time) — count pixels with a defined value (> 0)
        double[] mmt = new double[nf * nl * np];
        img.get_mean_micro_time_into(mmt, -1.0, 2, false, false);
        int mmtNonzero = 0; for (double v : mmt) if (v > 0) mmtNonzero++;

        // Phasor — count pixels with a defined g coordinate (> -1)
        float[] phasor = new float[nf * nl * np * 2];
        img.get_phasor_into(phasor, -1.0, 2, false, false);
        int phasorValid = 0; for (int i = 0; i < nf * nl * np; i++) if (phasor[i * 2] > -1) phasorValid++;

        System.out.println("CLSM " + nf + "x" + nl + "x" + np + " sum=" + sum + " max=" + max
                + " mmt_nonzero=" + mmtNonzero + " phasor_valid=" + phasorValid);
        boolean ok = nf == REF_FRAMES && nl == REF_LINES && np == REF_PIXEL
                     && sum == REF_SUM && max == REF_MAX
                     && mmtNonzero == REF_MMT_NONZERO && phasorValid == REF_PHASOR_VALID;

        ok &= checkBeckerHicklImaging(root);
        ok &= checkLeica(root, "SP8", 2,
                "/imaging/leica/sp8/da/G-28_C-28_S1_6_1.ptu", 93, 512, 512, 2758188L);
        ok &= checkLeica(root, "SP5", 1,
                "/imaging/leica/sp5/LSM_1.ptu", 230, 256, 256, 3486614L);

        assertTrue(ok, "CLSM test: FAIL");
        System.out.println("CLSM test: PASS");
    }

    // Becker & Hickl SPC imaging. The .spc record carries no scan geometry: it
    // comes from the .set sidecar (SP_IMG_X/SP_IMG_Y/SP_PIX_CLK) and the marker
    // layout from the BH_SPC_ReadingRoutine tag. That mapping used to live only
    // in the Python wrapper, so this file reconstructed to 0 frames from Java,
    // R and C++ while Python produced a correct image. Reference values are the
    // ones Python reports for the same file.
    static final int BH_FRAMES = 20, BH_LINES = 512, BH_PIXEL = 512;
    static final long BH_SUM = 1036407L;

    static boolean checkBeckerHicklImaging(String root) {
        String path = root + "/imaging/bh/spcm/FocalCheck_A1_20x_8xzoom_750nm_m1.spc";
        if (!new java.io.File(path).exists()) {
            System.out.println("BH imaging file not present; skipping BH check.");
            return true;
        }
        TTTR tttr = new TTTR(path);
        CLSMImage img = new CLSMImage(tttr, new CLSMSettings(), null, true, new VectorInt32());
        int nf = img.getN_frames(), nl = img.getN_lines(), np = img.getN_pixel();
        long sum = 0;
        if (nf > 0 && nl > 0 && np > 0) {
            int[] flat = new int[nf * nl * np];
            img.get_intensity_into(flat);
            for (int v : flat) sum += v;
        }
        System.out.println("BH  " + nf + "x" + nl + "x" + np + " sum=" + sum);
        boolean ok = nf == BH_FRAMES && nl == BH_LINES && np == BH_PIXEL && sum == BH_SUM;
        if (!ok) {
            System.err.println("BH imaging: FAIL (expected " + BH_FRAMES + "x" + BH_LINES + "x"
                    + BH_PIXEL + " sum=" + BH_SUM + ")");
        }
        return ok;
    }

    // Leica SP5/SP8. The marker conventions for these reading routines also used
    // to live only in the Python wrapper. Reference values are what Python
    // reports for the same files.
    static boolean checkLeica(String root, String label, int routine, String rel,
                              int frames, int lines, int pixel, long refSum) {
        String path = root + rel;
        if (!new java.io.File(path).exists()) {
            System.out.println("Leica " + label + " file not present; skipping.");
            return true;
        }
        TTTR tttr = new TTTR(path, "PTU");
        CLSMSettings s = new CLSMSettings();
        s.setReading_routine(routine);
        CLSMImage img = new CLSMImage(tttr, s, null, true, new VectorInt32());
        int nf = img.getN_frames(), nl = img.getN_lines(), np = img.getN_pixel();
        long sum = 0;
        if (nf > 0 && nl > 0 && np > 0) {
            int[] flat = new int[nf * nl * np];
            img.get_intensity_into(flat);
            for (int v : flat) sum += v;
        }
        System.out.println(label + " " + nf + "x" + nl + "x" + np + " sum=" + sum);
        boolean ok = nf == frames && nl == lines && np == pixel && sum == refSum;
        if (!ok) {
            System.err.println("Leica " + label + ": FAIL (expected " + frames + "x" + lines
                    + "x" + pixel + " sum=" + refSum + ")");
        }
        return ok;
    }
}
