// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import net.imagej.Dataset;
import net.imagej.DatasetService;
import net.imagej.axis.Axes;

import net.imglib2.Cursor;
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
import static org.junit.jupiter.api.Assumptions.assumeTrue;

/**
 * Headless {@link OpenClsmImage} test.
 *
 * <p>Pins the same canonical values as the Python reference test and
 * {@code test/java/CLSMTest.java} (the cross-language reference), and
 * additionally asserts the axis wiring that the Dataset migration introduced.</p>
 */
class OpenClsmImageTest {

    private static final int REF_FRAMES = 40, REF_LINES = 256, REF_PIXEL = 256;
    private static final long REF_SUM = 3364714L;
    private static final int REF_MAX = 26;

    // Single-frame PTU, pinned by the Python reference test.
    private static final int SF_LINES = 652, SF_PIXEL = 256;
    private static final long SF_SUM = 713854L;

    private static Context context;

    private static String dataRoot() {
        String r = System.getenv("TTTRLIB_DATA");
        return r == null || r.isEmpty() ? "tttr-data" : r;
    }

    private static File clsmFile() {
        return new File(dataRoot(), "imaging/pq/ht3/pq_ht3_clsm.ht3");
    }

    /** A SymPhoTime PTU with line markers but no frame marker. */
    private static File singleFramePtuFile() {
        return new File(dataRoot(), "imaging/pq/PicoHarp_SymPhoTime/Example_PTU_PicoHarp.ptu");
    }

    @BeforeAll
    static void setUp() {
        // A restricted context: a full one would try to start LegacyService,
        // which needs a real Fiji/ij1-patcher environment and fails headlessly.
        context = new Context(CommandService.class, DatasetService.class, LogService.class);
    }

    @AfterAll
    static void tearDown() {
        if (context != null) context.dispose();
    }

    private static Dataset runReader(Map<String, Object> extra) throws Exception {
        return runReader(clsmFile(), extra);
    }

    private static Dataset runReader(File file, Map<String, Object> extra) throws Exception {
        final Map<String, Object> params = new HashMap<>();
        params.put("file", file);
        params.put("channelGroups", "0");
        params.put("microTimeRanges", "");
        params.put("intensity", true);
        params.put("fastLifetime", false);
        params.put("phasor", false);
        params.put("numberAndBrightness", false);
        params.put("decay", false);
        params.put("minPhotons", 2);
        params.put("correctIrfOffset", false);
        params.put("stackFrames", false);
        params.putAll(extra);

        final CommandService cs = context.service(CommandService.class);
        final CommandModule m = cs.run(OpenClsmImage.class, true, params).get();
        return (Dataset) m.getOutput("intensityImage");
    }

    @Test
    void intensityMatchesPinnedReference() throws Exception {
        final Dataset d = runReader(new HashMap<>());
        assertNotNull(d, "intensity Dataset");

        // Axis wiring: X, Y, CHANNEL, TIME (no PIE axis without micro-time ranges).
        assertEquals(4, d.numDimensions(), "axis count");
        assertEquals(Axes.X, d.axis(0).type());
        assertEquals(Axes.Y, d.axis(1).type());
        assertEquals(Axes.CHANNEL, d.axis(2).type());
        assertEquals(Axes.TIME, d.axis(3).type());

        assertEquals(REF_PIXEL, d.dimension(0), "X");
        assertEquals(REF_LINES, d.dimension(1), "Y");
        assertEquals(1, d.dimension(2), "CHANNEL (one group)");
        assertEquals(REF_FRAMES, d.dimension(3), "TIME (frames)");

        long sum = 0;
        int max = 0;
        final Cursor<RealType<?>> c = d.cursor();
        while (c.hasNext()) {
            int v = (int) c.next().getRealDouble();
            sum += v;
            if (v > max) max = v;
        }
        assertEquals(REF_SUM, sum, "intensity sum");
        assertEquals(REF_MAX, max, "intensity max");
    }

    @Test
    void pieRangesGetTheirOwnAxis() throws Exception {
        final Map<String, Object> extra = new HashMap<>();
        extra.put("microTimeRanges", "0,1000;1000,2000");
        final Dataset d = runReader(extra);
        assertNotNull(d, "intensity Dataset");

        // Group and PIE window are separate axes, not a flattened channel index.
        assertEquals(5, d.numDimensions(), "axis count with PIE");
        assertEquals(DatasetBuilder.PIE_AXIS, d.axis(4).type());
        assertEquals(1, d.dimension(2), "CHANNEL (one group)");
        assertEquals(2, d.dimension(4), "PIE (two windows)");
    }

    /**
     * A PTU whose header declares a frame marker the stream never emits
     * used to open as an empty stack ("no image" in the plugin). It must now
     * reconstruct the single full-span frame -- 652 real scan lines, not the
     * nominal ImgHdr_PixY = 256.
     */
    @Test
    void singleFramePtuOpensInsteadOfShowingNoImage() throws Exception {
        final File f = singleFramePtuFile();
        assumeTrue(f.exists(), "single-frame PTU test file not available");

        final Map<String, Object> extra = new HashMap<>();
        extra.put("channelGroups", "1");  // the photons are on routing channel 1
        final Dataset d = runReader(f, extra);
        assertNotNull(d, "intensity Dataset");

        assertEquals(SF_PIXEL, d.dimension(0), "X");
        assertEquals(SF_LINES, d.dimension(1), "Y (salvaged line count)");
        assertEquals(1, d.dimension(3), "TIME (one synthesized frame)");

        long sum = 0;
        final Cursor<RealType<?>> c = d.cursor();
        while (c.hasNext()) sum += (long) c.next().getRealDouble();
        assertEquals(SF_SUM, sum, "intensity sum");
    }

    @Test
    void tttrPathIsAttachedForDownstreamCommands() throws Exception {
        final Dataset d = runReader(new HashMap<>());
        assertEquals(clsmFile().getAbsolutePath(), d.getProperties().get("tttr.path"));
    }
}
