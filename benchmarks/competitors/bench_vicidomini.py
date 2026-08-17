#!/usr/bin/env python
"""VicidominiLab competitors: birfi, BrightEyes-ISM (APR / focus-ISM), s2ISM.

Loads the shared inputs written by ``bench_vicidomini.py`` and times the
reference implementations on identical data. Run inside the ``vicidomini`` venv
(``build_envs.sh``: torch CPU + the upstream packages from GitHub / PyPI).

Same computation as the tttrlib side, with the reference conventions the A/B
tests pin: birfi's estimate comes out rolled by n/2 (not undone here -- it costs
nothing); s2ISM's ``max_iter=n`` performs n+1 updates, so it is called with
n_iter-1; focusISM is calibrated on the same central patch and run with
``parallelize=False`` (its ``threading`` backend does not speed up per-pixel
scipy fits under the GIL, and the tttrlib side is single-process too).
"""
import contextlib
import io
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from common import bench, RESULTS  # noqa: E402

SHARED = os.path.join(RESULTS, "shared", "vicidomini")
OUTPUTS = {}   # reference outputs, saved for check_vicidomini.py


def quiet(fn):
    def wrapped(*a, **k):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return fn(*a, **k)
    return wrapped


def bench_birfi():
    import torch
    from birfi.birfi import Birfi
    d = np.load(os.path.join(SHARED, "blind_irf.npz"))
    data, dt, it = d["data"], float(d["dt"]), int(d["rl_iterations"])
    n_bins, n_ch = data.shape
    torch.manual_seed(0)
    tdata = torch.tensor(data, dtype=torch.float32)

    @quiet
    def run():
        b = Birfi(tdata, dt=dt, device=torch.device("cpu"))
        return b.run(rl_iterations=it)

    OUTPUTS["blind_irf"] = run().cpu().numpy()
    bench("blind_irf", "birfi (torch, CPU)", f"blind IRF, {n_ch} ch x {n_bins} bins, {it} RL iterations",
          run, repeat=3, warmup=1, n_items=n_ch, unit="channel", dataset="simulated",
          extra={"n_bins": n_bins, "rl_iterations": it, "impl": "VicidominiLab/birfi"})


def bench_apr():
    from brighteyes_ism.analysis import APR_lib
    d = np.load(os.path.join(SHARED, "apr.npz"))
    cube, usf, ref, fs = d["cube"], int(d["usf"]), int(d["ref"]), float(d["filter_sigma"])
    dset = np.ascontiguousarray(np.moveaxis(cube, 0, -1))
    n_det, ny, nx = cube.shape

    def run_fourier():
        return APR_lib.APR(dset, usf, ref, apodize=True, filter_sigma=fs, mode="fourier")

    def run_interp():
        return APR_lib.APR(dset, usf, ref, apodize=True, filter_sigma=fs, mode="interp")

    sv, res = run_fourier()
    OUTPUTS["apr_shifts"] = sv
    OUTPUTS["apr_fourier"] = res.sum(-1)
    OUTPUTS["apr_interp"] = run_interp()[1].sum(-1)
    bench("apr", "BrightEyes-ISM APR (fourier)", f"APR, {n_det} elements x {ny}x{nx}, usf {usf}",
          run_fourier, repeat=3, warmup=1, n_items=ny * nx, unit="pixel", dataset="simulated",
          extra={"n_det": n_det, "usf": usf, "mode": "fourier"})
    bench("apr", "BrightEyes-ISM APR (interp, default)", f"APR, {n_det} elements x {ny}x{nx}, usf {usf}",
          run_interp, repeat=3, warmup=1, n_items=ny * nx, unit="pixel", dataset="simulated",
          extra={"n_det": n_det, "usf": usf, "mode": "interp"})


def bench_focus():
    from brighteyes_ism.analysis import FocusISM_lib
    d = np.load(os.path.join(SHARED, "focus_ism.npz"))
    cube, c0, c1, sb = d["cube"], int(d["c0"]), int(d["c1"]), float(d["sigma_bound"])
    dset = np.ascontiguousarray(np.moveaxis(cube, 0, -1))
    n_det, ny, nx = cube.shape

    @quiet
    def run():
        return FocusISM_lib.focusISM(dset, sigma_B_bound=sb, threshold=0, apr=True,
                                     calibration=dset[c0:c1, c0:c1, :], sum_results=True,
                                     parallelize=False)

    sig, bkg, ism = run()
    OUTPUTS["focus_signal"], OUTPUTS["focus_background"], OUTPUTS["focus_ism"] = sig, bkg, ism
    bench("focus_ism", "BrightEyes-ISM focusISM", f"focus-ISM, {n_det} elements x {ny}x{nx}",
          run, repeat=2, warmup=1, n_items=ny * nx, unit="pixel", dataset="simulated",
          extra={"n_det": n_det, "impl": "scipy curve_fit per pixel"})


def bench_s2ism():
    from s2ism.s2ism import max_likelihood_reconstruction
    d = np.load(os.path.join(SHARED, "s2ism.npz"))
    dset, psf, n_iter = d["dset"], d["psf"], int(d["n_iter"])
    dset5 = dset[:, :, None, :]                     # (Nx, Ny, Nt=1, Nch)
    psf5 = psf[:, :, :, None, :]                    # (Nz, Nx, Ny, Nt=1, Nch)
    n, nch, nz = dset.shape[0], dset.shape[-1], psf.shape[0]

    @quiet
    def run():
        return max_likelihood_reconstruction(dset5, psf5, stop="fixed", max_iter=n_iter - 1,
                                             rep_to_save="last", initialization="flat", process="cpu")

    OUTPUTS["s2ism"] = np.squeeze(np.asarray(run()[0]))
    bench("s2ism", "s2ISM (torch, CPU)", f"s2ISM, {nch} elements x {nz} planes x {n}x{n}, {n_iter} iterations",
          run, repeat=3, warmup=1, n_items=n * n, unit="pixel", dataset="simulated",
          extra={"n_det": nch, "n_planes": nz, "iterations": n_iter, "impl": "VicidominiLab/s2ISM"})


def main():
    for name, fn in (("birfi", bench_birfi), ("apr", bench_apr), ("focus", bench_focus), ("s2ism", bench_s2ism)):
        try:
            fn()
        except Exception as e:  # keep the other categories going
            print(f"[vicidomini] {name} failed: {type(e).__name__}: {e}", file=sys.stderr)
    np.savez(os.path.join(SHARED, "reference_outputs.npz"), **OUTPUTS)


if __name__ == "__main__":
    main()
