// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import ij.CompositeImage;
import ij.IJ;
import ij.ImagePlus;
import ij.ImageStack;
import ij.Prefs;
import ij.gui.GenericDialog;
import ij.gui.Plot;
import ij.io.OpenDialog;
import ij.plugin.PlugIn;
import ij.process.FloatProcessor;
import ij.process.ShortProcessor;

import io.github.fluorescencetools.tttrlib.CLSMImage;
import io.github.fluorescencetools.tttrlib.CLSMSettings;
import io.github.fluorescencetools.tttrlib.TTTR;
import io.github.fluorescencetools.tttrlib.VectorInt32;

import java.awt.Color;
import java.util.ArrayList;
import java.util.List;

/**
 * ImageJ/Fiji plugin that opens time-tagged time-resolved confocal (CLSM) files
 * (PicoQuant PTU/HT3, Becker &amp; Hickl SPC, Leica SP5/SP8) with tttrlib and
 * reconstructs composite (multi-colour) stacks for each routing-channel group and
 * micro-time window:
 * <ul>
 *   <li><b>Intensity</b> (photon counts),</li>
 *   <li><b>FastLifetime</b> (mean photon arrival time, optional IRF correction),</li>
 *   <li><b>Phasor</b> (g and s coordinates),</li>
 *   <li><b>Decay</b> (aggregate micro-time histogram, plotted and exportable).</li>
 * </ul>
 *
 * <p><b>Channel groups</b> use the syntax {@code 1,3;2,4}: channels 1 and 3 are
 * combined into one colour, 2 and 4 into another.</p>
 *
 * <p><b>PIE / micro-time ranges</b> use the syntax {@code 0,111;200,499;900,1200}:
 * each {@code start,stop} pair gates photons by micro-time (prompt vs delayed),
 * producing a separate image window per (group, range). Leave empty for no gating.</p>
 *
 * Menu: Plugins &gt; tttrlib &gt; Open TTTR CLSM Image.
 */
public class TTTR_CLSM_Reader implements PlugIn {

    static { io.github.fluorescencetools.tttrlib.NativeLoader.load(); }

    private static final Color[] SERIES = {
        Color.RED, Color.GREEN, Color.BLUE, Color.MAGENTA, Color.CYAN, Color.ORANGE
    };

