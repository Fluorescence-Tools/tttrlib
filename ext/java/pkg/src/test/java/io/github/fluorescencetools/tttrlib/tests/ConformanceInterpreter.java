// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.tests;

import com.fasterxml.jackson.databind.JsonNode;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import io.github.fluorescencetools.tttrlib.*;

/**
 * The Java dispatch table for the conformance vocabulary.
 *
 * <p>A port of {@code test/conformance/py/interpreter.py}; see
 * {@code test/conformance/OPS.md} for the vocabulary it implements. The values
 * it produces must match the other three runners exactly.
 *
 * <p>Java's array marshalling shapes two things here. Column data comes back
 * through the {@code get_*_into} accessors in {@code ext/java/helpers.i} — the
 * caller preallocates and the call returns the true length — because a
 * void-returning output-pointer method has no return for SWIG to bind. And the
 * 64-bit channels really are 64-bit: a macro time is a {@code long}, so unlike R
 * and JavaScript this runner has no 2^53 ceiling. It still refuses expectations
 * above it, because a case that only Java could check is a broken case.
 */
final class ConformanceInterpreter {

    /** A value a case can name: a scalar, a numeric array, or a string list. */
    static final class Value {
        final Object o;
        Value(Object o) { this.o = o; }

        boolean isStrings() { return o instanceof List; }
        boolean isArray() { return o instanceof double[]; }

        double[] array() {
            if (!(o instanceof double[]))
                throw new ConformanceException("expected an array, got " + kind());
            return (double[]) o;
        }

        @SuppressWarnings("unchecked")
        List<String> strings() {
            if (!(o instanceof List))
                throw new ConformanceException("expected a string list, got " + kind());
            return (List<String>) o;
        }

        String kind() { return o == null ? "null" : o.getClass().getSimpleName(); }
    }

    static final class ConformanceException extends RuntimeException {
        ConformanceException(String m) { super(m); }
    }

    /** Thrown when the case names an op this runner does not implement. */
    static final class UnsupportedOpException extends RuntimeException {
        final String op;
        UnsupportedOpException(String op) { super(op); this.op = op; }
    }

    private final String dataRoot;
    private final List<String> tmpPaths;
    private final List<String> dataFiles;
    final Map<String, Object> bindings = new LinkedHashMap<>();

    ConformanceInterpreter(String dataRoot, List<String> tmpPaths, List<String> dataFiles) {
        this.dataRoot = dataRoot;
        this.tmpPaths = tmpPaths;
        this.dataFiles = dataFiles;
    }

    // -- argument substitution ----------------------------------------------

    private Object arg(JsonNode n) {
        if (n.isTextual()) {
            String s = n.asText();
            if (s.startsWith("$")) {
                String name = s.substring(1);
                if (name.matches("data\\d+"))
                    return Paths.get(dataRoot, dataFiles.get(Integer.parseInt(name.substring(4)))).toString();
                if (name.matches("tmp\\d+"))
                    return tmpPaths.get(Integer.parseInt(name.substring(3)));
                if (!bindings.containsKey(name))
                    throw new ConformanceException("step reads unbound '" + name + "'");
                return bindings.get(name);
            }
            return s;
        }
        if (n.isBoolean()) return n.asBoolean();
        if (n.isNumber()) return n.asDouble();
        if (n.isArray()) {
            List<Object> out = new ArrayList<>();
            for (JsonNode c : n) out.add(arg(c));
            return out;
        }
        throw new ConformanceException("unsupported argument: " + n);
    }

    private static int i(List<Object> a, int k) { return (int) Math.round((Double) a.get(k)); }
    private static double d(List<Object> a, int k) { return (Double) a.get(k); }
    private static String s(List<Object> a, int k) { return (String) a.get(k); }

    /**
     * A numeric argument as doubles, whether it came from the case file as a
     * JSON list or from an earlier step as an array binding. Both are ordinary:
     * `[1, 2]` is a literal, `$setup` is the output of fit.setup_vector.
     */
    private static double[] numbers(Object o) {
        if (o instanceof double[]) return (double[]) o;
        @SuppressWarnings("unchecked") List<Object> l = (List<Object>) o;
        double[] out = new double[l.size()];
        for (int k = 0; k < out.length; k++) out[k] = ((Number) l.get(k)).doubleValue();
        return out;
    }

