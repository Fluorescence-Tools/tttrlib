#!/usr/bin/env python
"""tttrlib vs the VicidominiLab reference implementations -- tttrlib side.

Four kernels ported from (or validated against) VicidominiLab code:

  blind_irf   blind_irf_estimate      <- birfi (Gomez-Sanchez et al. 2024)
  apr         apr_reconstruction      <- BrightEyes-ISM APR_lib.APR (mode='fourier')
  focus_ism   focus_reconstruction    <- BrightEyes-ISM FocusISM_lib.focusISM
  s2ism       s2ism_reconstruction    <- s2ISM max_likelihood_reconstruction

Run in the base env. Writes the *exact* inputs to results/shared/vicidomini/ so
competitors/bench_vicidomini.py (in the ``vicidomini`` venv) times the reference
code on identical data. The A/B correctness of each pair is pinned in
test/python (test_ab_decay_reference.py::TestBlindIrfAgainstBirfi,
test_clsm_superres_ism_arrays.py, test_clsm_superres_s2ism.py); this file only
measures speed.
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tttrlib  # noqa: E402
from common import bench, RESULTS  # noqa: E402

SHARED = os.path.join(RESULTS, "shared", "vicidomini")
os.makedirs(SHARED, exist_ok=True)

SEED = 20260817


# --------------------------------------------------------------------------- inputs

def make_blind_irf(n_bins=1024, n_ch=25, dt=0.05, tau=2.5, photons=1e6, seed=SEED):
    """One decay per array-detector element: mono-exponential through per-channel
    Gaussian IRFs of slightly different position/width, Poisson noise + background."""
    rng = np.random.default_rng(seed)
    t = np.arange(n_bins) * dt
    data = np.zeros((n_bins, n_ch))
    true_irf = np.zeros((n_bins, n_ch))
    for c in range(n_ch):
        irf = np.exp(-0.5 * ((t - (10.0 + 0.02 * c)) / (0.15 + 0.004 * c)) ** 2)
        irf /= irf.sum()
        true_irf[:, c] = irf
        d = np.convolve(irf, np.exp(-t / tau))[:n_bins]
        data[:, c] = rng.poisson(d / d.sum() * photons + 20)
    return data, dt, true_irf


def make_ism_cube(n=256, side=5, pitch=1.6, sigma=2.2, photons=2000.0, seed=SEED):
    """A 5x5 array-detector cube of a two-blob object: element k is the object
    blurred by a Gaussian PSF and shifted by its detector offset (the ISM
    geometry APR undoes)."""
    rng = np.random.default_rng(seed)
    gy, gx = np.mgrid[0:n, 0:n]
    obj = (np.exp(-(((gx - n * 0.45) ** 2 + (gy - n * 0.4) ** 2) / (2 * 6.0 ** 2)))
           + 0.7 * np.exp(-(((gx - n * 0.62) ** 2 + (gy - n * 0.6) ** 2) / (2 * 4.0 ** 2))))
    idx = np.arange(side) - side // 2
    cube = np.zeros((side * side, n, n))
    for k, (iy, ix) in enumerate((a, b) for a in idx for b in idx):
        dy, dx = pitch * iy / 2.0, pitch * ix / 2.0
        psf = np.exp(-(((gx - n / 2 - dx) ** 2 + (gy - n / 2 - dy) ** 2) / (2 * sigma ** 2)))
        psf /= psf.sum()
        img = np.real(np.fft.ifft2(np.fft.fft2(obj) * np.fft.fft2(np.fft.ifftshift(psf))))
        cube[k] = rng.poisson(np.clip(img, 0, None) * photons + 2.0)
    return cube


def make_focus_cube(n=64, side=5, sigma_a=0.9, sigma_b=3.0, photons=500.0, seed=SEED):
    """Known in-focus / out-of-focus fingerprint mixture at every pixel (the
    focus-ISM test's construction, larger)."""
    rng = np.random.default_rng(seed)
    x = np.linspace(-(side // 2), side // 2, side)
    xx, yy = np.meshgrid(x, x)
    r2 = (xx ** 2 + yy ** 2).ravel()

    def fp(s):
        v = np.exp(-r2 / (2 * s ** 2))
        return v / v.sum()

    b = np.full((n, n), 0.6)
    b[:n // 4, :n // 4] = 0.2
    c0, c1 = (n - 12) // 2, (n + 12) // 2
    b[c0:c1, c0:c1] = 0.0
    ga, gb = fp(sigma_a), fp(sigma_b)
    cube = np.stack([rng.poisson(photons * ((1 - b) * ga[k] + b * gb[k])).astype(float)
                     for k in range(side * side)])
    return cube, (c0, c1), b


def make_s2ism(n=129, side=5, nz=3, seed=SEED):
    """s2ISM dataset (Nx, Ny, Nch) + PSF (Nz, Nx, Ny, Nch); odd size because the
    reference crops even ones."""
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:n, 0:n]
    nch = side * side
    psf = np.zeros((nz, n, n, nch))
    for z in range(nz):
        s = 1.6 + 0.9 * abs(z - nz // 2)
        for c in range(nch):
            dx, dy = (c % side - side // 2) * 0.8, (c // side - side // 2) * 0.8
            psf[z, :, :, c] = np.exp(-(((xx - (n // 2 + dx)) ** 2 + (yy - (n // 2 + dy)) ** 2) / (2 * s ** 2)))
    obj = np.zeros((n, n))
    for _ in range(12):
        i, j = rng.integers(10, n - 10, 2)
        obj[i, j] = rng.uniform(0.3, 1.0)
    dset = np.zeros((n, n, nch))
    for c in range(nch):
        spec = np.fft.fft2(obj) * np.fft.fft2(np.fft.ifftshift(psf[nz // 2, :, :, c]))
        dset[:, :, c] = np.real(np.fft.ifft2(spec))
    dset = rng.poisson(np.clip(dset, 0, None) * 800.0 + 1.0).astype(float)
    return dset, psf


# --------------------------------------------------------------------------- run

def main():
    # 1. blind IRF
    data, dt, true_irf = make_blind_irf()
    n_bins, n_ch = data.shape
    rl_iter = 500
    np.savez(os.path.join(SHARED, "blind_irf.npz"), data=data, dt=dt, rl_iterations=rl_iter, true_irf=true_irf)
    flat = data.ravel().tolist()

    def run_blind():
        return tttrlib.blind_irf_estimate(flat, n_bins, n_ch, dt, rl_iter, 3, 11, 3)

    bench("blind_irf", "tttrlib", f"blind IRF, {n_ch} ch x {n_bins} bins, {rl_iter} RL iterations",
          run_blind, repeat=5, warmup=1, n_items=n_ch, unit="channel", dataset="simulated",
          extra={"n_bins": n_bins, "rl_iterations": rl_iter})

    # 2. APR
    cube = make_ism_cube()
    n_det, ny, nx = cube.shape
    ref = n_det // 2
    np.savez(os.path.join(SHARED, "apr.npz"), cube=cube, usf=10, ref=ref, filter_sigma=1.0)

    def run_apr():
        return tttrlib.CLSMSuperRes.apr_reconstruction(cube, usf=10, ref_idx=ref, filter_sigma=1.0)

    bench("apr", "tttrlib", f"APR, {n_det} elements x {ny}x{nx}, usf 10",
          run_apr, repeat=5, warmup=1, n_items=ny * nx, unit="pixel", dataset="simulated",
          extra={"n_det": n_det, "usf": 10})

    # 3. focus-ISM
    fcube, (c0, c1), b_true = make_focus_cube()
    n_det, ny, nx = fcube.shape
    np.savez(os.path.join(SHARED, "focus_ism.npz"), cube=fcube, c0=c0, c1=c1, sigma_bound=2.0, b_true=b_true)

    def run_focus():
        return tttrlib.CLSMSuperRes.focus_reconstruction(fcube, sigma_bound=2.0, threshold=0.0,
                                                          calibration_size=c1 - c0)

    bench("focus_ism", "tttrlib", f"focus-ISM, {n_det} elements x {ny}x{nx}",
          run_focus, repeat=3, warmup=1, n_items=ny * nx, unit="pixel", dataset="simulated",
          extra={"n_det": n_det})

    # 4. s2ISM
    dset, psf = make_s2ism()
    n_iter = 30
    np.savez(os.path.join(SHARED, "s2ism.npz"), dset=dset, psf=psf, n_iter=n_iter)
    data_n = np.ascontiguousarray(np.moveaxis(dset, -1, 0))
    psf_n = np.ascontiguousarray(np.moveaxis(psf, -1, 1))
    n, nch = dset.shape[0], dset.shape[-1]

    def run_s2():
        return tttrlib.CLSMSuperRes.s2ism_reconstruction(data_n, psf_n, max_iter=n_iter)

    bench("s2ism", "tttrlib", f"s2ISM, {nch} elements x {psf.shape[0]} planes x {n}x{n}, {n_iter} iterations",
          run_s2, repeat=3, warmup=1, n_items=n * n, unit="pixel", dataset="simulated",
          extra={"n_det": nch, "n_planes": int(psf.shape[0]), "iterations": n_iter})

    with open(os.path.join(SHARED, "meta.json"), "w") as fh:
        json.dump({"tttrlib": tttrlib.__version__}, fh)


if __name__ == "__main__":
    main()
