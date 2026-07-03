// Cross-language reference test for the Java bindings (PRD-001).
// Asserts the SAME canonical values as the Python and R suites, now including
// the array getters marshalled via the PRD-002 `%ARRAY_INTO` accessors
// (get_macro_times_into / get_micro_times_into / get_routing_channels_into).
//
// Run:
//   javac -d classes <generated proxies> test/java/TTTRSmokeTest.java
//   java -cp classes -Djava.library.path=<native dir> TTTRSmokeTest

import io.github.fluorescencetools.tttrlib.TTTR;
import io.github.fluorescencetools.tttrlib.CorrelatorCurve;
import io.github.fluorescencetools.tttrlib.VectorInt32;
import io.github.fluorescencetools.tttrlib.VectorInt64;

public class TTTRSmokeTest {
    // Canonical values — source of truth: test/python/tttr/test_cross_language_reference.py
    static final long REF_SIZE = 183657L;
    static final long REF_N_MICRO_CHAN = 4096L;
    static final long REF_SUM_MICRO = 242477881L;
    static final long REF_SUM_MACRO = 443406877425185L;
    static final long REF_SUM_ROUTING = 880650L;
    // CorrelatorCurve(n_bins=3, n_casc=5)
    static final int REF_CORRCURVE_SIZE = 16;
    // burst_search(30, 10, 1e-3, "sliding_window")
    static final int REF_BURST_LEN = 586;
    static final long REF_BURST_SUM = 59237329L;
    // micro-time histogram (coarsening = 1)
    static final int REF_HIST_LEN = 4096;
    static final int REF_HIST_PEAK_CHAN = 814;
    static final int REF_HIST_PEAK_VAL = 676;
    // index getters (event 0), resolution, sub-selections
    static final long REF_MACRO_AT_0 = 56916L;
    static final int REF_MICRO_AT_0 = 1440;
    static final int REF_ROUTING_AT_0 = 9;
    static final double REF_MICRO_RES = 3.2958984375e-12;
    static final long REF_BY_CHANNEL_0 = 56499L;
    static final long REF_BY_CHANNEL_8 = 79468L;
    static final int[] REF_USED_CHANNELS = {0, 1, 8, 9};

    static void check(boolean cond, String msg) {
        if (!cond) { System.err.println("FAIL: " + msg); System.exit(1); }
    }