    private static List<String> texts(Object o) {
        @SuppressWarnings("unchecked") List<Object> l = (List<Object>) o;
        List<String> out = new ArrayList<>(l.size());
        for (Object x : l) out.add((String) x);
        return out;
    }

    // -- the case loop -------------------------------------------------------

    void run(JsonNode steps) {
        for (JsonNode step : steps) {
            String op = step.get("op").asText();
            List<Object> args = new ArrayList<>();
            if (step.has("args")) for (JsonNode a : step.get("args")) args.add(arg(a));
            Object on = step.has("on") ? bindings.get(step.get("on").asText()) : null;
            if (step.has("on") && on == null && !bindings.containsKey(step.get("on").asText()))
                throw new ConformanceException("step reads unbound '" + step.get("on").asText() + "'");

            if (step.has("throws") && step.get("throws").asBoolean()) {
                boolean threw;
                try {
                    dispatch(op, on, args);
                    threw = false;
                } catch (UnsupportedOpException e) {
                    throw e;   // a missing op is not the throw the case meant
                } catch (RuntimeException | Error e) {
                    threw = true;
                }
                if (step.has("as")) bindings.put(step.get("as").asText(), threw);
                continue;
            }

            Object result = dispatch(op, on, args);
            if (step.has("as")) {
                String as = step.get("as").asText();
                if (bindings.containsKey(as))
                    throw new ConformanceException("rebinds '" + as + "'");
                bindings.put(as, result);
            }
        }
    }

    // -- dispatch ------------------------------------------------------------

