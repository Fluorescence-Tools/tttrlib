// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej.core;

import io.github.fluorescencetools.tttrlib.CLSMImage;
import io.github.fluorescencetools.tttrlib.CLSMSettings;
import io.github.fluorescencetools.tttrlib.NativeLoader;
import io.github.fluorescencetools.tttrlib.TTTR;
import io.github.fluorescencetools.tttrlib.VectorInt32;

import java.util.Arrays;

/**
 * Headless micro-time decay extraction over a pixel mask.
 *
 * <p>Free of {@code ij.*} / {@code net.imagej.*}: the caller supplies a plain
 * frame-major {@code byte[]} mask, so the ROI can come from an IJ1 {@code Roi},
 * an ImgLib2 mask, or a script.</p>
 */
public final class DecayExtractor {

    static { NativeLoader.load(); }

    private DecayExtractor() { }

    public static final class Result {
        /** Routing channels, ascending; one decay column per entry. */
        public int[] channels;
        /** Decays as {@code [channel][bin]}. */
        public int[][] decays;
        public int nBins;
        /** Bin width in nanoseconds, coarsening already applied. */
        public double binWidthNs;
        public int nFrames, nLines, nPixel;
    }

    /**
     * Distinct routing channels present in the file, ascending.
     *
     * <p>Two-pass: {@code get_used_routing_channels_into} returns the true count
     * regardless of the buffer it was handed, so a zero-length probe gives an
     * exact size. The previous fixed {@code int[256]} buffer silently dropped
     * channels beyond 256.</p>
     */
    public static int[] usedRoutingChannels(TTTR tttr) {
        int n = tttr.get_used_routing_channels_into(new int[0]);
        if (n <= 0) return new int[0];
        int[] buf = new int[n];
        int written = tttr.get_used_routing_channels_into(buf);
        int[] out = Arrays.copyOf(buf, Math.min(written, buf.length));
        Arrays.sort(out);
        return out;
    }

    /**
     * Build a frame-major {@code uint8} mask (1 = selected) of size
     * {@code nFrames * nLines * nPixel} from a 2-D predicate replicated over
     * every frame. Pass {@code null} to select the whole field of view.
     */
    public static byte[] buildMask(PixelPredicate roi, int nFrames, int nLines, int nPixel) {
        byte[] mask = new byte[nFrames * nLines * nPixel];
        int per = nLines * nPixel;
        for (int y = 0; y < nLines; y++) {
            for (int x = 0; x < nPixel; x++) {
                if (roi == null || roi.contains(x, y)) {
                    for (int f = 0; f < nFrames; f++) mask[f * per + y * nPixel + x] = 1;
                }
            }
        }
        return mask;
    }

    /** 2-D pixel selection test, so the core needs no {@code ij.gui.Roi}. */
    public interface PixelPredicate {
        boolean contains(int x, int y);
    }

    /**
     * Reconstruct {@code path} and extract one micro-time decay per routing
     * channel over the masked pixels.
     *
     * @param channels explicit channel list, or {@code null}/empty to auto-detect
     * @param roi      pixel selection, or {@code null} for the whole field of view
     */
    public static Result extract(String path, int[] channels, int coarsening, PixelPredicate roi) {
        TTTR tttr = new TTTR(path);
        int[] ch = (channels == null || channels.length == 0)
                ? usedRoutingChannels(tttr) : channels.clone();
        if (ch.length == 0) throw new IllegalStateException("No routing channels found.");

        VectorInt32 all = new VectorInt32();
        for (int c : ch) all.add(c);

        // One reconstruction with all channels; get_decay_of_pixels returns the
        // per-channel decays vstacked as [channel][bin] in a single pass.
        CLSMImage img = new CLSMImage(tttr, new CLSMSettings(), null, true, all);
        Result r = new Result();
        r.channels = ch;
        r.nFrames = img.getN_frames();
        r.nLines = img.getN_lines();
        r.nPixel = img.getN_pixel();
        if (r.nFrames <= 0 || r.nLines <= 0 || r.nPixel <= 0) {
            throw new ClsmReconstructor.NoImageException(
                    "No CLSM image could be reconstructed (no scan markers?).");
        }

        byte[] mask = buildMask(roi, r.nFrames, r.nLines, r.nPixel);
        VectorInt32 flat = img.get_decay_of_pixels_masked(
                mask, r.nFrames, r.nLines, r.nPixel, Math.max(1, coarsening), all);

        r.nBins = (int) flat.size() / ch.length;
        r.decays = new int[ch.length][];
        for (int ci = 0; ci < ch.length; ci++) {
            int[] col = new int[r.nBins];
            for (int b = 0; b < r.nBins; b++) col[b] = flat.get(ci * r.nBins + b);
            r.decays[ci] = col;
        }
        r.binWidthNs = tttr.get_micro_time_resolution_s() * 1e9 * Math.max(1, coarsening);
        return r;
    }
}