    @Override
    public void run(String arg) {
        OpenDialog od = new OpenDialog("Open a TTTR CLSM file (PTU / HT3 / SPC)");
        if (od.getFileName() == null) return;
        String path = od.getDirectory() + od.getFileName();

        GenericDialog gd = new GenericDialog("tttrlib CLSM options");
        gd.addStringField("Channel groups (e.g. 1,3;2,4)", Prefs.get("tttrlib.groups", "0"), 14);
        gd.addStringField("Micro-time ranges / PIE (e.g. 0,111;200,499)", Prefs.get("tttrlib.mtranges", ""), 20);
        gd.addCheckbox("Intensity", Prefs.get("tttrlib.intensity", true));
        gd.addCheckbox("FastLifetime (mean micro time)", Prefs.get("tttrlib.fastlifetime", true));
        gd.addCheckbox("Phasor (g, s)", Prefs.get("tttrlib.phasor", false));
        gd.addCheckbox("Number & Brightness (N&B)", Prefs.get("tttrlib.nb", false));
        gd.addCheckbox("Decay (micro-time histogram)", Prefs.get("tttrlib.decay", false));
        gd.addNumericField("Min. photons / pixel", (int) Prefs.get("tttrlib.minphotons", 2), 0);
        gd.addCheckbox("Auto-correct IRF offset (decay rise)", Prefs.get("tttrlib.irf", true));
        gd.addCheckbox("Stack frames (collapse to one frame)", Prefs.get("tttrlib.stack", false));
        gd.showDialog();
        if (gd.wasCanceled()) return;
        String groupSpec = gd.getNextString();
        String rangeSpec = gd.getNextString();
        boolean doIntensity = gd.getNextBoolean();
        boolean doLifetime = gd.getNextBoolean();
        boolean doPhasor = gd.getNextBoolean();
        boolean doNB = gd.getNextBoolean();
        boolean doDecay = gd.getNextBoolean();
        int minPhotons = (int) gd.getNextNumber();
        boolean correctIrf = gd.getNextBoolean();
        boolean stackFrames = gd.getNextBoolean();
        Prefs.set("tttrlib.groups", groupSpec);
        Prefs.set("tttrlib.mtranges", rangeSpec);
        Prefs.set("tttrlib.intensity", doIntensity);
        Prefs.set("tttrlib.fastlifetime", doLifetime);
        Prefs.set("tttrlib.phasor", doPhasor);
        Prefs.set("tttrlib.nb", doNB);
        Prefs.set("tttrlib.decay", doDecay);
        Prefs.set("tttrlib.minphotons", minPhotons);
        Prefs.set("tttrlib.irf", correctIrf);
        Prefs.set("tttrlib.stack", stackFrames);
        // N&B needs the per-frame fluctuations, so it always uses the unstacked stack.
        if (doNB && stackFrames) IJ.log("tttrlib: N&B uses per-frame data; ignoring 'Stack frames' for N&B maps.");

        List<int[]> groups = parseGroups(groupSpec);
        if (groups.isEmpty()) { IJ.error("tttrlib", "No channel groups (use e.g. 1,3;2,4)."); return; }
        List<int[]> ranges = parseGroups(rangeSpec);   // each int[] = {start, stop}; empty list = no gating

        try {
            long t0 = System.nanoTime();
            TTTR tttr = new TTTR(path);
            int nG = groups.size();
            int nR = ranges.isEmpty() ? 1 : ranges.size();   // 1 pseudo-window = "all"
            int nWin = nG * nR;

            int nFrames = -1, nLines = -1, nPixel = -1;
            boolean gatherIntensity = doIntensity || doNB;      // N&B needs the frame stack
            int[][] intensity = gatherIntensity ? new int[nWin][] : null;
            double[][] lifetime = doLifetime ? new double[nWin][] : null;
            float[][] phasor = doPhasor ? new float[nWin][] : null;
            String[] labels = new String[nWin];

            for (int g = 0; g < nG; g++) {
                VectorInt32 channels = new VectorInt32();
                for (int c : groups.get(g)) channels.add(c);
                // fill=false: read geometry from markers, defer photon assignment to the window fills
                CLSMImage image = new CLSMImage(tttr, new CLSMSettings(), null, /*fill=*/false, channels);
                if (nFrames < 0) {
                    nFrames = image.getN_frames(); nLines = image.getN_lines(); nPixel = image.getN_pixel();
                    if (nFrames <= 0 || nLines <= 0 || nPixel <= 0) {
                        IJ.error("tttrlib", "No CLSM image could be reconstructed (no scan markers?).");
                        return;
                    }
                }
                int nEl = nFrames * nLines * nPixel;
                for (int r = 0; r < nR; r++) {
                    int w = g * nR + r;
                    int[] rg = ranges.isEmpty() ? null : ranges.get(r);
                    int mtStart = rg != null && rg.length > 0 ? rg[0] : 0;
                    int mtStop = rg != null && rg.length > 1 ? rg[1] : 0;
                    image.fill_micro_time_range(channels, mtStart, mtStop);
                    // one composite channel per (routing group x micro-time window) combination
                    labels[w] = "g" + (g + 1) + groupStr(groups.get(g))
                              + (rg != null ? " w" + (r + 1) + "[" + mtStart + "-" + mtStop + "]" : "");
                    if (gatherIntensity) { intensity[w] = new int[nEl]; image.get_intensity_into(intensity[w]); }
                    if (doLifetime)  { lifetime[w] = new double[nEl]; image.get_mean_micro_time_into(lifetime[w], -1.0, minPhotons, correctIrf, stackFrames); }
                    if (doPhasor)    { phasor[w] = new float[nEl * 2]; image.get_phasor_into(phasor[w], -1.0, minPhotons, correctIrf, stackFrames); }
                }
            }

            int perFrame = nLines * nPixel;
            int outFrames = stackFrames ? 1 : nFrames;
            String base = od.getFileName();
            // Tag every image with its source file so "Decay from Mask" can reload the photons.
            if (doIntensity) {
                int[][] shown = intensity;
                if (stackFrames) {                              // collapse frames -> one summed frame
                    shown = new int[nWin][];
                    for (int w = 0; w < nWin; w++) shown[w] = stackSum(intensity[w], nFrames, perFrame);
                }
                present(showIntensity(base + " Intensity", shown, labels, outFrames, nLines, nPixel), path);
            }
            if (doLifetime)  present(showFloat(base + " FastLifetime", lifetime, labels, outFrames, nLines, nPixel), path);
            if (doPhasor) {
                present(showPhasor(base + " Phasor g", phasor, 0, labels, outFrames, nLines, nPixel), path);
                present(showPhasor(base + " Phasor s", phasor, 1, labels, outFrames, nLines, nPixel), path);
            }
            if (doNB) {
                if (nFrames < 2) {
                    IJ.log("tttrlib: N&B needs >= 2 frames (have " + nFrames + "); skipping.");
                } else {
                    double[][] nbB = new double[nWin][], nbN = new double[nWin][], nbE = new double[nWin][];
                    for (int w = 0; w < nWin; w++) {
                        double[][] m = nbMaps(intensity[w], nFrames, perFrame);   // {B, N, epsilon}
                        nbB[w] = m[0]; nbN[w] = m[1]; nbE[w] = m[2];
                    }
                    present(showFloat(base + " N&B brightness (B)", nbB, labels, 1, nLines, nPixel), path);
                    present(showFloat(base + " N&B number (N)", nbN, labels, 1, nLines, nPixel), path);
                    present(showFloat(base + " N&B epsilon (B-1)", nbE, labels, 1, nLines, nPixel), path);
                }
            }
            if (doDecay) showDecay(base + " Decay", tttr, groups).show();

            double ms = (System.nanoTime() - t0) / 1e6;
            IJ.showStatus(String.format("tttrlib: %d group(s) x %d window(s), %d frames %dx%d in %.0f ms",
                    nG, nR, nFrames, nPixel, nLines, ms));
        } catch (Throwable t) {
            IJ.handleException(t);
        }
    }

