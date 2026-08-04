// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.tests;

import io.github.fluorescencetools.tttrlib.*;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * JUnit port of test/java/TTTRSmokeTest.java (PRD-001 cross-language reference).
 */
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


    @Test
    public void referenceValuesMatch() throws Exception {
        String dataRoot = System.getenv().getOrDefault("TTTRLIB_DATA", "tttr-data");
        TTTR tttr = new TTTR(dataRoot + "/bh/bh_spc132.spc", "SPC-130");

        // --- scalars ---
        assertTrue(tttr.size() == REF_SIZE, "size == " + REF_SIZE + " (got " + tttr.size() + ")");
        assertTrue(tttr.get_n_valid_events() == REF_SIZE, "get_n_valid_events == " + REF_SIZE);
        assertTrue(tttr.get_number_of_micro_time_channels() == REF_N_MICRO_CHAN,
              "micro time channels == " + REF_N_MICRO_CHAN);

        int n = (int) tttr.size();

        // --- macro times (unsigned long long -> long[]) ---
        long[] macro = new long[n];
        assertTrue(tttr.get_macro_times_into(macro) == n, "get_macro_times_into count");
        long sumMacro = 0; for (long v : macro) sumMacro += v;
        assertTrue(sumMacro == REF_SUM_MACRO, "sum(macro) == " + REF_SUM_MACRO + " (got " + sumMacro + ")");

        // --- micro times (unsigned short -> short[]) ---
        short[] micro = new short[n];
        assertTrue(tttr.get_micro_times_into(micro) == n, "get_micro_times_into count");
        long sumMicro = 0; for (short v : micro) sumMicro += (v & 0xffff);
        assertTrue(sumMicro == REF_SUM_MICRO, "sum(micro) == " + REF_SUM_MICRO + " (got " + sumMicro + ")");

        // --- routing channels (signed char -> byte[]) ---
        byte[] routing = new byte[n];
        assertTrue(tttr.get_routing_channels_into(routing) == n, "get_routing_channels_into count");
        long sumRouting = 0; for (byte v : routing) sumRouting += (v & 0xff);
        assertTrue(sumRouting == REF_SUM_ROUTING, "sum(routing) == " + REF_SUM_ROUTING + " (got " + sumRouting + ")");

        // --- correlator curve (deterministic, no data) ---
        CorrelatorCurve cc = new CorrelatorCurve();
        cc.setN_bins(3);
        cc.setN_casc(5);
        assertTrue(cc.size() == REF_CORRCURVE_SIZE, "CorrelatorCurve size == " + REF_CORRCURVE_SIZE);

        // --- burst search (std::vector<long long> -> VectorInt64) ---
        VectorInt64 bursts = tttr.burst_search(30, 10, 1e-3, "sliding_window");
        assertTrue(bursts.size() == REF_BURST_LEN, "burst_search len == " + REF_BURST_LEN);
        long sumBurst = 0; for (int i = 0; i < bursts.size(); i++) sumBurst += bursts.get(i);
        assertTrue(sumBurst == REF_BURST_SUM, "sum(burst ranges) == " + REF_BURST_SUM + " (got " + sumBurst + ")");

        // --- micro-time histogram (via the get_microtime_histogram_into accessor) ---
        double[] hist = new double[65536];
        int nh = tttr.get_microtime_histogram_into(hist, new VectorInt32(), 1);
        assertTrue(nh == REF_HIST_LEN, "microtime histogram length == " + REF_HIST_LEN + " (got " + nh + ")");
        int peak = 0; double mx = hist[0];
        for (int i = 1; i < nh; i++) if (hist[i] > mx) { mx = hist[i]; peak = i; }
        assertTrue(peak == REF_HIST_PEAK_CHAN, "histogram peak channel == " + REF_HIST_PEAK_CHAN + " (got " + peak + ")");
        assertTrue((int) mx == REF_HIST_PEAK_VAL, "histogram peak value == " + REF_HIST_PEAK_VAL + " (got " + (int) mx + ")");

        // --- index getters (event 0) ---
        assertTrue(tttr.get_macro_time_at(0).longValue() == REF_MACRO_AT_0, "get_macro_time_at(0) == " + REF_MACRO_AT_0);
        assertTrue(tttr.get_micro_time_at(0) == REF_MICRO_AT_0, "get_micro_time_at(0) == " + REF_MICRO_AT_0);
        assertTrue(tttr.get_routing_channel_at(0) == REF_ROUTING_AT_0, "get_routing_channel_at(0) == " + REF_ROUTING_AT_0);

        // --- micro-time resolution ---
        assertTrue(Math.abs(tttr.get_micro_time_resolution_s() - REF_MICRO_RES) < 1e-20,
              "micro_time_resolution == " + REF_MICRO_RES);

        // --- sub-selection by routing channel (signed char[] channels) ---
        assertTrue(tttr.get_tttr_by_channel(new byte[]{0}).size() == REF_BY_CHANNEL_0,
              "get_tttr_by_channel([0]).size == " + REF_BY_CHANNEL_0);
        assertTrue(tttr.get_tttr_by_channel(new byte[]{8}).size() == REF_BY_CHANNEL_8,
              "get_tttr_by_channel([8]).size == " + REF_BY_CHANNEL_8);

        // --- used routing channels (sorted) ---
        int[] uc = new int[256];
        int nuc = tttr.get_used_routing_channels_into(uc);
        int[] used = java.util.Arrays.copyOf(uc, nuc);
        java.util.Arrays.sort(used);
        assertTrue(java.util.Arrays.equals(used, REF_USED_CHANNELS),
              "used routing channels == " + java.util.Arrays.toString(REF_USED_CHANNELS)
              + " (got " + java.util.Arrays.toString(used) + ")");

        // --- header string ---
        assertTrue(tttr.get_header().get_json().contains("MeasDesc_ContainerType"),
              "header JSON contains MeasDesc_ContainerType");

        System.out.println("Java cross-language reference test: PASS");
    }
}
