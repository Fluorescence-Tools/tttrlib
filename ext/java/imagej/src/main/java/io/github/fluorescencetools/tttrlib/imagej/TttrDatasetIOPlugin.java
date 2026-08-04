// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import io.github.fluorescencetools.tttrlib.imagej.core.ClsmReconstructor;

import net.imagej.Dataset;
import net.imagej.DatasetService;

import org.scijava.Priority;
import org.scijava.io.AbstractIOPlugin;
import org.scijava.io.IOPlugin;
import org.scijava.io.location.FileLocation;
import org.scijava.io.location.Location;
import org.scijava.plugin.Parameter;
import org.scijava.plugin.Plugin;

import java.io.File;
import java.io.IOException;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

/**
 * Opens TTTR files as {@link Dataset}s through the SciJava I/O service, so
 * {@code File > Open} and drag-and-drop work on {@code .ptu}, {@code .ht3} and
 * {@code .spc} without going through the menu command.
 *
 * <p>Uses defaults (all routing channels as one group, intensity only, no PIE
 * gating). Use {@code Plugins > tttrlib > Open TTTR CLSM Image} for control over
 * channel groups, micro-time windows, lifetime, phasor and N&amp;B.</p>
 */
@Plugin(type = IOPlugin.class, priority = Priority.HIGH)
public class TttrDatasetIOPlugin extends AbstractIOPlugin<Dataset> {

    /** Extensions claimed by this plugin. */
    private static final Set<String> EXTENSIONS =
            Collections.unmodifiableSet(new HashSet<>(Arrays.asList("ptu", "ht3", "spc")));

    @Parameter
    private DatasetService datasetService;

    @Override
    public Class<Dataset> getDataType() {
        return Dataset.class;
    }

    @Override
    public boolean supportsOpen(final Location source) {
        return pathOf(source) != null;
    }

    @Override
    public boolean supportsOpen(final String source) {
        return hasKnownExtension(source);
    }

    @Override
    public Dataset open(final Location source) throws IOException {
        final String path = pathOf(source);
        if (path == null) throw new IOException("Not a local TTTR file: " + source);
        return open(path);
    }

    @Override
    public Dataset open(final String path) throws IOException {
        final ClsmReconstructor.Params p = new ClsmReconstructor.Params();
        p.path = path;
        // A single group containing every routing channel present in the file.
        p.groups.add(new int[0]);
        p.intensity = true;
        p.lifetime = false;
        p.phasor = false;
        p.numberAndBrightness = false;
        p.decay = false;

        final ClsmReconstructor.Result r;
        try {
            r = ClsmReconstructor.reconstruct(p);
        } catch (ClsmReconstructor.NoImageException e) {
            throw new IOException(e.getMessage(), e);
        }
        final Dataset d = DatasetBuilder.intensity(datasetService, r,
                new File(path).getName());
        if (d != null) d.getProperties().put("tttr.path", path);
        return d;
    }

    private static boolean hasKnownExtension(final String path) {
        if (path == null) return false;
        final int dot = path.lastIndexOf('.');
        if (dot < 0 || dot == path.length() - 1) return false;
        return EXTENSIONS.contains(path.substring(dot + 1).toLowerCase(Locale.ROOT));
    }

    /** Local filesystem path of a location, or {@code null} if not applicable. */
    private static String pathOf(final Location source) {
        if (!(source instanceof FileLocation)) return null;
        final File f = ((FileLocation) source).getFile();
        if (f == null || !hasKnownExtension(f.getName())) return null;
        return f.getAbsolutePath();
    }
}
