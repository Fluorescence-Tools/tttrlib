// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.tests;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;

import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.Assumptions;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.MethodSource;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import static org.junit.jupiter.api.Assertions.fail;

/**
 * The Java runner of the cross-language conformance suite.
 *
 * <p>One test per case in {@code test/conformance/cases/}, driven by
 * {@link ConformanceInterpreter}. The expected values are the committed ones —
 * a failure means the Java binding disagrees with Python, R and JavaScript,
 * never that the number needs updating. See {@code test/conformance/README.md}.
 */
class ConformanceTest {

    private static final ObjectMapper MAPPER = new ObjectMapper();
    private static final Path REPO = repoRoot();
    private static final Path CASES = REPO.resolve("test/conformance/cases");
    private static final String DATA_ROOT = dataRoot();

    /** 2^53: past here an IEEE double no longer holds every integer. */
    private static final double MAX_EXACT_INTEGER = 9007199254740992.0;

    private static final List<ObjectNode> REPORT = Collections.synchronizedList(new ArrayList<>());

    // -- discovery -----------------------------------------------------------

    private static Path repoRoot() {
        // The Maven module is ext/java/pkg, so the repository is three up.
        return Paths.get(System.getProperty("user.dir")).toAbsolutePath()
                   .resolve("../../..").normalize();
    }

    private static String dataRoot() {
        String env = System.getenv("TTTRLIB_DATA");
        if (env != null && !env.trim().isEmpty())
            return Paths.get(env.trim().replaceAll("^['\"]|['\"]$", "")).toAbsolutePath().toString();
        return REPO.resolve("tttr-data").toString();
    }

    /** Every case, paired with the area it came from. */
    static List<Object[]> cases() throws IOException {
        List<Object[]> out = new ArrayList<>();
        if (!Files.isDirectory(CASES)) return out;
        List<Path> files = new ArrayList<>();
        try (java.util.stream.Stream<Path> s = Files.list(CASES)) {
            s.filter(p -> p.toString().endsWith(".json")).sorted().forEach(files::add);
        }
        for (Path f : files) {
            JsonNode doc = MAPPER.readTree(f.toFile());
            String area = doc.get("area").asText();
            for (JsonNode c : doc.get("cases")) out.add(new Object[]{c.get("id").asText(), area, c});
        }
        return out;
    }

    // -- the test ------------------------------------------------------------

    @ParameterizedTest(name = "{0}")
    @MethodSource("cases")
    @DisplayName("conformance")
    void conformance(String id, String area, JsonNode case_) throws Exception {
        JsonNode unsupported = case_.get("unsupported");
        if (unsupported != null && unsupported.has("java")) {
            String why = unsupported.get("java").asText();
            record(id, area, "unsupported", why);
            Assumptions.abort("declared unsupported in Java: " + why);
        }

        List<String> dataFiles = new ArrayList<>();
        if (case_.has("data")) for (JsonNode d : case_.get("data")) dataFiles.add(d.asText());
        List<String> missing = new ArrayList<>();
        for (String d : dataFiles)
            if (!Files.exists(Paths.get(DATA_ROOT, d))) missing.add(d);
        if (!missing.isEmpty()) {
            String why = "missing " + String.join(", ", missing);
            record(id, area, "skip", why);
            Assumptions.abort("test data not present: " + why);
        }

        checkComparable(id, case_);

        Path tmpDir = Files.createTempDirectory("tttrlib-conf-");
        try {
            int nTmp = case_.has("tmp") ? case_.get("tmp").asInt() : 0;
            List<String> tmp = new ArrayList<>();
            for (int k = 0; k < nTmp; k++) tmp.add(tmpDir.resolve("scratch" + k).toString());

            ConformanceInterpreter interp =
                    new ConformanceInterpreter(DATA_ROOT, tmp, dataFiles);
            try {
                interp.run(case_.get("steps"));
            } catch (ConformanceInterpreter.UnsupportedOpException e) {
                record(id, area, "unsupported", "op '" + e.op + "' not implemented");
                Assumptions.abort("op '" + e.op + "' is not implemented in the Java runner");
            }

            JsonNode expect = case_.get("expect");
            JsonNode tolerance = case_.get("tolerance");
            List<String> failures = new ArrayList<>();
            expect.fieldNames().forEachRemaining(key -> {
                if (!interp.bindings.containsKey(key)) {
                    failures.add(key + ": the steps never bound it");
                    return;
                }
                double tol = (tolerance != null && tolerance.has(key))
                        ? tolerance.get(key).asDouble() : 0.0;
                String why = compare(expect.get(key), interp.bindings.get(key), tol);
                if (why != null) failures.add(key + ": " + why);
            });

            if (!failures.isEmpty()) {
                record(id, area, "fail", String.join("; ", failures));
                fail(id + "\n  " + String.join("\n  ", failures));
            }
            record(id, area, "pass", "");
        } finally {
            deleteTree(tmpDir);
        }
    }

