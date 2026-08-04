// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej.core;

import io.github.fluorescencetools.tttrlib.CLSMImage;
import io.github.fluorescencetools.tttrlib.CLSMSettings;
import io.github.fluorescencetools.tttrlib.NativeLoader;
import io.github.fluorescencetools.tttrlib.TTTR;
import io.github.fluorescencetools.tttrlib.VectorInt32;

import java.util.ArrayList;
import java.util.List;

/**
 * Headless CLSM reconstruction: TTTR file in, plain Java arrays out.
 *
 * <p>Deliberately free of {@code ij.*} and {@code net.imagej.*} so it can be
 * unit-tested without a SciJava context and scripted from Groovy/Jython. The
 * SciJava {@code Command} layer is a thin shell over this class.</p>
 */
public final class ClsmReconstructor {

    static { NativeLoader.load(); }

    private ClsmReconstructor() { }

    /**
     * One output channel: a routing-channel set with its own micro-time gate.
     *
     * <p>Used when the channels come from a detector setup, where each
     * {@code <window>_<detector>} entry carries its own range — that is not the
     * cross product of {@link Params#groups} and {@link Params#microTimeRanges},
     * because two detectors in the same window can be gated differently.</p>
     */
    public static final class Selection {
        public String label;
        public int[] channels;
        /** {start, stop}, or {@code null} for no micro-time gate. */
        public int[] range;

        public Selection(String label, int[] channels, int[] range) {
            this.label = label;
            this.channels = channels;
            this.range = range;
        }
    }

    /** Inputs for a reconstruction. */
    public static final class Params {
        /** Path to the TTTR file (PTU / HT3 / SPC). */
        public String path;
        /** Routing-channel groups; each group becomes one colour channel. */
        public List<int[]> groups = new ArrayList<>();
        /** Micro-time gates as {start, stop} pairs (PIE). Empty = no gating. */
        public List<int[]> microTimeRanges = new ArrayList<>();
        /**
         * Explicit per-channel selections. When non-empty these replace
         * {@link #groups} / {@link #microTimeRanges} entirely, and each entry
         * becomes one output channel.
         */
        public List<Selection> selections = new ArrayList<>();
        public boolean intensity = true;
        /** Mean photon arrival time ("FastLifetime"). */
        public boolean lifetime = true;
        /**
         * IRF-corrected mean lifetime (first moment of the decay), as opposed to
         * the raw mean arrival time above.
         */
        public boolean meanLifetime = false;
        public boolean phasor = false;
        public boolean numberAndBrightness = false;
        public boolean decay = false;
        public int minPhotons = 2;
        public boolean correctIrfOffset = true;
        /** Collapse frames into one. Ignored for N&amp;B, which needs the fluctuations. */
        public boolean stackFrames = false;
    }

    /**
     * Reconstruction output. Image arrays are indexed {@code [window]} where
     * {@code window = group * nWindows + range}, and each array is frame-major:
     * {@code value(f, l, p) = arr[(f * nLines + l) * nPixel + p]}.
     */
    public static final class Result {
        public int nFrames, nLines, nPixel;
        /** Number of routing-channel groups. */
        public int nGroups;
        /** Number of micro-time windows; 1 when no PIE gating was requested. */
        public int nWindows;
        /** True when the caller supplied explicit micro-time ranges. */
        public boolean gated;
        /** Per-window slice labels, length {@code nGroups * nWindows}. */
        public String[] labels;
        /** Frames actually present in the image arrays (1 when stacked). */
        public int nOutputFrames;

        public int[][] intensity;
        public double[][] lifetime;
        /** IRF-corrected mean lifetime per pixel, same layout as {@link #lifetime}. */
        public double[][] meanLifetime;
        /** Interleaved {@code [..., g, s, ...]}, so twice the pixel count. */
        public float[][] phasor;
        public double[][] nbBrightness, nbNumber, nbEpsilon;
        /** Aggregate micro-time histogram per group, {@code [group][bin]}. */
        public double[][] decay;
        public double microTimeResolutionNs;
    }

    /** Thrown when a file yields no reconstructable image. */
    public static class NoImageException extends RuntimeException {
        public NoImageException(String m) { super(m); }
    }

