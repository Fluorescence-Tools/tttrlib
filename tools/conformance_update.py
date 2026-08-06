#!/usr/bin/env python
# SPDX-License-Identifier: BSD-3-Clause
"""Fill in the `expect` blocks of the conformance cases, via the Python binding.

    python tools/conformance_update.py            # every case
    python tools/conformance_update.py --area tttr
    python tools/conformance_update.py --id tttr.spc130.size
    python tools/conformance_update.py --check    # exit 1 if anything would change

**Read the diff.** An expectation that changes without an intended behaviour
change is the bug this suite exists to catch, and regenerating to make a failing
test pass is the one thing it cannot survive. This tool is deliberately not
runnable in CI; the `--check` mode is, and it only reports.

Python generates the numbers but is not the oracle: `test/python/test_conformance.py`
asserts against the committed file exactly like the other three runners, so a
Python-side regression fails Python too.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import tempfile
from typing import Any, Dict, List

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "test", "conformance", "py"))
sys.path.insert(0, os.path.join(REPO, "test", "python"))

import cases as C  # noqa: E402
from interpreter import YIELDS_COMPARABLE, Interpreter, UnsupportedOp  # noqa: E402


def data_root() -> str:
    env = (os.environ.get("TTTRLIB_DATA") or "").strip().strip("'\"")
    if env:
        return os.path.abspath(env)
    with open(os.path.join(REPO, "test", "settings.json"), encoding="utf-8") as fh:
        return os.path.join(REPO, json.load(fh).get("data_root", "tttr-data"))


def missing_data(case: Dict[str, Any], root: str) -> List[str]:
    return [d for d in case.get("data", [])
            if not os.path.exists(os.path.join(root, d))]


def to_jsonable(value: Any) -> Any:
    """The comparable form of a binding: scalars, strings, and lists of them.

    A NumPy array is deliberately NOT comparable. Array-valued bindings are
    intermediates — `tttr.macro_times` is 183657 numbers, and writing it into
    the case file would be unreviewable and would pin the decoder rather than
    the binding. A case that means to compare a whole array says so with
    `to_list`, which returns a real list and is short by construction.
    """
    import numpy as np
    if isinstance(value, np.generic):
        value = value.item()
    if isinstance(value, np.ndarray):
        raise ValueError("array-valued binding; reduce it, or use to_list")
    if isinstance(value, (list, tuple)):
        return [to_jsonable(v) for v in value]
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, str)):
        return value
    if isinstance(value, float):
        if value != value or value in (float("inf"), float("-inf")):
            raise ValueError("a case must not expect NaN or an infinity")
        return value
    raise ValueError(f"binding of type {type(value).__name__} is not comparable")


YIELDS_COMPARABLE_OR_THROWS = YIELDS_COMPARABLE | {"throws"}


def run_case(case: Dict[str, Any], root: str) -> Dict[str, Any]:
    """Run the steps and return the bindings that become the `expect` block."""
    tmpdir = tempfile.mkdtemp(prefix="tttrlib-conf-")
    try:
        tmp_paths = [os.path.join(tmpdir, f"scratch{i}")
                     for i in range(case.get("tmp", 0))]
        interp = Interpreter(root, tmp_paths)
        bindings = interp.run(case)

        out: Dict[str, Any] = {}
        for name, value in bindings.items():
            if interp.produced_by.get(name) not in YIELDS_COMPARABLE_OR_THROWS:
                continue  # raw material; see interpreter.YIELDS_COMPARABLE
            out[name] = to_jsonable(value)
        return out
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def default_tolerances(expect: Dict[str, Any], existing: Dict[str, float]) -> Dict[str, float]:
    """A float expectation gets 1e-12 unless the case already chose a number.

    Integers, booleans and strings get nothing, which means exact. Keeping the
    default in the file rather than in each runner is what stops four languages
    disagreeing about how close is close enough.
    """
    out = dict(existing)
    for key, value in expect.items():
        if key in out:
            continue
        if any(isinstance(v, float) and not isinstance(v, bool)
               for v in C._flatten(value)):
            out[key] = 1e-12
    return {k: v for k, v in out.items() if k in expect}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--area", help="only this area's case file")
    ap.add_argument("--id", help="only this case id")
    ap.add_argument("--check", action="store_true",
                    help="report what would change and exit 1, writing nothing")
    args = ap.parse_args()

    root = data_root()
    changed: List[str] = []
    skipped: List[str] = []
    for path in C.case_files():
        with open(path, encoding="utf-8") as fh:
            doc = json.load(fh)
        if args.area and doc["area"] != args.area:
            continue
        dirty = False
        for case in doc["cases"]:
            if args.id and case["id"] != args.id:
                continue
            if "python" in case.get("unsupported", {}):
                skipped.append(f"{case['id']}: unsupported in the generator's own language")
                continue
            gone = missing_data(case, root)
            if gone:
                skipped.append(f"{case['id']}: missing {', '.join(gone)}")
                continue
            try:
                produced = run_case(case, root)
            except UnsupportedOp as e:
                skipped.append(f"{case['id']}: op '{e}' is not implemented in Python")
                continue

            expect = produced
            tolerance = default_tolerances(expect, case.get("tolerance", {}))

            probe = dict(case, expect=expect, tolerance=tolerance)
            C.check_expectations_are_comparable(probe)

            if case.get("expect") != expect or case.get("tolerance", {}) != tolerance:
                changed.append(case["id"])
                dirty = True
                case["expect"] = expect
                if tolerance:
                    case["tolerance"] = tolerance
                else:
                    case.pop("tolerance", None)

        if dirty and not args.check:
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(C.dumps(doc))

    # After writing, not before: a case is seeded with an empty `expect`, which
    # the schema rightly rejects, and this tool is what fills it.
    if not args.check:
        C.validate_all()

    for s in skipped:
        print(f"skipped  {s}", file=sys.stderr)
    for c in changed:
        print(f"{'would change' if args.check else 'updated'}  {c}")

    if not changed:
        print("no expectation changed")
    if args.check and changed:
        print("\nRun tools/conformance_update.py and review the diff.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
