"""Where the controlled vocabulary lives, for the tests that check against it.

**mmfdb is the only naming repository.** Every controlled term a `.pto` carries
-- an operation type, an artifact kind, a row grain, a data format, a
relationship, a unit, a burst-column name -- is defined in mmfdb's dictionaries
and nowhere else. tttrlib used to keep its own copy in
`okf/nomenclature/mmfdb.dic`; a copy of a vocabulary drifts from it, that one
had drifted by eighteen terms, and the registry and the writer had both been
brought into agreement with the drifted copy rather than with mmfdb.

So there is no local dictionary any more, and this is how the tests find the
real one. See `okf/specs/mmfdb-is-the-vocabulary.md`.
"""
from __future__ import annotations

import os
import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]


def dictionaries() -> list[pathlib.Path]:
    """Every mmfdb `.dic`: installed package, `$MMFDB_DIC_DIR`, or a sibling checkout."""
    roots = []
    try:
        import mmfdb  # noqa: F401

        roots.append(pathlib.Path(mmfdb.__file__).parent / "data")
    except Exception:  # noqa: BLE001
        pass
    env = os.environ.get("MMFDB_DIC_DIR")
    if env:
        roots.append(pathlib.Path(env))
    roots.append(REPO.parent / "mmfdb" / "src" / "mmfdb" / "data")
    for root in roots:
        if root.is_dir():
            found = sorted(root.glob("*.dic"))
            if found:
                return found
    return []


WHERE_WE_LOOKED = (
    "mmfdb not found; looked for the installed package, $MMFDB_DIC_DIR, and "
    "../mmfdb/src/mmfdb/data"
)

#: Set in CI. Turns "mmfdb is not here" from a skip into a failure.
#:
#: A skipped conformance test reads as a pass in a CI summary, so a vocabulary
#: check that silently does not run is worse than no check -- it is the same
#: closed loop it exists to break, one level up. Locally a skip is right: a
#: developer without mmfdb checked out should not be blocked. In CI it is never
#: right, because CI is the only place the check protects anybody.
REQUIRED = bool(os.environ.get("MMFDB_REQUIRED"))


def require() -> None:
    """Raise if mmfdb is mandatory here and missing. Call before skipping."""
    if REQUIRED and not dictionaries():
        raise AssertionError(
            "MMFDB_REQUIRED is set and " + WHERE_WE_LOOKED + ". The vocabulary "
            "tests would have skipped, which in CI is indistinguishable from "
            "passing."
        )


def text() -> str:
    """Every mmfdb dictionary concatenated — one namespace, several files."""
    return "\n".join(p.read_text(errors="replace") for p in dictionaries())


def enumeration(item: str, source: str | None = None) -> set[str]:
    """The `_item_enumeration.value` list of one item.

    Hand-rolled rather than through an mmCIF parser: tttrlib must not acquire a
    dependency to run its own tests. A `_item_enumeration.detail` column is
    tolerated by taking the first whitespace-separated token.
    """
    body = source if source is not None else text()
    match = re.search(r"save_" + re.escape(item) + r"\b(.*?)\nsave_", body, re.S)
    if not match:
        return set()
    block = match.group(1)
    if "_item_enumeration.value" not in block:
        return set()
    tail = block.split("_item_enumeration.value", 1)[1]
    tail = tail.split("_item_enumeration.detail", 1)[-1]
    out = set()
    for line in tail.splitlines():
        line = line.strip()
        if not line or line[0] in "_#;":
            continue
        if line.startswith(("save_", "loop_")):
            break
        out.add(line.split()[0].strip('"'))
    return out
