"""A/B of the phasor kernels against ``phasorpy`` (2026-08-17).

`DecayPhasor.compute_phasor_bincounts` / `compute_phasor` and the streaming
`StreamingPhasor` are compared with phasor coordinates recorded from
phasorpy 0.4 (`phasor_from_signal` for the raw phasor, `phasor_to_polar` +
`phasor_transform` for the IRF calibration) on Poisson-sampled decays and IRFs.
The fixture `test/data/reference/phasor_phasorpy_reference.npz` stores the
histograms next to the results, so no RNG agreement with the generator
(`gen_phasor_phasorpy_reference.py`) is needed; phasorpy itself is not a
project dependency and is not imported here.

Conventions bridged: phasorpy's harmonic h over an N-bin signal is a per-bin
frequency of h/N, which is what `DecayPhasor` takes as `frequency`
(phase = 2*pi*frequency*bin); phasorpy's calibration (rotate by -phase_ref,
scale by 1/mod_ref) is the complex division `DecayPhasor.g/s` implement.
"""
import os
import unittest

import numpy as np
import tttrlib

FIXTURE = os.path.join(os.path.dirname(__file__), "..", "..", "data", "reference",
                       "phasor_phasorpy_reference.npz")


def _cases():
    d = np.load(FIXTURE)
    for i in range(int(d["n_cases"])):
        yield dict(counts=d[f"case{i}_counts"], irf_counts=d[f"case{i}_irf_counts"],
                   harmonic=int(d[f"case{i}_harmonic"]), raw=d[f"case{i}_raw"],
                   irf=d[f"case{i}_irf"], calibrated=d[f"case{i}_calibrated"])


def _v(a):
    return tttrlib.VectorInt32([int(x) for x in a])


@unittest.skipUnless(os.path.exists(FIXTURE), "phasorpy reference fixture missing")
class TestDecayPhasorAgainstPhasorpy(unittest.TestCase):

    def test_bincounts_raw_irf_and_calibrated(self):
        P = tttrlib.DecayPhasor.compute_phasor_bincounts
        for i, c in enumerate(_cases()):
            with self.subTest(case=i, harmonic=c["harmonic"]):
                f = c["harmonic"] / len(c["counts"])
                raw = np.asarray(P(_v(c["counts"]), f, 1, 1.0, 0.0))
                irf = np.asarray(P(_v(c["irf_counts"]), f, 1, 1.0, 0.0))
                cal = np.asarray(P(_v(c["counts"]), f, 1, irf[0], irf[1]))
                np.testing.assert_allclose(raw, c["raw"], rtol=0, atol=1e-13)
                np.testing.assert_allclose(irf, c["irf"], rtol=0, atol=1e-13)
                np.testing.assert_allclose(cal, c["calibrated"], rtol=0, atol=1e-13)

    def test_the_array_form_phasor_of_bincounts_agrees(self):
        for i, c in enumerate(_cases()):
            with self.subTest(case=i):
                f = c["harmonic"] / len(c["counts"])
                got = np.asarray(tttrlib.DecayPhasor.phasor_of_bincounts(
                    c["counts"].astype(np.int32), f, 1, 1.0, 0.0))
                np.testing.assert_allclose(got, c["raw"], rtol=0, atol=1e-13)

    def test_the_microtime_list_form_equals_the_histogram_form(self):
        """`DecayPhasor.compute_phasor(microtimes, ...)` (photon list) and
        `phasor_of_bincounts` on the same photons' histogram give the same
        phasor, and so does the index-selection form on a subset. Until
        2026-08-17 the list form had no NumPy typemap and was unreachable from
        Python (found by this A/B, fixed the same day)."""
        for i, c in enumerate(_cases()):
            with self.subTest(case=i):
                mt = np.repeat(np.arange(len(c["counts"]), dtype=np.uint16), c["counts"])
                f = c["harmonic"] / len(c["counts"])
                lst = np.asarray(tttrlib.DecayPhasor.compute_phasor(mt, f, 1, 1.0, 0.0))
                np.testing.assert_allclose(lst, c["raw"], rtol=0, atol=1e-12)
                sel = np.arange(0, mt.size, 3, dtype=np.int32)
                got = np.asarray(tttrlib.DecayPhasor.compute_phasor_selection(mt, sel, f, 1, 1.0, 0.0))
                bc = np.bincount(mt[sel], minlength=len(c["counts"])).astype(np.int32)
                ref = np.asarray(tttrlib.DecayPhasor.phasor_of_bincounts(bc, f, 1, 1.0, 0.0))
                np.testing.assert_allclose(got, ref, rtol=0, atol=1e-12)

    def test_calibration_is_the_complex_division(self):
        rng = np.random.default_rng(0)
        for _ in range(20):
            gi, si, ge, se = rng.uniform(-1, 1, 4)
            z = complex(ge, se) / complex(gi, si)
            self.assertAlmostEqual(tttrlib.DecayPhasor.g(gi, si, ge, se), z.real, places=13)
            self.assertAlmostEqual(tttrlib.DecayPhasor.s(gi, si, ge, se), z.imag, places=13)


@unittest.skipUnless(os.path.exists(FIXTURE), "phasorpy reference fixture missing")
class TestStreamingPhasorAgainstPhasorpy(unittest.TestCase):
    """StreamingPhasor takes a laser frequency and a bin width; choose them so
    omega*dt equals phasorpy's 2*pi*h/N per bin and the raw phasor must agree."""

    def test_raw_phasor(self):
        for i, c in enumerate(_cases()):
            with self.subTest(case=i):
                n = len(c["counts"])
                freq_MHz = 80.0
                dt_s = c["harmonic"] / (n * freq_MHz * 1e6)     # omega*dt = 2 pi h / n
                sp = tttrlib.StreamingPhasor(freq_MHz, n, dt_s)
                mt = np.repeat(np.arange(n, dtype=np.uint16), c["counts"])
                for m in mt:
                    sp.push_photon(int(m))
                got = np.asarray(sp.get_phasor())
                np.testing.assert_allclose(got[:2], c["raw"], rtol=0, atol=1e-11)
                self.assertEqual(int(got[2]), int(c["counts"].sum()))


if __name__ == "__main__":
    unittest.main()
