// CLSM reconstruction test for the Java bindings. Verifies that the header-based
// CLSM auto-configuration (ported from the Python wrapper into src/CLSMImage.cpp)
// reconstructs the same intensity image as the Python API, and that the
// get_intensity_into(int[]) bulk accessor marshals the pixels correctly.
//
// Reference: imaging/pq/ht3/pq_ht3_clsm.ht3 -> 40 x 256 x 256, sum 3364714, max 26.

import io.github.fluorescencetools.tttrlib.*;

public class CLSMTest {
    static final int REF_FRAMES = 40, REF_LINES = 256, REF_PIXEL = 256;
    static final long REF_SUM = 3364714L;
    static final int REF_MAX = 26;
    // FastLifetime (mean micro time) and phasor pixel counts (default params)
    static final int REF_MMT_NONZERO = 610553;
    static final int REF_PHASOR_VALID = 412275;

    public static void main(String[] args) {
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
        if (!ok) {
            System.err.println("CLSM test: FAIL");
            System.exit(1);
        }
        System.out.println("CLSM test: PASS");
    }
}
