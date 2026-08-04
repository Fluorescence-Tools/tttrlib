// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej.core;

import io.github.fluorescencetools.tttrlib.CLSMImage;
import io.github.fluorescencetools.tttrlib.CLSMSettings;
import io.github.fluorescencetools.tttrlib.NativeLoader;
import io.github.fluorescencetools.tttrlib.TTTR;
import io.github.fluorescencetools.tttrlib.VectorInt32;

/**
 * Headless correlation analyses over a reconstructed CLSM image: per-pixel FCS
 * straight from the photon stream, and image correlation spectroscopy (ICS).
 *
 * <p>No {@code ij.*} / {@code net.imagej.*} dependencies, so it is unit-testable
 * and scriptable independently of the ImageJ commands.</p>
 */
public final class CorrelationAnalysis {

    static { NativeLoader.load(); }

    private CorrelationAnalysis() { }

    /** Correlation algorithms understood by the tttrlib correlator. */
    public static final String[] METHODS = { "wahl", "felekyan", "laurence" };

    public static final class FcsResult {
        public int nFrames, nLines, nPixel, nTau;
        /**
         * Correlation curves, lag axis innermost:
         * {@code value(f, y, x, tau) = curves[((f*nLines + y)*nPixel + x)*nTau + tau]}
         */
        public float[] curves;
    }

    public static final class IcsResult {
        public int nFrames, nLines, nPixel;
        /** Frame-major, same layout as the intensity image. */
        public double[] values;
    }

    private static CLSMImage image(String path, int[] channels) {
        TTTR tttr = new TTTR(path);
        VectorInt32 ch = new VectorInt32();
        if (channels != null) for (int c : channels) ch.add(c);
        CLSMImage img = new CLSMImage(tttr, new CLSMSettings(), null, true, ch);
        if (img.getN_frames() <= 0 || img.getN_lines() <= 0 || img.getN_pixel() <= 0) {
            throw new ClsmReconstructor.NoImageException(
                    "No CLSM image could be reconstructed (no scan markers?).");
        }
        return img;
    }

    /**
     * Per-pixel correlation curves.
     *
     * @param stackFrames collapse frames before correlating; strongly recommended,
     *                    since the un-stacked result is {@code frames} times larger
     * @param minPhotons  pixels with fewer photons are left at zero
     */
    public static FcsResult fcsImage(String path, int[] channels, String method,
                                     int nBins, int nCasc, boolean stackFrames,
                                     boolean normalized, int minPhotons) {
        final CLSMImage img = image(path, channels);
        final FcsResult r = new FcsResult();
        r.nFrames = stackFrames ? 1 : img.getN_frames();
        r.nLines = img.getN_lines();
        r.nPixel = img.getN_pixel();

        // The curve length is not known until the correlator has been configured,
        // so probe with a zero-length array: the helper still returns the true
        // element count, then allocate exactly.
        final int total = img.get_fcs_image_into(new float[0], method, nBins, nCasc,
                stackFrames, normalized, minPhotons);
        final long perPixel = (long) r.nFrames * r.nLines * r.nPixel;
        if (perPixel <= 0 || total <= 0) {
            r.nTau = 0;
            r.curves = new float[0];
            return r;
        }
        r.nTau = (int) (total / perPixel);
        r.curves = new float[total];
        img.get_fcs_image_into(r.curves, method, nBins, nCasc,
                stackFrames, normalized, minPhotons);
        return r;
    }

    /**
     * Image correlation spectroscopy.
     *
     * @param subtractAverage {@code "stack"}, {@code "frame"} or {@code ""} for none
     */
    public static IcsResult ics(String path, int[] channels, String subtractAverage) {
        final CLSMImage img = image(path, channels);
        final IcsResult r = new IcsResult();
        r.nFrames = img.getN_frames();
        r.nLines = img.getN_lines();
        r.nPixel = img.getN_pixel();
        r.values = new double[r.nFrames * r.nLines * r.nPixel];
        img.compute_ics_into(r.values, subtractAverage == null ? "" : subtractAverage);
        return r;
    }
}