    private Object dispatch(String op, Object on, List<Object> a) {
        switch (op) {
            // -- generic ------------------------------------------------------
            case "len": return (double) lengthOf(on);
            case "sum": {
                double[] v = (double[]) on;
                double t = 0; for (double x : v) t += x; return t;
            }
            case "mean": {
                double[] v = (double[]) on;
                double t = 0; for (double x : v) t += x; return t / v.length;
            }
            case "min": { double t = Double.POSITIVE_INFINITY;
                for (double x : (double[]) on) t = Math.min(t, x); return t; }
            case "max": { double t = Double.NEGATIVE_INFINITY;
                for (double x : (double[]) on) t = Math.max(t, x); return t; }
            case "argmax": {
                double[] v = (double[]) on; int best = 0;
                for (int k = 1; k < v.length; k++) if (v[k] > v[best]) best = k;
                return (double) best;   // strict >, so ties keep the lowest index
            }
            case "argmin": {
                double[] v = (double[]) on; int best = 0;
                for (int k = 1; k < v.length; k++) if (v[k] < v[best]) best = k;
                return (double) best;
            }
            case "first": return element(on, 0);
            case "last": return element(on, lengthOf(on) - 1);
            case "nth": return element(on, i(a, 0));
            case "slice": {
                int lo = i(a, 0), hi = Math.min(i(a, 1), lengthOf(on));
                if (on instanceof double[]) {
                    double[] v = (double[]) on;
                    double[] out = new double[Math.max(0, hi - lo)];
                    System.arraycopy(v, lo, out, 0, out.length);
                    return out;
                }
                List<String> v = texts(on);
                return new ArrayList<>(v.subList(lo, Math.max(lo, hi)));
            }
            case "to_list": {
                if (on instanceof double[]) {
                    List<Object> out = new ArrayList<>();
                    for (double x : (double[]) on) out.add(x);
                    return out;
                }
                return new ArrayList<>(texts(on));
            }
            case "unique_sorted": {
                double[] v = ((double[]) on).clone();
                java.util.Arrays.sort(v);
                List<Object> out = new ArrayList<>();
                for (int k = 0; k < v.length; k++)
                    if (k == 0 || v[k] != v[k - 1]) out.add(v[k]);
                return out;
            }
            case "shape": {
                List<Object> out = new ArrayList<>();
                out.add((double) lengthOf(on));
                return out;
            }
            case "dtype":
                throw new UnsupportedOpException("dtype");
            case "contains": return ((String) on).contains(s(a, 0));
            case "round": {
                double f = Math.pow(10, i(a, 0));
                return Math.round(((Number) on).doubleValue() * f) / f;
            }
            case "identity": return on;
            case "count_gt": {
                double t = d(a, 0); int n = 0;
                for (double x : (double[]) on) if (x > t) n++;
                return (double) n;
            }

            // -- tttr ---------------------------------------------------------
            case "tttr.open": return new TTTR(s(a, 0), s(a, 1));
            case "tttr.size": return (double) ((TTTR) on).size();
            case "tttr.n_valid_events": return (double) ((TTTR) on).get_n_valid_events();
            case "tttr.n_micro_channels":
                return (double) ((TTTR) on).get_number_of_micro_time_channels();
            case "tttr.macro_times": {
                TTTR t = (TTTR) on;
                long[] buf = new long[(int) t.size()];
                t.get_macro_times_into(buf);
                return widen(buf);
            }
            case "tttr.micro_times": {
                TTTR t = (TTTR) on;
                short[] buf = new short[(int) t.size()];
                t.get_micro_times_into(buf);
                double[] out = new double[buf.length];
                // A micro time is unsigned 16-bit; Java's short is signed, so the
                // top half of the range would read negative without the mask.
                for (int k = 0; k < buf.length; k++) out[k] = buf[k] & 0xFFFF;
                return out;
            }
            case "tttr.routing_channels": {
                TTTR t = (TTTR) on;
                byte[] buf = new byte[(int) t.size()];
                t.get_routing_channels_into(buf);
                double[] out = new double[buf.length];
                for (int k = 0; k < buf.length; k++) out[k] = buf[k];  // signed 8-bit
                return out;
            }
            // A macro time is unsigned 64-bit, which SWIG-Java carries as a
            // BigInteger because Java has no unsigned long.
            case "tttr.macro_time_at":
                return ((TTTR) on).get_macro_time_at(i(a, 0)).doubleValue();
            case "tttr.micro_time_at": return (double) ((TTTR) on).get_micro_time_at(i(a, 0));
            case "tttr.routing_channel_at":
                return (double) ((TTTR) on).get_routing_channel_at(i(a, 0));
            case "tttr.used_routing_channels": {
                // The count is not known up front; ask with a generous buffer and
                // use the returned true length, which is what these accessors are
                // for. A routing channel is one signed byte, so 256 is the ceiling.
                int[] buf = new int[256];
                int got = ((TTTR) on).get_used_routing_channels_into(buf);
                double[] out = new double[got];
                for (int k = 0; k < got; k++) out[k] = buf[k];
                return out;
            }
            case "tttr.by_channel": {
                // A routing channel is a signed byte, and jarrays.i marshals the
                // selection as byte[] rather than as a vector proxy.
                @SuppressWarnings("unchecked") List<Object> ch = (List<Object>) a.get(0);
                byte[] v = new byte[ch.size()];
                for (int k = 0; k < v.length; k++)
                    v[k] = (byte) Math.round((Double) ch.get(k));
                return ((TTTR) on).get_tttr_by_channel(v);
            }
            case "tttr.burst_search": {
                VectorInt64 v = ((TTTR) on).burst_search(
                        i(a, 0), i(a, 1), d(a, 2), s(a, 3));
                double[] out = new double[(int) v.size()];
                for (int k = 0; k < out.length; k++) out[k] = v.get(k);
                return out;
            }
            case "tttr.microtime_histogram": {
                TTTR t = (TTTR) on;
                double[] buf = new double[(int) t.get_number_of_micro_time_channels()];
                int got = t.get_microtime_histogram_into(buf, new VectorInt32(), i(a, 0));
                if (got == buf.length) return buf;
                double[] exact = new double[got];
                t.get_microtime_histogram_into(exact, new VectorInt32(), i(a, 0));
                return exact;
            }
            case "tttr.header_json": return ((TTTR) on).get_header().get_json();
            case "tttr.micro_time_resolution":
                return ((TTTR) on).get_header().getMicro_time_resolution();
            case "tttr.macro_time_resolution":
                return ((TTTR) on).get_header().getMacro_time_resolution();

            // -- correlator ---------------------------------------------------
            case "correlator.curve_size": {
                CorrelatorCurve cc = new CorrelatorCurve();
                cc.setN_bins(i(a, 0));
                cc.setN_casc(i(a, 1));
                return (double) cc.size();
            }

            // -- datastore ----------------------------------------------------
            case "ds.new": return new DataStore();
            case "ds.n_rows": return (double) ((DataStore) on).n_rows();
            case "ds.n_columns": return (double) ((DataStore) on).n_columns();
            case "ds.n_groups": return (double) ((DataStore) on).n_groups();
            case "ds.column_names": return vectorStrings(((DataStore) on).column_names());
            case "ds.add_group": return ((DataStore) on).add_group(s(a, 0));
            case "ds.ensure_group": return ((DataStore) on).ensure_group(s(a, 0));
            case "ds.group": return ((DataStore) on).group(s(a, 0));
            case "ds.has_group": return ((DataStore) on).has_group(s(a, 0));
            case "ds.remove_group": return ((DataStore) on).remove_group(s(a, 0));
            case "ds.group_names": return vectorStrings(((DataStore) on).group_names());
            case "ds.group_paths": return vectorStrings(((DataStore) on).group_paths());
            case "ds.set_label": ((DataStore) on).set_label(s(a, 0)); return null;
            case "ds.label": return ((DataStore) on).label();
            case "ds.nbytes": return (double) ((DataStore) on).nbytes();
            case "ds.add_string": {
                DataStore st = (DataStore) on;
                List<String> vals = texts(a.get(1));
                Column c = st.column(st.add_column(s(a, 0), ColumnType.String));
                for (String v : vals) c.push_string(v);
                if (st.n_rows() < vals.size()) st.set_n_rows(vals.size());
                return null;
            }
            case "ds.column_strings": {
                Column c = ((DataStore) on).column_by_name(s(a, 0));
                List<String> out = new ArrayList<>();
                for (int k = 0; k < c.size(); k++) out.add(c.string_at(k));
                return out;
            }
            case "ds.column_dtype":
                return dtypeName(((DataStore) on).column_by_name(s(a, 0)).type());
            case "ds.select_range": {
                DataStore st = (DataStore) on;
                int idx = st.find(s(a, 0));
                if (idx < 0) throw new ConformanceException("no column " + s(a, 0));
                st.select_range(idx, d(a, 1), d(a, 2));
                return null;
            }
            case "ds.n_selected": return (double) ((DataStore) on).n_selected();

            // -- tiff -----------------------------------------------------------
            // jarrays.i's IN_ARRAY3 takes double[][][], so the flat values are
            // shaped here; the read side goes through helpers.i's free helper,
            // since a free function cannot be %extend-ed.
            case "tiff.write_f64": {
                int nf = i(a, 1), nh = i(a, 2), nw = i(a, 3);
                double[] flat = numbers(a.get(4));
                double[][][] block = new double[nf][nh][nw];
                for (int f = 0; f < nf; f++)
                    for (int r = 0; r < nh; r++)
                        for (int c = 0; c < nw; c++)
                            block[f][r][c] = flat[(f * nh + r) * nw + c];
                tttrlib._tiff_write_f64(s(a, 0), block);
                return null;
            }
            case "tiff.read_f64": {
                int n = tttrlib.tiff_read_f64_into(s(a, 0), new double[0]);
                double[] out = new double[n];
                tttrlib.tiff_read_f64_into(s(a, 0), out);
                return out;
            }

            // -- bursts ---------------------------------------------------------
            case "burst.new": return new BurstFilter((TTTR) a.get(0));
            case "burst.find": {
                BurstFilter bf = (BurstFilter) on;
                int n = bf.find_bursts_into(new long[0]);
                long[] out = new long[n];
                bf.find_bursts_into(out);
                return widen(out);
            }
            case "burst.properties": {
                // A nested vector proxy rather than a flat block, so it is read
                // row by row -- the layout the other three runners flatten to.
                VectorDouble_2D rows = ((BurstFilter) on).get_all_burst_properties();
                int nr = rows.size();
                int nc = nr > 0 ? (int) rows.get(0).size() : 0;
                double[] out = new double[nr * nc];
                for (int r = 0; r < nr; r++) {
                    VectorDouble row = rows.get(r);
                    for (int c = 0; c < nc; c++) out[r * nc + c] = row.get(c);
                }
                return out;
            }

            // -- pda ------------------------------------------------------------
            // The two getters are helpers.i's INPLACE-fill accessors: ask once
            // with an empty array for the true length, then once for real.
            case "pda.new":
                return new Pda(i(a, 0), i(a, 1), d(a, 2), d(a, 3),
                               doubleVector(numbers(a.get(4))));
            case "pda.append": ((Pda) on).append(d(a, 0), d(a, 1)); return null;
            case "pda.evaluate": ((Pda) on).evaluate(); return null;
            case "pda.s1s2": {
                Pda pda = (Pda) on;
                int n = pda.get_S1S2_matrix_into(new double[0]);
                double[] out = new double[n];
                pda.get_S1S2_matrix_into(out);
                return out;
            }
            case "pda.histogram_y": {
                Pda pda = (Pda) on;
                int n = pda.get_1dhistogram_y_into(new double[0]);
                double[] out = new double[n];
                pda.get_1dhistogram_y_into(out);
                return out;
            }

            // -- decay fitting --------------------------------------------------
            case "fit.names": return vectorStrings(tttrlib.decay_fit_names());
            case "fit.setup_names":
                return vectorStrings(tttrlib.decay_fit_setup_names(s(a, 0)));
            case "fit.result_names":
                return vectorStrings(tttrlib.decay_fit_result_names(s(a, 0), 0));
            case "fit.setup_vector":
                return vectorDoubles(tttrlib.decay_fit_setup_vector(s(a, 0), s(a, 1)));
            case "fit.problem": {
                DecayFitProblem p = new DecayFitProblem(i(a, 0), i(a, 1), d(a, 2));
                p.setIrf(doubleVector(numbers(a.get(3))));
                p.setBackground(doubleVector(numbers(a.get(4))));
                p.setData(doubleVector(numbers(a.get(5))));
                return p;
            }
            case "fit.new":
                return new DecayFit2(s(a, 0), doubleVector(numbers(a.get(1))),
                                     doubleVector(numbers(a.get(2))));
            case "fit.run": {
                VectorInt32 link = new VectorInt32();
                for (double v : numbers(a.get(1))) link.add((int) Math.round(v));
                return ((DecayFit2) on).fit(doubleVector(numbers(a.get(0))),
                                            new DecayFitConstraints(link),
                                            (DecayFitProblem) a.get(2));
            }
            case "fit.objective": return ((DecayFitOutcome) on).getObjective();
            case "fit.parameters": return vectorDoubles(((DecayFitOutcome) on).getParameters());
            case "fit.results": return vectorDoubles(((DecayFitOutcome) on).getResults());

            // -- clsm -----------------------------------------------------------
            // get_intensity_into / get_mean_micro_time_into fill a 1-D buffer:
            // jarrays.i has no 3-D output typemap, so the image arrives flat and
            // the case pins the three dimensions separately.
            case "clsm.open": {
                @SuppressWarnings("unchecked") List<Object> ch = (List<Object>) a.get(1);
                VectorInt32 channels = new VectorInt32();
                for (Object c : ch) channels.add((int) Math.round((Double) c));
                return new CLSMImage((TTTR) a.get(0), new CLSMSettings(), null,
                                     true, channels);
            }
            case "clsm.n_frames": return (double) ((CLSMImage) on).getN_frames();
            case "clsm.n_lines": return (double) ((CLSMImage) on).getN_lines();
            case "clsm.n_pixel": return (double) ((CLSMImage) on).getN_pixel();
            case "clsm.intensity": {
                CLSMImage img = (CLSMImage) on;
                int[] flat = new int[(int) (img.getN_frames() * img.getN_lines()
                                            * img.getN_pixel())];
                img.get_intensity_into(flat);
                double[] out = new double[flat.length];
                for (int k = 0; k < flat.length; k++) out[k] = flat[k];
                return out;
            }
            case "clsm.mean_micro_time": {
                CLSMImage img = (CLSMImage) on;
                double[] out = new double[(int) (img.getN_frames() * img.getN_lines()
                                                 * img.getN_pixel())];
                img.get_mean_micro_time_into(out);
                return out;
            }

            case "clsm.fluorescence_decay": {
                // helpers.i's 4-D accessor; ask once for the true length.
                CLSMImage img = (CLSMImage) on;
                int n = img.get_fluorescence_decay_into(new int[0], i(a, 1),
                                                        (Boolean) a.get(2));
                int[] buf = new int[n];
                img.get_fluorescence_decay_into(buf, i(a, 1), (Boolean) a.get(2));
                double[] out = new double[n];
                for (int k = 0; k < n; k++) out[k] = buf[k];
                return out;
            }

            // -- histogram ------------------------------------------------------
            // jarrays.i's IN_ARRAY2 takes double[][] and flattens it, so the
            // (n, 1) shape is the array's own shape rather than a parameter.
            case "hist.new": return new doubleHistogram();
            case "hist.set_axis":
                ((doubleHistogram) on).set_axis(i(a, 0), s(a, 1), d(a, 2), d(a, 3),
                                                i(a, 4), s(a, 5));
                return null;
            case "hist.update": {
                double[] flat = numbers(a.get(0));
                double[][] shaped = new double[flat.length][1];
                for (int k = 0; k < flat.length; k++) shaped[k][0] = flat[k];
                ((doubleHistogram) on).update(shaped);
                return null;
            }
            case "hist.counts": {
                doubleHistogram h = (doubleHistogram) on;
                double[] probe = new double[0];
                int n = h.get_histogram_into(probe);
                double[] out = new double[n];
                h.get_histogram_into(out);
                return out;
            }

            // -- bitmask --------------------------------------------------------
            // jarrays.i releases an INPLACE array with JNI mode 0, so the C++
            // writes land back in the caller's byte[].
            case "bitmask.new": return new BitMask(i(a, 0));
            case "bitmask.set":
                ((BitMask) on).set(i(a, 0), (Boolean) a.get(1));
                return null;
            case "bitmask.size": return (double) ((BitMask) on).size();
            case "bitmask.count": return (double) ((BitMask) on).count();
            case "bitmask.to_bytes": {
                BitMask b = (BitMask) on;
                byte[] out = new byte[(int) b.size()];
                b.to_bytes(out);
                double[] v = new double[out.length];
                for (int k = 0; k < out.length; k++) v[k] = out[k];
                return v;
            }

            // -- registry -------------------------------------------------------
            case "registry.json": return tttrlib.registry_json();
            case "registry.category_json": return tttrlib.registry_category_json(s(a, 0));
            case "registry.categories": return vectorStrings(tttrlib.registry_categories());

            // -- files ---------------------------------------------------------
            case "file.write_text":
                try {
                    Files.write(Paths.get(s(a, 0)), s(a, 1).getBytes(StandardCharsets.UTF_8));
                } catch (IOException e) {
                    throw new UncheckedIOException(e);
                }
                return null;

            // -- hdf5 ----------------------------------------------------------
            case "hdf5.write":
                return tttrlib.write_hdf5_table(s(a, 0), (DataStore) a.get(1), s(a, 2),
                                                0, Hdf5WriteMode.Update);
            case "hdf5.read": {
                DataStore out = new DataStore();
                tttrlib.read_hdf5_table_into(out, s(a, 0), s(a, 1), true);
                return out;
            }
            case "hdf5.groups": return vectorStrings(tttrlib.hdf5_table_groups(s(a, 0)));
            case "hdf5.has": return tttrlib.hdf5_table_has(s(a, 0), s(a, 1));

            default:
                Object typed = typedColumnOp(op, on, a);
                if (typed != NOT_HANDLED) return typed;
                throw new UnsupportedOpException(op);
        }
    }

