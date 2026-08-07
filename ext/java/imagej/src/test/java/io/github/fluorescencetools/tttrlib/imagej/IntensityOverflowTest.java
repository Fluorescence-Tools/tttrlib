// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import io.github.fluorescencetools.tttrlib.CLSMImage;
import io.github.fluorescencetools.tttrlib.CLSMSettings;
import io.github.fluorescencetools.tttrlib.NativeLoader;
import io.github.fluorescencetools.tttrlib.TTTR;
import io.github.fluorescencetools.tttrlib.VectorInt32;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Proves the 32-bit intensity counter path.
 *
 * <p>No file in the reference data set comes near the limit (the densest,
 * {@code AF555-WGA-1.ptu}, peaks at 374 photons/pixel), so the wrap can only be
 * exercised with a synthetic stream. Before the widening, {@code get_intensity}
 * accumulated into {@code unsigned short}: {@value #PHOTONS} photons in one pixel
 * wrapped to {@code PHOTONS % 65536}.</p>
 */
class IntensityOverflowTest {

    static { NativeLoader.load(); }

    /** Comfortably above the 65535 ceiling, and not a multiple of it. */
    private static final int PHOTONS = 70_000;
    private static final int WRAPPED = PHOTONS % 65536;   // 4464

    private static final byte EVENT_PHOTON = 0;
    private static final byte EVENT_MARKER = 1;
    private static final int MARKER_LINE_START = 1;
    private static final int MARKER_LINE_STOP = 2;
    private static final int MARKER_FRAME_START = 4;

    /**
     * A two-line, one-pixel-per-line scan. Line 0 carries {@link #PHOTONS}
     * photons, line 1 carries a handful, so the single pixel of line 0 must
     * report the full count.
     */
    private static TTTR syntheticScan() {
        final int[] perLine = { PHOTONS, 10 };
        int nEvents = 0;
        for (int n : perLine) nEvents += n + 2;   // + line start and stop markers

        final long[] macroTimes = new long[nEvents];
        final short[] microTimes = new short[nEvents];
        final byte[] routing = new byte[nEvents];
        final byte[] eventTypes = new byte[nEvents];

        int i = 0;
        long t = 0;
        for (int n : perLine) {
            macroTimes[i] = t++;
            routing[i] = (byte) MARKER_LINE_START;
            eventTypes[i++] = EVENT_MARKER;

            for (int p = 0; p < n; p++) {
                macroTimes[i] = t++;
                microTimes[i] = 0;
                routing[i] = 0;
                eventTypes[i++] = EVENT_PHOTON;
            }

            macroTimes[i] = t++;
            routing[i] = (byte) MARKER_LINE_STOP;
            eventTypes[i++] = EVENT_MARKER;
        }
        return new TTTR(macroTimes, microTimes, routing, eventTypes);
    }

    private static CLSMSettings settings() {
        final CLSMSettings s = new CLSMSettings();
        s.setMarker_event_type(EVENT_MARKER);
        s.setMarker_line_start(MARKER_LINE_START);
        s.setMarker_line_stop(MARKER_LINE_STOP);
        final VectorInt32 frameMarkers = new VectorInt32();
        frameMarkers.add(MARKER_FRAME_START);   // configured but absent -> salvage path
        s.setMarker_frame_start(frameMarkers);
        s.setN_pixel_per_line(1);
        s.setN_lines(2);
        return s;
    }

    @Test
    void pixelCountsAboveUnsignedShortAreNotWrapped() {
        final VectorInt32 channels = new VectorInt32();
        channels.add(0);
        final CLSMImage img = new CLSMImage(syntheticScan(), settings(), null, true, channels);

        final int n = img.getN_frames() * img.getN_lines() * img.getN_pixel();
        assertTrue(n > 0, "synthetic scan produced no image");

        final int[] flat = new int[n];
        img.get_intensity_into(flat);

        int max = 0;
        long sum = 0;
        for (int v : flat) {
            sum += v;
            if (v > max) max = v;
        }

        assertEquals(PHOTONS, max,
                "brightest pixel wrapped (would be " + WRAPPED + " on the 16-bit path)");
        assertEquals(PHOTONS + 10L, sum, "total photon count");
    }
}
