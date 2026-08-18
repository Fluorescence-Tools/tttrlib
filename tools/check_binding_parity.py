#!/usr/bin/env python3
"""Fail when an interface file reaches Python and silently misses another binding.

The four bindings do not share one `%include` list. `ext/python/tttrlib.i`,
`ext/r/tttrlib.i`, `ext/java/tttrlib.i` and `ext/js/tttrlib.i` each keep their
own, and on 2026-08-11 they had drifted by 16-18 files -- whole subsystems
(`Cluster.i` and its HDBSCAN kernels, `Deconvolution.i`, `MaxEnt.i`,
`Streaming.i`, `Pda3cCore.i`) present in Python and absent everywhere else.

Nothing caught it. `check_swig_multilang.sh` runs SWIG four times and passes if
four wrappers *generate*; it never compares what they expose. Worse, all three
non-Python files carried a comment claiming their list was identical to
Python's, so a reader who checked found a reassurance instead of the truth.

This script does not demand parity. It demands that every gap be **written
down**: an interface missing from a binding must appear in
`binding_parity_exceptions.txt` with a reason. A gap someone chose is fine; a
gap nobody knows about is what this exists to prevent.

Exit 1 on an undeclared gap, or on an exception for a gap that no longer
exists -- a stale exception is how a list stops meaning anything.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BINDINGS = ("r", "java", "js")
EXCEPTIONS = pathlib.Path(__file__).resolve().parent / "binding_parity_exceptions.txt"


def includes(path):
    """The %include'd interface files, in order, ignoring commented-out lines."""
    text = path.read_text()
    return [m.group(1) for m in re.finditer(r'^%include\s+"([^"]+)"', text, re.M)]


def read_exceptions():
    """`file.i: r,java,js  # reason` -> {(file, binding): reason}."""
    out = {}
    if not EXCEPTIONS.exists():
        return out
    for n, raw in enumerate(EXCEPTIONS.read_text().splitlines(), 1):
        line = raw.split("#")[0].strip()
        if not line:
            continue
        if ":" not in line:
            sys.exit(f"{EXCEPTIONS}:{n}: expected `file.i: bindings  # reason`")
        name, bindings = line.split(":", 1)
        reason = raw.split("#", 1)[1].strip() if "#" in raw else ""
        if not reason:
            sys.exit(f"{EXCEPTIONS}:{n}: every exception needs a reason after `#`")
        for b in (x.strip() for x in bindings.split(",")):
            if b not in BINDINGS:
                sys.exit(f"{EXCEPTIONS}:{n}: unknown binding {b!r}")
            out[(name.strip(), b)] = reason
    return out


def check_split_modules(python):
    """The split Python extensions (ext/python/split/mod_*.i, TTTRLIB_PYTHON_SPLIT)
    must wrap exactly the fragments the monolithic ext/python/tttrlib.i wraps --
    each once. A fragment added to one list and not the other is a Python API
    that exists in one build mode only."""
    split_dir = ROOT / "ext" / "python" / "split"
    seen = {}
    for mod in sorted(split_dir.glob("mod_*.i")):
        for name in includes(mod):
            if name in ("split/common.i", "misc_types.i", "info.h", "documentation.i", "stdint.i"):
                continue
            seen.setdefault(name, []).append(mod.name)
    problems = []
    for name in python:
        if name in ("misc_types.i", "info.h", "documentation.i"):   # common.i carries these
            continue
        if name not in seen:
            problems.append(f"{name}: in ext/python/tttrlib.i but in no split module")
    for name, mods in seen.items():
        if name not in python:
            problems.append(f"{name}: in {', '.join(mods)} but not in ext/python/tttrlib.i")
        if len(mods) > 1:
            problems.append(f"{name}: wrapped by more than one split module ({', '.join(mods)})")
    return problems


def main():
    python = includes(ROOT / "ext" / "python" / "tttrlib.i")
    exceptions = read_exceptions()
    split_problems = check_split_modules(python)
    if split_problems:
        print("split Python modules disagree with ext/python/tttrlib.i:", file=sys.stderr)
        for p in split_problems:
            print("   " + p, file=sys.stderr)
        return 1

    undeclared, declared = [], set()
    for binding in BINDINGS:
        have = set(includes(ROOT / "ext" / binding / "tttrlib.i"))
        for name in python:
            if name in have:
                continue
            if (name, binding) in exceptions:
                declared.add((name, binding))
            else:
                undeclared.append((name, binding))

    stale = sorted(set(exceptions) - declared)

    if undeclared:
        print("ERROR: reachable in Python, missing from another binding, "
              "and not declared:", file=sys.stderr)
        for name, binding in undeclared:
            print(f"   {name} is missing from {binding}", file=sys.stderr)
        print(f"\nAdd each to {EXCEPTIONS.relative_to(ROOT)} with a reason, or "
              f"%include it in ext/<binding>/tttrlib.i.", file=sys.stderr)
        print("Note: an interface using IN_ARRAY/ARGOUTVIEW typemaps needs no "
              "per-language surface -- ext/r/rarrays.i, ext/java/jarrays.i and "
              "ext/js/jsarrays.i implement those same names against R vectors, "
              "Java arrays and JS TypedArrays. Adding the %include is usually "
              "the whole job.", file=sys.stderr)

    if stale:
        print("ERROR: exceptions for gaps that no longer exist -- delete them:",
              file=sys.stderr)
        for name, binding in stale:
            print(f"   {name} / {binding}", file=sys.stderr)

    if undeclared or stale:
        return 1

    print(f"   OK ({len(declared)} declared gaps, none undeclared)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
