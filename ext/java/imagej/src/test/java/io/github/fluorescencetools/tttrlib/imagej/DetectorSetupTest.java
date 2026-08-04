// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import io.github.fluorescencetools.tttrlib.imagej.core.ClsmReconstructor;
import io.github.fluorescencetools.tttrlib.imagej.core.DetectorSetup;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * File compatibility with chisurf's {@code detector_setups.json}.
 *
 * <p>The fixtures are verbatim chisurf documents: the small one ships in
 * chisurf's burst-selection assets, the large one is a real BH SPC-130 setup from
 * its burst-analysis handoff data.</p>
 */
class DetectorSetupTest {

    /** chisurf/plugins/burst/burst_selection/gui/assets/channel_settings.json */
    private static final String SMALL = "{\n"
            + "    \"windows\": {\n"
            + "        \"prompt\": [0, 2048],\n"
            + "        \"delayed\": [2048, 4095]\n"
            + "    },\n"
            + "    \"detectors\": {\n"
            + "        \"all\":   { \"chs\": [0, 1], \"micro_time_ranges\": [[0, 1024]] },\n"
            + "        \"green\": { \"chs\": [0],    \"micro_time_ranges\": [[0, 1000]] },\n"
            + "        \"red\":   { \"chs\": [1],    \"micro_time_ranges\": [[0, 2048]] }\n"
            + "    }\n"
            + "}\n";

    /** A real setup, including keys this plugin does not model. */
    private static final String FULL = "{\n"
            + "  \"setups\": {\n"
            + "    \"BS\": {\n"
            + "      \"setup_name\": \"BS\",\n"
            + "      \"apply_lut\": true,\n"
            + "      \"windows\": { \"prompt\": [0, 2048], \"delayed\": [2048, 4095] },\n"
            + "      \"detectors\": {\n"
            + "        \"green\":  { \"chs\": [8, 0, 3], \"micro_time_ranges\": [[0, 4095]],\n"
            + "                     \"g_factor\": 1.0, \"l1\": 0.0, \"l2\": 0.0,\n"
            + "                     \"mle_settings\": { \"min_photons\": 10, \"shift\": 0 } },\n"
            + "        \"red\":    { \"chs\": [9, 1, 2], \"micro_time_ranges\": [[0, 2048]],\n"
            + "                     \"g_factor\": 1.1, \"l1\": 0.02, \"l2\": 0.03 },\n"
            + "        \"yellow\": { \"chs\": [9, 1, 2], \"micro_time_ranges\": [[2048, 4095]] }\n"
            + "      },\n"
            + "      \"tttr_reading\": { \"file_type\": \"SPC-130\",\n"
            + "                        \"micro_time_resolution\": 3.2958984375 }\n"
            + "    }\n"
            + "  },\n"
            + "  \"last_used\": \"BS\"\n"
            + "}\n";

    private static Path write(Path dir, String name, String content) throws Exception {
        final Path p = dir.resolve(name);
        Files.write(p, content.getBytes(StandardCharsets.UTF_8));
        return p;
    }

    /** A bare payload with no "setups" envelope must still load. */
    @Test
    void readsABareChisurfPayload(@TempDir Path dir) throws Exception {
        final DetectorSetup s = DetectorSetup.load(
                write(dir, "small.json", SMALL).toString());

        final Map<String, int[]> w = s.windows();
        assertArrayEquals(new int[] { 0, 2048 }, w.get("prompt"));
        assertArrayEquals(new int[] { 2048, 4095 }, w.get("delayed"));

        final Map<String, DetectorSetup.Detector> d = s.detectors();
        assertEquals(3, d.size());
        assertArrayEquals(new int[] { 0, 1 }, d.get("all").channels);
        assertArrayEquals(new int[] { 0, 1000 }, d.get("green").microTimeRanges.get(0));
    }

