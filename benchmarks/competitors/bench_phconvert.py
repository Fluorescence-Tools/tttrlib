#!/usr/bin/env python
"""File-reading competitor: phconvert (Ingargiola et al.) -- PTU, HT3 and SPC-130
readers on the same files tttrlib reads in bench_tttrlib.py. Run in the ``read``
venv (build_envs.sh installs ptufile and phconvert). Saves what it decoded to
results/shared/reading/phconvert_outputs.npz for check_reading.py.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from common import bench, RESULTS, data  # noqa: E402

SHARED = os.path.join(RESULTS, "shared", "reading")
os.makedirs(SHARED, exist_ok=True)
def _p(*a):
    try:
        return data(*a)
    except FileNotFoundError:
        return ""


F_PTU = _p("pq", "ptu", "pq_ptu_hh_t3.ptu")
F_HT3 = _p("imaging", "pq", "ht3", "pq_ht3_clsm.ht3")
F_SPC = _p("bh", "bh_spc132.spc")
F_SPC630 = _p("bh", "bh_spc630_256.spc")
F_SPCQC = _p("bh", "bh_spcqc004.spc")
F_SMFILE = _p("sm", "data.sm")
OUT = {}


def main():
    from phconvert import pqreader, bhreader
    if F_PTU and os.path.exists(F_PTU):
        ts, det, nano, meta = pqreader.load_ptu(F_PTU)[:4]
        OUT["ptu_ts"], OUT["ptu_det"], OUT["ptu_nano"] = ts, det, nano
        bench("file_read", "phconvert", "read PTU T3 (HydraHarp)",
              lambda: pqreader.load_ptu(F_PTU), repeat=3, warmup=1,
              n_items=int((det < 64).sum()), unit="photons", dataset="pq_ptu_hh_t3.ptu")
    if F_HT3 and os.path.exists(F_HT3):
        ts, det, nano, meta, _ = pqreader.load_ht3(F_HT3)
        OUT["ht3_ts"], OUT["ht3_det"], OUT["ht3_nano"] = ts, det, nano
        bench("file_read", "phconvert (HT3)", "read HT3 T3 (HydraHarp, CLSM)",
              lambda: pqreader.load_ht3(F_HT3), repeat=3, warmup=1,
              n_items=int((det < 64).sum()), unit="photons", dataset="pq_ht3_clsm.ht3")
    if F_SPC and os.path.exists(F_SPC):
        def read_spc():
            with open(F_SPC, "rb") as f:
                return bhreader._read_spc1xx_8xx(f)
        r = read_spc()
        OUT["spc_ts"], OUT["spc_det"], OUT["spc_nano"] = r["timestamps"], r["detectors"], r["nanotimes"]
        OUT["spc_marker_min"] = np.array(r["marker_ids"].min() if r["marker_ids"].size else 255)
        n_ph = int((r["detectors"] < OUT["spc_marker_min"]).sum())
        bench("file_read", "phconvert (SPC-130)", "read SPC-130 (Becker & Hickl)",
              read_spc, repeat=3, warmup=1, n_items=n_ph, unit="photons", dataset="bh_spc132.spc")
    if F_SPC630 and os.path.exists(F_SPC630):
        def read_spc630():
            with open(F_SPC630, "rb") as f:          # a path would re-read the header word as a record
                return bhreader._read_spc6xx_32bit(f)
        r = read_spc630()
        OUT["spc630_ts"], OUT["spc630_det"], OUT["spc630_nano"] = r["timestamps"], r["detectors"], r["nanotimes"]
        bench("file_read", "phconvert (SPC-630)", "read SPC-600/630 256-ch (Becker & Hickl)",
              read_spc630, repeat=3, warmup=1, n_items=int(r["timestamps"].size), unit="photons", dataset="bh_spc630_256.spc")
    if F_SPCQC and os.path.exists(F_SPCQC):
        def read_spcqc():
            with open(F_SPCQC, "rb") as f:
                return bhreader._read_QCX04(f)
        r = read_spcqc()
        OUT["spcqc_ts"], OUT["spcqc_det"], OUT["spcqc_nano"] = r["timestamps"], r["detectors"], r["nanotimes"]
        bench("file_read", "phconvert (SPC-QC)", "read SPC-QC-104 (Becker & Hickl)",
              read_spcqc, repeat=3, warmup=1, n_items=int(r["timestamps"].size), unit="photons", dataset="bh_spcqc004.spc")
    if F_SMFILE and os.path.exists(F_SMFILE):
        from phconvert import smreader
        ts, det, _ = smreader.load_sm(F_SMFILE, return_labels=True)
        OUT["sm_ts"], OUT["sm_det"] = ts, det
        bench("file_read", "phconvert (SM)", "read .sm (Weiss lab)",
              lambda: smreader.load_sm(F_SMFILE), repeat=3, warmup=1, n_items=int(ts.size), unit="photons", dataset="data.sm")
    np.savez(os.path.join(SHARED, "phconvert_outputs.npz"), **OUT)


if __name__ == "__main__":
    main()
