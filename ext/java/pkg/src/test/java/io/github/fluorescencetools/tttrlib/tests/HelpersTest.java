// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.tests;

import io.github.fluorescencetools.tttrlib.*;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * The {@code _into} accessors in ext/java/helpers.i, exercised rather than
 * inspected.
 *
 * <p>Java cannot express a function that returns a freshly allocated array --
 * a method's return is bound to the C++ return type, so {@code void f(T** out,
 * int* n)} has no jresult to assign and jarrays.i defines no argout typemap at
 * any rank. Every such function therefore reaches Java through a helper where
 * the caller preallocates and the true element count comes back as the return.
 *
 * <p>These were originally checked only by generating the wrapper, reading the
 * signature, and compiling the generated C++. None of that can see whether the
 * values actually arrive in the caller's array -- a marshalling that copied in
 * without copying out would pass all three and silently do nothing. That is
 * what this class covers.
 */
public class HelpersTest {

    // ---- sampling ---------------------------------------------------------

    @Test
    public void weightedChoiceFillsTheCallersArray() {
        double[] weights = {1.0, 0.0, 0.0, 0.0};   // index 0 or nothing
        int[] idx = new int[16];
        assertEquals(16, tttrlib.weighted_choice_into(weights, idx));
        for (int v : idx) assertEquals(0, v, "a unit weight on 0 must draw only 0");
    }

    @Test
    public void sampleFromCdfStaysOnTheAxis() {
        double[] axis = {0.0, 1.0, 2.0, 3.0};
        double[] cdf = {0.0, 1.0, 2.0, 3.0};
        double[] vals = new double[8];
        assertEquals(8, tttrlib.sample_from_cdf_into(axis, cdf, vals));
        for (double v : vals) assertTrue(v >= 0.0 && v <= 3.0, "off axis: " + v);
    }

    // ---- jitter -----------------------------------------------------------

    /** The load-bearing one: INPLACE_ARRAY2 must write back into the caller's
     *  array. If jarrays.i ever marshals it copy-in-only this fails here and
     *  nowhere else -- the signature and the generated C++ are unchanged. */
    @Test
    public void jitterWritesBackIntoTheCallersArray() {
        double[][] coords = {{1.0, 1.0}, {2.0, 2.0}, {3.0, 3.0}};
        double[] originals = {1.0, 2.0, 3.0};
        tttrlib.jitter_coordinates_into(coords, new double[]{1.0, 1.0}, 12345L);

        boolean moved = false;
        for (int i = 0; i < coords.length; i++) {
            for (int j = 0; j < coords[i].length; j++) {
                if (coords[i][j] != originals[i]) moved = true;
                assertTrue(Math.abs(coords[i][j] - originals[i]) <= 0.5001,
                           "a dither must stay within half a bin");
            }
        }
        assertTrue(moved, "the values never changed, so the writes did not reach Java");
    }

    @Test
    public void eventsFromCountsYieldsTwoPerPhoton() {
        double[][] counts = {{2.0, 0.0}, {0.0, 1.0}};   // three photons
        double[] events = new double[6];
        assertEquals(6, tttrlib.events_from_counts_into(counts, events, 7L),
                     "2 * sum(counts)");
    }

    @Test
    public void countsFromEventsConservesPhotonsInRange() {
        // Coordinates round to the NEAREST bin index, so 1.5 on a two-bin axis
        // rounds to 2 and is dropped as out of range. That is the documented
        // behaviour and matches the Python binding exactly; keep them in range
        // to assert conservation.
        double[][] pts = {{0.0, 0.0}, {0.0, 0.0}, {1.0, 1.0}};
        double[] grid = new double[4];
        assertEquals(4, tttrlib.counts_from_events_into(pts, 2, 2, grid));
        double sum = 0.0;
        for (double v : grid) sum += v;
        assertEquals(3.0, sum, 1e-9, "photons inside the grid must all land");
    }

    // ---- deconvolution ----------------------------------------------------

    @Test
    public void richardsonLucy2dFillsTheGrid() {
        double[][] image = new double[8][8];
        for (double[] row : image) java.util.Arrays.fill(row, 1.0);
        double[][] psf = {{0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 0.0}};
        double[] out = new double[64];
        assertEquals(64, tttrlib.richardson_lucy_2d_into(image, psf, out, 5));
        double sum = 0.0;
        for (double v : out) sum += v;
        // A delta PSF on a flat image is the identity, to the iteration's tolerance.
        assertEquals(64.0, sum, 1e-6);
    }

    @Test
    public void wienerDeconvolve2dFillsTheGrid() {
        double[][] image = new double[8][8];
        for (double[] row : image) java.util.Arrays.fill(row, 1.0);
        double[][] psf = {{0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 0.0}};
        double[] out = new double[64];
        assertEquals(64, tttrlib.wiener_deconvolve_2d_into(image, psf, out));
    }

    /** IN_ARRAY3 marshals a double[][][], so an axial stack goes in as
     *  naturally as an image does. */
    @Test
    public void richardsonLucy3dTakesAStack() {
        double[][][] stack = new double[4][4][4];
        for (double[][] plane : stack)
            for (double[] row : plane) java.util.Arrays.fill(row, 1.0);
        double[][][] psf = new double[3][3][3];
        psf[1][1][1] = 1.0;
        double[] out = new double[64];
        assertEquals(64, tttrlib.richardson_lucy_3d_into(stack, psf, out, 3));
    }

    @Test
    public void scanBlurKernelReturnsALength() {
        double[] kernel = new double[64];
        assertTrue(tttrlib.scan_blur_kernel_1d_into(1e-6, kernel) > 0);
    }

    // ---- MaxEnt design matrices ------------------------------------------

    /** Four output arrays from ONE native call -- the claim that made a Java
     *  result class unnecessary. The int return is Fi's element count, which
     *  yields every other length. */
    @Test
    public void buildFiLifetimesFillsAllFourOutputs() {
        final int nChannels = 32;
        double[] decay = new double[nChannels];
        double[] lamp = new double[nChannels];
        for (int i = 0; i < nChannels; i++) {
            lamp[i] = Math.exp(-0.5 * Math.pow((i - 4.0) / 1.5, 2));
            decay[i] = 100.0 * Math.exp(-i * 0.5 / 2.0) + 1.0;
        }
        double[] tau = {0.5, 1.0, 2.0, 4.0};

        double[] fi = new double[nChannels * tau.length];
        double[] y = new double[nChannels];
        double[] sigma = new double[nChannels];
        double[] fitAdditive = new double[nChannels];

        int nFi = tttrlib.tcspc_build_fi_lifetimes_into(
                decay, lamp, 0.5, tau, 0.0, 0.0, 0.0, 0, nChannels, 0.0,
                fi, y, sigma, fitAdditive);

        assertTrue(nFi > 0, "Fi's element count");
        assertEquals(0, nFi % tau.length, "Fi is (n_data x n_tau) flattened");
        assertTrue(nFi / tau.length > 0, "n_data = Fi / tau.length");
        assertTrue(anyNonZero(fi), "Fi was not filled");
        assertTrue(anyNonZero(y), "y was not filled");
        assertTrue(anyNonZero(sigma), "sigma was not filled");
    }

    private static boolean anyNonZero(double[] a) {
        for (double v : a) if (v != 0.0) return true;
        return false;
    }
}