    @Test
    void readsTheEnvelopeAndPerDetectorCorrections(@TempDir Path dir) throws Exception {
        final DetectorSetup s = DetectorSetup.load(write(dir, "full.json", FULL).toString());
        assertEquals("BS", s.setupName());
        assertEquals(Arrays.asList("BS"), s.setupNames());
        assertEquals(3.2958984375, s.microTimeResolutionNs(), 1e-12);

        final DetectorSetup.Detector red = s.detectors().get("red");
        assertEquals(1.1, red.gFactor, 1e-12);
        assertEquals(0.02, red.l1, 1e-12);
        assertEquals(0.03, red.l2, 1e-12);
        // Defaults for a detector that omits them.
        assertEquals(1.0, s.detectors().get("yellow").gFactor, 1e-12);
    }

    /**
     * Polarization splits on list POSITION, not channel number: chisurf's imaging
     * stack uses chs[::2] / chs[1::2].
     */
    @Test
    void derivesParallelAndPerpendicularFromChannelOrder(@TempDir Path dir) throws Exception {
        final DetectorSetup s = DetectorSetup.load(write(dir, "full.json", FULL).toString());
        final DetectorSetup.Detector green = s.detectors().get("green");
        assertTrue(s.polarizationResolved(), "default is polarization-resolved");
        assertArrayEquals(new int[] { 8, 3 }, green.parallel(true));
        assertArrayEquals(new int[] { 0 }, green.perpendicular(true));
        // Not polarization-resolved: everything is parallel.
        assertArrayEquals(new int[] { 8, 0, 3 }, green.parallel(false));
        assertArrayEquals(new int[0], green.perpendicular(false));
    }

    /** A window clips the detector's own gate; a disjoint pair drops out. */
    @Test
    void intersectsDetectorGatesWithWindows(@TempDir Path dir) throws Exception {
        final DetectorSetup s = DetectorSetup.load(write(dir, "full.json", FULL).toString());
        // green is [0,4095], prompt is [0,2048] -> clipped
        assertArrayEquals(new int[] { 0, 2048 }, s.effectiveRanges("green", "prompt").get(0));
        // yellow is [2048,4095]; it does not overlap prompt at all
        assertTrue(s.effectiveRanges("yellow", "prompt").isEmpty(),
                "a detector gated outside the window must yield nothing");
        assertArrayEquals(new int[] { 2048, 4095 },
                s.effectiveRanges("yellow", "delayed").get(0));
    }

    @Test
    void expandsIntoOneSelectionPerWindowDetectorPair(@TempDir Path dir) throws Exception {
        final DetectorSetup s = DetectorSetup.load(write(dir, "full.json", FULL).toString());
        final List<ClsmReconstructor.Selection> sel = s.selections(null, null, false);

        // 2 windows x 3 detectors = 6 candidates, of which two intersect to zero
        // width and are dropped:
        //   prompt  [0,2048]    n yellow [2048,4095] -> empty
        //   delayed [2048,4095] n red    [0,2048]    -> empty
        // Dropping these matters: fill_micro_time_range treats stop <= start as
        // "no micro-time gate", so keeping a zero-width range would silently let
        // every photon through instead of none.
        assertEquals(4, sel.size(), "zero-width intersections must be dropped");

        final java.util.Set<String> labels = new java.util.HashSet<>();
        for (ClsmReconstructor.Selection x : sel) {
            labels.add(x.label);
            if ("prompt_green".equals(x.label)) {
                assertArrayEquals(new int[] { 8, 0, 3 }, x.channels);
                assertArrayEquals(new int[] { 0, 2048 }, x.range);
            }
        }
        assertEquals(new java.util.HashSet<>(Arrays.asList(
                "prompt_green", "prompt_red", "delayed_green", "delayed_yellow")), labels);
    }

    @Test
    void splitsPolarizationIntoSeparateChannels(@TempDir Path dir) throws Exception {
        final DetectorSetup s = DetectorSetup.load(write(dir, "full.json", FULL).toString());
        final List<ClsmReconstructor.Selection> sel =
                s.selections(Arrays.asList("green"), Arrays.asList("prompt"), true);
        assertEquals(2, sel.size());
        assertEquals("prompt_green_p", sel.get(0).label);
        assertArrayEquals(new int[] { 8, 3 }, sel.get(0).channels);
        assertEquals("prompt_green_s", sel.get(1).label);
        assertArrayEquals(new int[] { 0 }, sel.get(1).channels);
    }

