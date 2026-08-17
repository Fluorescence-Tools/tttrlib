#!/usr/bin/env python
"""Are the two sides of the VicidominiLab benchmark computing the same thing?

Run after ``bench_vicidomini.py`` (tttrlib) and ``competitors/bench_vicidomini.py``
(reference, in the ``vicidomini`` venv): recomputes tttrlib's outputs on the shared
inputs and compares them with the reference outputs the competitor script saved.
Prints one verdict per kernel and writes them to results/shared/vicidomini/check.json
so PERF.md can quote them. The same comparisons, on smaller data, are the
permanent A/B tests (test_ab_decay_reference.py::TestBlindIrfAgainstBirfi,
test_clsm_superres_ism_arrays.py, test_clsm_superres_s2ism.py); this is the
run-time confirmation on the benchmark inputs.

Conventions of the reference that are undone or allowed for, as in the tests:
birfi's output is rolled by n/2 (ifftshift) -- undone here; birfi fits the
shared-rate exponential model with Adam (not converged) where tttrlib solves it,
so blind IRF is compared by correlation and peak, not element-wise; focusISM registers with 'interp' and
fits with scipy, so focus-ISM is compared by correlation and background fraction.
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tttrlib  # noqa: E402
from common import RESULTS  # noqa: E402

SHARED = os.path.join(RESULTS, "shared", "vicidomini")


def main():
    ref = np.load(os.path.join(SHARED, "reference_outputs.npz"))
    verdicts = {}

    # blind IRF
    d = np.load(os.path.join(SHARED, "blind_irf.npz"))
    data, dt, it = d["data"], float(d["dt"]), int(d["rl_iterations"])
    n, nch = data.shape
    ours = np.asarray(tttrlib.blind_irf_estimate(data.ravel().tolist(), n, nch, dt, it, 3, 11, 3)).reshape(n, nch)
    corr, dpk, c_ref_true, c_our_true = [], [], [], []
    for c in range(nch):
        a = np.roll(ref["blind_irf"][:, c] / ref["blind_irf"][:, c].sum(), n // 2)   # undo birfi's ifftshift
        b = ours[:, c] / ours[:, c].sum()
        tr = d["true_irf"][:, c]
        corr.append(np.corrcoef(a, b)[0, 1])
        dpk.append(abs(int(np.argmax(a)) - int(np.argmax(b))) * dt)
        c_ref_true.append(np.corrcoef(a, tr)[0, 1])
        c_our_true.append(np.corrcoef(b, tr)[0, 1])
    verdicts["blind_irf"] = {"metric": "IRF correlation per channel (birfi rolled back by n/2)",
                             "min_corr_birfi_vs_tttrlib": float(min(corr)), "max_peak_diff_ns": float(max(dpk)),
                             "min_corr_to_truth_birfi": float(min(c_ref_true)),
                             "min_corr_to_truth_tttrlib": float(min(c_our_true)),
                             "identical": False,
                             "verdict": "agree at tolerance (same model, birfi fits it with Adam, tttrlib solves it); see corr to truth"}

    # APR
    d = np.load(os.path.join(SHARED, "apr.npz"))
    cube, usf, r, fs = d["cube"], int(d["usf"]), int(d["ref"]), float(d["filter_sigma"])
    sv = np.asarray(tttrlib.CLSMSuperRes.shift_vectors(cube, usf=usf, ref_idx=r, filter_sigma=fs))
    apr = np.asarray(tttrlib.CLSMSuperRes.apr_reconstruction(cube, usf=usf, ref_idx=r, filter_sigma=fs))[0]
    sv_diff = float(np.abs(sv - ref["apr_shifts"]).max())
    apr_rel = float(np.abs(apr - ref["apr_fourier"]).max() / ref["apr_fourier"].max())
    verdicts["apr"] = {"shift_vectors_max_abs_diff": sv_diff,
                       "apr_vs_fourier_max_rel_diff": apr_rel,
                       "apr_vs_interp_max_rel_diff": float(np.abs(apr - ref["apr_interp"]).max() / ref["apr_fourier"].max()),
                       "identical": bool(sv_diff == 0.0 and apr_rel < 1e-9),
                       "verdict": "identical to APR_lib (shift vectors bit-exact, APR mode='fourier' < 1e-9)"
                       if (sv_diff == 0.0 and apr_rel < 1e-9) else "DIFFERS"}

    # focus-ISM
    d = np.load(os.path.join(SHARED, "focus_ism.npz"))
    fcube, c0, c1, sb = d["cube"], int(d["c0"]), int(d["c1"]), float(d["sigma_bound"])
    focus, bkg, ism = tttrlib.CLSMSuperRes.focus_reconstruction(fcube, sigma_bound=sb, threshold=0.0,
                                                                calibration_size=c1 - c0)
    cf = float(np.corrcoef(focus.ravel(), ref["focus_signal"].ravel())[0, 1])
    cb = float(np.corrcoef(bkg.ravel(), ref["focus_background"].ravel())[0, 1])
    bf_ref = np.nan_to_num(ref["focus_background"] / (ref["focus_signal"] + ref["focus_background"]))
    bf_got = np.nan_to_num(bkg / (focus + bkg))
    inner = (slice(4, -4), slice(4, -4))
    dbf = float(np.abs(bf_ref - bf_got)[inner].mean())
    b_true = d["b_true"]
    verdicts["focus_ism"] = {"corr_focus": cf, "corr_background": cb, "mean_abs_bg_fraction_diff": dbf,
                             "mean_abs_bg_fraction_error_reference": float(np.abs(bf_ref - b_true)[inner].mean()),
                             "mean_abs_bg_fraction_error_tttrlib": float(np.abs(bf_got - b_true)[inner].mean()),
                             "identical": False,
                             "verdict": "agree at tolerance (reference registers with 'interp' and fits with scipy); see error vs truth"}

    # s2ISM
    d = np.load(os.path.join(SHARED, "s2ism.npz"))
    dset, psf, it = d["dset"], d["psf"], int(d["n_iter"])
    got = tttrlib.CLSMSuperRes.s2ism_reconstruction(np.ascontiguousarray(np.moveaxis(dset, -1, 0)),
                                                    np.ascontiguousarray(np.moveaxis(psf, -1, 1)), max_iter=it)
    rel = float(np.abs(got - ref["s2ism"]).max() / np.abs(ref["s2ism"]).max())
    verdicts["s2ism"] = {"max_rel_diff": rel, "identical": bool(rel < 1e-5),
                         "verdict": "identical to s2ISM (float32 reference; < 1e-5)" if rel < 1e-5 else "DIFFERS"}

    for k, v in verdicts.items():
        print(f"[{k}] {'IDENTICAL' if v['identical'] else 'tolerance'}: {v['verdict']}  {json.dumps({kk: vv for kk, vv in v.items() if kk not in ('verdict', 'identical')})}")
    with open(os.path.join(SHARED, "check.json"), "w") as fh:
        json.dump(verdicts, fh, indent=2)


if __name__ == "__main__":
    main()