    // -- comparison ----------------------------------------------------------

    private static String compare(JsonNode expected, Object actual, double tol) {
        if (expected.isArray()) {
            List<Object> act = asList(actual);
            if (act == null) return "expected a list of " + expected.size() + ", got " + actual;
            if (act.size() != expected.size())
                return "expected " + expected.size() + " elements, got " + act.size();
            for (int k = 0; k < act.size(); k++) {
                String why = compare(expected.get(k), act.get(k), tol);
                if (why != null) return "[" + k + "]: " + why;
            }
            return null;
        }
        if (expected.isBoolean()) {
            if (!(actual instanceof Boolean) || expected.asBoolean() != (Boolean) actual)
                return "expected " + expected + ", got " + actual;
            return null;
        }
        if (expected.isTextual()) {
            if (!(actual instanceof String) || !expected.asText().equals(actual))
                return "expected '" + expected.asText() + "', got '" + actual + "'";
            return null;
        }
        if (!(actual instanceof Number))
            return "expected a number, got " + actual;
        double e = expected.asDouble(), v = ((Number) actual).doubleValue();
        if (tol == 0.0)
            return e == v ? null : "expected " + fmt(e) + ", got " + fmt(v);
        double scale = Math.max(Math.max(Math.abs(e), Math.abs(v)), 1e-300);
        if (Math.abs(e - v) <= tol * scale) return null;
        return "expected " + fmt(e) + ", got " + fmt(v)
                + " (relative error " + (Math.abs(e - v) / scale) + " > " + tol + ")";
    }

    private static String fmt(double v) {
        return v == Math.rint(v) && Math.abs(v) < MAX_EXACT_INTEGER
                ? Long.toString((long) v) : Double.toString(v);
    }

    @SuppressWarnings("unchecked")
    private static List<Object> asList(Object actual) {
        if (actual instanceof List) return (List<Object>) actual;
        if (actual instanceof double[]) {
            List<Object> out = new ArrayList<>();
            for (double x : (double[]) actual) out.add(x);
            return out;
        }
        return null;
    }

    /**
     * Refuse an exact integer expectation at or above 2^53.
     *
     * <p>Java could compare it — a long is a long — but R and JavaScript could
     * not, and a case only some runners can check is not a conformance case.
     * Every runner enforces this, so the rule cannot be met by three of four.
     */
    private static void checkComparable(String id, JsonNode case_) {
        JsonNode expect = case_.get("expect");
        JsonNode tolerance = case_.get("tolerance");
        expect.fieldNames().forEachRemaining(key -> {
            if (tolerance != null && tolerance.has(key) && tolerance.get(key).asDouble() != 0)
                return;
            for (double v : scalars(expect.get(key)))
                if (Math.abs(v) >= MAX_EXACT_INTEGER)
                    fail(id + ": expected integer " + key + "=" + fmt(v)
                         + " is at or above 2^53, which a double-backed runner "
                         + "cannot compare exactly");
        });
    }

    private static List<Double> scalars(JsonNode n) {
        List<Double> out = new ArrayList<>();
        if (n.isArray()) for (JsonNode c : n) out.addAll(scalars(c));
        else if (n.isNumber()) out.add(n.asDouble());
        return out;
    }

    // -- report --------------------------------------------------------------

    private static void record(String id, String area, String status, String reason) {
        ObjectNode n = MAPPER.createObjectNode();
        n.put("id", id); n.put("area", area);
        n.put("status", status); n.put("reason", reason);
        REPORT.add(n);
    }

    @AfterAll
    static void writeReport() throws IOException {
        String path = System.getenv("TTTRLIB_CONFORMANCE_REPORT");
        if (path == null || path.trim().isEmpty()) return;
        ObjectNode root = MAPPER.createObjectNode();
        root.put("language", "java");
        root.set("results", MAPPER.valueToTree(REPORT));
        Files.write(Paths.get(path),
                    MAPPER.writerWithDefaultPrettyPrinter().writeValueAsString(root)
                          .getBytes(java.nio.charset.StandardCharsets.UTF_8));
    }

    private static void deleteTree(Path dir) throws IOException {
        if (!Files.exists(dir)) return;
        try (java.util.stream.Stream<Path> s = Files.walk(dir)) {
            s.sorted(java.util.Comparator.reverseOrder()).forEach(p -> {
                try { Files.deleteIfExists(p); } catch (IOException ignored) { }
            });
        }
    }
}
