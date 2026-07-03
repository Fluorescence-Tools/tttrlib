// FastLifetime (mean micro time) test for the Java bindings, including the
// C++ IRF-offset correction (CLSMImage::get_mean_micro_time correct_irf_offset /
// get_decay_irf_offset). Verifies that the IRF offset is the decay leading-edge
// rise and that subtracting it lowers the mean arrival time.
//
// Reference: imaging/pq/ht3/pq_ht3_clsm.ht3 (micro-time resolution 1 ps),
// decay rise near channel 680 -> IRF offset ~6.8e-10 s.

import io.github.fluorescencetools.tttrlib.*;

public class LifetimeTest {
    static double meanNonzero(double[] a) {
        double s = 0; int c = 0;
        for (double v : a) if (v > 0) { s += v; c++; }
        return c > 0 ? s / c : 0;
    }

    public static void main(String[] args) {
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
        if (!ok) {
            System.err.println("Lifetime IRF-correction test: FAIL");
            System.exit(1);
        }
        System.out.println("Lifetime IRF-correction test: PASS");
    }
}