    // -- the eleven typed ds.add_* / ds.column_* ops --------------------------

    private static final Object NOT_HANDLED = new Object();

    private Object typedColumnOp(String op, Object on, List<Object> a) {
        if (op.startsWith("ds.add_")) {
            String sfx = op.substring("ds.add_".length());
            ColumnType t = COLUMN_TYPES.get(sfx);
            if (t == null) return NOT_HANDLED;
            DataStore st = (DataStore) on;
            double[] v = numbers(a.get(1));
            Column c = st.column(st.add_column(s(a, 0), t));
            fill(c, sfx, v);
            if (st.n_rows() < v.length) st.set_n_rows(v.length);
            return null;
        }
        if (op.startsWith("ds.column_")) {
            String sfx = op.substring("ds.column_".length());
            ColumnType want = COLUMN_TYPES.get(sfx);
            if (want == null) return NOT_HANDLED;
            Column c = ((DataStore) on).column_by_name(s(a, 0));
            if (c.type() != want)
                throw new ConformanceException("column '" + s(a, 0) + "' is "
                        + dtypeName(c.type()) + ", the case asked for " + dtypeName(want));
            return read(c, sfx);
        }
        return NOT_HANDLED;
    }

    private static final Map<String, ColumnType> COLUMN_TYPES = new HashMap<>();
    private static final Map<ColumnType, String> DTYPE_NAMES = new HashMap<>();
    static {
        COLUMN_TYPES.put("f64", ColumnType.Float64); COLUMN_TYPES.put("f32", ColumnType.Float32);
        COLUMN_TYPES.put("i64", ColumnType.Int64);   COLUMN_TYPES.put("i32", ColumnType.Int32);
        COLUMN_TYPES.put("i16", ColumnType.Int16);   COLUMN_TYPES.put("i8", ColumnType.Int8);
        COLUMN_TYPES.put("u64", ColumnType.UInt64);  COLUMN_TYPES.put("u32", ColumnType.UInt32);
        COLUMN_TYPES.put("u16", ColumnType.UInt16);  COLUMN_TYPES.put("u8", ColumnType.UInt8);
        DTYPE_NAMES.put(ColumnType.Float64, "float64"); DTYPE_NAMES.put(ColumnType.Float32, "float32");
        DTYPE_NAMES.put(ColumnType.Int64, "int64");     DTYPE_NAMES.put(ColumnType.Int32, "int32");
        DTYPE_NAMES.put(ColumnType.Int16, "int16");     DTYPE_NAMES.put(ColumnType.Int8, "int8");
        DTYPE_NAMES.put(ColumnType.UInt64, "uint64");   DTYPE_NAMES.put(ColumnType.UInt32, "uint32");
        DTYPE_NAMES.put(ColumnType.UInt16, "uint16");   DTYPE_NAMES.put(ColumnType.UInt8, "uint8");
        DTYPE_NAMES.put(ColumnType.Bool, "bool");       DTYPE_NAMES.put(ColumnType.String, "string");
    }

