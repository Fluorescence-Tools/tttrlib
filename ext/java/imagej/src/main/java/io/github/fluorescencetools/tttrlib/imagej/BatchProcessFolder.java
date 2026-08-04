// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import io.github.fluorescencetools.tttrlib.imagej.core.ClsmReconstructor;

import io.scif.services.DatasetIOService;

import net.imagej.Dataset;
import net.imagej.DatasetService;

import org.scijava.ItemIO;
import org.scijava.command.Command;
import org.scijava.log.LogService;
import org.scijava.plugin.Parameter;
import org.scijava.plugin.Plugin;
import org.scijava.table.DefaultGenericTable;
import org.scijava.table.GenericColumn;
import org.scijava.table.GenericTable;

import java.io.File;
import java.util.Arrays;
import java.util.Locale;

/**
 * Reconstructs every TTTR file in a folder and writes the results as TIFF.
 *
 * <p>Headless-capable, so it can be driven from a macro, a script or
 * {@code --headless --run}.</p>
 */
@Plugin(type = Command.class, menuPath = "Plugins>tttrlib>Batch Process Folder...",
        headless = true)
public class BatchProcessFolder implements Command {

    @Parameter
    private DatasetService datasetService;

    @Parameter
    private DatasetIOService datasetIOService;

    @Parameter
    private LogService log;

    @Parameter(label = "Input folder", style = "directory")
    private File inputFolder;

    @Parameter(label = "Output folder", style = "directory")
    private File outputFolder;

    @Parameter(label = "Channel groups (e.g. 1,3;2,4)", persist = true)
    private String channelGroups = "0";

    @Parameter(label = "Micro-time ranges / PIE (e.g. 0,111;200,499)",
               required = false, persist = true)
    private String microTimeRanges = "";

    @Parameter(label = "Intensity", persist = true)
    private boolean intensity = true;

    @Parameter(label = "FastLifetime (mean micro time)", persist = true)
    private boolean fastLifetime = false;

    @Parameter(label = "Min. photons / pixel", min = "0", persist = true)
    private int minPhotons = 2;

    @Parameter(label = "Auto-correct IRF offset (decay rise)", persist = true)
    private boolean correctIrfOffset = true;

    @Parameter(label = "Stack frames (collapse to one frame)", persist = true)
    private boolean stackFrames = false;

    @Parameter(type = ItemIO.OUTPUT, label = "Batch summary")
    private GenericTable summary;

    @Override
    public void run() {
        final File[] files = inputFolder == null ? null : inputFolder.listFiles(
                (dir, name) -> {
                    String n = name.toLowerCase(Locale.ROOT);
                    return n.endsWith(".ptu") || n.endsWith(".ht3") || n.endsWith(".spc");
                });
        if (files == null || files.length == 0) {
            log.error("tttrlib: no .ptu/.ht3/.spc files in " + inputFolder);
            return;
        }
        Arrays.sort(files);
        if (!outputFolder.exists() && !outputFolder.mkdirs()) {
            log.error("tttrlib: cannot create output folder " + outputFolder);
            return;
        }

        final GenericColumn fileCol = new GenericColumn("file");
        final GenericColumn dimCol = new GenericColumn("dimensions");
        final GenericColumn statusCol = new GenericColumn("status");

        for (final File f : files) {
            String dims = "";
            String status;
            try {
                final ClsmReconstructor.Params p = new ClsmReconstructor.Params();
                p.path = f.getAbsolutePath();
                p.groups = ClsmReconstructor.parseGroups(channelGroups);
                p.microTimeRanges = ClsmReconstructor.parseGroups(microTimeRanges);
                p.intensity = intensity;
                p.lifetime = fastLifetime;
                p.minPhotons = minPhotons;
                p.correctIrfOffset = correctIrfOffset;
                p.stackFrames = stackFrames;

                final ClsmReconstructor.Result r = ClsmReconstructor.reconstruct(p);
                dims = r.nFrames + "x" + r.nLines + "x" + r.nPixel;

                final String base = stripExtension(f.getName());
                if (intensity) {
                    save(DatasetBuilder.intensity(datasetService, r, base + "_intensity"),
                         base + "_intensity.tif");
                }
                if (fastLifetime) {
                    save(DatasetBuilder.floats(datasetService, r, base + "_fastlifetime",
                                               r.lifetime, r.nOutputFrames),
                         base + "_fastlifetime.tif");
                }
                status = "ok";
            } catch (Exception e) {
                // One bad file must not abort the batch.
                status = "failed: " + e.getMessage();
                log.warn("tttrlib: " + f.getName() + " -> " + status);
            }
            fileCol.add(f.getName());
            dimCol.add(dims);
            statusCol.add(status);
        }

        final DefaultGenericTable t = new DefaultGenericTable();
        t.add(fileCol);
        t.add(dimCol);
        t.add(statusCol);
        summary = t;
        log.info("tttrlib: batch processed " + files.length + " file(s) into " + outputFolder);
    }

    private void save(final Dataset d, final String name) throws Exception {
        if (d == null) return;
        datasetIOService.save(d, new File(outputFolder, name).getAbsolutePath());
    }

    private static String stripExtension(final String name) {
        final int dot = name.lastIndexOf('.');
        return dot > 0 ? name.substring(0, dot) : name;
    }
}
