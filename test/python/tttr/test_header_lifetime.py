"""A member proxy must keep its owner alive.

`TTTR(path).header` used to segfault: `get_header` returns a pointer into the
TTTR, the temporary was collected at the end of the expression, and the proxy
was left reading freed memory (BUGS 2026-08-11). Ordinary-looking Python, a
hard crash. The same treatment covers the microtime linearizer and the CLSM
container accessors.
"""

import gc
import unittest

import tttrlib

from test_settings import settings, DATA_AVAILABLE  # type: ignore


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestMemberProxyLifetime(unittest.TestCase):

    def test_the_header_of_a_temporary_survives(self):
        h = tttrlib.TTTR(settings["spc132_filename"], "SPC-130").header
        gc.collect()
        # Reading through the proxy used to be the crash.
        self.assertGreater(h.macro_time_resolution, 0.0)

    def test_the_proxy_carries_its_owner(self):
        h = tttrlib.TTTR(settings["spc132_filename"], "SPC-130").get_header()
        self.assertIsInstance(getattr(h, "_keepalive_owner", None), tttrlib.TTTR)
