// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import io.github.fluorescencetools.tttrlib.imagej.core.ClsmReconstructor;

import net.imagej.Dataset;
import net.imagej.DatasetService;

import org.scijava.ItemIO;
import org.scijava.command.Command;
import org.scijava.log.LogService;
import org.scijava.plugin.Parameter;
import org.scijava.plugin.Plugin;

import java.io.File;

/**
 * Per-pixel mean fluorescence lifetime.
 *
 * <p>Distinct from the reader's <em>FastLifetime</em> output: that is the mean
 * photon arrival time, whereas this is the IRF-corrected first moment of the
 * decay, i.e. an actual lifetime.</p>
 */
@Plugin(type = Command.class, menuPath = "Plugins>tttrlib>FLIM>Lifetime Map",
        headless = true)
public class LifetimeMap implements Command {

    @Parameter
    private DatasetService datasetService;

    @Parameter
    private LogService log;

    @Parameter(required = false)
    private Dataset dataset;

    @Parameter(label = "TTTR file (blank = use the active image's source)",
               required = false, persist = false)
    private File file;

    @Parameter(label = "Channel groups (e.g. 1,3;2,4)", persist = true)
    private String channelGroups = "0";

    @Parameter(label = "Micro-time ranges / PIE (e.g. 0,111;200,499)",
               required = false, persist = true)
    private String microTimeRanges = "";

    @Parameter(label = "Min. photons / pixel", min = "1", persist = true)
    private int minPhotons = 3;

    @Parameter(label = "Stack frames (collapse to one frame)", persist = true)
    private boolean stackFrames = false;

    @Parameter(type = ItemIO.OUTPUT, label = "Lifetime")
    private Dataset lifetimeImage;

    @Override
    public void run() {
        String path = null;
        if (file != null) {
            path = file.getAbsolutePath();
        } else if (dataset != null) {
            Object v = dataset.getProperties().get("tttr.path");
            if (v instanceof String) path = (String) v;
        }
        if (path == null) {
            log.error("tttrlib: no TTTR file selected and no active image with a source path.");
            return;
        }

        final ClsmReconstructor.Params p = new ClsmReconstructor.Params();
        p.path = path;
        p.groups = ClsmReconstructor.parseGroups(channelGroups);
        p.microTimeRanges = ClsmReconstructor.parseGroups(microTimeRanges);
        p.intensity = false;
        p.lifetime = false;
        p.meanLifetime = true;
        p.minPhotons = minPhotons;
        p.stackFrames = stackFrames;

        if (p.groups.isEmpty()) {
            log.error("tttrlib: no channel groups (use e.g. 1,3;2,4).");
            return;
        }

        final ClsmReconstructor.Result r;
        try {
            r = ClsmReconstructor.reconstruct(p);
        } catch (ClsmReconstructor.NoImageException e) {
            log.error("tttrlib: " + e.getMessage());
            return;
        }

        lifetimeImage = DatasetBuilder.floats(datasetService, r,
                new File(path).getName() + " Lifetime", r.meanLifetime, r.nOutputFrames);
        if (lifetimeImage != null) lifetimeImage.getProperties().put("tttr.path", path);
        log.info("tttrlib: lifetime map " + r.nFrames + " frames " + r.nPixel + "x" + r.nLines);
    }
}
