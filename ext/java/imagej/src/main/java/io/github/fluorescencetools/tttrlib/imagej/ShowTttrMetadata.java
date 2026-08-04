// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import io.github.fluorescencetools.tttrlib.NativeLoader;
import io.github.fluorescencetools.tttrlib.TTTR;
import io.github.fluorescencetools.tttrlib.TTTRHeader;

import net.imagej.Dataset;

import org.scijava.ItemIO;
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

/**
 * Shows the acquisition metadata of a TTTR file as a table: the header tags plus
 * the derived quantities most often needed when setting up an analysis
 * (micro-time channel count and resolution, macro time resolution, photon count,
 * routing channels present).
 */
@Plugin(type = Command.class, menuPath = "Plugins>tttrlib>Show TTTR Metadata",
        headless = true)
public class ShowTttrMetadata implements Command {

    static { NativeLoader.load(); }

    @Parameter
    private LogService log;

    /** Active dataset; used for its {@code tttr.path} property. */
    @Parameter(required = false)
    private Dataset dataset;

    @Parameter(label = "TTTR file (blank = use the active image's source)",
               required = false, persist = false)
    private File file;

    @Parameter(type = ItemIO.OUTPUT, label = "TTTR metadata")
    private GenericTable metadata;

    /** Raw header JSON, for scripts that want more than the table shows. */
    @Parameter(type = ItemIO.OUTPUT, label = "Header JSON")
    private String headerJson;

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

        final TTTR tttr = new TTTR(path);
        final TTTRHeader header = tttr.get_header();
        headerJson = header.get_json();

        final List<String[]> rows = new ArrayList<>();
        rows.add(new String[] { "file", path });
        rows.add(new String[] { "n_photons", Long.toString(tttr.size()) });
        rows.add(new String[] { "n_micro_time_channels",
                Long.toString(tttr.get_number_of_micro_time_channels()) });
        rows.add(new String[] { "micro_time_resolution_ns",
                Double.toString(tttr.get_micro_time_resolution_s() * 1e9) });
        rows.add(new String[] { "routing_channels", channelList(tttr) });

        final DefaultGenericTable t = new DefaultGenericTable();
        final GenericColumn key = new GenericColumn("property");
        final GenericColumn val = new GenericColumn("value");
        for (String[] row : rows) {
            key.add(row[0]);
            val.add(row[1]);
        }
        t.add(key);
        t.add(val);
        metadata = t;
    }

    private static String channelList(TTTR tttr) {
        // Zero-length probe returns the true count, so the buffer is sized exactly.
        int n = tttr.get_used_routing_channels_into(new int[0]);
        if (n <= 0) return "";
        int[] buf = new int[n];
        int written = tttr.get_used_routing_channels_into(buf);
        int[] ch = java.util.Arrays.copyOf(buf, Math.min(written, buf.length));
        java.util.Arrays.sort(ch);
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < ch.length; i++) {
            if (i > 0) b.append(", ");
            b.append(ch[i]);
        }
        return b.toString();
    }
}