    public static void main(String[] args) {
        String dataRoot = System.getenv().getOrDefault("TTTRLIB_DATA", "tttr-data");
        TTTR tttr = new TTTR(dataRoot + "/bh/bh_spc132.spc", "SPC-130");

        // --- scalars ---
        check(tttr.size() == REF_SIZE, "size == " + REF_SIZE + " (got " + tttr.size() + ")");
        check(tttr.get_n_valid_events() == REF_SIZE, "get_n_valid_events == " + REF_SIZE);
        check(tttr.get_number_of_micro_time_channels() == REF_N_MICRO_CHAN,
              "micro time channels == " + REF_N_MICRO_CHAN);

        int n = (int) tttr.size();

        // --- macro times (unsigned long long -> long[]) ---
        long[] macro = new long[n];
        check(tttr.get_macro_times_into(macro) == n, "get_macro_times_into count");
        long sumMacro = 0; for (long v : macro) sumMacro += v;
        check(sumMacro == REF_SUM_MACRO, "sum(macro) == " + REF_SUM_MACRO + " (got " + sumMacro + ")");

        // --- micro times (unsigned short -> short[]) ---
        short[] micro = new short[n];
        check(tttr.get_micro_times_into(micro) == n, "get_micro_times_into count");
        long sumMicro = 0; for (short v : micro) sumMicro += (v & 0xffff);
        check(sumMicro == REF_SUM_MICRO, "sum(micro) == " + REF_SUM_MICRO + " (got " + sumMicro + ")");

        // --- routing channels (signed char -> byte[]) ---
        byte[] routing = new byte[n];
        check(tttr.get_routing_channels_into(routing) == n, "get_routing_channels_into count");
        long sumRouting = 0; for (byte v : routing) sumRouting += (v & 0xff);
        check(sumRouting == REF_SUM_ROUTING, "sum(routing) == " + REF_SUM_ROUTING + " (got " + sumRouting + ")");

        // --- correlator curve (deterministic, no data) ---
        CorrelatorCurve cc = new CorrelatorCurve();
        cc.setN_bins(3);
        cc.setN_casc(5);
        check(cc.size() == REF_CORRCURVE_SIZE, "CorrelatorCurve size == " + REF_CORRCURVE_SIZE);

        // --- burst search (std::vector<long long> -> VectorInt64) ---
        VectorInt64 bursts = tttr.burst_search(30, 10, 1e-3, "sliding_window");
        check(bursts.size() == REF_BURST_LEN, "burst_search len == " + REF_BURST_LEN);
        long sumBurst = 0; for (int i = 0; i < bursts.size(); i++) sumBurst += bursts.get(i);
        check(sumBurst == REF_BURST_SUM, "sum(burst ranges) == " + REF_BURST_SUM + " (got " + sumBurst + ")");

        // --- micro-time histogram (via the get_microtime_histogram_into accessor) ---
        double[] hist = new double[65536];
        int nh = tttr.get_microtime_histogram_into(hist, new VectorInt32(), 1);
        check(nh == REF_HIST_LEN, "microtime histogram length == " + REF_HIST_LEN + " (got " + nh + ")");
        int peak = 0; double mx = hist[0];
        for (int i = 1; i < nh; i++) if (hist[i] > mx) { mx = hist[i]; peak = i; }
        check(peak == REF_HIST_PEAK_CHAN, "histogram peak channel == " + REF_HIST_PEAK_CHAN + " (got " + peak + ")");
        check((int) mx == REF_HIST_PEAK_VAL, "histogram peak value == " + REF_HIST_PEAK_VAL + " (got " + (int) mx + ")");

        // --- index getters (event 0) ---
        check(tttr.get_macro_time_at(0).longValue() == REF_MACRO_AT_0, "get_macro_time_at(0) == " + REF_MACRO_AT_0);
        check(tttr.get_micro_time_at(0) == REF_MICRO_AT_0, "get_micro_time_at(0) == " + REF_MICRO_AT_0);
        check(tttr.get_routing_channel_at(0) == REF_ROUTING_AT_0, "get_routing_channel_at(0) == " + REF_ROUTING_AT_0);

        // --- micro-time resolution ---
        check(Math.abs(tttr.get_micro_time_resolution_s() - REF_MICRO_RES) < 1e-20,
              "micro_time_resolution == " + REF_MICRO_RES);

        // --- sub-selection by routing channel (signed char[] channels) ---
        check(tttr.get_tttr_by_channel(new byte[]{0}).size() == REF_BY_CHANNEL_0,
              "get_tttr_by_channel([0]).size == " + REF_BY_CHANNEL_0);
        check(tttr.get_tttr_by_channel(new byte[]{8}).size() == REF_BY_CHANNEL_8,
              "get_tttr_by_channel([8]).size == " + REF_BY_CHANNEL_8);

        // --- used routing channels (sorted) ---
        int[] uc = new int[256];
        int nuc = tttr.get_used_routing_channels_into(uc);
        int[] used = java.util.Arrays.copyOf(uc, nuc);
        java.util.Arrays.sort(used);
        check(java.util.Arrays.equals(used, REF_USED_CHANNELS),
              "used routing channels == " + java.util.Arrays.toString(REF_USED_CHANNELS)
              + " (got " + java.util.Arrays.toString(used) + ")");

        // --- header string ---
        check(tttr.get_header().get_json().contains("MeasDesc_ContainerType"),
              "header JSON contains MeasDesc_ContainerType");

        System.out.println("Java cross-language reference test: PASS");
    }
}
