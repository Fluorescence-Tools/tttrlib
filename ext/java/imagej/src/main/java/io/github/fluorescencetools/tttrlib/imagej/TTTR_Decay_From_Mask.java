// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import ij.IJ;
import ij.ImagePlus;
import ij.WindowManager;
import ij.gui.GenericDialog;
import ij.gui.Plot;
import ij.gui.Roi;
import ij.io.OpenDialog;
import ij.measure.ResultsTable;
import ij.plugin.PlugIn;

import io.github.fluorescencetools.tttrlib.CLSMImage;
import io.github.fluorescencetools.tttrlib.CLSMSettings;
import io.github.fluorescencetools.tttrlib.TTTR;
import io.github.fluorescencetools.tttrlib.VectorInt32;

import java.awt.Color;
import java.util.Arrays;

/**
 * ImageJ/Fiji plugin: compute the fluorescence decay (micro-time histogram) of a
 * masked region of a CLSM image, one column per routing channel.
 *
 * <p>Workflow: open a file with <i>Open TTTR CLSM Image</i>, draw a selection
 * (ROI) on the resulting image, then run this command. The photons inside the
 * selected pixels are histogrammed per routing channel and written to a Results
 * table (column 0 = routing channel 0, column 1 = channel 1, …) plus a time
 * axis; the table exports to CSV via <b>File ▸ Save As</b>. The curves are also
 * shown as a Plot.</p>
 *
 * <p>The source file is taken from the active image's {@code tttr.path} property
 * (set by the reader); if absent, a file chooser opens. With no ROI the whole
 * field of view is used.</p>
 *
 * Menu: Plugins &gt; tttrlib &gt; Decay from Mask.
 */
public class TTTR_Decay_From_Mask implements PlugIn {

    static { io.github.fluorescencetools.tttrlib.NativeLoader.load(); }

    private static final Color[] SERIES = {
        Color.RED, Color.GREEN, Color.BLUE, Color.MAGENTA, Color.CYAN, Color.ORANGE
    };

    @Override
    public void run(String arg) {
        ImagePlus imp = WindowManager.getCurrentImage();
        Roi roi = imp != null ? imp.getRoi() : null;

        // Source file: reuse the reader's tag, else ask.
        String path = imp != null && imp.getProperty("tttr.path") instanceof String
                ? (String) imp.getProperty("tttr.path") : null;
        if (path == null) {
            OpenDialog od = new OpenDialog("Open the TTTR file for this image");
            if (od.getFileName() == null) return;
            path = od.getDirectory() + od.getFileName();
        }

        GenericDialog gd = new GenericDialog("Decay from Mask");
        gd.addStringField("Channels (columns, blank = auto)", "", 16);
        gd.addNumericField("Micro-time coarsening", 1, 0);
        gd.addCheckbox("Plot curves", true);
        if (roi == null) gd.addMessage("No selection on the active image — using the whole field of view.");
        gd.showDialog();
        if (gd.wasCanceled()) return;
        String chSpec = gd.getNextString().trim();
        int coarsening = Math.max(1, (int) gd.getNextNumber());
        boolean plot = gd.getNextBoolean();

        try {
            TTTR tttr = new TTTR(path);

            // Channels -> columns (explicit list, or auto-detected + sorted).
            int[] channels;
            if (!chSpec.isEmpty()) {
                String[] p = chSpec.split("[,;\\s]+");
                channels = new int[p.length];
                for (int i = 0; i < p.length; i++) channels[i] = Integer.parseInt(p[i].trim());
            } else {
                int[] buf = new int[256];
                int nc = tttr.get_used_routing_channels_into(buf);
                channels = Arrays.copyOf(buf, Math.min(nc, buf.length));
                Arrays.sort(channels);
            }
            if (channels.length == 0) { IJ.error("tttrlib", "No routing channels found."); return; }

            // One reconstruction with all channels; get_decay_of_pixels returns the
            // per-channel decays vstacked as [channel][bin] in a single pass.
            VectorInt32 allCh = new VectorInt32();
            for (int c : channels) allCh.add(c);
            CLSMImage img = new CLSMImage(tttr, new CLSMSettings(), null, /*fill=*/true, allCh);
            int nFrames = img.getN_frames(), nLines = img.getN_lines(), nPixel = img.getN_pixel();
            if (nFrames <= 0 || nLines <= 0 || nPixel <= 0) {
                IJ.error("tttrlib", "No CLSM image could be reconstructed (no scan markers?)."); return;
            }
            if (imp != null && (imp.getWidth() != nPixel || imp.getHeight() != nLines)) {
                IJ.log("tttrlib: warning — active image " + imp.getWidth() + "x" + imp.getHeight()
                        + " differs from reconstruction " + nPixel + "x" + nLines + "; ROI may not align.");
            }
            byte[] mask = buildMask(roi, nFrames, nLines, nPixel);

            VectorInt32 flat = img.get_decay_of_pixels_masked(mask, nFrames, nLines, nPixel, coarsening, allCh);
            int nBins = (int) flat.size() / channels.length;   // vstacked [channel][bin]
            int[][] decays = new int[channels.length][];
            for (int ci = 0; ci < channels.length; ci++) {
                int[] col = new int[nBins];
                for (int b = 0; b < nBins; b++) col[b] = flat.get(ci * nBins + b);
                decays[ci] = col;
            }

            double resNs = tttr.get_micro_time_resolution_s() * 1e9 * coarsening;
            ResultsTable rt = new ResultsTable();
            for (int i = 0; i < nBins; i++) {
                rt.incrementCounter();
                rt.addValue("time_ns", i * resNs);
                for (int ci = 0; ci < channels.length; ci++)
                    rt.addValue("ch" + channels[ci], decays[ci][i]);
            }
            rt.show("Decay from Mask" + (roi != null ? " (ROI)" : " (FOV)"));

            if (plot) {
                Plot p = new Plot("Decay from Mask", "micro time (ns)", "counts");
                double ymax = 1;
                for (int ci = 0; ci < channels.length; ci++) {
                    double[] x = new double[nBins], y = new double[nBins];
                    for (int i = 0; i < nBins; i++) { x[i] = i * resNs; y[i] = decays[ci][i]; if (y[i] > ymax) ymax = y[i]; }
                    p.setColor(SERIES[ci % SERIES.length]);
                    p.addPoints(x, y, Plot.LINE);
                    p.addLabel(0.02, 0.05 + 0.06 * ci, "ch" + channels[ci]);
                }
                p.setColor(Color.BLACK);
                p.setLimits(0, nBins * resNs, 0, ymax * 1.05);
                p.show();
            }
            IJ.showStatus("tttrlib: decay from mask, " + channels.length + " channel column(s), " + nBins + " bins");
        } catch (Throwable t) {
            IJ.handleException(t);
        }
    }

    // Frame-major uint8 mask (1 = selected) matching (nFrames, nLines, nPixel);
    // the 2D ROI (or whole FOV when roi == null) is replicated across all frames.
    private static byte[] buildMask(Roi roi, int nFrames, int nLines, int nPixel) {
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
}
