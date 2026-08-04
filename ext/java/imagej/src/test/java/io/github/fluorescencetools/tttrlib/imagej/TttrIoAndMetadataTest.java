// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import net.imagej.Dataset;
import net.imagej.DatasetService;

import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import org.scijava.Context;
import org.scijava.command.CommandModule;
import org.scijava.command.CommandService;
import org.scijava.io.IOService;
import org.scijava.io.location.FileLocation;
import org.scijava.log.LogService;
import org.scijava.table.GenericTable;

import java.io.File;
import java.util.HashMap;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** Covers the File&gt;Open path ({@link TttrDatasetIOPlugin}) and {@link ShowTttrMetadata}. */
class TttrIoAndMetadataTest {

    /**
     * File&gt;Open uses defaults: every routing channel combined into one group,
     * unlike the pinned single-channel reference (channel 0 = 3364714). The
     * reference file carries channels 0,1,2,4,5 with sums
     * 3364714 + 8156834 + 0 + 650171 + 1318043.
     */
    private static final long REF_SUM_ALL_CHANNELS = 13489762L;
    private static final long REF_SUM_CHANNEL_0 = 3364714L;

    private static Context context;

    private static File clsmFile() {
        String r = System.getenv("TTTRLIB_DATA");
        return new File(r == null || r.isEmpty() ? "tttr-data" : r,
                        "imaging/pq/ht3/pq_ht3_clsm.ht3");
    }

    @BeforeAll
    static void setUp() {
        // A full Context would try to start LegacyService, which needs a real
        // Fiji/ij1-patcher environment and fails headlessly, so services are
        // listed explicitly.
        context = new Context(CommandService.class, DatasetService.class,
                              IOService.class, io.scif.services.FormatService.class,
                              org.scijava.io.handle.DataHandleService.class,
                              LogService.class);
    }

    @AfterAll
    static void tearDown() {
        if (context != null) context.dispose();
    }

    @Test
    void ioServiceRoutesTttrFilesToOurPlugin() throws Exception {
        final IOService io = context.service(IOService.class);
        final FileLocation loc = new FileLocation(clsmFile());

        // Discovery: our plugin must be the one that claims .ht3.
        assertNotNull(io.getOpener(loc), "no IOPlugin claimed the TTTR file");
        assertEquals(TttrDatasetIOPlugin.class, io.getOpener(loc).getClass(),
                "a different IOPlugin outranked TttrDatasetIOPlugin");

        final Object opened = io.open(loc);
        assertTrue(opened instanceof Dataset, "File>Open did not yield a Dataset");
        final Dataset d = (Dataset) opened;

        long sum = 0;
        for (net.imglib2.type.numeric.RealType<?> t : d) sum += (long) t.getRealDouble();
        assertEquals(REF_SUM_ALL_CHANNELS, sum, "intensity sum via File>Open");
        assertTrue(sum > REF_SUM_CHANNEL_0,
                "File>Open must combine every routing channel, not just channel 0");
        assertEquals(clsmFile().getAbsolutePath(), d.getProperties().get("tttr.path"));
    }

    /**
     * Fiji's {@code File > Open} for images goes through {@code DatasetIOService},
     * which consults SCIFIO formats and ignores plain {@code IOPlugin}s — so the
     * IOPlugin alone left File&gt;Open broken. This covers {@link TttrFormat}.
     */
    @Test
    void scifioResolvesTttrFilesToOurFormat() throws Exception {
        // DatasetIOService.canOpen (and therefore File>Open) delegates to
        // FormatService, so this is what decides whether Fiji routes a TTTR file
        // to us. Exercising DatasetIOService itself needs a dozen more services
        // than a headless test can reasonably assemble; the end-to-end canOpen is
        // checked against a real Fiji instead.
        final io.scif.services.FormatService fs =
                context.service(io.scif.services.FormatService.class);
        final FileLocation loc = new FileLocation(clsmFile());

        final io.scif.Format fmt = fs.getFormat(loc);
        assertNotNull(fmt, "no SCIFIO format claimed the TTTR file");
        assertEquals(TttrFormat.class, fmt.getClass(),
                "a different SCIFIO format outranked TttrFormat");

        // Parse + read plane 0 through the SCIFIO pipeline.
        final io.scif.Reader reader = fmt.createReader();
        reader.setSource(loc);
        final io.scif.Metadata meta = reader.getMetadata();
        assertEquals(1, meta.getImageCount(), "image count");
        final io.scif.ImageMetadata im = meta.get(0);
        assertEquals(256, im.getAxisLength(net.imagej.axis.Axes.X), "X");
        assertEquals(256, im.getAxisLength(net.imagej.axis.Axes.Y), "Y");
        assertEquals(40, im.getAxisLength(net.imagej.axis.Axes.TIME), "frames");

        final io.scif.Plane plane = reader.openPlane(0, 0);
        assertNotNull(plane, "plane 0");
        assertEquals(256 * 256 * 4, plane.getBytes().length, "uint32 plane size");
        reader.close();
    }

    @Test
    void nonTttrExtensionsAreNotClaimed() {
        final TttrDatasetIOPlugin p = new TttrDatasetIOPlugin();
        assertTrue(p.supportsOpen("scan.ptu"));
        assertTrue(p.supportsOpen("scan.HT3"), "extension match must be case-insensitive");
        assertFalse(p.supportsOpen("image.tif"));
        assertFalse(p.supportsOpen("noextension"));
    }

    @Test
    void metadataReportsAcquisitionParameters() throws Exception {
        final Map<String, Object> params = new HashMap<>();
        params.put("file", clsmFile());
        final CommandModule m = context.service(CommandService.class)
                .run(ShowTttrMetadata.class, true, params).get();

        final GenericTable t = (GenericTable) m.getOutput("metadata");
        assertNotNull(t, "metadata table");

        final Map<String, String> kv = new HashMap<>();
        for (int r = 0; r < t.getRowCount(); r++) {
            kv.put(String.valueOf(t.get(0, r)), String.valueOf(t.get(1, r)));
        }
        assertEquals(clsmFile().getAbsolutePath(), kv.get("file"));
        assertTrue(Long.parseLong(kv.get("n_photons")) > 0, "photon count");
        assertTrue(Long.parseLong(kv.get("n_micro_time_channels")) > 0, "micro-time channels");

        final String json = (String) m.getOutput("headerJson");
        assertNotNull(json, "header JSON");
        assertTrue(json.trim().startsWith("{"), "header JSON should be a JSON object");
    }
}
