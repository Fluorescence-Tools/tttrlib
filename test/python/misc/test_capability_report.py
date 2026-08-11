"""The SIMD capability report must come from the compiler that built the library.

`tttrlib.TTTRLIB_COMPILE_NEON` used to be exported as a constant whose value
SWIG's own preprocessor decided — 0 on every platform, including an arm64
build whose NEON kernels were compiled in and running (BUGS 2026-08-11). The
constants are gone; `get_neon_compiled()` / `get_avx_compiled()` answer from
compiled code. `fconv_simd` / `fconv_per_simd` are deprecated aliases of the
self-dispatching `fconv` / `fconv_per`.
"""

import platform
import unittest
import warnings

import numpy as np

import tttrlib


class TestCapabilityReport(unittest.TestCase):

    def test_the_fabricated_constants_are_gone(self):
        # A constant frozen at wrap time can only lie; nothing may read one.
        for name in ("TTTRLIB_COMPILE_NEON", "TTTRLIB_COMPILE_AVX",
                     "TTTRLIB_X86_FEATURES"):
            self.assertFalse(hasattr(tttrlib, name), name)

    def test_compiled_answers_are_booleans(self):
        self.assertIsInstance(tttrlib.get_neon_compiled(), bool)
        self.assertIsInstance(tttrlib.get_avx_compiled(), bool)

    def test_enabled_implies_compiled(self):
        if tttrlib.get_neon_enabled():
            self.assertTrue(tttrlib.get_neon_compiled())
        if tttrlib.get_avx_enabled():
            self.assertTrue(tttrlib.get_avx_compiled())

    def test_the_answer_matches_the_machine(self):
        machine = platform.machine().lower()
        if machine in ("arm64", "aarch64"):
            # NEON is baseline ISA on AArch64; the kernels are always built.
            self.assertTrue(tttrlib.get_neon_compiled())
            self.assertFalse(tttrlib.get_avx_compiled())
        elif machine in ("x86_64", "amd64"):
            self.assertFalse(tttrlib.get_neon_compiled())


class TestSimdAliasDeprecation(unittest.TestCase):

    def test_fconv_simd_warns_and_matches_fconv(self):
        n = 64
        irf = np.exp(-0.5 * ((np.arange(n) - 10.0) / 2.0) ** 2)
        x = np.array([1.0, 4.1])
        want = np.zeros(n)
        tttrlib.fconv(fit=want, irf=irf, x=x, dt=0.1)

        got = np.zeros(n)
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            tttrlib.fconv_simd(fit=got, irf=irf, x=x, dt=0.1)
        self.assertTrue(any(issubclass(w.category, DeprecationWarning)
                            for w in caught))
        np.testing.assert_array_equal(got, want)

    def test_fconv_per_simd_warns_and_matches_fconv_per(self):
        n = 64
        irf = np.exp(-0.5 * ((np.arange(n) - 10.0) / 2.0) ** 2)
        x = np.array([1.0, 4.1])
        want = np.zeros(n)
        tttrlib.fconv_per(fit=want, irf=irf, x=x, period=12.0, dt=0.1)

        got = np.zeros(n)
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            tttrlib.fconv_per_simd(fit=got, irf=irf, x=x, period=12.0, dt=0.1)
        self.assertTrue(any(issubclass(w.category, DeprecationWarning)
                            for w in caught))
        np.testing.assert_array_equal(got, want)


if __name__ == "__main__":
    unittest.main()
