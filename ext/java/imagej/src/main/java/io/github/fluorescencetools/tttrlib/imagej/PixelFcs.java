// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import io.github.fluorescencetools.tttrlib.imagej.core.ClsmReconstructor;
import io.github.fluorescencetools.tttrlib.imagej.core.CorrelationAnalysis;

import net.imagej.Dataset;
import net.imagej.DatasetService;
import net.imagej.axis.Axes;
import net.imagej.axis.AxisType;

import net.imglib2.RandomAccess;
import net.imglib2.type.numeric.RealType;

import org.scijava.ItemIO;
import org.scijava.command.Command;
import org.scijava.log.LogService;
import org.scijava.plugin.Parameter;
import org.scijava.plugin.Plugin;

import java.io.File;

/**
 * Per-pixel fluorescence correlation spectroscopy, computed directly from the
 * photon stream rather than from binned intensities.
 *
 * <p>The result is a stack whose third axis is the correlation lag, so an
 * intensity profile through it is that pixel's correlation curve.</p>
 */
@Plugin(type = Command.class, menuPath = "Plugins>tttrlib>Correlation>Pixel-wise FCS",
        headless = true)
public class PixelFcs implements Command {

    /** Lag axis of the correlation curves. */
    static final AxisType TAU_AXIS = Axes.get("Tau");

    @Parameter
    private DatasetService datasetService;

    @Parameter
    private LogService log;

    @Parameter(required = false)
    private Dataset dataset;

    @Parameter(label = "TTTR file (blank = use the active image's source)",
               required = false, persist = false)
    private File file;

    @Parameter(label = "Channels (blank = all)", required = false, persist = true)
    private String channels = "0";

    @Parameter(label = "Correlation method", choices = { "wahl", "felekyan", "laurence" },
               persist = true)
    private String method = "wahl";

    @Parameter(label = "Bins per cascade", min = "1", persist = true)
    private int nBins = 10;

    @Parameter(label = "Cascades", min = "1", persist = true)
    private int nCasc = 1;

    @Parameter(label = "Min. photons / pixel", min = "1", persist = true)
    private int minPhotons = 5;

    @Parameter(label = "Normalized correlation", persist = true)
    private boolean normalized = false;

    @Parameter(label = "Stack frames (recommended)", persist = true)
    private boolean stackFrames = true;

    @Parameter(type = ItemIO.OUTPUT, label = "Pixel-wise FCS")
    private Dataset fcs;

    @Override
    public void run() {
        String path = null;
        if (file != null) {
            path = file.getAbsolutePath();
        } else if (dataset != null) {
            Object v = dataset.getProperties().get("tttr.path");
            if (v instanceof String) path = (String) v;
        }
        if (path == null) {
            log.error("tttrlib: no TTTR file selected and no active image with a source path.");
            return;
        }

        final CorrelationAnalysis.FcsResult r;
        try {
            r = CorrelationAnalysis.fcsImage(path, parseChannels(channels), method,
                    nBins, nCasc, stackFrames, normalized, minPhotons);
        } catch (ClsmReconstructor.NoImageException e) {
            log.error("tttrlib: " + e.getMessage());
            return;
        }
        if (r.nTau <= 0) {
            log.error("tttrlib: correlation produced no curves "
                    + "(no pixel reached " + minPhotons + " photons?).");
            return;
        }

        final boolean framed = r.nFrames > 1;
        final long[] dims = framed
                ? new long[] { r.nPixel, r.nLines, r.nTau, r.nFrames }
                : new long[] { r.nPixel, r.nLines, r.nTau };
        final AxisType[] axes = framed
                ? new AxisType[] { Axes.X, Axes.Y, TAU_AXIS, Axes.TIME }
                : new AxisType[] { Axes.X, Axes.Y, TAU_AXIS };

        final Dataset d = datasetService.create(dims, new File(path).getName() + " FCS",
                axes, 32, true, true);
        final RandomAccess<RealType<?>> ra = d.randomAccess();
        final long[] pos = new long[dims.length];

        for (int f = 0; f < r.nFrames; f++) {
            if (framed) pos[3] = f;
            for (int y = 0; y < r.nLines; y++) {
                pos[1] = y;
                for (int x = 0; x < r.nPixel; x++) {
                    pos[0] = x;
                    final int base = (((f * r.nLines) + y) * r.nPixel + x) * r.nTau;
                    for (int tau = 0; tau < r.nTau; tau++) {
                        pos[2] = tau;
                        ra.setPosition(pos);
                        ra.get().setReal(r.curves[base + tau]);
                    }
                }
            }
        }
        d.getProperties().put("tttr.path", path);
        fcs = d;
        log.info("tttrlib: pixel-wise FCS " + r.nPixel + "x" + r.nLines
                + ", " + r.nTau + " lag(s), method=" + method);
    }

    private static int[] parseChannels(String spec) {
        if (spec == null || spec.trim().isEmpty()) return null;
        final String[] parts = spec.trim().split("[,;\\s]+");
        final int[] out = new int[parts.length];
        int n = 0;
        for (String s : parts) {
            s = s.trim();
            if (s.isEmpty()) continue;
            try { out[n++] = Integer.parseInt(s); } catch (NumberFormatException ignore) { }
        }
        return n == out.length ? out : java.util.Arrays.copyOf(out, n);
    }
}
