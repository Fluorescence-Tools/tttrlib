// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import net.imagej.Dataset;
import net.imagej.DatasetService;
import net.imagej.axis.Axes;

import net.imglib2.type.numeric.RealType;

import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import org.scijava.Context;
import org.scijava.command.CommandModule;
import org.scijava.command.CommandService;
import org.scijava.log.LogService;

import java.io.File;
import java.util.HashMap;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** M2 (FLIM depth) and M3 (correlation) commands, run headlessly. */
class FlimAndCorrelationTest {

    private static Context context;

    private static File clsmFile() {
        String r = System.getenv("TTTRLIB_DATA");
        return new File(r == null || r.isEmpty() ? "tttr-data" : r,
                        "imaging/pq/ht3/pq_ht3_clsm.ht3");
    }

    @BeforeAll
    static void setUp() {
        context = new Context(CommandService.class, DatasetService.class, LogService.class);
    }

    @AfterAll
    static void tearDown() {
        if (context != null) context.dispose();
    }

    private static CommandModule run(Class<?> cmd, Map<String, Object> params) throws Exception {
        @SuppressWarnings("unchecked")
        final Class<? extends org.scijava.command.Command> c =
                (Class<? extends org.scijava.command.Command>) cmd;
        return context.service(CommandService.class).run(c, true, params).get();
    }

    /**
     * The IRF-corrected lifetime map. Values are pinned against Python's
     * CLSMImage.get_mean_lifetime for the same file.
     */
    @Test
    void lifetimeMapMatchesPython() throws Exception {
        final Map<String, Object> p = new HashMap<>();
        p.put("file", clsmFile());
        p.put("channelGroups", "0");
        p.put("microTimeRanges", "");
        p.put("minPhotons", 3);
        p.put("stackFrames", false);

        final Dataset d = (Dataset) run(LifetimeMap.class, p).getOutput("lifetimeImage");
        assertNotNull(d, "lifetime Dataset");
        assertEquals(256, d.dimension(0));
        assertEquals(256, d.dimension(1));
        assertEquals(40, d.dimension(3), "frames");

        int nonZero = 0;
        double sum = 0;
        for (RealType<?> t : d) {
            final double v = t.getRealDouble();
            if (v > 0) nonZero++;
            sum += v;
        }
        assertEquals(303299, nonZero, "pixels with a defined lifetime");
        assertEquals(1139037.627429, sum, 1e-3, "lifetime sum");
    }

    /**
     * Per-pixel FCS. Every one of these numbers used to be unreachable:
     * get_fcs_image segfaulted on a null clsm_other, then returned all zeros
     * because its default correlation method is not one the correlator knows,
     * then aborted on a dangling pixel-selection pointer.
     */
    @Test
    void pixelFcsProducesCurves() throws Exception {
        final Map<String, Object> p = new HashMap<>();
        p.put("file", clsmFile());
        p.put("channels", "0");
        p.put("method", "wahl");
        p.put("nBins", 10);
        p.put("nCasc", 1);
        p.put("minPhotons", 5);
        p.put("normalized", false);
        p.put("stackFrames", true);

        final Dataset d = (Dataset) run(PixelFcs.class, p).getOutput("fcs");
        assertNotNull(d, "FCS Dataset");
        assertEquals(3, d.numDimensions(), "X, Y, Tau (frames stacked)");
        assertEquals(PixelFcs.TAU_AXIS, d.axis(2).type());
        assertEquals(256, d.dimension(0));
        assertEquals(256, d.dimension(1));
        assertEquals(11, d.dimension(2), "lag channels");

        int nonZero = 0;
        double max = 0;
        for (RealType<?> t : d) {
            final double v = t.getRealDouble();
            if (v != 0) nonZero++;
            if (v > max) max = v;
        }
        assertEquals(57388, nonZero, "non-zero correlation values");
        assertEquals(19.0, max, 1e-6, "peak correlation");
    }

