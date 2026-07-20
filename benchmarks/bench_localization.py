#!/usr/bin/env python3
"""Single-molecule localization benchmark — 2D Gaussian PSF fitting.

The rest of the suite does not touch `fit2DGaussian`, so the exact-gradient work
in `ImageLocalization` was invisible in `PERF.md`. This closes that gap and adds
a competitor for it.

Compared on the identical image:

* **tttrlib** `localization.fit2DGaussian` — Poisson MLE, L-BFGS with one-pass
  forward-mode AD gradients and smoothly reparameterised bounds.
* **scipy** `least_squares` on the same model — the baseline most people reach
  for. Note it minimises a *least-squares* cost, not the Poisson likelihood, so
  this is a wall-clock comparison of the usual tool for the job rather than a
  like-for-like objective.

Run in the base env. Results append to ``results/localization.jsonl``.
"""
import numpy as np

import tttrlib
from common import bench, record

XLEN = YLEN = 15
SEED = 20260720


def render(emitters, bg=5.0, sigma=2.0, xlen=XLEN, ylen=YLEN, noise_seed=None):
    """Render one or more 2D Gaussians on a pixel grid."""
    y, x = np.mgrid[0:ylen, 0:xlen]
    img = np.full((ylen, xlen), float(bg))
    for (x0, y0, amp) in emitters:
        img += amp * np.exp(-((x - x0) ** 2 + (y - y0) ** 2) / (2.0 * sigma * sigma))
    if noise_seed is not None:
        img = np.random.default_rng(noise_seed).poisson(img).astype(float)
    return img


def make_vars(guess, n_gauss, bg=5.0, sigma=2.0):
    v = [0.0] * 18
    v[0], v[1], v[2] = guess[0][0], guess[0][1], guess[0][2]
    v[3], v[4], v[5] = sigma, 1.0, bg
    if n_gauss >= 2:
        v[6], v[7], v[8] = guess[1][0], guess[1][1], guess[1][2]
    if n_gauss >= 3:
        v[9], v[10], v[11] = guess[2][0], guess[2][1], guess[2][2]
    v[14], v[15], v[16] = 0, 1, n_gauss - 1
    return v


def tttrlib_fit(img, guess, n_gauss):
    out = tttrlib.VectorDouble(make_vars(guess, n_gauss))
    tttrlib.localization.fit2DGaussian_numpy(
        out, np.ascontiguousarray(img, dtype=float))
    return list(out)


def scipy_fit(img, guess, n_gauss):
    from scipy.optimize import least_squares

    y, x = np.mgrid[0:YLEN, 0:XLEN]

    def model(p):
        sigma, bg = p[0], p[1]
        m = np.full_like(img, bg)
        for k in range(n_gauss):
            x0, y0, amp = p[2 + 3 * k: 5 + 3 * k]
            m = m + amp * np.exp(-((x - x0) ** 2 + (y - y0) ** 2) / (2.0 * sigma * sigma))
        return m

    p0 = [2.0, 5.0]
    for k in range(n_gauss):
        p0 += list(guess[k])
    res = least_squares(lambda p: (model(p) - img).ravel(), p0, method="trf")
    return res.x


def main():
    cases = [
        ("1 emitter", [(7.2, 6.8, 200.0)], [(7.0, 7.0, 150.0)], 1),
        ("2 emitters", [(4.5, 7.0, 250.0), (10.5, 7.0, 250.0)],
         [(4.0, 7.0, 200.0), (11.0, 7.0, 200.0)], 2),
        ("3 emitters", [(4.0, 4.0, 200.0), (11.0, 4.0, 200.0), (7.5, 11.0, 200.0)],
         [(4.0, 4.0, 180.0), (11.0, 4.0, 180.0), (7.5, 11.0, 180.0)], 3),
    ]

    for label, truth, guess, n_gauss in cases:
        img = render(truth, noise_seed=SEED)
        npx = XLEN * YLEN

        bench("localization", "tttrlib", f"2D Gaussian MLE, {label}",
              lambda i=img, g=guess, n=n_gauss: tttrlib_fit(i, g, n),
              repeat=7, n_items=npx, unit="pixels",
              extra={"n_gauss": n_gauss, "grid": f"{XLEN}x{YLEN}"})

        try:
            bench("localization", "scipy (least_squares)", f"2D Gaussian LSQ, {label}",
                  lambda i=img, g=guess, n=n_gauss: scipy_fit(i, g, n),
                  repeat=5, n_items=npx, unit="pixels",
                  extra={"n_gauss": n_gauss, "grid": f"{XLEN}x{YLEN}"})
        except ImportError:
            record("localization", "scipy (least_squares)", f"2D Gaussian LSQ, {label}",
                   None, None, None, status="skipped")

        # accuracy on the same image, so speed is not quoted without it
        got = tttrlib_fit(img, guess, n_gauss)
        pos = [(got[0], got[1])]
        if n_gauss >= 2:
            pos.append((got[6], got[7]))
        if n_gauss >= 3:
            pos.append((got[9], got[10]))
        err = np.mean([min(np.hypot(px - tx, py - ty) for (tx, ty, _) in truth)
                       for (px, py) in pos])
        print(f"    {label}: mean localization error {err:.4f} px")
        record("localization", "tttrlib", f"accuracy, {label}", None, None, None,
               extra={"mean_error_px": float(err), "n_gauss": n_gauss})


if __name__ == "__main__":
    main()
