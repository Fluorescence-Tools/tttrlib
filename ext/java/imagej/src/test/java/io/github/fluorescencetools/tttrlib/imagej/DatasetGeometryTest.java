// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import io.github.fluorescencetools.tttrlib.imagej.core.ClsmReconstructor;

import net.imagej.Dataset;
import net.imagej.DatasetService;

import net.imglib2.RandomAccess;
import net.imglib2.type.numeric.RealType;

import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import org.scijava.Context;

import java.io.File;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Guards the array &rarr; {@link Dataset} spatial mapping.
 *
 * <p>The other tests assert dimensions, axis types and pixel sums — every one of
 * which is invariant under an arbitrary spatial permutation. A transposed axis, an
 * off-by-one row stride or mirrored alternate rows would pass all of them while
 * producing a visibly wrong image. These tests compare pixel-for-pixel against the
 * frame-major source array, and check row-to-row coherence so a
 * bidirectional-style comb artefact cannot slip through.</p>
 */
class DatasetGeometryTest {

    private static Context context;
    private static DatasetService datasetService;

    private static File clsmFile() {
        String r = System.getenv("TTTRLIB_DATA");
        return new File(r == null || r.isEmpty() ? "tttr-data" : r,
                        "imaging/pq/ht3/pq_ht3_clsm.ht3");
    }

    @BeforeAll
    static void setUp() {
        context = new Context(DatasetService.class);
        datasetService = context.service(DatasetService.class);
    }

    @AfterAll
    static void tearDown() {
        if (context != null) context.dispose();
    }

    private static ClsmReconstructor.Result reconstruct() {
        final ClsmReconstructor.Params p = new ClsmReconstructor.Params();
        p.path = clsmFile().getAbsolutePath();
        p.groups.add(new int[] { 0 });
        p.intensity = true;
        p.lifetime = false;
        return ClsmReconstructor.reconstruct(p);
    }

    @Test
    void everyPixelLandsAtTheRightPosition() {
        final ClsmReconstructor.Result r = reconstruct();
        final Dataset d = DatasetBuilder.intensity(datasetService, r, "geometry");
        final RandomAccess<RealType<?>> ra = d.randomAccess();
        final int perFrame = r.nLines * r.nPixel;

        // Exhaustive over x and y for a few frames: a transpose, a stride error or
        // a mirrored row cannot survive this.
        final int[] frames = { 0, 1, r.nOutputFrames / 2, r.nOutputFrames - 1 };
        for (int f : frames) {
            for (int y = 0; y < r.nLines; y++) {
                for (int x = 0; x < r.nPixel; x++) {
                    ra.setPosition(new long[] { x, y, 0, f });
                    final int expected = r.intensity[0][f * perFrame + y * r.nPixel + x];
                    assertEquals(expected, (int) ra.get().getRealDouble(),
                            "pixel mismatch at x=" + x + " y=" + y + " frame=" + f);
                }
            }
        }
    }

    /**
     * A bidirectional scan rendered without correction mirrors every other line, so
     * a row correlates better with its neighbour reversed than with it as-is. On
     * this reference file the data is unidirectional, so the plain correlation must
     * dominate — if the Dataset mapping ever mirrors alternate rows, this flips.
     */
    @Test
    void alternateRowsAreNotMirrored() {
        final ClsmReconstructor.Result r = reconstruct();
        final Dataset d = DatasetBuilder.intensity(datasetService, r, "geometry");
        final RandomAccess<RealType<?>> ra = d.randomAccess();

        // Collapse frames into one plane to get usable statistics per row.
        final double[][] plane = new double[r.nLines][r.nPixel];
        for (int f = 0; f < r.nOutputFrames; f++) {
            for (int y = 0; y < r.nLines; y++) {
                for (int x = 0; x < r.nPixel; x++) {
                    ra.setPosition(new long[] { x, y, 0, f });
                    plane[y][x] += ra.get().getRealDouble();
                }
            }
        }

        double asIs = 0, reversed = 0;
        int n = 0;
        for (int y = 0; y + 1 < r.nLines; y++) {
            final double[] a = plane[y];
            final double[] b = plane[y + 1];
            final double[] bRev = new double[b.length];
            for (int i = 0; i < b.length; i++) bRev[i] = b[b.length - 1 - i];
            final double c1 = corr(a, b);
            final double c2 = corr(a, bRev);
            if (!Double.isNaN(c1) && !Double.isNaN(c2)) { asIs += c1; reversed += c2; n++; }
        }
        assertTrue(n > 0, "no comparable row pairs");
        asIs /= n;
        reversed /= n;

        assertTrue(asIs > reversed + 0.05,
                "alternate rows look mirrored — bidirectional-style comb artefact: "
                        + "corr(row, next)=" + asIs + " vs corr(row, reversed next)=" + reversed);
    }

    private static double corr(double[] a, double[] b) {
        final int n = a.length;
        double sa = 0, sb = 0;
        for (int i = 0; i < n; i++) { sa += a[i]; sb += b[i]; }
        final double ma = sa / n, mb = sb / n;
        double num = 0, da = 0, db = 0;
        for (int i = 0; i < n; i++) {
            final double x = a[i] - ma, y = b[i] - mb;
            num += x * y; da += x * x; db += y * y;
        }
        if (da == 0 || db == 0) return Double.NaN;
        return num / Math.sqrt(da * db);
    }
}
