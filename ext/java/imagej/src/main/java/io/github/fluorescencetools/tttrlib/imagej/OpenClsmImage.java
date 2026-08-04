// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import io.github.fluorescencetools.tttrlib.imagej.core.ClsmReconstructor;

import net.imagej.Dataset;
import net.imagej.DatasetService;

import org.scijava.ItemIO;
import org.scijava.ItemVisibility;
import org.scijava.command.Command;
import org.scijava.log.LogService;
import org.scijava.plugin.Parameter;
import org.scijava.plugin.Plugin;
import org.scijava.table.DefaultGenericTable;
import org.scijava.table.DoubleColumn;
import org.scijava.table.GenericTable;

import java.io.File;

/**
 * Opens a time-tagged time-resolved confocal (CLSM) file (PicoQuant PTU/HT3,
 * Becker &amp; Hickl SPC, Leica SP5/SP8) and reconstructs image stacks.
 *
 * <p>Routing-channel groups and micro-time (PIE) windows occupy separate named
 * axes, so a group and a window can be addressed independently.</p>
 *
 * <p>The menu label is kept verbatim from the previous IJ1 plugin so existing
 * macros calling {@code run("Open TTTR CLSM Image", ...)} keep working.</p>
 */
@Plugin(type = Command.class, menuPath = "Plugins>tttrlib>Open TTTR CLSM Image",
        headless = true)
public class OpenClsmImage implements Command {

    @Parameter
    private DatasetService datasetService;

    @Parameter
    private LogService log;

    @Parameter
    private org.scijava.prefs.PrefService prefs;

    @Parameter(label = "TTTR file (PTU / HT3 / SPC)")
    private File file;

    @Parameter(label = "Detector setup (chisurf JSON; blank = use the fields below)",
               required = false, persist = false)
    private File setupFile;

    @Parameter(label = "  Detectors (blank = all in the setup)",
               required = false, persist = true)
    private String setupDetectors = "";

    @Parameter(label = "  PIE windows (blank = all in the setup)",
               required = false, persist = true)
    private String setupWindows = "";

    @Parameter(label = "  Split parallel / perpendicular", persist = true)
    private boolean splitPolarization = false;

    @Parameter(label = "Channel groups (e.g. 1,3;2,4)", persist = true)
    private String channelGroups = "0";

    @Parameter(label = "Micro-time ranges / PIE (e.g. 0,111;200,499)",
               required = false, persist = true)
    private String microTimeRanges = "";

    @Parameter(label = "Intensity", persist = true)
    private boolean intensity = true;

    @Parameter(label = "FastLifetime (mean micro time)", persist = true)
    private boolean fastLifetime = true;

    @Parameter(label = "Phasor (g, s)", persist = true)
    private boolean phasor = false;

    @Parameter(label = "Number & Brightness (N&B)", persist = true)
    private boolean numberAndBrightness = false;

    @Parameter(label = "Decay (micro-time histogram)", persist = true)
    private boolean decay = false;

    @Parameter(label = "Min. photons / pixel", min = "0", persist = true)
    private int minPhotons = 2;

    @Parameter(label = "Auto-correct IRF offset (decay rise)", persist = true)
    private boolean correctIrfOffset = true;

    @Parameter(label = "Stack frames (collapse to one frame)", persist = true)
    private boolean stackFrames = false;

    @Parameter(visibility = ItemVisibility.MESSAGE, required = false)
    private String note = "N&B always uses per-frame data and ignores 'Stack frames'.";

    @Parameter(type = ItemIO.OUTPUT, label = "Intensity")
    private Dataset intensityImage;

    @Parameter(type = ItemIO.OUTPUT, label = "FastLifetime")
    private Dataset fastLifetimeImage;

    @Parameter(type = ItemIO.OUTPUT, label = "Phasor g")
    private Dataset phasorG;

    @Parameter(type = ItemIO.OUTPUT, label = "Phasor s")
    private Dataset phasorS;

    @Parameter(type = ItemIO.OUTPUT, label = "N&B brightness (B)")
    private Dataset nbBrightness;

    @Parameter(type = ItemIO.OUTPUT, label = "N&B number (N)")
    private Dataset nbNumber;

    @Parameter(type = ItemIO.OUTPUT, label = "N&B epsilon (B-1)")
    private Dataset nbEpsilon;

    @Parameter(type = ItemIO.OUTPUT, label = "Decay")
    private GenericTable decayTable;

