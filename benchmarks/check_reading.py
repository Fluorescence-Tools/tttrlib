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
    for k, r in v.items():
        print(f"[{k}] {'IDENTICAL' if r['identical'] else 'DIFFERS'} {json.dumps({kk: vv for kk, vv in r.items() if kk != 'identical'})}")
    with open(os.path.join(SHARED, "check.json"), "w") as fh:
        json.dump(v, fh, indent=2)


if __name__ == "__main__":
    main()
