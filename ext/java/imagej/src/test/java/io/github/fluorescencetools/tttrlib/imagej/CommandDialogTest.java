// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej;

import org.junit.jupiter.api.Test;

import org.scijava.plugin.Parameter;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Static checks on the dialog declarations.
 *
 * <p>The other tests invoke commands through {@code CommandService} with an
 * explicit parameter map, which bypasses the input harvester entirely — so a
 * declaration that is fine for a programmatic call but breaks when Swing builds
 * the dialog goes unnoticed. That is exactly how an empty-string entry in
 * {@code ImageCorrelation}'s "Subtract average" choices shipped: it threw a
 * NullPointerException inside SwingChoiceWidget while the panel was being built,
 * so the command could not be opened from the menu at all.</p>
 */
class CommandDialogTest {

    private static final Class<?>[] COMMANDS = {
        OpenClsmImage.class, DecayFromMask.class, ShowTttrMetadata.class,
        BatchProcessFolder.class, LifetimeMap.class, FitDecayPerPixel.class,
        PhasorPlot.class, PixelFcs.class, ImageCorrelation.class,
        DefineDetectors.class, TttrlibSettings.class,
    };

    @Test
    void noChoiceListContainsAnEmptyOrNullEntry() {
        final List<String> bad = new ArrayList<>();
        for (Class<?> c : COMMANDS) {
            for (Field f : c.getDeclaredFields()) {
                final Parameter p = f.getAnnotation(Parameter.class);
                if (p == null) continue;
                for (String choice : p.choices()) {
                    if (choice == null || choice.trim().isEmpty()) {
                        bad.add(c.getSimpleName() + "." + f.getName());
                    }
                }
            }
        }
        assertTrue(bad.isEmpty(),
                "empty choice entries break SwingChoiceWidget when the dialog is "
                        + "built, so the command cannot be opened: " + bad);
    }

    /** A declared default must be one of the offered choices. */
    @Test
    void choiceDefaultsAreAmongTheChoices() throws Exception {
        final List<String> bad = new ArrayList<>();
        for (Class<?> c : COMMANDS) {
            final Object instance = c.getDeclaredConstructor().newInstance();
            for (Field f : c.getDeclaredFields()) {
                final Parameter p = f.getAnnotation(Parameter.class);
                if (p == null || p.choices().length == 0) continue;
                f.setAccessible(true);
                final Object value = f.get(instance);
                if (value == null) continue;
                boolean found = false;
                for (String choice : p.choices()) {
                    if (choice.equals(value)) { found = true; break; }
                }
                if (!found) {
                    bad.add(c.getSimpleName() + "." + f.getName() + " = '" + value + "'");
                }
            }
        }
        assertTrue(bad.isEmpty(), "default is not one of the choices: " + bad);
    }
}
