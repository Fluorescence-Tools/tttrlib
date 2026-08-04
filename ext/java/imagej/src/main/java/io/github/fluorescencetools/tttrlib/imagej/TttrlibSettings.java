// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import org.scijava.ItemIO;
import org.scijava.ItemVisibility;
import org.scijava.command.Command;
import org.scijava.log.LogService;
import org.scijava.plugin.Parameter;
import org.scijava.plugin.Plugin;
import org.scijava.prefs.PrefService;
import org.scijava.table.DefaultGenericTable;
import org.scijava.table.GenericColumn;
import org.scijava.table.GenericTable;

import java.io.File;
import java.io.Reader;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

/**
 * Plugin-wide defaults, remembered between Fiji sessions and exportable as JSON.
 *
 * <p>Individual dialog fields already persist on their own — every
 * {@code @Parameter(persist = true)} is stored per user by SciJava's
 * {@code PrefService}. This command covers what that cannot: settings shared
 * across several commands (above all the detector setup file), and moving a
 * configuration between machines or into version control.</p>
 */
@Plugin(type = Command.class, menuPath = "Plugins>tttrlib>Settings...",
        headless = true)
public class TttrlibSettings implements Command {

    /** Preference keys, shared with the commands that read these defaults. */
    public static final String KEY_SETUP_FILE = "tttrlib.setupFile";
    public static final String KEY_DETECTORS = "tttrlib.detectors";
    public static final String KEY_WINDOWS = "tttrlib.windows";
    public static final String KEY_SPLIT_POL = "tttrlib.splitPolarization";
    public static final String KEY_MIN_PHOTONS = "tttrlib.minPhotons";
    public static final String KEY_CORRECT_IRF = "tttrlib.correctIrfOffset";

    @Parameter
    private PrefService prefs;

    @Parameter
    private LogService log;

    @Parameter(label = "Action",
               choices = { "Show current", "Apply the values below",
                           "Save to file", "Load from file", "Reset to defaults" },
               persist = false)
    private String action = "Show current";

    @Parameter(label = "Settings file (for Save / Load)", required = false, persist = false)
    private File settingsFile;

    @Parameter(visibility = ItemVisibility.MESSAGE, required = false)
    private String note = "The values below are used by 'Apply the values below'.";

    @Parameter(label = "Default detector setup (chisurf JSON)",
               required = false, persist = false)
    private File defaultSetupFile;

    @Parameter(label = "Default detectors (blank = all)", required = false, persist = false)
    private String defaultDetectors = "";

    @Parameter(label = "Default PIE windows (blank = all)", required = false, persist = false)
    private String defaultWindows = "";

    @Parameter(label = "Split parallel / perpendicular by default", persist = false)
    private boolean splitPolarization = false;

    @Parameter(label = "Default min. photons / pixel", min = "0", persist = false)
    private int minPhotons = 2;

    @Parameter(label = "Auto-correct IRF offset by default", persist = false)
    private boolean correctIrfOffset = true;

    @Parameter(type = ItemIO.OUTPUT, label = "tttrlib settings")
    private GenericTable settings;

    @Override
    public void run() {
        try {
            switch (action) {
                case "Apply the values below":
                    store();
                    log.info("tttrlib: settings saved for this user.");
                    break;
                case "Save to file":
                    if (settingsFile == null) {
                        log.error("tttrlib: choose a settings file to save to.");
                        return;
                    }
                    store();
                    writeJson(settingsFile);
                    log.info("tttrlib: settings written to " + settingsFile);
                    break;
                case "Load from file":
                    if (settingsFile == null || !settingsFile.exists()) {
                        log.error("tttrlib: settings file not found: " + settingsFile);
                        return;
                    }
                    readJson(settingsFile);
                    log.info("tttrlib: settings loaded from " + settingsFile);
                    break;
                case "Reset to defaults":
                    for (String k : new String[] { KEY_SETUP_FILE, KEY_DETECTORS, KEY_WINDOWS,
                                                   KEY_SPLIT_POL, KEY_MIN_PHOTONS,
                                                   KEY_CORRECT_IRF }) {
                        prefs.remove(TttrlibSettings.class, k);
                    }
                    log.info("tttrlib: settings reset.");
                    break;
                default:
                    break;   // "Show current" only reports
            }
        } catch (Exception e) {
            log.error("tttrlib: " + e);
            return;
        }
        settings = report();
    }

