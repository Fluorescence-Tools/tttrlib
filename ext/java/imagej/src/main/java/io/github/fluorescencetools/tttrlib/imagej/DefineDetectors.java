// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import io.github.fluorescencetools.tttrlib.imagej.core.DetectorSetup;

import org.scijava.ItemIO;
import org.scijava.ItemVisibility;
import org.scijava.command.Command;
import org.scijava.log.LogService;
import org.scijava.plugin.Parameter;
import org.scijava.plugin.Plugin;
import org.scijava.table.DefaultGenericTable;
import org.scijava.table.GenericColumn;
import org.scijava.table.GenericTable;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * View and edit a chisurf detector setup: named detectors (routing channels with
 * micro-time gates, G-factor and leakage) and named PIE windows.
 *
 * <p>Reads and writes chisurf's {@code detector_setups.json} format, so a setup
 * defined in chisurf's <em>Setup:Channel Definition</em> tool can be used here
 * and vice versa. Keys this plugin does not model — {@code mle_settings},
 * {@code fret_calibration}, LUTs — are preserved on save.</p>
 *
 * <p>Bounds are RAW micro-time channels of the TTTR file, as in chisurf, never
 * nanoseconds.</p>
 *
 * <p>chisurf's live GUI migrates {@code ~/.chisurf/detector_setups.json} into its
 * database and then deletes it, so prefer a side file and load it in chisurf via
 * the setup picker.</p>
 */
@Plugin(type = Command.class, menuPath = "Plugins>tttrlib>Detector Definition (headless)...",
        headless = true)
public class DefineDetectors implements Command {

    @Parameter
    private LogService log;

    @Parameter(label = "Setup file (chisurf detector_setups.json)", persist = false)
    private File setupFile;

    @Parameter(label = "Setup name (blank = last used)", required = false, persist = false)
    private String setupName = "";

    @Parameter(label = "Create the file if missing", persist = true)
    private boolean createIfMissing = false;

    /**
     * Setup-level flag. Mirrors chisurf's "Polarization resolved" checkbox: when
     * off, each detector's channels are a single unpolarised stream instead of
     * interleaved VV/VH.
     */
    @Parameter(label = "Polarization resolved",
               choices = { "(leave unchanged)", "yes", "no" },
               required = false, persist = false)
    private String polarizationResolved = "(leave unchanged)";

    @Parameter(visibility = ItemVisibility.MESSAGE, required = false)
    private String note =
            "Leave the edit fields blank to only inspect. Ranges are RAW micro-time channels.";

    @Parameter(label = "Detector name (blank = no change)", required = false, persist = false)
    private String detectorName = "";

    @Parameter(label = "  Channels (e.g. 0,2)", required = false, persist = false)
    private String detectorChannels = "";

    @Parameter(label = "  Micro-time ranges (e.g. 0,2048;2048,4095)",
               required = false, persist = false)
    private String detectorRanges = "";

    @Parameter(label = "  G-factor", required = false, persist = false)
    private double gFactor = 1.0;

    @Parameter(label = "  l1", required = false, persist = false)
    private double l1 = 0.0;

    @Parameter(label = "  l2", required = false, persist = false)
    private double l2 = 0.0;

    @Parameter(label = "  Parallel channels (blank = even positions of Channels)",
               required = false, persist = false)
    private String parallelChannels = "";

    @Parameter(label = "  Perpendicular channels (blank = odd positions)",
               required = false, persist = false)
    private String perpendicularChannels = "";

    @Parameter(label = "PIE window name (blank = no change)", required = false, persist = false)
    private String windowName = "";

    @Parameter(label = "  Window start", required = false, persist = false)
    private int windowStart = 0;

    @Parameter(label = "  Window end", required = false, persist = false)
    private int windowEnd = 0;

    @Parameter(label = "Save changes back to the file", persist = false)
    private boolean save = false;

    @Parameter(type = ItemIO.OUTPUT, label = "Detectors")
    private GenericTable detectors;

    @Parameter(type = ItemIO.OUTPUT, label = "PIE windows")
    private GenericTable windows;