    /** Image correlation spectroscopy. */
    @Test
    void imageCorrelationRuns() throws Exception {
        final Map<String, Object> p = new HashMap<>();
        p.put("file", clsmFile());
        p.put("channels", "0");
        p.put("subtractAverage", "");

        final Dataset d = (Dataset) run(ImageCorrelation.class, p).getOutput("ics");
        assertNotNull(d, "ICS Dataset");
        assertEquals(256, d.dimension(0));
        assertEquals(256, d.dimension(1));
        assertEquals(40, d.dimension(2), "frames");
        assertEquals(Axes.TIME, d.axis(2).type());

        double sum = 0;
        for (RealType<?> t : d) sum += t.getRealDouble();
        assertTrue(sum != 0, "ICS produced an all-zero image");
    }

    /** Phasor plot: a 2-D histogram of per-pixel (g, s), plus the universal circle. */
    @Test
    void phasorPlotBuildsHistogram() throws Exception {
        final Map<String, Object> p = new HashMap<>();
        p.put("file", clsmFile());
        p.put("channelGroups", "0");
        p.put("microTimeRanges", "");
        p.put("bins", 128);
        p.put("minPhotons", 10);
        p.put("correctIrfOffset", true);

        final CommandModule m = run(PhasorPlot.class, p);
        final Dataset d = (Dataset) m.getOutput("phasorHistogram");
        assertNotNull(d, "phasor histogram");
        assertEquals(2, d.numDimensions(), "g, s (single group and window)");
        assertEquals(PhasorPlot.G_AXIS, d.axis(0).type());
        assertEquals(PhasorPlot.S_AXIS, d.axis(1).type());
        assertEquals(128, d.dimension(0));
        assertEquals(128, d.dimension(1));

        double total = 0;
        for (RealType<?> t : d) total += t.getRealDouble();
        assertTrue(total > 0, "phasor histogram is empty");

        final org.scijava.table.GenericTable circle =
                (org.scijava.table.GenericTable) m.getOutput("universalSemicircle");
        assertNotNull(circle, "universal semicircle");
        assertEquals(181, circle.getRowCount(), "semicircle samples");
        // Endpoints of the universal circle: (1,0) and (0,0).
        assertEquals(1.0, (Double) circle.get(0, 0), 1e-9);
        assertEquals(0.0, (Double) circle.get(1, 0), 1e-9);
        assertEquals(0.0, (Double) circle.get(0, 180), 1e-9);
        assertEquals(0.0, (Double) circle.get(1, 180), 1e-9);
    }

    /**
     * Per-pixel multi-exponential fitting. Confocal pixels hold ~tens of photons,
     * so the fit only makes sense on binned, coarsened, frame-stacked data; the
     * assertions check that the machinery runs and yields physically plausible
     * lifetimes rather than pinning exact values.
     */
    @Test
    void fitDecayPerPixelProducesPlausibleLifetimes() throws Exception {
        final Map<String, Object> p = new HashMap<>();
        p.put("file", clsmFile());
        p.put("channels", "0");
        p.put("nExponentials", 1);
        p.put("binning", 16);
        p.put("microTimeCoarsening", 32);
        p.put("minPhotons", 50);
        p.put("stackFrames", true);

        final CommandModule m = run(FitDecayPerPixel.class, p);
        final Dataset tau = (Dataset) m.getOutput("lifetimes");
        final Dataset ph = (Dataset) m.getOutput("photons");
        assertNotNull(tau, "lifetime map");
        assertNotNull(ph, "photon map");

        // 256 pixels binned 16x16 -> a 16x16 grid.
        assertEquals(16, tau.dimension(0));
        assertEquals(16, tau.dimension(1));

        int fitted = 0;
        double maxTau = 0;
        for (RealType<?> t : tau) {
            final double v = t.getRealDouble();
            if (v > 0) { fitted++; maxTau = Math.max(maxTau, v); }
        }
        assertTrue(fitted > 0, "no pixel was fitted");
        // The TAC window is ~26 ns here, so a recovered lifetime beyond it would
        // mean the fit ran away rather than converged.
        assertTrue(maxTau < 100.0, "implausible lifetime " + maxTau + " ns");

        double photonSum = 0;
        for (RealType<?> t : ph) photonSum += t.getRealDouble();
        assertTrue(photonSum > 0, "no photons entered any fit");
    }
}
