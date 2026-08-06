# SPDX-License-Identifier: BSD-3-Clause
"""Loading, validating and comparing conformance cases, in Python.

Kept apart from `interpreter.py` because the generator, the pytest runner and
the matrix tool all need this and only two of them need the dispatch table.
"""
from __future__ import annotations

import json
import math
import os
from typing import Any, Dict, Iterator, List, Tuple

HERE = os.path.dirname(os.path.abspath(__file__))
CONFORMANCE_DIR = os.path.dirname(HERE)
CASES_DIR = os.path.join(CONFORMANCE_DIR, "cases")
SCHEMA_PATH = os.path.join(CONFORMANCE_DIR, "schema.json")

LANGUAGES = ("python", "r", "java", "js")

# Beyond this an IEEE double cannot hold every integer, so a runner backed by
# doubles must refuse the comparison rather than perform it approximately and
# call the result exact. Python is not one of those runners, but it is the
# generator: rejecting here stops a case that only Python could ever check from
# being written in the first place.
MAX_EXACT_INTEGER = 2 ** 53


def case_files() -> List[str]:
    return sorted(
        os.path.join(CASES_DIR, f)
        for f in os.listdir(CASES_DIR) if f.endswith(".json"))


def load_all() -> List[Tuple[str, Dict[str, Any]]]:
    """Every (area, case) pair, in file then declaration order."""
    out = []
    seen = set()
    for path in case_files():
        with open(path, encoding="utf-8") as fh:
            doc = json.load(fh)
        area = doc["area"]
        for case in doc["cases"]:
            if case["id"] in seen:
                raise ValueError(f"duplicate case id {case['id']} in {path}")
            seen.add(case["id"])
            out.append((area, case))
    return out


def validate_all() -> None:
    """Check every case file against the schema, if jsonschema is installed.

    Optional on purpose: the schema is there so a malformed case fails fast for
    whoever is editing it, and a contributor without the package should still be
    able to run the suite.
    """
    try:
        import jsonschema
    except ImportError:  # pragma: no cover - depends on the environment
        return
    with open(SCHEMA_PATH, encoding="utf-8") as fh:
        schema = json.load(fh)
    for path in case_files():
        with open(path, encoding="utf-8") as fh:
            jsonschema.validate(json.load(fh), schema)


def check_expectations_are_comparable(case: Dict[str, Any]) -> None:
    """Reject an expectation no double-backed runner could ever check.

    `tolerance: 0` promises exact equality. R has one numeric type and
    JavaScript's plain numbers are doubles, so an exact integer expectation
    above 2^53 would silently become an approximate one in two of the four
    languages -- the exact failure this suite exists to make impossible.
    """
    tol = case.get("tolerance", {})
    for key, value in case["expect"].items():
        for scalar in _flatten(value):
            if isinstance(scalar, bool) or not isinstance(scalar, int):
                continue
            if abs(scalar) >= MAX_EXACT_INTEGER and not tol.get(key):
                raise ValueError(
                    f"{case['id']}: expected integer {key}={scalar} is at or "
                    f"above 2^53, which a double-backed runner cannot compare "
                    f"exactly. Reduce it, or give it a tolerance and say why.")


def _flatten(value: Any) -> Iterator[Any]:
    if isinstance(value, list):
        for v in value:
            yield from _flatten(v)
    else:
        yield value


def dumps(obj: Any, width: int = 112) -> str:
    """JSON that stays readable as a diff.

    `json.dumps(indent=2)` puts every element of every array on its own line,
    which turns `"args": [3, 5]` into four lines and a fifty-step case into a
    wall. This is the usual fit-or-expand printer: a value goes on one line when
    it fits, and only otherwise expands. A changed expectation then shows up as
    one changed line, which is the whole point of reviewing these as diffs.
    """
    return _fmt(obj, 0, width) + "\n"


def _fmt(obj: Any, indent: int, width: int, col: int = None) -> str:
    # `indent` is where continuation lines go; `col` is where this value starts,
    # which is further right when it follows a key. Only the fit test wants the
    # second, and conflating them is what produces hanging indents.
    col = indent if col is None else col
    flat = json.dumps(obj, ensure_ascii=False)
    if col + len(flat) <= width or not isinstance(obj, (dict, list)):
        return flat

    pad, inner = " " * indent, " " * (indent + 2)
    if isinstance(obj, dict):
        parts = []
        for k, v in obj.items():
            key = json.dumps(k, ensure_ascii=False)
            body = _fmt(v, indent + 2, width, col=indent + 2 + len(key) + 2)
            parts.append(f"{inner}{key}: {body}")
        return "{\n" + ",\n".join(parts) + "\n" + pad + "}"
    parts = [inner + _fmt(v, indent + 2, width) for v in obj]
    return "[\n" + ",\n".join(parts) + "\n" + pad + "]"


def compare(expected: Any, actual: Any, tolerance: float) -> Tuple[bool, str]:
    """Compare one expectation. Returns (ok, why-not)."""
    if isinstance(expected, list):
        if not isinstance(actual, list):
            return False, f"expected a list of {len(expected)}, got {actual!r}"
        if len(expected) != len(actual):
            return False, f"expected {len(expected)} elements, got {len(actual)}"
        for i, (e, a) in enumerate(zip(expected, actual)):
            ok, why = compare(e, a, tolerance)
            if not ok:
                return False, f"[{i}]: {why}"
        return True, ""

    if isinstance(expected, bool) or isinstance(actual, bool):
        # Guarded first: in Python `True == 1`, and a boolean expectation that
        # silently accepted 1 would hide a runner returning the wrong kind.
        if bool(expected) != bool(actual) or isinstance(expected, bool) != isinstance(actual, bool):
            return False, f"expected {expected!r}, got {actual!r}"
        return True, ""

    if isinstance(expected, str):
        return (expected == actual, f"expected {expected!r}, got {actual!r}")

    if not isinstance(actual, (int, float)):
        return False, f"expected a number, got {actual!r}"

    if not tolerance:
        return (expected == actual, f"expected {expected!r}, got {actual!r}")

    if math.isnan(expected) or math.isnan(actual):
        return False, "NaN does not compare; a case must not expect one"
    scale = max(abs(expected), abs(actual), 1e-300)
    if abs(expected - actual) <= tolerance * scale:
        return True, ""
    return False, (f"expected {expected!r}, got {actual!r} "
                   f"(relative error {abs(expected - actual) / scale:.3g} "
                   f"> {tolerance:g})")
