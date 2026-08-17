#!/usr/bin/env python
"""Record BrightEyes-ISM / PyFocus vectorial PSFs for ``TestVectorialPsfAgainstPyFocus``.

Runs under the vicidomini venv (``benchmarks/.venvs/vicidomini/bin/python``,
torch on the CPU; set ``KMP_DUPLICATE_LIB_OK=TRUE`` next to tttrlib's OpenMP).
``brighteyes_ism.simulation.PSF_sim.singlePSF`` drives PyFocus's
``VectorialCartesianPropagator`` (Richards-Wolf on a Cartesian pupil grid,
apodised, chirp-z to the focal plane). Conventions recorded here because the
A/B needs them: PyFocus indexes the PSF ``[x, y]`` (``meshgrid(indexing='ij')``),
and its focal grid is ``linspace(-fov/2, fov/2, Nx)`` so the pixel is
``fov / (Nx - 1)``, not ``fov / Nx``.

    KMP_DUPLICATE_LIB_OK=TRUE benchmarks/.venvs/vicidomini/bin/python \\
        test/python/clsm/gen_ab_vectorial_psf_pyfocus_reference.py
"""
import os
import warnings

import numpy as np

warnings.simplefilter("ignore")
from importlib.metadata import version as _pkg_version  # noqa: E402
import brighteyes_ism.simulation.PSF_sim as P  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "data", "reference",
                   "vectorial_psf_pyfocus_reference.npz")

NX, PX_NOMINAL, NA, N, WL, PUPIL = 161, 8.0, 1.4, 1.518, 520.0, 1200
CASES = {                      # name: (gamma, beta, z_nm)
    "x_z0": (0.0, 0.0, 0.0),
    "y_z0": (90.0, 0.0, 0.0),
    "circular_z0": (45.0, 90.0, 0.0),
    "x_z400": (0.0, 0.0, 400.0),
    "circular_z400": (45.0, 90.0, 400.0),
}


def main():
    out = {"cases": np.array(list(CASES)), "brighteyes_ism_version": np.array(_pkg_version("brighteyes-ism")), "psf_generator_version": np.array(_pkg_version("psf-generator")),
           "nx": np.array(NX), "pixel_nm": np.array(NX * PX_NOMINAL / (NX - 1)), "na": np.array(NA),
           "n_immersion": np.array(N), "wavelength_nm": np.array(WL), "pupil_samples": np.array(PUPIL)}
    for name, (gamma, beta, z) in CASES.items():
        par = P.simSettings(na=NA, n=N, wl=WL, gamma=gamma, beta=beta, field="PlaneWave", mask=None, mask_sampl=PUPIL)
        psf, _ = P.singlePSF(par, PX_NOMINAL, NX, (z, z), 1, device="cpu")
        psf = np.asarray(psf.detach().cpu().numpy())[0]
        out[f"{name}/psf"] = (psf / psf.max()).T          # -> [y, x], peak-normalised
        out[f"{name}/z_nm"] = np.array(z)
        print(name, "peak at", np.unravel_index(psf.argmax(), psf.shape))
    np.savez_compressed(OUT, **out)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
