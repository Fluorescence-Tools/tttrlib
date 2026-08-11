"""A SWIG interface basename must be unique across the tree.

`ext/python/tttrlib.i` includes its pieces by quoted name (`%include
"Streaming.i"`), and SWIG resolves a quoted include from the *including file's
own directory first*. So a module that keeps an interface of the same basename
under `modules/*/include/` is shadowed: the copy in `ext/python/` wins, the
module's copy is never parsed, and there is no warning of any kind. Edits to
the shadowed file — new classes, numpy typemaps — silently do not reach the
built library.

That is not hypothetical: `Streaming.i` existed in both places, and for as long
as it did, `StreamingIntensityTrace` was documented, listed in CMake and tested
while being absent from the binding (BUGS 2026-08-11, fixed by another
session). The failure is silent and decided by directory-search order, so the
next module to relocate an interface inherits it. This pins that it cannot
come back.
"""

import pathlib
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]


class TestSwigInterfaceLayout(unittest.TestCase):

    def test_no_interface_basename_is_defined_twice(self):
        shadowed = {}
        for f in sorted((REPO / "ext" / "python").glob("*.i")):
            others = [p for p in (REPO / "modules").rglob(f.name)
                      if p.is_file()]
            if others:
                shadowed[f.name] = [str(p.relative_to(REPO)) for p in others]

        self.assertEqual(
            shadowed, {},
            f"a quoted %include resolves from ext/python/ first, so the "
            f"module's copy of each of these is dead code and edits to it do "
            f"nothing: {shadowed}")


if __name__ == "__main__":
    unittest.main()