    @Override
    public void run() {
        final ClsmReconstructor.Params p = new ClsmReconstructor.Params();
        p.path = file.getAbsolutePath();
        p.groups = ClsmReconstructor.parseGroups(channelGroups);
        p.microTimeRanges = ClsmReconstructor.parseGroups(microTimeRanges);
        p.intensity = intensity;
        p.lifetime = fastLifetime;
        p.phasor = phasor;
        p.numberAndBrightness = numberAndBrightness;
        p.decay = decay;
        p.minPhotons = minPhotons;
        p.correctIrfOffset = correctIrfOffset;
        p.stackFrames = stackFrames;

        // A detector setup replaces the group/window fields entirely: each
        // <window>_<detector> pair carries its own micro-time gate, which is not
        // expressible as a cross product of groups and windows.
        // Fall back to Plugins > tttrlib > Settings. The remembered detector and
        // window selection travels with the file: falling back to the file alone
        // would silently reconstruct every detector instead of the saved subset.
        java.io.File setup0 = setupFile;
        String useDetectors = setupDetectors;
        String useWindows = setupWindows;
        boolean useSplit = splitPolarization;
        if (setup0 == null) {
            final String remembered = TttrlibSettings.setupFile(prefs);
            if (remembered != null && !remembered.isEmpty()) {
                final java.io.File f = new java.io.File(remembered);
                if (f.exists()) {
                    setup0 = f;
                    if (useDetectors == null || useDetectors.trim().isEmpty()) {
                        useDetectors = prefs.get(TttrlibSettings.class,
                                TttrlibSettings.KEY_DETECTORS, "");
                    }
                    if (useWindows == null || useWindows.trim().isEmpty()) {
                        useWindows = prefs.get(TttrlibSettings.class,
                                TttrlibSettings.KEY_WINDOWS, "");
                    }
                    useSplit = splitPolarization || prefs.getBoolean(TttrlibSettings.class,
                            TttrlibSettings.KEY_SPLIT_POL, false);
                }
            }
        }
        if (setup0 != null) {
            try {
                final io.github.fluorescencetools.tttrlib.imagej.core.DetectorSetup setup =
                        io.github.fluorescencetools.tttrlib.imagej.core.DetectorSetup
                                .load(setup0.getAbsolutePath());
                p.selections = setup.selections(names(useDetectors), names(useWindows),
                        useSplit);
                if (p.selections.isEmpty()) {
                    log.error("tttrlib: the setup yielded no channels "
                            + "(check the detector and window names).");
                    return;
                }
                log.info("tttrlib: setup '" + setup.setupName() + "' -> "
                        + p.selections.size() + " channel(s)");
            } catch (Exception e) {
                log.error("tttrlib: could not read detector setup: " + e);
                return;
            }
        } else if (p.groups.isEmpty()) {
            log.error("tttrlib: no channel groups (use e.g. 1,3;2,4).");
            return;
        }
        if (numberAndBrightness && stackFrames) {
            log.info("tttrlib: N&B uses per-frame data; ignoring 'Stack frames' for N&B maps.");
        }

        final long t0 = System.nanoTime();
        final ClsmReconstructor.Result r;
        try {
            r = ClsmReconstructor.reconstruct(p);
        } catch (ClsmReconstructor.NoImageException e) {
            log.error("tttrlib: " + e.getMessage());
            return;
        }

        final String base = file.getName();
        if (intensity) {
            intensityImage = tag(DatasetBuilder.intensity(datasetService, r, base + " Intensity"), p.path);
        }
        if (fastLifetime) {
            fastLifetimeImage = tag(DatasetBuilder.floats(datasetService, r,
                    base + " FastLifetime", r.lifetime, r.nOutputFrames), p.path);
        }
        if (phasor) {
            phasorG = tag(DatasetBuilder.phasorComponent(datasetService, r, base + " Phasor g", 0), p.path);
            phasorS = tag(DatasetBuilder.phasorComponent(datasetService, r, base + " Phasor s", 1), p.path);
        }
        if (numberAndBrightness) {
            if (r.nbBrightness == null) {
                log.info("tttrlib: N&B needs >= 2 frames (have " + r.nFrames + "); skipping.");
            } else {
                nbBrightness = tag(DatasetBuilder.floats(datasetService, r,
                        base + " N&B brightness (B)", r.nbBrightness, 1), p.path);
                nbNumber = tag(DatasetBuilder.floats(datasetService, r,
                        base + " N&B number (N)", r.nbNumber, 1), p.path);
                nbEpsilon = tag(DatasetBuilder.floats(datasetService, r,
                        base + " N&B epsilon (B-1)", r.nbEpsilon, 1), p.path);
            }
        }
        if (decay && r.decay != null) {
            decayTable = decayTable(r);
        }

        log.info(String.format("tttrlib: %d group(s) x %d window(s), %d frames %dx%d in %.0f ms",
                r.nGroups, r.nWindows, r.nFrames, r.nPixel, r.nLines,
                (System.nanoTime() - t0) / 1e6));
    }

    /** Comma/semicolon separated names, or null when blank. */
    private static java.util.List<String> names(String spec) {
        if (spec == null || spec.trim().isEmpty()) return null;
        final java.util.List<String> out = new java.util.ArrayList<>();
        for (String s : spec.trim().split("[,;]")) {
            final String t = s.trim();
            if (!t.isEmpty()) out.add(t);
        }
        return out;
    }

    /** Tag the source file so downstream commands can reload the photons. */
    private Dataset tag(Dataset d, String path) {
        if (d != null) d.getProperties().put("tttr.path", path);
        return d;
    }

    private GenericTable decayTable(ClsmReconstructor.Result r) {
        int nBins = 0;
        for (double[] d : r.decay) nBins = Math.max(nBins, d.length);
        final DefaultGenericTable t = new DefaultGenericTable();
        final DoubleColumn time = new DoubleColumn("time_ns");
        for (int i = 0; i < nBins; i++) time.add(i * r.microTimeResolutionNs);
        t.add(time);
        for (int g = 0; g < r.decay.length; g++) {
            final DoubleColumn c = new DoubleColumn("g" + (g + 1));
            for (int i = 0; i < nBins; i++) {
                c.add(i < r.decay[g].length ? r.decay[g][i] : 0.0);
            }
            t.add(c);
        }
        return t;
    }
}