    @Override
    public void run() {
        DetectorSetup setup;
        final String name = setupName == null || setupName.trim().isEmpty()
                ? null : setupName.trim();
        try {
            if (!setupFile.exists()) {
                if (!createIfMissing) {
                    log.error("tttrlib: setup file does not exist: " + setupFile
                            + " (tick 'Create the file if missing' to start a new one)");
                    return;
                }
                setup = DetectorSetup.empty(name == null ? "default" : name);
            } else {
                setup = DetectorSetup.load(setupFile.getAbsolutePath(), name);
            }
        } catch (Exception e) {
            log.error("tttrlib: could not read setup: " + e);
            return;
        }

        boolean changed = false;
        if ("yes".equals(polarizationResolved) || "no".equals(polarizationResolved)) {
            setup.setPolarizationResolved("yes".equals(polarizationResolved));
            changed = true;
        }
        if (windowName != null && !windowName.trim().isEmpty()) {
            if (windowEnd <= windowStart) {
                log.error("tttrlib: window end must be greater than start.");
                return;
            }
            setup.putWindow(windowName.trim(), windowStart, windowEnd);
            changed = true;
        }
        if (detectorName != null && !detectorName.trim().isEmpty()) {
            final DetectorSetup.Detector d = new DetectorSetup.Detector();
            d.name = detectorName.trim();
            d.channels = parseInts(detectorChannels);
            d.microTimeRanges = parseRanges(detectorRanges);
            d.gFactor = gFactor;
            d.l1 = l1;
            d.l2 = l2;
            d.parallelChannels = parseInts(parallelChannels);
            d.perpendicularChannels = parseInts(perpendicularChannels);
            if (d.channels.length == 0) {
                log.error("tttrlib: detector '" + d.name + "' needs at least one channel.");
                return;
            }
            setup.putDetector(d);
            changed = true;
        }

        if (changed && save) {
            try {
                setup.save(setupFile.getAbsolutePath());
                log.info("tttrlib: wrote " + setupFile);
            } catch (Exception e) {
                log.error("tttrlib: could not write setup: " + e);
                return;
            }
        } else if (changed) {
            log.info("tttrlib: changes applied in memory only "
                    + "(tick 'Save changes back to the file' to persist).");
        }

        detectors = detectorTable(setup);
        windows = windowTable(setup);
        log.info("tttrlib: setup '" + setup.setupName() + "' has "
                + setup.detectors().size() + " detector(s), "
                + setup.windows().size() + " window(s); available setups: "
                + setup.setupNames());
    }

    private static GenericTable detectorTable(DetectorSetup s) {
        final boolean pol = s.polarizationResolved();
        final GenericColumn name = new GenericColumn("detector");
        final GenericColumn chs = new GenericColumn("channels");
        final GenericColumn par = new GenericColumn("parallel");
        final GenericColumn perp = new GenericColumn("perpendicular");
        final GenericColumn ranges = new GenericColumn("micro_time_ranges");
        final GenericColumn g = new GenericColumn("g_factor");
        final GenericColumn c1 = new GenericColumn("l1");
        final GenericColumn c2 = new GenericColumn("l2");
        for (Map.Entry<String, DetectorSetup.Detector> e : s.detectors().entrySet()) {
            final DetectorSetup.Detector d = e.getValue();
            name.add(e.getKey());
            chs.add(join(d.channels));
            par.add(join(d.parallel(pol)));
            perp.add(join(d.perpendicular(pol)));
            ranges.add(joinRanges(d.microTimeRanges));
            g.add(d.gFactor);
            c1.add(d.l1);
            c2.add(d.l2);
        }
        final DefaultGenericTable t = new DefaultGenericTable();
        t.add(name); t.add(chs); t.add(par); t.add(perp);
        t.add(ranges); t.add(g); t.add(c1); t.add(c2);
        return t;
    }

    private static GenericTable windowTable(DetectorSetup s) {
        final GenericColumn name = new GenericColumn("window");
        final GenericColumn start = new GenericColumn("start");
        final GenericColumn end = new GenericColumn("end");
        for (Map.Entry<String, int[]> e : s.windows().entrySet()) {
            name.add(e.getKey());
            start.add(e.getValue()[0]);
            end.add(e.getValue()[1]);
        }
        final DefaultGenericTable t = new DefaultGenericTable();
        t.add(name); t.add(start); t.add(end);
        return t;
    }

    static String join(int[] v) {
        final StringBuilder b = new StringBuilder();
        for (int i = 0; i < v.length; i++) {
            if (i > 0) b.append(',');
            b.append(v[i]);
        }
        return b.toString();
    }

    static String joinRanges(List<int[]> ranges) {
        final StringBuilder b = new StringBuilder();
        for (int i = 0; i < ranges.size(); i++) {
            if (i > 0) b.append(';');
            b.append(ranges.get(i)[0]).append(',').append(ranges.get(i)[1]);
        }
        return b.toString();
    }

    static int[] parseInts(String spec) {
        if (spec == null || spec.trim().isEmpty()) return new int[0];
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

    static List<int[]> parseRanges(String spec) {
        final List<int[]> out = new ArrayList<>();
        if (spec == null || spec.trim().isEmpty()) return out;
        for (String part : spec.trim().split(";")) {
            final int[] v = parseInts(part);
            if (v.length >= 2) out.add(new int[] { v[0], v[1] });
        }
        return out;
    }
}
