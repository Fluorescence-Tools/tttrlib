// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;

/**
 * Loads the native JNI library {@code tttrlibjni}. First tries the normal
 * {@code java.library.path}; if that fails, extracts the platform-specific
 * native from the JAR ({@code /native/<os>-<arch>/}) to a temp file and loads
 * it, so a self-contained JAR works without configuring java.library.path.
 */
public final class NativeLoader {
    private static boolean loaded = false;

    private NativeLoader() {}

    public static synchronized void load() {
        if (loaded) return;
        try {
            System.loadLibrary("tttrlibjni");
            loaded = true;
            return;
        } catch (UnsatisfiedLinkError ignored) {
            // fall through to extraction
        }
        try {
            String resource = "/native/" + platformDir() + "/" + mapLibraryName();
            InputStream in = NativeLoader.class.getResourceAsStream(resource);
            if (in == null) {
                throw new UnsatisfiedLinkError("Bundled native not found: " + resource);
            }
            File tmp = File.createTempFile("tttrlibjni", suffix());
            tmp.deleteOnExit();
            try (FileOutputStream out = new FileOutputStream(tmp)) {
                byte[] buf = new byte[1 << 16];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            } finally {
                in.close();
            }
            System.load(tmp.getAbsolutePath());
            loaded = true;
        } catch (IOException e) {
            throw new UnsatisfiedLinkError("Failed to extract native tttrlibjni: " + e.getMessage());
        }
    }

    private static String platformDir() {
        String os = System.getProperty("os.name").toLowerCase();
        String arch = System.getProperty("os.arch").toLowerCase();
        if (arch.equals("amd64")) arch = "x86-64";
        if (arch.equals("x86_64")) arch = "x86-64";
        if (arch.equals("arm64")) arch = "aarch64";   // normalise Apple/ARM naming
        if (os.contains("win")) return "win32-" + arch;
        if (os.contains("mac")) return "darwin-" + arch;
        return "linux-" + arch;
    }

    private static String mapLibraryName() {
        return System.mapLibraryName("tttrlibjni"); // libtttrlibjni.so / .dylib / tttrlibjni.dll
    }

    private static String suffix() {
        String n = mapLibraryName();
        int dot = n.lastIndexOf('.');
        return dot >= 0 ? n.substring(dot) : ".so";
    }
}