    /**
     * Parse {@code "1,3;2,4"} into {@code [[1,3],[2,4]]}. Also used for
     * micro-time ranges, where each element is a {@code {start, stop}} pair.
     */
    public static List<int[]> parseGroups(String spec) {
        List<int[]> groups = new ArrayList<>();
        if (spec == null) return groups;
        for (String part : spec.trim().split(";")) {
            if (part.trim().isEmpty()) continue;
            List<Integer> ch = new ArrayList<>();
            for (String s : part.split(",")) {
                s = s.trim();
                if (!s.isEmpty()) {
                    try { ch.add(Integer.parseInt(s)); } catch (NumberFormatException ignore) { }
                }
            }
            if (!ch.isEmpty()) {
                int[] a = new int[ch.size()];
                for (int i = 0; i < a.length; i++) a[i] = ch.get(i);
                groups.add(a);
            }
        }
        return groups;
    }

    /** Render a channel group as {@code "(1,3)"} for slice labels. */
    public static String groupLabel(int[] ch) {
        StringBuilder b = new StringBuilder("(");
        for (int i = 0; i < ch.length; i++) {
            if (i > 0) b.append(',');
            b.append(ch[i]);
        }
        return b.append(')').toString();
    }

    public static Result reconstruct(Params p) {
        // Explicit selections take over: one output channel each, and the gate
        // travels with the channel set rather than being cross-producted.
        final boolean explicit = !p.selections.isEmpty();
        if (!explicit && p.groups.isEmpty()) {
            throw new IllegalArgumentException("No channel groups (use e.g. 1,3;2,4).");
        }
        TTTR tttr = new TTTR(p.path);
        Result r = new Result();
        r.nGroups = explicit ? p.selections.size() : p.groups.size();
        r.gated = false;                      // gates live inside each selection
        r.nWindows = 1;
        if (!explicit) {
            r.gated = !p.microTimeRanges.isEmpty();
            r.nWindows = r.gated ? p.microTimeRanges.size() : 1;
        }
        r.microTimeResolutionNs = tttr.get_micro_time_resolution_s() * 1e9;

        final int nWin = r.nGroups * r.nWindows;
        r.labels = new String[nWin];

        // N&B needs the per-frame fluctuations, so it always gathers the frame stack.
        boolean gatherIntensity = p.intensity || p.numberAndBrightness;
        int[][] intensity = gatherIntensity ? new int[nWin][] : null;
        double[][] lifetime = p.lifetime ? new double[nWin][] : null;
        double[][] meanLifetime = p.meanLifetime ? new double[nWin][] : null;
        float[][] phasor = p.phasor ? new float[nWin][] : null;

        r.nFrames = -1;
        for (int g = 0; g < r.nGroups; g++) {
            VectorInt32 channels = new VectorInt32();
            for (int c : (explicit ? p.selections.get(g).channels : p.groups.get(g))) {
                channels.add(c);
            }
            // fill=false: read geometry from markers, defer photon assignment to
            // the per-window fills below.
            CLSMImage image = new CLSMImage(tttr, new CLSMSettings(), null, false, channels);
            if (r.nFrames < 0) {
                r.nFrames = image.getN_frames();
                r.nLines = image.getN_lines();
                r.nPixel = image.getN_pixel();
                if (r.nFrames <= 0 || r.nLines <= 0 || r.nPixel <= 0) {
                    throw new NoImageException(
                            "No CLSM image could be reconstructed (no scan markers?).");
                }
            }
            final int nEl = r.nFrames * r.nLines * r.nPixel;
            for (int w = 0; w < r.nWindows; w++) {
                int idx = g * r.nWindows + w;
                int[] range = explicit
                        ? p.selections.get(g).range
                        : (r.gated ? p.microTimeRanges.get(w) : null);
                int mtStart = range != null && range.length > 0 ? range[0] : 0;
                int mtStop = range != null && range.length > 1 ? range[1] : 0;
                image.fill_micro_time_range(channels, mtStart, mtStop);
                if (explicit) {
                    r.labels[idx] = p.selections.get(g).label;
                } else {
                    r.labels[idx] = "g" + (g + 1) + groupLabel(p.groups.get(g))
                            + (range != null ? " w" + (w + 1) + "[" + mtStart + "-" + mtStop + "]" : "");
                }
                if (gatherIntensity) {
                    intensity[idx] = new int[nEl];
                    image.get_intensity_into(intensity[idx]);
                }
                if (p.lifetime) {
                    lifetime[idx] = new double[nEl];
                    image.get_mean_micro_time_into(lifetime[idx], -1.0, p.minPhotons,
                            p.correctIrfOffset, p.stackFrames);
                }
                if (p.meanLifetime) {
                    meanLifetime[idx] = new double[nEl];
                    image.get_mean_lifetime_into(meanLifetime[idx], p.minPhotons, p.stackFrames);
                }
                if (p.phasor) {
                    phasor[idx] = new float[nEl * 2];
                    image.get_phasor_into(phasor[idx], -1.0, p.minPhotons,
                            p.correctIrfOffset, p.stackFrames);
                }
            }
        }

        final int perFrame = r.nLines * r.nPixel;
        r.nOutputFrames = p.stackFrames ? 1 : r.nFrames;

        if (p.intensity) {
            if (p.stackFrames) {
                r.intensity = new int[nWin][];
                for (int i = 0; i < nWin; i++) {
                    r.intensity[i] = stackSum(intensity[i], r.nFrames, perFrame);
                }
            } else {
                r.intensity = intensity;
            }
        }
        // get_mean_micro_time/get_mean_lifetime/get_phasor already honour
        // stack_frames internally.
        r.lifetime = lifetime;
        r.meanLifetime = meanLifetime;
        r.phasor = phasor;

        if (p.numberAndBrightness && r.nFrames >= 2) {
            r.nbBrightness = new double[nWin][];
            r.nbNumber = new double[nWin][];
            r.nbEpsilon = new double[nWin][];
            for (int i = 0; i < nWin; i++) {
                double[][] m = nbMaps(intensity[i], r.nFrames, perFrame);
                r.nbBrightness[i] = m[0];
                r.nbNumber[i] = m[1];
                r.nbEpsilon[i] = m[2];
            }
        }

        if (p.decay) {
            r.decay = new double[r.nGroups][];
            // Exact bin count from the header - the previous hard-coded 65536 cap
            // silently truncated files with more micro-time channels.
            int nBins = (int) tttr.get_number_of_micro_time_channels();
            for (int g = 0; g < r.nGroups; g++) {
                VectorInt32 channels = new VectorInt32();
                for (int c : (explicit ? p.selections.get(g).channels : p.groups.get(g))) {
                    channels.add(c);
                }
                double[] hist = new double[Math.max(nBins, 1)];
                int n = tttr.get_microtime_histogram_into(hist, channels, 1);
                r.decay[g] = n < hist.length ? java.util.Arrays.copyOf(hist, Math.max(n, 0)) : hist;
            }
        }
        return r;
    }

