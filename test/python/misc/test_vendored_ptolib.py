# SPDX-License-Identifier: BSD-3-Clause
"""The ptolib headers under thirdparty/ptolib are ptolib's own, and stay so.

The PTO container and the DataStore are written once, in ptolib
(https://github.com/tpeulen/ptolib), and carried here under thirdparty/ptolib
as verbatim copies -- a clone has no ptolib checkout beside it, so a symlink
would dangle there and is refused here (`tools/sync_ptolib.sh --link` is a
working-tree convenience only). IMP.bff carries
the same header under include/internal, chimol the Python one. A copy diverges
silently, and a container written by one library that the other cannot open
is exactly the defect ptolib exists to end -- hence a test, not a convention.
Never edit the files here.

Skips when the sibling checkout is absent (a wheel or conda build), like
IMP.bff's test_vendored_headers.py.
"""
import hashlib
import os
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_TTTRLIB = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
_PTOLIB = os.environ.get("PTOLIB_CHECKOUT", os.path.join(os.path.dirname(_TTTRLIB), "ptolib"))

PAIRS = [
    ("thirdparty/ptolib/ptolib.h", "include/ptolib/ptolib.h"),
    ("thirdparty/ptolib/pto_tui.hpp", "include/ptolib/pto_tui.hpp"),
]


def _sha(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


@unittest.skipUnless(os.path.isdir(os.path.join(_PTOLIB, "include", "ptolib")),
                     "no ptolib checkout beside this one")
class TestVendoredPtolib(unittest.TestCase):
    def test_copies_are_verbatim(self):
        for ours, theirs in PAIRS:
            a = os.path.join(_TTTRLIB, ours)
            b = os.path.join(_PTOLIB, theirs)
            self.assertTrue(os.path.isfile(a), ours)
            self.assertFalse(os.path.islink(a), f"{ours} must be a copy, not a symlink")
            self.assertTrue(os.path.isfile(b), theirs)
            self.assertEqual(_sha(a), _sha(b),
                             f"{ours} differs from ptolib's {theirs}; run tools/sync_ptolib.sh")

    def test_vendoring_record_exists(self):
        self.assertTrue(os.path.isfile(os.path.join(_TTTRLIB, "thirdparty/ptolib/VENDORING.md")))


if __name__ == "__main__":
    unittest.main()
