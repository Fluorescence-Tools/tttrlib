// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import io.github.fluorescencetools.tttrlib.imagej.core.ClsmReconstructor;

import io.scif.AbstractChecker;
import io.scif.AbstractFormat;
import io.scif.AbstractMetadata;
import io.scif.AbstractParser;
import io.scif.ByteArrayPlane;
import io.scif.ByteArrayReader;
import io.scif.Format;
import io.scif.FormatException;
import io.scif.ImageMetadata;
import io.scif.config.SCIFIOConfig;
import io.scif.util.FormatTools;

import net.imagej.axis.Axes;

import net.imglib2.Interval;

import org.scijava.io.handle.DataHandle;
import org.scijava.io.location.FileLocation;
import org.scijava.io.location.Location;
import org.scijava.plugin.Plugin;

import java.io.IOException;

/**
 * SCIFIO {@link Format} for time-tagged time-resolved confocal files
 * ({@code .ptu}, {@code .ht3}, {@code .spc}).
 *
 * <p>{@link TttrDatasetIOPlugin} already serves {@code IOService} (scripts and
 * ImageJ2 drag-and-drop), but Fiji's <em>File&nbsp;&gt;&nbsp;Open</em> for images
 * goes through {@code DatasetIOService}, which consults SCIFIO formats only and
 * ignores plain {@code IOPlugin}s. This format is what makes double-clicking a
 * TTTR file work.</p>
 *
 * <p>Reconstruction uses defaults (every routing channel as one group, intensity
 * only, no PIE gating); {@code Plugins &gt; tttrlib &gt; Open TTTR CLSM Image}
 * exposes the full options.</p>
 */
@Plugin(type = Format.class, name = "TTTR CLSM")
public class TttrFormat extends AbstractFormat {

    @Override
    protected String[] makeSuffixArray() {
        return new String[] { "ptu", "ht3", "spc" };
    }

    @Override
    public String getFormatName() {
        return "Time-tagged time-resolved confocal (tttrlib)";
    }

    // ── Metadata ────────────────────────────────────────────────────────────

    public static class Metadata extends AbstractMetadata {

        /** Frame-major photon counts, {@code [(f * nLines + y) * nPixel + x]}. */
        private int[] intensity;
        private int nFrames, nLines, nPixel;
        private String sourcePath;

        int[] intensity() { return intensity; }
        int nLines() { return nLines; }
        int nPixel() { return nPixel; }

        void setReconstruction(ClsmReconstructor.Result r, String path) {
            this.intensity = r.intensity[0];
            this.nFrames = r.nOutputFrames;
            this.nLines = r.nLines;
            this.nPixel = r.nPixel;
            this.sourcePath = path;
        }

        String sourcePath() { return sourcePath; }

        @Override
        public void populateImageMetadata() {
            createImageMetadata(1);
            final ImageMetadata m = get(0);
            // Photon counts exceed 65535 in long acquisitions, so 32-bit.
            m.setAxes(new net.imagej.axis.DefaultLinearAxis(Axes.X),
                      new net.imagej.axis.DefaultLinearAxis(Axes.Y),
                      new net.imagej.axis.DefaultLinearAxis(Axes.TIME));
            m.setAxisLengths(new long[] { nPixel, nLines, Math.max(nFrames, 1) });
            m.setPlanarAxisCount(2);
            m.setPixelType(FormatTools.UINT32);
            m.setLittleEndian(true);
            m.setIndexed(false);
            m.setFalseColor(false);
            m.setMetadataComplete(true);
        }
    }

    // ── Checker ─────────────────────────────────────────────────────────────

    public static class Checker extends AbstractChecker {
        // Suffix matching from makeSuffixArray() is the whole test: opening a
        // multi-gigabyte photon stream just to sniff it would be wasteful, and
        // these three extensions are not shared with other Fiji formats.
    }

    // ── Parser ──────────────────────────────────────────────────────────────

    public static class Parser extends AbstractParser<Metadata> {

        @Override
        protected void typedParse(final DataHandle<Location> handle, final Metadata meta,
                                  final SCIFIOConfig config) throws IOException, FormatException {
            final Location loc = handle.get();
            if (!(loc instanceof FileLocation)) {
                throw new FormatException("tttrlib needs a local file, got: " + loc);
            }
            // tttrlib reads the file itself (memory-mapped, and it needs the .set
            // sidecar next to a BH .spc), so the handle is only a locator here.
            final String path = ((FileLocation) loc).getFile().getAbsolutePath();

            final ClsmReconstructor.Params p = new ClsmReconstructor.Params();
            p.path = path;
            p.groups.add(new int[0]);   // empty group = every routing channel
            p.intensity = true;
            p.lifetime = false;
            p.phasor = false;
            p.numberAndBrightness = false;
            p.decay = false;

            try {
                meta.setReconstruction(ClsmReconstructor.reconstruct(p), path);
            } catch (ClsmReconstructor.NoImageException e) {
                throw new FormatException(e.getMessage(), e);
            }
        }
    }

    // ── Reader ──────────────────────────────────────────────────────────────

    public static class Reader extends ByteArrayReader<Metadata> {

        @Override
        protected String[] createDomainArray() {
            return new String[] { FormatTools.LM_DOMAIN };
        }

        @Override
        public ByteArrayPlane openPlane(final int imageIndex, final long planeIndex,
                                        final ByteArrayPlane plane, final Interval bounds,
                                        final SCIFIOConfig config)
                throws FormatException, IOException {
            final Metadata meta = getMetadata();
            final int[] src = meta.intensity();
            if (src == null) throw new FormatException("no reconstruction available");

            final int nPixel = meta.nPixel();
            final int perFrame = meta.nLines() * nPixel;
            final int frameOffset = (int) planeIndex * perFrame;

            final int x0 = (int) bounds.min(0), y0 = (int) bounds.min(1);
            final int w = (int) bounds.dimension(0), h = (int) bounds.dimension(1);

            final byte[] buf = plane.getBytes();
            int o = 0;
            for (int y = 0; y < h; y++) {
                int row = frameOffset + (y0 + y) * nPixel + x0;
                for (int x = 0; x < w; x++, o += 4) {
                    final int v = src[row + x];
                    // little-endian uint32, matching setLittleEndian(true)
                    buf[o]     = (byte) (v);
                    buf[o + 1] = (byte) (v >>> 8);
                    buf[o + 2] = (byte) (v >>> 16);
                    buf[o + 3] = (byte) (v >>> 24);
                }
            }
            return plane;
        }
    }
}