    static String dtypeName(ColumnType t) {
        String n = DTYPE_NAMES.get(t);
        if (n == null) throw new ConformanceException("dtype " + t + " is not in the canonical set");
        return n;
    }

    private static void fill(Column c, String sfx, double[] v) {
        switch (sfx) {
            case "f64": c.set_f64(v); return;
            case "f32": { float[] b = new float[v.length];
                for (int k = 0; k < v.length; k++) b[k] = (float) v[k]; c.set_f32(b); return; }
            case "i64": c.set_i64(longs(v)); return;
            case "i32": { int[] b = new int[v.length];
                for (int k = 0; k < v.length; k++) b[k] = (int) Math.round(v[k]); c.set_i32(b); return; }
            case "i16": case "u16": { short[] b = new short[v.length];
                for (int k = 0; k < v.length; k++) b[k] = (short) Math.round(v[k]);
                if (sfx.equals("i16")) c.set_i16(b); else c.set_u16(b); return; }
            case "i8": case "u8": { byte[] b = new byte[v.length];
                for (int k = 0; k < v.length; k++) b[k] = (byte) Math.round(v[k]);
                if (sfx.equals("i8")) c.set_i8(b); else c.set_u8(b); return; }
            case "u64": c.set_u64(longs(v)); return;
            case "u32": { int[] b = new int[v.length];
                for (int k = 0; k < v.length; k++) b[k] = (int) Math.round(v[k]); c.set_u32(b); return; }
            default: throw new ConformanceException("no setter for " + sfx);
        }
    }

