// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import io.github.fluorescencetools.tttrlib.imagej.core.ClsmReconstructor;
import io.github.fluorescencetools.tttrlib.imagej.core.PerPixelFit;

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
 * Fits a multi-exponential decay to every (optionally binned) pixel and returns
 * lifetime, amplitude and photon-count maps.
 *
 * <p>Confocal FLIM pixels hold far too few photons to fit individually, so the
 * defaults stack frames, coarsen the micro-time axis and bin 4&times;4 pixels.
 * Loosen those only when the data can support it.</p>
 *
 * <p>With no IRF file the fit uses a delta response, i.e. no deconvolution:
 * lifetimes are then biased by the width of the real instrument response. Supply
 * an IRF when the absolute value matters.</p>
 */
@Plugin(type = Command.class, menuPath = "Plugins>tttrlib>FLIM>Fit Decay per Pixel...",
        headless = true)
public class FitDecayPerPixel implements Command {

    @Parameter
    private DatasetService datasetService;

    @Parameter
    private LogService log;

    @Parameter(required = false)
    private Dataset dataset;

    @Parameter(label = "TTTR file (blank = use the active image's source)",
               required = false, persist = false)
    private File file;

    @Parameter(label = "IRF file (blank = no deconvolution)",
               required = false, persist = false)
    private File irfFile;

    @Parameter(label = "Channels (blank = all)", required = false, persist = true)
    private String channels = "0";

    @Parameter(label = "Exponential components", min = "1", max = "3", persist = true)
    private int nExponentials = 1;

    @Parameter(label = "Spatial binning (n x n pixels)", min = "1", persist = true)
    private int binning = 4;

    @Parameter(label = "Micro-time coarsening", min = "1", persist = true)
    private int microTimeCoarsening = 16;

    @Parameter(label = "Min. photons / binned pixel", min = "1", persist = true)
    private int minPhotons = 100;

    @Parameter(label = "Stack frames (recommended)", persist = true)
    private boolean stackFrames = true;

    @Parameter(type = ItemIO.OUTPUT, label = "Lifetimes")
    private Dataset lifetimes;

    @Parameter(type = ItemIO.OUTPUT, label = "Amplitudes")
    private Dataset amplitudes;

    @Parameter(type = ItemIO.OUTPUT, label = "Photons per fit")
    private Dataset photons;

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

        final PerPixelFit.Params p = new PerPixelFit.Params();
        p.path = path;
        p.channels = parseChannels(channels);
        p.nExponentials = nExponentials;
        p.binning = binning;
        p.microTimeCoarsening = microTimeCoarsening;
        p.minPhotons = minPhotons;
        p.stackFrames = stackFrames;
        p.irf = irfFile == null ? null : loadIrf(irfFile.getAbsolutePath(), p);

        final PerPixelFit.Result r;
        try {
            r = PerPixelFit.fit(p);
        } catch (ClsmReconstructor.NoImageException | IllegalStateException e) {
            log.error("tttrlib: " + e.getMessage());
            return;
        }

        final String base = new File(path).getName();
        lifetimes = stack(base + " Lifetimes", r, r.lifetimes);
        amplitudes = stack(base + " Amplitudes", r, r.amplitudes);
        photons = plane(base + " Photons", r, r.photons);
        for (Dataset d : new Dataset[] { lifetimes, amplitudes, photons }) {
            if (d != null) d.getProperties().put("tttr.path", path);
        }

        log.info("tttrlib: per-pixel fit " + r.nPixel + "x" + r.nLines
                + " (binning " + binning + "), " + r.fitted + " fitted, "
                + r.skipped + " below " + minPhotons + " photons, dt="
                + String.format("%.4f", r.dtNanoseconds) + " ns");
    }

    /** One plane per exponential component. */
    private Dataset stack(String name, PerPixelFit.Result r, double[][] maps) {
        final int nExp = maps.length;
        final long[] dims = nExp > 1
                ? new long[] { r.nPixel, r.nLines, nExp }
                : new long[] { r.nPixel, r.nLines };
        final AxisType[] axes = nExp > 1
                ? new AxisType[] { Axes.X, Axes.Y, Axes.CHANNEL }
                : new AxisType[] { Axes.X, Axes.Y };
        final Dataset d = datasetService.create(dims, name, axes, 32, true, true);
        final RandomAccess<RealType<?>> ra = d.randomAccess();
        final long[] pos = new long[dims.length];
        for (int k = 0; k < nExp; k++) {
            if (nExp > 1) pos[2] = k;
            for (int y = 0; y < r.nLines; y++) {
                pos[1] = y;
                for (int x = 0; x < r.nPixel; x++) {
                    pos[0] = x;
                    ra.setPosition(pos);
                    ra.get().setReal(maps[k][y * r.nPixel + x]);
                }
            }
        }
        return d;
    }

    private Dataset plane(String name, PerPixelFit.Result r, double[] map) {
        final Dataset d = datasetService.create(new long[] { r.nPixel, r.nLines }, name,
                new AxisType[] { Axes.X, Axes.Y }, 32, true, true);
        final RandomAccess<RealType<?>> ra = d.randomAccess();
        for (int y = 0; y < r.nLines; y++) {
            for (int x = 0; x < r.nPixel; x++) {
                ra.setPosition(new long[] { x, y });
                ra.get().setReal(map[y * r.nPixel + x]);
            }
        }
        return d;
    }

    /** Aggregate micro-time histogram of the IRF measurement, matched to the data axis. */
    private double[] loadIrf(String irfPath, PerPixelFit.Params p) {
        try {
            final io.github.fluorescencetools.tttrlib.TTTR irf =
                    new io.github.fluorescencetools.tttrlib.TTTR(irfPath);
            final int bins = (int) irf.get_number_of_micro_time_channels();
            final double[] hist = new double[Math.max(bins, 1)];
            final int n = irf.get_microtime_histogram_into(hist,
                    new io.github.fluorescencetools.tttrlib.VectorInt32(),
                    Math.max(1, p.microTimeCoarsening));
            return n < hist.length ? java.util.Arrays.copyOf(hist, Math.max(n, 0)) : hist;
        } catch (Throwable t) {
            log.warn("tttrlib: could not read IRF (" + t + "); fitting without deconvolution.");
            return null;
        }
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
