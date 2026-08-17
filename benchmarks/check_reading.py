#!/usr/bin/env python
"""Photon-for-photon identity of the reading benchmark: tttrlib vs the phconvert
outputs saved by competitors/bench_phconvert.py (PTU, HT3, SPC-130), same
comparisons as test/python/test_ab_core_reference.py. Writes
results/shared/reading/check.json.
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tttrlib  # noqa: E402
from common import RESULTS, data  # noqa: E402

SHARED = os.path.join(RESULTS, "shared", "reading")


def main():
    ref = np.load(os.path.join(SHARED, "phconvert_outputs.npz"))
    v = {}
    if "ptu_ts" in ref:
        d = tttrlib.TTTR(data("pq", "ptu", "pq_ptu_hh_t3.ptu"), "PTU")
        et = np.asarray(d.event_types); ph = et == 0
        det = ref["ptu_det"]; pr = det < 64
        same = (np.array_equal(np.asarray(d.macro_times)[ph], ref["ptu_ts"][pr]) and
                np.array_equal(np.asarray(d.micro_times)[ph], ref["ptu_nano"][pr]) and
                np.array_equal(np.asarray(d.routing_channels)[ph], det[pr]))
        v["ptu"] = {"n_photons": int(ph.sum()), "identical": bool(same)}
    if "ht3_ts" in ref:
        d = tttrlib.TTTR(data("imaging", "pq", "ht3", "pq_ht3_clsm.ht3"), "HT3")
        et = np.asarray(d.event_types)
        det = ref["ht3_det"]; pr = det < 64; mk = (det >= 64) & (det < 127)
        same = (np.array_equal(np.asarray(d.macro_times)[et == 0], ref["ht3_ts"][pr]) and
                np.array_equal(np.asarray(d.micro_times)[et == 0], ref["ht3_nano"][pr]) and
                np.array_equal(np.asarray(d.routing_channels)[et == 0], det[pr]) and
                np.array_equal(np.asarray(d.macro_times)[et == 1], ref["ht3_ts"][mk]))
        v["ht3"] = {"n_photons": int((et == 0).sum()), "n_markers": int((et == 1).sum()), "identical": bool(same)}
    if "spc_ts" in ref:
        d = tttrlib.TTTR(data("bh", "bh_spc132.spc"), "SPC-130")
        et = np.asarray(d.event_types); ph = et == 0
        det = ref["spc_det"]; pr = det < int(ref["spc_marker_min"])
        same = (np.array_equal(np.asarray(d.macro_times)[ph], ref["spc_ts"][pr]) and
                np.array_equal(np.asarray(d.micro_times)[ph], ref["spc_nano"][pr]) and
                np.array_equal(np.asarray(d.routing_channels)[ph], det[pr]))
        v["spc130"] = {"n_photons": int(ph.sum()), "identical": bool(same)}
    if "spc630_ts" in ref:
        d = tttrlib.TTTR(data("bh", "bh_spc630_256.spc"), "SPC-600_256")
        mt = np.asarray(d.macro_times).astype(np.int64)
        # phconvert adds 2^12 per overflow to a 17-bit field and inverts the 8-bit ADC
        # against 4095: channels and ADC (mod 256) agree, macro times only after undoing that
        chan_ok = np.array_equal(np.asarray(d.routing_channels), ref["spc630_det"])
        nano_ok = np.array_equal(np.asarray(d.micro_times), ref["spc630_nano"] & 0xFF)
        raw = np.fromfile(data("bh", "bh_spc630_256.spc"), dtype=np.uint32)[1:]
        keep = ((raw >> 31) & 1) == 0
        ts17 = ((raw & 0x01FFFF00) >> 8).astype(np.int64) + (np.cumsum((raw >> 30) & 1) << 17)
        macro_ok = np.array_equal(ts17[keep], mt)
        v["spc630"] = {"n_photons": int(mt.size), "channels_identical": bool(chan_ok), "adc_identical_mod_256": bool(nano_ok),
                       "macro_identical_with_2_17_overflow": bool(macro_ok),
                       "phconvert_backward_steps": int((np.diff(ref["spc630_ts"].astype(np.int64)) < 0).sum()),
                       "identical": bool(chan_ok and nano_ok and macro_ok),
                       "note": "identical up to phconvert's SPC-6xx defects (2^12 overflow shift on a 17-bit field; ADC inverted against 4095)"}
    if "spcqc_ts" in ref:
        d = tttrlib.TTTR(data("bh", "bh_spcqc004.spc"), "SPC-QC")
        same = (np.array_equal(np.asarray(d.macro_times), ref["spcqc_ts"]) and np.array_equal(np.asarray(d.micro_times), ref["spcqc_nano"])
                and np.array_equal(np.asarray(d.routing_channels), ref["spcqc_det"]))
        v["spcqc"] = {"n_photons": int(len(d.macro_times)), "identical": bool(same)}
    if "sm_ts" in ref:
        d = tttrlib.TTTR(data("sm", "data.sm"), "SM")
        same = np.array_equal(np.asarray(d.macro_times), ref["sm_ts"]) and np.array_equal(np.asarray(d.routing_channels), ref["sm_det"])
        v["sm"] = {"n_photons": int(len(d.macro_times)), "identical": bool(same)}
    pht3 = os.path.join(SHARED, "ptufile_pht3_outputs.npz")
    if os.path.exists(pht3):
        r = np.load(pht3)
        d = tttrlib.TTTR(data("imaging", "pq", "PicoHarp_SymPhoTime", "Example_PTU_PicoHarp.ptu"), "PTU")
        et = np.asarray(d.event_types); ph = (r["channel"] >= 0) & (r["marker"] == 0); mk = r["marker"] != 0
        same = (np.array_equal(np.asarray(d.macro_times)[et == 0], r["time"][ph]) and np.array_equal(np.asarray(d.micro_times)[et == 0], r["dtime"][ph])
                and np.array_equal(np.asarray(d.routing_channels)[et == 0] - 1, r["channel"][ph])
                and np.array_equal(np.asarray(d.macro_times)[et == 1], r["time"][mk]) and np.array_equal(np.asarray(d.micro_times)[et == 1], r["marker"][mk]))
        v["ptu_picoharp_t3"] = {"n_photons": int((et == 0).sum()), "n_markers": int((et == 1).sum()), "identical": bool(same),
                                "note": "tttrlib keeps PicoHarp's 1-based channel field; ptufile reports it 0-based"}
    for k, r in v.items():
        print(f"[{k}] {'IDENTICAL' if r['identical'] else 'DIFFERS'} {json.dumps({kk: vv for kk, vv in r.items() if kk != 'identical'})}")
    with open(os.path.join(SHARED, "check.json"), "w") as fh:
        json.dump(v, fh, indent=2)


if __name__ == "__main__":
    main()