    private static double[] read(Column c, String sfx) {
        int n = (int) c.size();
        switch (sfx) {
            case "f64": { double[] b = new double[n]; c.get_f64_into(b); return b; }
            case "f32": { float[] b = new float[n]; c.get_f32_into(b);
                double[] o = new double[n]; for (int k = 0; k < n; k++) o[k] = b[k]; return o; }
            case "i64": { long[] b = new long[n]; c.get_i64_into(b); return widen(b); }
            case "u64": { long[] b = new long[n]; c.get_u64_into(b); return widen(b); }
            case "i32": { int[] b = new int[n]; c.get_i32_into(b);
                double[] o = new double[n]; for (int k = 0; k < n; k++) o[k] = b[k]; return o; }
            case "u32": { int[] b = new int[n]; c.get_u32_into(b);
                double[] o = new double[n];
                for (int k = 0; k < n; k++) o[k] = b[k] & 0xFFFFFFFFL; return o; }
            case "i16": { short[] b = new short[n]; c.get_i16_into(b);
                double[] o = new double[n]; for (int k = 0; k < n; k++) o[k] = b[k]; return o; }
            case "u16": { short[] b = new short[n]; c.get_u16_into(b);
                double[] o = new double[n]; for (int k = 0; k < n; k++) o[k] = b[k] & 0xFFFF; return o; }
            case "i8": { byte[] b = new byte[n]; c.get_i8_into(b);
                double[] o = new double[n]; for (int k = 0; k < n; k++) o[k] = b[k]; return o; }
            case "u8": { byte[] b = new byte[n]; c.get_u8_into(b);
                double[] o = new double[n]; for (int k = 0; k < n; k++) o[k] = b[k] & 0xFF; return o; }
            default: throw new ConformanceException("no view for " + sfx);
        }
    }

