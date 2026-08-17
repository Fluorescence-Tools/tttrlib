#!/usr/bin/env python
"""Output identity of the FRET / burst benchmark pairs (run after ``bench_fret.py``
and ``competitors/bench_fret.py``): recomputes tttrlib's outputs on the shared
inputs and compares them with the reference outputs the competitor saved.
Writes results/shared/fret/check.json.

PDA, BurstML, 2CDE and 2D-FDC are exact pairs. CUSUM vs PAM is behavioural by
construction (PAM discretises the same Zhang & Yang search with its own alpha
and offset heuristics): every PAM burst must be matched by one of ours with a
photon-Jaccard >= 0.85 and vice versa, as the A/B test pins.
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tttrlib  # noqa: E402
from common import RESULTS  # noqa: E402
from bench_fret import tttr_from_ticks  # noqa: E402

SHARED = os.path.join(RESULTS, "shared", "fret")


def main():
    ref = np.load(os.path.join(SHARED, "reference_outputs.npz"))
    v = {}

    if "pda_s1s2" in ref:
        z = np.load(os.path.join(SHARED, "pda.npz"))
        nmax = int(z["nmax"])
        pda = tttrlib.Pda(hist2d_nmax=nmax, hist2d_nmin=0, background_ch1=float(z["bg1"]), background_ch2=float(z["bg2"]), pF=z["pF"].tolist())
        for a, p in zip(z["amps"], z["probs"]):
            pda.append(float(a), float(p))
        diff = float(np.abs(np.asarray(pda.s1s2) - ref["pda_s1s2"]).max())
        v["pda"] = {"max_abs_diff": diff, "identical": diff < 1e-15}

    if "burstml_nll" in ref:
        z = np.load(os.path.join(SHARED, "burstml.npz"))
        ml = tttrlib.BurstML()
        ml.set_burst_data((z["t100"] * 1e-4).tolist(), [int(c) for c in z["colours"]], [int(o) for o in z["offsets"]])
        ml.set_n_states(int(z["n_states"])); ml.set_n_colours(int(z["n_colours"])); ml.set_jmax(int(z["jmax"]))
        ml.set_qmax(float(z["qmax"])); ml.set_t_th(float(z["t_th"])); ml.set_n_th(float(z["n_th"]))
        ours = np.array([ml.neg_log_likelihood(list(map(float, p))) for p in z["params"]])
        ratio = float(np.max(np.abs(ours / ref["burstml_nll"] - 1.0)))
        v["burstml"] = {"max_abs_ratio_minus_one": ratio, "identical": ratio < 1e-12}

    if "two_cde" in ref:
        z = np.load(os.path.join(SHARED, "two_cde.npz"))
        macro, chan, bounds = z["macro"], z["chan"], z["bounds"]
        d = tttrlib.TTTR()
        d.append_events(macro.astype(np.uint64), np.zeros(macro.size, np.uint16), chan.astype(np.int8), np.zeros(macro.size, np.int8), False, 0)
        d.header.set_macro_time_resolution(1.0)
        eng = tttrlib.TwoCDE(d); eng.set_donor([0]); eng.set_acceptor([1])
        eng.compute(bounds, float(z["tau"]), tttrlib.TwoCDE.FRET_2CDE, tttrlib.TwoCDE.LAPLACE)
        got = np.asarray(eng.two_cde); r = ref["two_cde"]
        ok = np.isfinite(r)
        rel = float(np.max(np.abs(got[ok] - r[ok]) / np.maximum(np.abs(r[ok]), 1e-9)))
        v["two_cde"] = {"max_rel_diff": rel, "n_bursts": int(r.size), "identical": rel < 1e-9 and bool(np.array_equal(np.isfinite(got), ok))}

    if "fdc2d_log" in ref:
        z = np.load(os.path.join(SHARED, "fdc2d.npz"))
        lags, L = z["lags"], int(z["logt_imax"])
        out = np.zeros(lags.size * L * L, dtype=np.int64)
        tttrlib.fdc_scan_log(z["macro"], z["micro"], lags, int(z["ddT"]), int(z["t_min"]), int(z["t_max"]), L, 1, out, 1)
        got = out.reshape(lags.size, L, L)
        same = bool(np.array_equal(got, ref["fdc2d_log"]))
        v["fdc2d"] = {"identical": same, "total_pairs": int(got.sum()), "differing_cells": int(np.count_nonzero(got != ref["fdc2d_log"]))}

    if "cusum_bursts" in ref:
        z = np.load(os.path.join(SHARED, "cusum.npz"))
        tttr = tttr_from_ticks(z["ticks"])
        L = int(z["L"])
        got = np.asarray(tttr.burst_search_cusum_sprt(L, float(z["IB_khz"]) * 1e3, float(z["IT_khz"]) / float(z["IB_khz"]), 0.05, 0.05), dtype=np.int64).reshape(-1, 2)
        pam = ref["cusum_bursts"]
        pam = pam[(pam[:, 1] - pam[:, 0] + 1) >= L]

        def best_overlap(b, others):
            s, e = b
            ov = [(min(e, e2) - max(s, s2) + 1) / (max(e, e2) - min(s, s2) + 1) for s2, e2 in others if min(e, e2) >= max(s, s2)]
            return max(ov) if ov else 0.0

        j_pam = [best_overlap(b, got) for b in pam]
        j_ours = [best_overlap(b, pam) for b in got]
        v["cusum"] = {"n_bursts_tttrlib": int(got.shape[0]), "n_bursts_pam": int(pam.shape[0]),
                      "min_jaccard": float(min(j_pam + j_ours)) if (j_pam or j_ours) else 1.0,
                      "identical": False, "behavioural_match": bool(min(j_pam + j_ours, default=1.0) >= 0.85),
                      "note": "PAM discretises the same search (alpha = 1/N, offset heuristics); edges within a few photons"}

    for k, r in v.items():
        tag = "IDENTICAL" if r["identical"] else ("behavioural match" if r.get("behavioural_match") else "DIFFERS")
        print(f"[{k}] {tag}  {json.dumps({kk: vv for kk, vv in r.items() if kk not in ('identical',)})}")
    with open(os.path.join(SHARED, "check.json"), "w") as fh:
        json.dump(v, fh, indent=2)


if __name__ == "__main__":
    main()