    /** Sum a frame-major stack into a single frame. */
    public static int[] stackSum(int[] stack, int nFrames, int perFrame) {
        int[] out = new int[perFrame];
        for (int f = 0; f < nFrames; f++) {
            int off = f * perFrame;
            for (int i = 0; i < perFrame; i++) out[i] += stack[off + i];
        }
        return out;
    }

    /**
     * Number &amp; Brightness maps from a frame-major intensity stack, following the
     * chisurf {@code nb_maps} convention: mean = &lt;k&gt;, variance = population
     * variance (ddof=0), B = var/mean, N = mean²/var, epsilon = B − 1. Undefined
     * pixels are 0. Returns {@code {B, N, epsilon}}.
     */
    public static double[][] nbMaps(int[] stack, int nFrames, int perFrame) {
        double[] mean = new double[perFrame];
        double[] var = new double[perFrame];
        for (int i = 0; i < perFrame; i++) {
            double s = 0;
            for (int f = 0; f < nFrames; f++) s += stack[f * perFrame + i];
            mean[i] = s / nFrames;
        }
        for (int i = 0; i < perFrame; i++) {
            double s = 0;
            for (int f = 0; f < nFrames; f++) {
                double d = stack[f * perFrame + i] - mean[i];
                s += d * d;
            }
            var[i] = s / nFrames;
        }
        double[] b = new double[perFrame];
        double[] n = new double[perFrame];
        double[] e = new double[perFrame];
        for (int i = 0; i < perFrame; i++) {
            b[i] = mean[i] > 0.0 ? var[i] / mean[i] : 0.0;
            n[i] = var[i] > 0.0 ? mean[i] * mean[i] / var[i] : 0.0;
            e[i] = mean[i] > 0.0 ? b[i] - 1.0 : 0.0;
        }
        return new double[][] { b, n, e };
    }
}