    private static long[] longs(double[] v) {
        long[] b = new long[v.length];
        for (int k = 0; k < v.length; k++) b[k] = Math.round(v[k]);
        return b;
    }

    /**
     * A long array as doubles.
     *
     * <p>Exact only below 2^53, which is why the runner refuses expectations at
     * or above it — see {@code ConformanceTest.checkComparable}. Carrying every
     * binding as a double is what lets one comparison serve all four languages.
     */
    private static double[] widen(long[] v) {
        double[] out = new double[v.length];
        for (int k = 0; k < v.length; k++) out[k] = v[k];
        return out;
    }

    private static VectorDouble doubleVector(double[] v) {
        VectorDouble out = new VectorDouble();
        for (double x : v) out.add(x);
        return out;
    }

    private static double[] vectorDoubles(VectorDouble v) {
        double[] out = new double[(int) v.size()];
        for (int k = 0; k < out.length; k++) out[k] = v.get(k);
        return out;
    }

    private static List<String> vectorStrings(VectorString v) {
        List<String> out = new ArrayList<>((int) v.size());
        for (int k = 0; k < v.size(); k++) out.add(v.get(k));
        return out;
    }

    private static int lengthOf(Object on) {
        if (on instanceof double[]) return ((double[]) on).length;
        if (on instanceof List) return ((List<?>) on).size();
        if (on instanceof String) return ((String) on).length();
        throw new ConformanceException("expected a sequence, got " + on);
    }

    private static Object element(Object on, int k) {
        if (on instanceof double[]) return ((double[]) on)[k];
        if (on instanceof List) return ((List<?>) on).get(k);
        throw new ConformanceException("expected a sequence, got " + on);
    }
}
