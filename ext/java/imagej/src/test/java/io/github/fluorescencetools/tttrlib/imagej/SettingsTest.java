// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import org.scijava.Context;
import org.scijava.command.CommandModule;
import org.scijava.command.CommandService;
import org.scijava.log.LogService;
import org.scijava.prefs.PrefService;
import org.scijava.table.GenericTable;

import java.io.File;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** User settings: persisted via PrefService, and exportable/importable as JSON. */
class SettingsTest {

    private static Context context;

    @BeforeAll
    static void setUp() {
        context = new Context(CommandService.class, PrefService.class, LogService.class);
    }

    @AfterAll
    static void tearDown() {
        if (context != null) {
            context.service(PrefService.class).clear(TttrlibSettings.class);
            context.dispose();
        }
    }

    private static Map<String, Object> base() {
        final Map<String, Object> p = new HashMap<>();
        p.put("settingsFile", null);
        p.put("defaultSetupFile", null);
        p.put("defaultDetectors", "");
        p.put("defaultWindows", "");
        p.put("splitPolarization", false);
        p.put("minPhotons", 2);
        p.put("correctIrfOffset", true);
        return p;
    }

    private static GenericTable run(Map<String, Object> p) throws Exception {
        final CommandModule m = context.service(CommandService.class)
                .run(TttrlibSettings.class, true, p).get();
        return (GenericTable) m.getOutput("settings");
    }

    private static String value(GenericTable t, String key) {
        for (int r = 0; r < t.getRowCount(); r++) {
            if (key.equals(String.valueOf(t.get(0, r)))) return String.valueOf(t.get(1, r));
        }
        return null;
    }

    @Test
    void settingsPersistAndRoundTripThroughJson(@TempDir Path dir) throws Exception {
        final File setup = dir.resolve("my_setup.json").toFile();
        final File out = dir.resolve("tttrlib_settings.json").toFile();

        // Apply, then confirm the values are remembered for this user.
        Map<String, Object> p = base();
        p.put("action", "Apply the values below");
        p.put("defaultSetupFile", setup);
        p.put("defaultDetectors", "green,red");
        p.put("splitPolarization", true);
        p.put("minPhotons", 7);
        GenericTable t = run(p);
        assertEquals(setup.getAbsolutePath(), value(t, TttrlibSettings.KEY_SETUP_FILE));
        assertEquals("green,red", value(t, TttrlibSettings.KEY_DETECTORS));
        assertEquals("true", value(t, TttrlibSettings.KEY_SPLIT_POL));
        assertEquals("7", value(t, TttrlibSettings.KEY_MIN_PHOTONS));

        // A fresh command instance must see them (this is the persistence).
        p = base();
        p.put("action", "Show current");
        t = run(p);
        assertEquals("7", value(t, TttrlibSettings.KEY_MIN_PHOTONS));

        // Export.
        p = base();
        p.put("action", "Save to file");
        p.put("settingsFile", out);
        p.put("defaultSetupFile", setup);
        p.put("defaultDetectors", "green,red");
        p.put("splitPolarization", true);
        p.put("minPhotons", 7);
        run(p);
        assertTrue(out.exists(), "settings file not written");

        // Wipe, then re-import.
        p = base();
        p.put("action", "Reset to defaults");
        t = run(p);
        assertEquals("2", value(t, TttrlibSettings.KEY_MIN_PHOTONS), "reset did not take");

        p = base();
        p.put("action", "Load from file");
        p.put("settingsFile", out);
        t = run(p);
        assertEquals("7", value(t, TttrlibSettings.KEY_MIN_PHOTONS));
        assertEquals("green,red", value(t, TttrlibSettings.KEY_DETECTORS));
        assertEquals(setup.getAbsolutePath(), value(t, TttrlibSettings.KEY_SETUP_FILE));
        assertNotNull(t);
    }
}
