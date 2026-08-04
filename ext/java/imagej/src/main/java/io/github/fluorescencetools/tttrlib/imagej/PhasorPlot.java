// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import io.github.fluorescencetools.tttrlib.imagej.core.ClsmReconstructor;

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
import org.scijava.table.DefaultGenericTable;
import org.scijava.table.DoubleColumn;
import org.scijava.table.GenericTable;

import java.io.File;

/**
 * Phasor plot: the 2-D histogram of every pixel's phasor coordinates (g, s).
 *
 * <p>Single-exponential lifetimes lie on the universal semicircle
 * (centre 0.5, radius 0.5); mixtures fall inside it. The semicircle is emitted
 * as a table rather than burned into the image, so it can be overlaid without
 * corrupting the counts.</p>
 *
 * <p>The histogram axes are calibrated in phasor units, so Fiji's cursor readout
 * gives (g, s) directly.</p>
 */
@Plugin(type = Command.class, menuPath = "Plugins>tttrlib>FLIM>Phasor Plot",
        headless = true)
public class PhasorPlot implements Command {

    static final AxisType G_AXIS = Axes.get("g");
    static final AxisType S_AXIS = Axes.get("s");

    @Parameter
    private DatasetService datasetService;

    @Parameter
    private LogService log;

    @Parameter(required = false)
    private Dataset dataset;

    @Parameter(label = "TTTR file (blank = use the active image's source)",
               required = false, persist = false)
    private File file;

    @Parameter(label = "Channel groups (e.g. 1,3;2,4)", persist = true)
    private String channelGroups = "0";

    @Parameter(label = "Micro-time ranges / PIE (e.g. 0,111;200,499)",
               required = false, persist = true)
    private String microTimeRanges = "";

    @Parameter(label = "Histogram bins per axis", min = "16", persist = true)
    private int bins = 256;

    @Parameter(label = "Min. photons / pixel", min = "1", persist = true)
    private int minPhotons = 10;

    @Parameter(label = "Auto-correct IRF offset (decay rise)", persist = true)
    private boolean correctIrfOffset = true;

    @Parameter(type = ItemIO.OUTPUT, label = "Phasor plot")
    private Dataset phasorHistogram;

    @Parameter(type = ItemIO.OUTPUT, label = "Universal semicircle")
    private GenericTable universalSemicircle;

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

        final ClsmReconstructor.Params p = new ClsmReconstructor.Params();
        p.path = path;
        p.groups = ClsmReconstructor.parseGroups(channelGroups);
        p.microTimeRanges = ClsmReconstructor.parseGroups(microTimeRanges);
        p.intensity = false;
        p.lifetime = false;
        p.phasor = true;
        p.minPhotons = minPhotons;
        p.correctIrfOffset = correctIrfOffset;
        p.stackFrames = false;

        if (p.groups.isEmpty()) {
            log.error("tttrlib: no channel groups (use e.g. 1,3;2,4).");
            return;
        }

        final ClsmReconstructor.Result r;
        try {
            r = ClsmReconstructor.reconstruct(p);
        } catch (ClsmReconstructor.NoImageException e) {
            log.error("tttrlib: " + e.getMessage());
            return;
        }
        if (r.phasor == null) {
            log.error("tttrlib: no phasor data.");
            return;
        }

        // One histogram plane per (group x window), so PIE windows stay separable.
        final int nWin = r.nGroups * r.nWindows;
        final long[] dims = nWin > 1
                ? new long[] { bins, bins, nWin }
                : new long[] { bins, bins };
        final AxisType[] axes = nWin > 1
                ? new AxisType[] { G_AXIS, S_AXIS, Axes.CHANNEL }
                : new AxisType[] { G_AXIS, S_AXIS };

        final Dataset d = datasetService.create(dims, new File(path).getName() + " Phasor",
                axes, 32, false, false);
        // Phasor space spans g in [0,1] and s in [0,0.5]; calibrate so the cursor
        // readout is in phasor units rather than bin indices.
        calibrate(d, 0, 1.0 / bins);
        calibrate(d, 1, 0.5 / bins);

        final RandomAccess<RealType<?>> ra = d.randomAccess();
        final long[] pos = new long[dims.length];
        long counted = 0;

        for (int w = 0; w < nWin; w++) {
            final float[] gs = r.phasor[w];
            if (gs == null) continue;
            if (nWin > 1) pos[2] = w;
            final int nPix = gs.length / 2;
            for (int i = 0; i < nPix; i++) {
                final float g = gs[i * 2];
                final float s = gs[i * 2 + 1];
                // Undefined pixels are flagged with -1 by get_phasor.
                if (g < 0 || s < 0 || g > 1.0f || s > 0.5f) continue;
                int gb = (int) (g * bins);
                int sb = (int) (s / 0.5 * bins);
                if (gb >= bins) gb = bins - 1;
                if (sb >= bins) sb = bins - 1;
                pos[0] = gb;
                pos[1] = sb;
                ra.setPosition(pos);
                ra.get().setReal(ra.get().getRealDouble() + 1);
                counted++;
            }
        }

        d.getProperties().put("tttr.path", path);
        phasorHistogram = d;
        universalSemicircle = semicircle();
        log.info("tttrlib: phasor plot, " + counted + " pixel(s) in "
                + bins + "x" + bins + " bins over " + nWin + " window(s)");
    }

    /** Set an axis scale when the axis is linear (which is what create() makes). */
    private static void calibrate(final Dataset d, final int axis, final double scale) {
        final net.imagej.axis.CalibratedAxis a = d.axis(axis);
        if (a instanceof net.imagej.axis.LinearAxis) {
            ((net.imagej.axis.LinearAxis) a).setScale(scale);
        }
    }

    /** Universal circle g = 0.5 + 0.5 cos(theta), s = 0.5 sin(theta). */
    private static GenericTable semicircle() {
        final DefaultGenericTable t = new DefaultGenericTable();
        final DoubleColumn g = new DoubleColumn("g");
        final DoubleColumn s = new DoubleColumn("s");
        final int n = 181;
        for (int i = 0; i < n; i++) {
            final double th = Math.PI * i / (n - 1);
            g.add(0.5 + 0.5 * Math.cos(th));
            s.add(0.5 * Math.sin(th));
        }
        t.add(g);
        t.add(s);
        return t;
    }
}