    /** Show an image, tagging it with the source TTTR file path for downstream tools. */
    private static void present(ImagePlus imp, String path) {
        imp.setProperty("tttr.path", path);
        imp.show();
    }

    /** Sum a frame-major intensity stack into a single frame. */
    private static int[] stackSum(int[] stack, int nFrames, int per) {
        int[] out = new int[per];
        for (int f = 0; f < nFrames; f++) {
            int off = f * per;
            for (int i = 0; i < per; i++) out[i] += stack[off + i];
        }
        return out;
    }

    // Number & Brightness maps from a frame-major intensity stack, following the
    // chisurf convention (nb_maps): mean = <k>, variance = population var (ddof=0),
    // B = var/mean (apparent brightness), N = mean^2/var (apparent number),
    // epsilon = B - 1 (true molecular brightness). Undefined pixels -> 0.
    // Returns {B, N, epsilon}, each a per-pixel single-frame map.
    private static double[][] nbMaps(int[] stack, int nFrames, int per) {
        double[] mean = new double[per], var = new double[per];
        for (int i = 0; i < per; i++) {
            double s = 0;
            for (int f = 0; f < nFrames; f++) s += stack[f * per + i];
            mean[i] = s / nFrames;
        }
        for (int i = 0; i < per; i++) {
            double s = 0;
            for (int f = 0; f < nFrames; f++) { double d = stack[f * per + i] - mean[i]; s += d * d; }
            var[i] = s / nFrames;
        }
        double[] B = new double[per], N = new double[per], E = new double[per];
        for (int i = 0; i < per; i++) {
            B[i] = mean[i] > 0.0 ? var[i] / mean[i] : 0.0;
            N[i] = var[i] > 0.0 ? mean[i] * mean[i] / var[i] : 0.0;
            E[i] = mean[i] > 0.0 ? B[i] - 1.0 : 0.0;
        }
        return new double[][] { B, N, E };
    }

    /** Render a channel group as "(1,3)" for slice labels. */
    static String groupStr(int[] ch) {
        StringBuilder b = new StringBuilder("(");
        for (int i = 0; i < ch.length; i++) { if (i > 0) b.append(','); b.append(ch[i]); }
        return b.append(')').toString();
    }

