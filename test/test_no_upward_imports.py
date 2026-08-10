"""tttrlib's shipped code is the bottom of the stack and must not import upward.

The four-repository layering is tttrlib -> imp.bff -> imp-tricks -> chisurf:
tttrlib owns photons and the fluorescence algorithms that run on them, imp.bff
owns structure and dye simulation, imp-tricks adds to IMP, chisurf is the
application that binds them. imp.bff may reach *down* into tttrlib; shipped
tttrlib code may not reach up.

This is the one edge whose violation turns a layered stack into a cycle, and a
cycle here is not merely inelegant: IMP is a compiled C++ module built from a
sibling checkout, so an ``import IMP`` inside the shipped library would make a
photon library undistributable without a structural-modelling toolkit.

**The carve-out is deliberate.** ``bench/``, ``test/`` and ``tools/`` may import
chisurf, because measuring this library against its consumer, and cross-checking
results with it, is the point of those trees -- eight files do so today. What
must stay clean is what ships: ``ext/python``. A rule without that carve-out
would either be violated on day one or would delete the comparisons that keep
the two implementations honest.

The rule is written down in ../chisurf/okf/references/imp-ecosystem.md. This is
the executable half.
"""

from __future__ import annotations

import ast
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]

#: Packages tttrlib sits *below*. Importing any of them from shipped code is
#: the violation.
UPWARD = {"IMP", "imp_tricks", "chisurf"}

#: What actually ships. Everything else in the repo is build scaffolding,
#: vendored reference code, benchmarks or tests.
SHIPPED = [
    ROOT / "ext" / "python",
]


def _sources() -> list[pathlib.Path]:
    out: list[pathlib.Path] = []
    for root in SHIPPED:
        if not root.is_dir():
            continue
        out += [
            p for p in root.rglob("*.py")
            if "__pycache__" not in p.parts
        ]
    return sorted(out)


def _imported_roots(path: pathlib.Path) -> set[str]:
    try:
        tree = ast.parse(path.read_text(errors="replace"))
    except SyntaxError:
        return set()
    roots: set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            roots.update(a.name.split(".")[0] for a in node.names)
        elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
            roots.add(node.module.split(".")[0])
    return roots


def test_shipped_code_does_not_import_upward() -> None:
    assert _sources(), "found no shipped sources to check — SHIPPED is stale"
    offenders = {
        str(p.relative_to(ROOT)): sorted(bad)
        for p in _sources()
        if (bad := _imported_roots(p) & UPWARD)
    }
    assert not offenders, (
        "tttrlib is the bottom of the layering; shipped code must not import "
        "upward.\n"
        + "\n".join(f"  {f}: imports {mods}" for f, mods in offenders.items())
        + "\nMove the code that needs IMP into imp.bff, which may depend on "
          "tttrlib, rather than reaching up from here. "
          "See ../chisurf/okf/references/imp-ecosystem.md."
    )


def test_the_guard_can_actually_fail(tmp_path: pathlib.Path) -> None:
    """A guard that cannot fail is decoration."""
    probe = tmp_path / "probe.py"
    probe.write_text("import IMP.bff\nfrom chisurf import core\n")
    assert _imported_roots(probe) & UPWARD == {"IMP", "chisurf"}