    private void store() {
        prefs.put(TttrlibSettings.class, KEY_SETUP_FILE,
                defaultSetupFile == null ? "" : defaultSetupFile.getAbsolutePath());
        prefs.put(TttrlibSettings.class, KEY_DETECTORS, nz(defaultDetectors));
        prefs.put(TttrlibSettings.class, KEY_WINDOWS, nz(defaultWindows));
        prefs.put(TttrlibSettings.class, KEY_SPLIT_POL, splitPolarization);
        prefs.put(TttrlibSettings.class, KEY_MIN_PHOTONS, minPhotons);
        prefs.put(TttrlibSettings.class, KEY_CORRECT_IRF, correctIrfOffset);
    }

    private void writeJson(File f) throws Exception {
        final JsonObject o = new JsonObject();
        o.addProperty(KEY_SETUP_FILE, setupFile(prefs));
        o.addProperty(KEY_DETECTORS, prefs.get(TttrlibSettings.class, KEY_DETECTORS, ""));
        o.addProperty(KEY_WINDOWS, prefs.get(TttrlibSettings.class, KEY_WINDOWS, ""));
        o.addProperty(KEY_SPLIT_POL, prefs.getBoolean(TttrlibSettings.class, KEY_SPLIT_POL, false));
        o.addProperty(KEY_MIN_PHOTONS, prefs.getInt(TttrlibSettings.class, KEY_MIN_PHOTONS, 2));
        o.addProperty(KEY_CORRECT_IRF,
                prefs.getBoolean(TttrlibSettings.class, KEY_CORRECT_IRF, true));
        final Gson gson = new GsonBuilder().setPrettyPrinting().create();
        final Path p = f.toPath();
        if (p.getParent() != null) Files.createDirectories(p.getParent());
        try (Writer w = Files.newBufferedWriter(p, StandardCharsets.UTF_8)) {
            gson.toJson(o, w);
        }
    }

    private void readJson(File f) throws Exception {
        try (Reader r = Files.newBufferedReader(Paths.get(f.getAbsolutePath()),
                                                StandardCharsets.UTF_8)) {
            final JsonObject o = JsonParser.parseReader(r).getAsJsonObject();
            if (o.has(KEY_SETUP_FILE)) {
                prefs.put(TttrlibSettings.class, KEY_SETUP_FILE,
                        o.get(KEY_SETUP_FILE).getAsString());
            }
            if (o.has(KEY_DETECTORS)) {
                prefs.put(TttrlibSettings.class, KEY_DETECTORS,
                        o.get(KEY_DETECTORS).getAsString());
            }
            if (o.has(KEY_WINDOWS)) {
                prefs.put(TttrlibSettings.class, KEY_WINDOWS,
                        o.get(KEY_WINDOWS).getAsString());
            }
            if (o.has(KEY_SPLIT_POL)) {
                prefs.put(TttrlibSettings.class, KEY_SPLIT_POL,
                        o.get(KEY_SPLIT_POL).getAsBoolean());
            }
            if (o.has(KEY_MIN_PHOTONS)) {
                prefs.put(TttrlibSettings.class, KEY_MIN_PHOTONS,
                        o.get(KEY_MIN_PHOTONS).getAsInt());
            }
            if (o.has(KEY_CORRECT_IRF)) {
                prefs.put(TttrlibSettings.class, KEY_CORRECT_IRF,
                        o.get(KEY_CORRECT_IRF).getAsBoolean());
            }
        }
    }

    private GenericTable report() {
        final GenericColumn k = new GenericColumn("setting");
        final GenericColumn v = new GenericColumn("value");
        k.add(KEY_SETUP_FILE);   v.add(setupFile(prefs));
        k.add(KEY_DETECTORS);    v.add(prefs.get(TttrlibSettings.class, KEY_DETECTORS, ""));
        k.add(KEY_WINDOWS);      v.add(prefs.get(TttrlibSettings.class, KEY_WINDOWS, ""));
        k.add(KEY_SPLIT_POL);
        v.add(String.valueOf(prefs.getBoolean(TttrlibSettings.class, KEY_SPLIT_POL, false)));
        k.add(KEY_MIN_PHOTONS);
        v.add(String.valueOf(prefs.getInt(TttrlibSettings.class, KEY_MIN_PHOTONS, 2)));
        k.add(KEY_CORRECT_IRF);
        v.add(String.valueOf(prefs.getBoolean(TttrlibSettings.class, KEY_CORRECT_IRF, true)));
        final DefaultGenericTable t = new DefaultGenericTable();
        t.add(k);
        t.add(v);
        return t;
    }

    private static String nz(String s) { return s == null ? "" : s.trim(); }

    /**
     * The remembered detector setup path, or {@code ""}. Commands call this so a
     * blank setup field falls back to the user's default.
     */
    public static String setupFile(PrefService prefs) {
        return prefs.get(TttrlibSettings.class, KEY_SETUP_FILE, "");
    }
}
