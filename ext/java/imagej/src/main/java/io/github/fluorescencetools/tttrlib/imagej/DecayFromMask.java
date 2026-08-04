// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import io.github.fluorescencetools.tttrlib.imagej.core.ClsmReconstructor;
import io.github.fluorescencetools.tttrlib.imagej.core.DecayExtractor;

import ij.ImagePlus;
import ij.gui.Roi;

import net.imagej.Dataset;

import org.scijava.ItemIO;
import org.scijava.command.Command;
import org.scijava.log.LogService;
import org.scijava.plugin.Parameter;
import org.scijava.plugin.Plugin;
import org.scijava.table.DefaultGenericTable;
import org.scijava.table.DoubleColumn;
import org.scijava.table.GenericTable;

import java.io.File;

/**
 * Extracts one micro-time decay per routing channel over the current ROI (or the
 * whole field of view) of a reconstructed CLSM image.
 *
 * <p>The source photons are reloaded from the TTTR file, located via the
 * {@code tttr.path} property that {@link OpenClsmImage} attaches to its outputs,
 * or from an explicitly supplied file.</p>
 *
 * <p>The menu label is kept verbatim from the previous IJ1 plugin so existing
 * macros calling {@code run("Decay from Mask", ...)} keep working.</p>
 */
@Plugin(type = Command.class, menuPath = "Plugins>tttrlib>FLIM>Decay from Mask",
        headless = true)
public class DecayFromMask implements Command {

    @Parameter
    private LogService log;

    /** Active dataset; used for its {@code tttr.path} property. */
    @Parameter(required = false)
    private Dataset dataset;

    /** Active IJ1 image; used for the ROI and as a second source of the path. */
    @Parameter(required = false)
    private ImagePlus imp;

    @Parameter(label = "TTTR file (blank = use the active image's source)",
               required = false, persist = false)
    private File file;

    @Parameter(label = "Channels (blank = auto)", required = false, persist = true)
    private String channels = "";

    @Parameter(label = "Micro-time coarsening", min = "1", persist = true)
    private int coarsening = 1;

    @Parameter(type = ItemIO.OUTPUT, label = "Decay from Mask")
    private GenericTable decay;

    @Override
    public void run() {
        final String path = resolvePath();
        if (path == null) {
            log.error("tttrlib: no TTTR source file. Open an image with "
                    + "'Open TTTR CLSM Image' first, or choose a file.");
            return;
        }

        int[] ch = parseChannels(channels);
        final Roi roi = imp != null ? imp.getRoi() : null;

        final DecayExtractor.Result r;
        try {
            r = DecayExtractor.extract(path, ch, coarsening,
                    roi == null ? null : roi::contains);
        } catch (ClsmReconstructor.NoImageException | IllegalStateException e) {
            log.error("tttrlib: " + e.getMessage());
            return;
        }

        if (imp != null && (imp.getWidth() != r.nPixel || imp.getHeight() != r.nLines)) {
            log.warn("tttrlib: active image " + imp.getWidth() + "x" + imp.getHeight()
                    + " differs from reconstruction " + r.nPixel + "x" + r.nLines
                    + "; ROI may not align.");
        }

        final DefaultGenericTable t = new DefaultGenericTable();
        final DoubleColumn time = new DoubleColumn("time_ns");
        for (int i = 0; i < r.nBins; i++) time.add(i * r.binWidthNs);
        t.add(time);
        for (int ci = 0; ci < r.channels.length; ci++) {
            final DoubleColumn c = new DoubleColumn("ch" + r.channels[ci]);
            for (int i = 0; i < r.nBins; i++) c.add((double) r.decays[ci][i]);
            t.add(c);
        }
        decay = t;

        log.info("tttrlib: decay from " + (roi != null ? "ROI" : "FOV") + ", "
                + r.channels.length + " channel column(s), " + r.nBins + " bins");
    }

    private String resolvePath() {
        if (file != null) return file.getAbsolutePath();
        if (dataset != null) {
            Object v = dataset.getProperties().get("tttr.path");
            if (v instanceof String) return (String) v;
        }
        if (imp != null && imp.getProperty("tttr.path") instanceof String) {
            return (String) imp.getProperty("tttr.path");
        }
        return null;
    }

    private static int[] parseChannels(String spec) {
        if (spec == null || spec.trim().isEmpty()) return null;
        String[] parts = spec.trim().split("[,;\\s]+");
        int[] out = new int[parts.length];
        int n = 0;
        for (String s : parts) {
            s = s.trim();
            if (s.isEmpty()) continue;
            try { out[n++] = Integer.parseInt(s); } catch (NumberFormatException ignore) { }
        }
        return n == out.length ? out : java.util.Arrays.copyOf(out, n);
    }
}