    /** Parse "1,3;2,4" (or "0,111;200,499") into [[1,3],[2,4]] / [[0,111],[200,499]]. */
    static List<int[]> parseGroups(String spec) {
        List<int[]> groups = new ArrayList<>();
        if (spec == null) return groups;
        for (String part : spec.trim().split(";")) {
            if (part.trim().isEmpty()) continue;
            List<Integer> ch = new ArrayList<>();
            for (String s : part.split(",")) {
                s = s.trim();
                if (!s.isEmpty()) try { ch.add(Integer.parseInt(s)); } catch (NumberFormatException ignore) {}
            }
            if (!ch.isEmpty()) {
                int[] a = new int[ch.size()];
                for (int i = 0; i < a.length; i++) a[i] = ch.get(i);
                groups.add(a);
            }
        }
        return groups;
    }

    // ImageJ hyperstack slice order is channel-fastest, then z (frame): (z,c).
    private static ImagePlus showIntensity(String title, int[][] g, String[] labels, int nf, int nl, int np) {
        ImageStack st = new ImageStack(np, nl);
        int per = nl * np;
        for (int f = 0; f < nf; f++)
            for (int c = 0; c < g.length; c++) {
                short[] slice = new short[per];
                int off = f * per;
                for (int i = 0; i < per; i++) slice[i] = (short) g[c][off + i];
                st.addSlice(labels[c] + " f" + (f + 1), new ShortProcessor(np, nl, slice, null));
            }
        return composite(title, st, g.length, nf);
    }

    private static ImagePlus showFloat(String title, double[][] g, String[] labels, int nf, int nl, int np) {
        ImageStack st = new ImageStack(np, nl);
        int per = nl * np;
        for (int f = 0; f < nf; f++)
            for (int c = 0; c < g.length; c++) {
                float[] slice = new float[per];
                int off = f * per;
                for (int i = 0; i < per; i++) slice[i] = (float) g[c][off + i];
                st.addSlice(labels[c] + " f" + (f + 1), new FloatProcessor(np, nl, slice, null));
            }
        return composite(title, st, g.length, nf);
    }

    // phasor arrays are interleaved [g,s]; comp selects 0=g or 1=s.
    private static ImagePlus showPhasor(String title, float[][] g, int comp, String[] labels, int nf, int nl, int np) {
        ImageStack st = new ImageStack(np, nl);
        int per = nl * np;
        for (int f = 0; f < nf; f++)
            for (int c = 0; c < g.length; c++) {
                float[] slice = new float[per];
                int off = f * per;
                for (int i = 0; i < per; i++) slice[i] = g[c][(off + i) * 2 + comp];
                st.addSlice(labels[c] + " f" + (f + 1), new FloatProcessor(np, nl, slice, null));
            }
        return composite(title, st, g.length, nf);
    }

    // Aggregate micro-time decay histogram per channel group -> a Plot window.
    // The Plot's "Data >>" / "Save..." menu exports the curves as text.
    private static Plot showDecay(String title, TTTR tttr, List<int[]> groups) {
        double resNs = tttr.get_micro_time_resolution_s() * 1e9;
        final int cap = 65536;   // unsigned-short micro-time range (coarsening = 1)
        Plot plot = new Plot(title, "micro time (ns)", "counts");
        double ymax = 1.0; int nmax = 1;
        for (int g = 0; g < groups.size(); g++) {
            VectorInt32 channels = new VectorInt32();
            for (int c : groups.get(g)) channels.add(c);
            double[] hist = new double[cap];
            int nh = tttr.get_microtime_histogram_into(hist, channels, 1);
            int n = Math.min(nh, cap);
            if (n > nmax) nmax = n;
            double[] x = new double[n], y = new double[n];
            for (int i = 0; i < n; i++) { x[i] = i * resNs; y[i] = hist[i]; if (hist[i] > ymax) ymax = hist[i]; }
            plot.setColor(SERIES[g % SERIES.length]);
            plot.addPoints(x, y, Plot.LINE);
            plot.addLabel(0.02, 0.05 + 0.06 * g, "ch" + (g + 1));
        }
        plot.setColor(Color.BLACK);
        plot.setLimits(0, nmax * resNs, 0, ymax * 1.05);
        return plot;
    }

    private static ImagePlus composite(String title, ImageStack st, int nChannels, int nFrames) {
        ImagePlus imp = new ImagePlus(title, st);
        imp.setDimensions(nChannels, nFrames, 1);
        if (nChannels > 1) {
            return new CompositeImage(imp, CompositeImage.COMPOSITE);
        }
        imp.setOpenAsHyperStack(true);
        return imp;
    }
}
