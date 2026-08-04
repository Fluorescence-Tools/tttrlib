// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import io.github.fluorescencetools.tttrlib.imagej.core.ClsmReconstructor;

import net.imagej.Dataset;
import net.imagej.DatasetService;
import net.imagej.axis.Axes;
import net.imagej.axis.AxisType;
import net.imglib2.RandomAccess;
import net.imglib2.type.numeric.RealType;

/**
 * Turns {@link ClsmReconstructor.Result} arrays into {@link Dataset}s with named
 * axes.
 *
 * <p>Routing group and micro-time window get their own axes ({@code CHANNEL} and
 * a custom {@code PIE} axis) rather than being flattened into a single channel
 * index, so a caller can address "all windows of group 1" as an axis range
 * instead of parsing slice labels.</p>
 */
final class DatasetBuilder {

    /** Custom axis carrying the PIE / micro-time window index. */
    static final AxisType PIE_AXIS = Axes.get("PIE");

    private DatasetBuilder() { }

    private static long[] dims(ClsmReconstructor.Result r, int nFrames, boolean interleavedPair) {
        // X, Y, CHANNEL, TIME [, PIE]
        int n = 4 + (r.gated ? 1 : 0);
        long[] d = new long[n];
        d[0] = r.nPixel;
        d[1] = r.nLines;
        d[2] = r.nGroups;
        d[3] = nFrames;
        if (r.gated) d[4] = r.nWindows;
        return d;
    }

    private static AxisType[] axes(ClsmReconstructor.Result r) {
        return r.gated
                ? new AxisType[] { Axes.X, Axes.Y, Axes.CHANNEL, Axes.TIME, PIE_AXIS }
                : new AxisType[] { Axes.X, Axes.Y, Axes.CHANNEL, Axes.TIME };
    }

    /**
     * Photon counts as {@code UnsignedIntType}. 32-bit throughout: the C++ path
     * (get_intensity_u32) no longer wraps at 65536, and the pixel type must not
     * reintroduce the ceiling.
     */
    static Dataset intensity(DatasetService ds, ClsmReconstructor.Result r, String name) {
        return fill(ds, r, name, r.intensity, r.nOutputFrames, 32, false, false, -1);
    }

    /** A {@code double[][]} per-window map as {@code FloatType}. */
    static Dataset floats(DatasetService ds, ClsmReconstructor.Result r, String name,
                          double[][] data, int nFrames) {
        return fill(ds, r, name, data, nFrames, 32, true, true, -1);
    }

    /**
     * One component of the interleaved phasor array as {@code FloatType}.
     *
     * @param component 0 for g, 1 for s
     */
    static Dataset phasorComponent(DatasetService ds, ClsmReconstructor.Result r, String name,
                                   int component) {
        return fill(ds, r, name, r.phasor, r.nOutputFrames, 32, true, true, component);
    }

    /**
     * @param data      per-window arrays; element type is {@code int[]},
     *                  {@code double[]} or {@code float[]}
     * @param component &ge; 0 selects a component of an interleaved pair array
     */
    private static Dataset fill(DatasetService ds, ClsmReconstructor.Result r, String name,
                                Object[] data, int nFrames,
                                int bitsPerPixel, boolean signed, boolean floating,
                                int component) {
        if (data == null) return null;
        final Dataset dataset = ds.create(dims(r, nFrames, component >= 0), name, axes(r),
                bitsPerPixel, signed, floating);
        final RandomAccess<RealType<?>> ra = dataset.randomAccess();
        final int perFrame = r.nLines * r.nPixel;
        final long[] pos = new long[dims(r, nFrames, component >= 0).length];

        for (int g = 0; g < r.nGroups; g++) {
            for (int w = 0; w < r.nWindows; w++) {
                Object arr = data[g * r.nWindows + w];
                if (arr == null) continue;
                pos[2] = g;
                if (r.gated) pos[4] = w;
                for (int f = 0; f < nFrames; f++) {
                    pos[3] = f;
                    int off = f * perFrame;
                    for (int y = 0; y < r.nLines; y++) {
                        pos[1] = y;
                        int rowOff = off + y * r.nPixel;
                        for (int x = 0; x < r.nPixel; x++) {
                            pos[0] = x;
                            int i = rowOff + x;
                            double v;
                            if (component >= 0) {
                                v = ((float[]) arr)[i * 2 + component];
                            } else if (arr instanceof int[]) {
                                // widen through int, not short - counts may exceed 65535
                                v = ((int[]) arr)[i];
                            } else if (arr instanceof double[]) {
                                v = ((double[]) arr)[i];
                            } else {
                                v = ((float[]) arr)[i];
                            }
                            ra.setPosition(pos);
                            ra.get().setReal(v);
                        }
                    }
                }
            }
        }
        return dataset;
    }
}
