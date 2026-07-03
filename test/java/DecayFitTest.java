// Cross-language DecayFit23 reference test for the Java bindings (PRD-001).
// DecayFit23.modelf takes plain double[] arrays, so it is callable identically
// from Python, R and Java. Asserts the SAME model-function values as
// test/python/decayfit/test_decayfit_cross_language.py and test/r/test_decayfit.R.

import io.github.fluorescencetools.tttrlib.DecayFit23;
import io.github.fluorescencetools.tttrlib.DecayFit24;
import io.github.fluorescencetools.tttrlib.DecayFit25;
import io.github.fluorescencetools.tttrlib.DecayFit26;
import io.github.fluorescencetools.tttrlib.DecayFitData;
import io.github.fluorescencetools.tttrlib.VectorDouble;
import io.github.fluorescencetools.tttrlib.VectorInt32;

public class DecayFitTest {
    static final int N = 16;
    static final double REF_OUT_4 = 0.092688;
    static final double REF_OUT_20 = 0.043233;
    static final double REF_SUM = 0.99;
    // full fit23 loop reference (fit_v)
    static final int FN = 32;
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

    static void check(boolean cond, String msg) {
        if (!cond) { System.err.println("FAIL: " + msg); System.exit(1); }
    }

    static VectorDouble vd(double... v) { VectorDouble r = new VectorDouble(); for (double x : v) r.add(x); return r; }
    static VectorInt32 vi(int... v) { VectorInt32 r = new VectorInt32(); for (int x : v) r.add(x); return r; }

    // DecayFitData with the shared deterministic IRF/data and a chosen background.
    static DecayFitData fitData(double bgLevel) {
        VectorDouble irf = new VectorDouble(), bg = new VectorDouble(), corr = new VectorDouble();
        for (int half = 0; half < 2 * FN; half += FN)
            for (int i = 0; i < FN; i++) irf.add(Math.exp(-Math.pow(i - 8.0, 2) / (2 * 0.5 * 0.5)));
        for (int i = 0; i < 2 * FN; i++) bg.add(bgLevel);
        double[] cc = {2.0 * FN, 1.0, 0.1, 0.1, FN / 2 - 1};
        for (double c : cc) corr.add(c);
        VectorInt32 data = new VectorInt32();
        for (int v : DATA) data.add(v);
        return new DecayFitData(0.5, corr, irf, bg, data);
    }

    public static void main(String[] args) {
        double[] irf = new double[2 * N], bg = new double[2 * N], m = new double[2 * N];
        for (int half = 0; half < 2 * N; half += N)
            for (int i = 0; i < N; i++)
                irf[half + i] = Math.exp(-Math.pow(i - 4.0, 2) / (2 * 0.5 * 0.5));
        double[] param = {2.0, 0.01, 0.38, 1.2};                 // tau, gamma, r0, rho
        double[] corrections = {2.0 * N, 1.0, 0.1, 0.1, N - 1};  // period, g, l1, l2, conv_stop

        DecayFit23.modelf(param, irf, bg, 0.5, corrections, m);

        double sum = 0; for (double v : m) sum += v;
        check(Math.abs(m[4] - REF_OUT_4) < 1e-5, "modelf out[4] == " + REF_OUT_4 + " (got " + m[4] + ")");
        check(Math.abs(m[20] - REF_OUT_20) < 1e-5, "modelf out[20] == " + REF_OUT_20 + " (got " + m[20] + ")");
        check(Math.abs(sum - REF_SUM) < 1e-5, "modelf sum == " + REF_SUM + " (got " + sum + ")");

        // --- full fit loop via the cross-language fit_v accessor ---
        // fit23 (bg = 0): fit lifetime, r0/rho fixed
        VectorDouble r23 = DecayFit23.fit_v(vd(2.1, 0.01, 0.38, 1.2, -1, 0, 0, 0), vi(0, 0, 1, 1), fitData(0.0));
        check(Math.abs(r23.get(0) - REF_TWO_ISTAR) < 1e-3, "fit23 twoIstar == " + REF_TWO_ISTAR + " (got " + r23.get(0) + ")");
        check(Math.abs(r23.get(1) - REF_FIT_TAU) < 1e-3, "fit23 fitted tau == " + REF_FIT_TAU + " (got " + r23.get(1) + ")");
        check(Math.abs(r23.get(7) - REF_FIT_RS) < 1e-3, "fit23 fitted r_s == " + REF_FIT_RS + " (got " + r23.get(7) + ")");

        // fit24/25/26 (bg = 0.2 keeps the MLE finite)
        VectorDouble r24 = DecayFit24.fit_v(vd(3.8, 0.02, 0.4, 0.8, 1.0, -1.0, 0, 0), vi(0, 0, 0, 0, 0), fitData(0.2));
        check(Math.abs(r24.get(0) - REF_FIT24_TI) < 1e-3, "fit24 twoIstar == " + REF_FIT24_TI + " (got " + r24.get(0) + ")");

        VectorDouble r25 = DecayFit25.fit_v(vd(0.5, 1.0, 2.0, 4.0, 0.02, 0.38, 0, 0, 0), vi(0, 0, 0, 0, 1, 1), fitData(0.2));
        check(Math.abs(r25.get(0) - REF_FIT25_TI) < 1e-3, "fit25 twoIstar == " + REF_FIT25_TI + " (got " + r25.get(0) + ")");
        check(Math.abs(r25.get(1) - REF_FIT25_BEST_TAU) < 1e-3, "fit25 best tau == " + REF_FIT25_BEST_TAU + " (got " + r25.get(1) + ")");

        VectorDouble r26 = DecayFit26.fit_v(vd(0.5, 0), vi(0), fitData(0.2));
        check(Math.abs(r26.get(0) - REF_FIT26_TI) < 1e-3, "fit26 twoIstar == " + REF_FIT26_TI + " (got " + r26.get(0) + ")");

        System.out.println("Java DecayFit cross-language reference test (fit23-26): PASS");
    }
}
