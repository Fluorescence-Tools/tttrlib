"""Record phasor coordinates from ``phasorpy`` for ``test_ab_phasor_reference.py``.

Run under an environment that has phasorpy (it is NOT a project dependency)::

    uv venv /tmp/phasorpy_env && uv pip install -p /tmp/phasorpy_env/bin/python phasorpy
    /tmp/phasorpy_env/bin/python test/python/clsm/gen_phasor_phasorpy_reference.py

The inputs (histograms) are stored beside the outputs so the pytest needs no
RNG agreement with this script. Recorded 2026-08-17 with phasorpy 0.4.
"""
import os
import numpy as np
import phasorpy
from phasorpy.phasor import phasor_from_signal, phasor_to_polar, phasor_transform

OUT = os.path.join(os.path.dirname(__file__), "..", "..", "data", "reference",
                   "phasor_phasorpy_reference.npz")

rng = np.random.default_rng(20260817)
n_bins = 256
records = {}
cases = []
for k, (tau_bins, irf_pos, irf_sigma, n_photons) in enumerate([
        (20.0, 12.0, 2.0, 200000),
        (60.0, 30.0, 4.5, 50000),
        (5.0, 8.0, 1.0, 20000),
        (100.0, 20.0, 3.0, 5000),
]):
    t = np.arange(n_bins)
    irf = np.exp(-0.5 * ((t - irf_pos) / irf_sigma) ** 2)
    decay = np.convolve(irf, np.exp(-t / tau_bins))[:n_bins]
    counts = rng.poisson(decay / decay.sum() * n_photons).astype(np.int64)
    irf_counts = rng.poisson(irf / irf.sum() * n_photons).astype(np.int64)
    for harmonic in (1, 2):
        # phasorpy: sample_phase runs over the full axis; harmonic h means
        # frequency h / n_bins per bin, which is what tttrlib takes as `frequency`.
        _, re_d, im_d = phasor_from_signal(counts.astype(float), harmonic=harmonic)
        _, re_i, im_i = phasor_from_signal(irf_counts.astype(float), harmonic=harmonic)
        ph_i, mod_i = phasor_to_polar(re_i, im_i)
        # calibrate the decay phasor by the IRF phasor: rotate by -phase, scale by 1/mod
        re_c, im_c = phasor_transform(re_d, im_d, -ph_i, 1.0 / mod_i)
        cases.append(dict(
            counts=counts, irf_counts=irf_counts, harmonic=harmonic,
            raw=np.array([re_d, im_d]), irf=np.array([re_i, im_i]),
            calibrated=np.array([re_c, im_c])))

for i, c in enumerate(cases):
    for key, v in c.items():
        records[f"case{i}_{key}"] = np.asarray(v)
records["n_cases"] = np.array(len(cases))
records["phasorpy_version"] = np.array(phasorpy.__version__)
np.savez_compressed(OUT, **records)
print("wrote", OUT, len(cases), "cases")
