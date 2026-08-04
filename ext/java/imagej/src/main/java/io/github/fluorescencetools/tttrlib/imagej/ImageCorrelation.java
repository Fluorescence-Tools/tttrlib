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
 * Image correlation spectroscopy (ICS): the spatial autocorrelation of each
 * frame, from which beam waist and particle density are usually read off.
 *
 * <p>Subtracting an average removes the static structure that would otherwise
 * dominate the correlation — {@code stack} uses the mean image over all frames,
 * {@code frame} the mean of each frame separately.</p>
 */
@Plugin(type = Command.class,
        menuPath = "Plugins>tttrlib>Correlation>Image Correlation (ICS)",
        headless = true)
public class ImageCorrelation implements Command {

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

    /**
     * Which average to remove before correlating.
     *
     * <p>"none" rather than an empty string: an empty choice makes SciJava's
     * Swing dropdown throw while the dialog is being built, so the command could
     * not be opened from the menu at all. It is mapped back to {@code ""} for
     * the C++ call in {@link #run()}.</p>
     */
    @Parameter(label = "Subtract average", choices = { "stack", "frame", "none" },
               required = false, persist = true)
    private String subtractAverage = "stack";

    @Parameter(type = ItemIO.OUTPUT, label = "ICS")
    private Dataset ics;

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

        // "none" is the UI label for "subtract nothing"; the C++ side wants "".
        final String subtract =
                (subtractAverage == null || "none".equals(subtractAverage))
                        ? "" : subtractAverage;

        final CorrelationAnalysis.IcsResult r;
        try {
            r = CorrelationAnalysis.ics(path, parseChannels(channels), subtract);
        } catch (ClsmReconstructor.NoImageException e) {
            log.error("tttrlib: " + e.getMessage());
            return;
        }

        final boolean framed = r.nFrames > 1;
        final long[] dims = framed
                ? new long[] { r.nPixel, r.nLines, r.nFrames }
                : new long[] { r.nPixel, r.nLines };
        final AxisType[] axes = framed
                ? new AxisType[] { Axes.X, Axes.Y, Axes.TIME }
                : new AxisType[] { Axes.X, Axes.Y };

        final Dataset d = datasetService.create(dims, new File(path).getName() + " ICS",
                axes, 32, true, true);
        final RandomAccess<RealType<?>> ra = d.randomAccess();
        final long[] pos = new long[dims.length];
        final int perFrame = r.nLines * r.nPixel;

        for (int f = 0; f < r.nFrames; f++) {
            if (framed) pos[2] = f;
            for (int y = 0; y < r.nLines; y++) {
                pos[1] = y;
                final int row = f * perFrame + y * r.nPixel;
                for (int x = 0; x < r.nPixel; x++) {
                    pos[0] = x;
                    ra.setPosition(pos);
                    ra.get().setReal(r.values[row + x]);
                }
            }
        }
        d.getProperties().put("tttr.path", path);
        ics = d;
        log.info("tttrlib: ICS " + r.nFrames + " frame(s) " + r.nPixel + "x" + r.nLines
                + ", subtract_average='" + subtractAverage + "'");
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
