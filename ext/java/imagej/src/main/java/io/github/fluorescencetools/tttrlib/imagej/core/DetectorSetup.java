// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej.core;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import java.io.IOException;
import java.io.Reader;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * A chisurf detector setup: named detectors (routing-channel sets with
 * micro-time gates and polarization corrections) and named PIE windows.
 *
 * <p>File-compatible with chisurf's {@code detector_setups.json}. The document is
 * kept as a {@link JsonObject} rather than mapped onto fields, so keys this code
 * does not model — {@code mle_settings}, {@code fret_calibration},
 * {@code channel_luts}, cached decays — survive a load/save round trip
 * unchanged.</p>
 *
 * <p><b>Units.</b> Window bounds and {@code micro_time_ranges} are RAW micro-time
 * channels of the TTTR file, never nanoseconds and never divided by the
 * micro-time binning. Multiply by {@code tttr_reading.micro_time_resolution} to
 * get nanoseconds.</p>
 *
 * <p><b>Envelope.</b> {@code {"setups": {name: payload}, "last_used": name}}. A
 * bare payload (a single setup, as exported by chisurf's per-setup save) is also
 * accepted and wrapped.</p>
 */
public final class DetectorSetup {

    /** One named detector: routing channels plus gating and polarization terms. */
    public static final class Detector {
        public String name;
        /** Routing channels. */
        public int[] channels = new int[0];
        /** Micro-time gates, OR-combined; empty means ungated. */
        public List<int[]> microTimeRanges = new ArrayList<>();
        public double gFactor = 1.0;
        public double l1 = 0.0;
        public double l2 = 0.0;
        /** Explicit parallel channels, or empty to derive them. */
        public int[] parallelChannels = new int[0];
        /** Explicit perpendicular channels, or empty to derive them. */
        public int[] perpendicularChannels = new int[0];

        /**
         * Parallel channels: {@code ch_p} when set, otherwise the even
         * <em>positions</em> of {@code chs}.
         *
         * <p>Position, not channel number — chisurf's imaging stack splits
         * {@code chs[::2]}/{@code chs[1::2]}. (Its MLE setup parser instead splits
         * on channel-number parity; the two disagree for non-interleaved channel
         * lists, and this follows the imaging convention.)</p>
         */
        public int[] parallel(boolean polarizationResolved) {
            // The setup-level flag outranks a per-detector assignment: a setup
            // declared not polarisation-resolved must not have a split
            // resurrected by a leftover ch_p/ch_s.
            if (!polarizationResolved) return channels.clone();
            if (parallelChannels.length > 0 || perpendicularChannels.length > 0) {
                return parallelChannels;
            }
            return stride(channels, 0);
        }

        /** Perpendicular channels; see {@link #parallel(boolean)}. */
        public int[] perpendicular(boolean polarizationResolved) {
            if (!polarizationResolved) return new int[0];
            if (parallelChannels.length > 0 || perpendicularChannels.length > 0) {
                return perpendicularChannels;
            }
            return stride(channels, 1);
        }

        private static int[] stride(int[] src, int offset) {
            final int n = (src.length - offset + 1) / 2;
            final int[] out = new int[Math.max(n, 0)];
            for (int i = offset, k = 0; i < src.length; i += 2, k++) out[k] = src[i];
            return out;
        }
    }

    /** The raw document, preserved verbatim apart from what is edited. */
    private final JsonObject root;
    /** Name of the setup this view refers to. */
    private final String setupName;

    private DetectorSetup(JsonObject root, String setupName) {
        this.root = root;
        this.setupName = setupName;
    }

    public String setupName() { return setupName; }

    /** Names of every setup in the document. */
    public List<String> setupNames() {
        final List<String> out = new ArrayList<>();
        final JsonObject setups = root.getAsJsonObject("setups");
        if (setups != null) for (String k : setups.keySet()) out.add(k);
        return out;
    }

    private JsonObject payload() {
        final JsonObject setups = root.getAsJsonObject("setups");
        if (setups == null) return new JsonObject();
        final JsonElement e = setups.get(setupName);
        return e != null && e.isJsonObject() ? e.getAsJsonObject() : new JsonObject();
    }

    /** Setups are polarization-resolved unless the file says otherwise. */
    public boolean polarizationResolved() {
        final JsonElement e = payload().get("polarization_resolved");
        return e == null || !e.isJsonPrimitive() || e.getAsBoolean();
    }

    /** PIE windows, in insertion order. Values are raw micro-time channels. */
    public Map<String, int[]> windows() {
        final Map<String, int[]> out = new LinkedHashMap<>();
        final JsonObject w = payload().getAsJsonObject("windows");
        if (w == null) return out;
        for (String k : w.keySet()) {
            final int[] r = pair(w.get(k));
            if (r != null) out.put(k, r);
        }
        return out;
    }

    /** Detectors, in insertion order. */
    public Map<String, Detector> detectors() {
        final Map<String, Detector> out = new LinkedHashMap<>();
        final JsonObject d = payload().getAsJsonObject("detectors");
        if (d == null) return out;
        for (String k : d.keySet()) {
            final JsonElement e = d.get(k);
            if (!e.isJsonObject()) continue;
            final JsonObject o = e.getAsJsonObject();
            final Detector det = new Detector();
            det.name = k;
            det.channels = ints(o.get("chs"));
            det.microTimeRanges = pairs(o.get("micro_time_ranges"));
            det.gFactor = dbl(o.get("g_factor"), 1.0);
            det.l1 = dbl(o.get("l1"), 0.0);
            det.l2 = dbl(o.get("l2"), 0.0);
            det.parallelChannels = ints(o.get("ch_p"));
            det.perpendicularChannels = ints(o.get("ch_s"));
            out.put(k, det);
        }
        return out;
    }

    /** Micro-time resolution in nanoseconds, or &lt;= 0 when the file omits it. */
    public double microTimeResolutionNs() {
        final JsonObject t = payload().getAsJsonObject("tttr_reading");
        if (t == null) return -1.0;
        return dbl(t.get("micro_time_resolution"), -1.0);
    }

    /**
     * Effective micro-time gates for a detector within a window: each of the
     * detector's ranges intersected with the window. A detector with no ranges is
     * ungated and yields the window itself.
     *
     * @param window window name, or {@code null} for no window restriction
     * @return possibly empty when the detector's gates lie outside the window
     */
    public List<int[]> effectiveRanges(String detectorName, String window) {
        final Detector det = detectors().get(detectorName);
        if (det == null) return new ArrayList<>();
        final int[] win = window == null ? null : windows().get(window);

        final List<int[]> out = new ArrayList<>();
        if (det.microTimeRanges.isEmpty()) {
            if (win != null) out.add(new int[] { win[0], win[1] });
            return out;
        }
        for (int[] r : det.microTimeRanges) {
            if (win == null) {
                out.add(new int[] { r[0], r[1] });
            } else {
                final int lo = Math.max(r[0], win[0]);
                final int hi = Math.min(r[1], win[1]);
                if (hi > lo) out.add(new int[] { lo, hi });
            }
        }
        return out;
    }

    /**
     * Expand this setup into one reconstruction channel per
     * {@code <window>_<detector>} combination, matching chisurf's derived
     * {@code channels} map.
     *
     * @param detectorNames detectors to include, or {@code null}/empty for all
     * @param windowNames   PIE windows to include, or {@code null}/empty for all
     *                      (and if the setup defines none, a single ungated pass)
     * @param splitPolarization emit separate parallel and perpendicular channels
     *                      instead of one channel per detector
     */
    public List<ClsmReconstructor.Selection> selections(List<String> detectorNames,
                                                        List<String> windowNames,
                                                        boolean splitPolarization) {
        final Map<String, Detector> dets = detectors();
        final Map<String, int[]> wins = windows();
        final boolean pol = polarizationResolved();

        final List<String> useDets = (detectorNames == null || detectorNames.isEmpty())
                ? new ArrayList<>(dets.keySet()) : detectorNames;
        final List<String> useWins = (windowNames == null || windowNames.isEmpty())
                ? new ArrayList<>(wins.keySet()) : windowNames;

        final List<ClsmReconstructor.Selection> out = new ArrayList<>();
        // No windows at all: one ungated pass per detector, using its own gates.
        final List<String> windowLoop = useWins.isEmpty()
                ? java.util.Collections.<String>singletonList(null) : useWins;

        for (String wname : windowLoop) {
            for (String dname : useDets) {
                final Detector det = dets.get(dname);
                if (det == null) continue;
                final List<int[]> ranges = effectiveRanges(dname, wname);
                // Two different reasons for an empty result, and they must not be
                // conflated: an ungated detector outside any window accepts every
                // arrival time (null range), whereas a detector whose gates miss
                // the window entirely contributes nothing at all.
                final boolean ungated = det.microTimeRanges.isEmpty() && wname == null;
                if (ranges.isEmpty() && !ungated) continue;
                final List<int[]> loop = ranges.isEmpty()
                        ? java.util.Collections.<int[]>singletonList(null) : ranges;

                for (int i = 0; i < loop.size(); i++) {
                    final int[] range = loop.get(i);
                    final String suffix = loop.size() > 1 ? ("[" + i + "]") : "";
                    final String base = (wname == null ? dname : wname + "_" + dname) + suffix;
                    if (splitPolarization) {
                        final int[] par = det.parallel(pol);
                        final int[] perp = det.perpendicular(pol);
                        if (par.length > 0) {
                            out.add(new ClsmReconstructor.Selection(base + "_p", par, range));
                        }
                        if (perp.length > 0) {
                            out.add(new ClsmReconstructor.Selection(base + "_s", perp, range));
                        }
                    } else {
                        out.add(new ClsmReconstructor.Selection(base, det.channels.clone(), range));
                    }
                }
            }
        }
        return out;
    }

    // ── I/O ────────────────────────────────────────────────────────────────

    /** Load a setup file and select {@code last_used}, or the first setup. */
    public static DetectorSetup load(String path) throws IOException {
        return load(path, null);
    }

    /**
     * @param setupName which setup to view, or {@code null} for {@code last_used}
     *                  (falling back to the first one present)
     */
    public static DetectorSetup load(String path, String setupName) throws IOException {
        try (Reader r = Files.newBufferedReader(Paths.get(path), StandardCharsets.UTF_8)) {
            JsonObject doc = JsonParser.parseReader(r).getAsJsonObject();
            // A bare payload (chisurf's standalone per-setup export) has no
            // "setups" envelope; wrap it so both forms load.
            if (!doc.has("setups")) {
                final JsonObject setups = new JsonObject();
                final String n = doc.has("setup_name")
                        ? doc.get("setup_name").getAsString() : "default";
                setups.add(n, doc);
                final JsonObject wrapped = new JsonObject();
                wrapped.add("setups", setups);
                wrapped.addProperty("last_used", n);
                doc = wrapped;
            }
            String name = setupName;
            if (name == null && doc.has("last_used") && doc.get("last_used").isJsonPrimitive()) {
                name = doc.get("last_used").getAsString();
            }
            final JsonObject setups = doc.getAsJsonObject("setups");
            if (name == null || setups == null || !setups.has(name)) {
                name = (setups != null && !setups.keySet().isEmpty())
                        ? setups.keySet().iterator().next() : "default";
            }
            return new DetectorSetup(doc, name);
        }
    }

    /**
     * Write the document back, regenerating the derived {@code channels} cache so
     * a chisurf reader does not see a stale cross-product.
     */
    public void save(String path) throws IOException {
        regenerateChannels();
        final Gson gson = new GsonBuilder().setPrettyPrinting().create();
        Path p = Paths.get(path);
        if (p.getParent() != null) Files.createDirectories(p.getParent());
        try (Writer w = Files.newBufferedWriter(p, StandardCharsets.UTF_8)) {
            gson.toJson(root, w);
        }
    }

    /**
     * Rebuild {@code channels} as the window x detector cross-product, matching
     * chisurf's {@code get_settings()}. Keys are {@code "<window>_<detector>"}.
     */
    void regenerateChannels() {
        final JsonObject payload = payload();
        final Map<String, int[]> wins = windows();
        final Map<String, Detector> dets = detectors();
        final JsonObject channels = new JsonObject();
        for (Map.Entry<String, int[]> w : wins.entrySet()) {
            for (Map.Entry<String, Detector> d : dets.entrySet()) {
                final JsonArray entries = new JsonArray();
                final List<int[]> ranges = d.getValue().microTimeRanges.isEmpty()
                        ? java.util.Collections.singletonList(w.getValue())
                        : d.getValue().microTimeRanges;
                for (int[] r : ranges) {
                    final JsonObject e = new JsonObject();
                    e.add("window_range", arr(w.getValue()));
                    e.add("detector_chs", arr(d.getValue().channels));
                    e.add("micro_time_range", arr(r));
                    entries.add(e);
                }
                channels.add(w.getKey() + "_" + d.getKey(), entries);
            }
        }
        payload.add("channels", channels);
    }

    /** Replace (or add) a detector, preserving any keys this class does not model. */
    public void putDetector(Detector det) {
        final JsonObject payload = payload();
        JsonObject dets = payload.getAsJsonObject("detectors");
        if (dets == null) {
            dets = new JsonObject();
            payload.add("detectors", dets);
        }
        final JsonObject o = dets.has(det.name) && dets.get(det.name).isJsonObject()
                ? dets.getAsJsonObject(det.name) : new JsonObject();
        o.add("chs", arr(det.channels));
        final JsonArray ranges = new JsonArray();
        for (int[] r : det.microTimeRanges) ranges.add(arr(r));
        o.add("micro_time_ranges", ranges);
        o.addProperty("g_factor", det.gFactor);
        o.addProperty("l1", det.l1);
        o.addProperty("l2", det.l2);
        if (det.parallelChannels.length > 0) o.add("ch_p", arr(det.parallelChannels));
        if (det.perpendicularChannels.length > 0) o.add("ch_s", arr(det.perpendicularChannels));
        dets.add(det.name, o);
    }

    /**
     * Set the setup-level polarisation flag. When {@code false} a detector's
     * channels are one unpolarised stream rather than interleaved VV/VH.
     */
    public void setPolarizationResolved(boolean resolved) {
        payload().addProperty("polarization_resolved", resolved);
    }

    /** Replace (or add) a PIE window. */
    public void putWindow(String name, int start, int end) {
        final JsonObject payload = payload();
        JsonObject wins = payload.getAsJsonObject("windows");
        if (wins == null) {
            wins = new JsonObject();
            payload.add("windows", wins);
        }
        wins.add(name, arr(new int[] { start, end }));
    }

    /** An empty setup with chisurf's default prompt/delayed windows. */
    public static DetectorSetup empty(String name) {
        final JsonObject payload = new JsonObject();
        payload.addProperty("setup_name", name);
        payload.addProperty("polarization_resolved", true);
        payload.add("windows", new JsonObject());
        payload.add("detectors", new JsonObject());
        final JsonObject setups = new JsonObject();
        setups.add(name, payload);
        final JsonObject doc = new JsonObject();
        doc.add("setups", setups);
        doc.addProperty("last_used", name);
        return new DetectorSetup(doc, name);
    }

    // ── json helpers ───────────────────────────────────────────────────────

    private static JsonArray arr(int[] v) {
        final JsonArray a = new JsonArray();
        for (int x : v) a.add(x);
        return a;
    }

    private static int[] ints(JsonElement e) {
        if (e == null || !e.isJsonArray()) return new int[0];
        final JsonArray a = e.getAsJsonArray();
        final int[] out = new int[a.size()];
        for (int i = 0; i < a.size(); i++) out[i] = a.get(i).getAsInt();
        return out;
    }

    private static int[] pair(JsonElement e) {
        final int[] v = ints(e);
        return v.length >= 2 ? new int[] { v[0], v[1] } : null;
    }

    private static List<int[]> pairs(JsonElement e) {
        final List<int[]> out = new ArrayList<>();
        if (e == null || !e.isJsonArray()) return out;
        for (JsonElement x : e.getAsJsonArray()) {
            final int[] p = pair(x);
            if (p != null) out.add(p);
        }
        return out;
    }

    private static double dbl(JsonElement e, double dflt) {
        return e != null && e.isJsonPrimitive() ? e.getAsDouble() : dflt;
    }
}