    /** Unmodelled keys must survive, and the derived cache must be rebuilt. */
    @Test
    void roundTripPreservesUnknownKeys(@TempDir Path dir) throws Exception {
        final Path p = write(dir, "full.json", FULL);
        final DetectorSetup s = DetectorSetup.load(p.toString());
        final DetectorSetup.Detector d = new DetectorSetup.Detector();
        d.name = "blue";
        d.channels = new int[] { 4, 5 };
        d.microTimeRanges.add(new int[] { 0, 512 });
        s.putDetector(d);
        s.putWindow("late", 3000, 4095);
        s.save(p.toString());

        final String out = new String(Files.readAllBytes(p), StandardCharsets.UTF_8);
        assertTrue(out.contains("mle_settings"), "unmodelled detector keys were dropped");
        assertTrue(out.contains("apply_lut"), "unmodelled setup keys were dropped");
        assertTrue(out.contains("\"last_used\""), "envelope was dropped");
        // The derived cross-product cache chisurf keeps must be regenerated.
        assertTrue(out.contains("late_blue"), "derived channels cache was not rebuilt");

        final DetectorSetup again = DetectorSetup.load(p.toString());
        assertNotNull(again.detectors().get("blue"));
        assertArrayEquals(new int[] { 4, 5 }, again.detectors().get("blue").channels);
        assertArrayEquals(new int[] { 3000, 4095 }, again.windows().get("late"));
        // and the originals are still intact
        assertArrayEquals(new int[] { 8, 0, 3 }, again.detectors().get("green").channels);
    }

    /**
     * The setup-level flag decides whether there is a VV/VH split at all, and it
     * outranks a per-detector ch_p/ch_s so a setup declared unpolarised cannot
     * have a split resurrected by a leftover assignment.
     */
    @Test
    void polarizationResolvedFlagGovernsTheSplit(@TempDir Path dir) throws Exception {
        final Path p = write(dir, "full.json", FULL);
        DetectorSetup s = DetectorSetup.load(p.toString());
        assertTrue(s.polarizationResolved(), "absent flag defaults to resolved");

        // Turn it off, persist, reload.
        s.setPolarizationResolved(false);
        s.save(p.toString());
        s = DetectorSetup.load(p.toString());
        assertTrue(!s.polarizationResolved(), "flag did not survive the round trip");

        final DetectorSetup.Detector green = s.detectors().get("green");
        assertArrayEquals(new int[] { 8, 0, 3 }, green.parallel(false));
        assertArrayEquals(new int[0], green.perpendicular(false));

        // Splitting is a no-op: one channel per detector, no _p / _s suffixes.
        final List<ClsmReconstructor.Selection> sel =
                s.selections(Arrays.asList("green"), Arrays.asList("prompt"), true);
        assertEquals(1, sel.size());
        assertEquals("prompt_green_p", sel.get(0).label);
        assertArrayEquals(new int[] { 8, 0, 3 }, sel.get(0).channels);
    }

    /** An explicit ch_p/ch_s must not override an unpolarised setup. */
    @Test
    void unpolarisedSetupOutranksExplicitChannels(@TempDir Path dir) throws Exception {
        final String json = "{\"polarization_resolved\": false, \"detectors\": "
                + "{\"g\": {\"chs\": [8, 0, 3], \"ch_p\": [8], \"ch_s\": [0, 3]}}}";
        final DetectorSetup s = DetectorSetup.load(write(dir, "np.json", json).toString());
        final DetectorSetup.Detector d = s.detectors().get("g");
        assertArrayEquals(new int[] { 8, 0, 3 }, d.parallel(s.polarizationResolved()));
        assertArrayEquals(new int[0], d.perpendicular(s.polarizationResolved()));
    }
}
