#!/usr/bin/env python3
"""Fail when an interface ends its `%exception` scope by clearing to nothing.

`%exception` is global state in SWIG, not a scope. `MicrotimeLinearization.i`
installs the handler the whole library relies on, and a bare `%exception;` in
any interface included *after* it removes that handler for everything that
follows -- not just for the file that wrote it.

The result is not a wrong answer. It is:

    libc++abi: terminating due to uncaught exception of type
    std::invalid_argument: ...

because a C++ throw with no handler installed terminates the interpreter
instead of raising a Python exception. It surfaces far from the cause: adding
`Fdc2D.i` in 2026-08-11 killed the *decay-fit* suite, which shares no code with
it and simply happens to be included later.

The fix is to end the scope by restoring the global handler rather than by
clearing it -- copy the block at the bottom of `ext/python/Fdc2D.i`.

Clearing is fine *before* the global handler is installed (`Cluster.i`,
`Deconvolution.i` and `Jitter.i` all do it, harmlessly), which is why this
checks position rather than banning the pattern.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MASTER = ROOT / "ext" / "python" / "tttrlib.i"
INSTALLS_GLOBAL = "MicrotimeLinearization.i"


def main():
    order = [m.group(1) for m in
             re.finditer(r'^%include\s+"([^"]+)"', MASTER.read_text(), re.M)]
    if INSTALLS_GLOBAL not in order:
        print(f"   SKIP: {INSTALLS_GLOBAL} is no longer in the include list; "
              f"this check needs updating", file=sys.stderr)
        return 0
    after = order[order.index(INSTALLS_GLOBAL) + 1:]

    offenders = []
    for name in after:
        path = ROOT / "ext" / "python" / name
        if not path.exists():
            continue
        # A bare clear: `%exception;` with nothing between it and the semicolon.
        if re.search(r'^%exception\s*;\s*$', path.read_text(), re.M):
            offenders.append(name)

    if offenders:
        print("ERROR: these are included after " + INSTALLS_GLOBAL +
              " and clear the global %exception handler, leaving every "
              "interface after them with none:", file=sys.stderr)
        for name in offenders:
            print(f"   ext/python/{name}", file=sys.stderr)
        print("\nA C++ throw with no handler TERMINATES the interpreter rather "
              "than raising. End the scope by restoring the global handler "
              "instead -- see the block at the bottom of ext/python/Fdc2D.i.",
              file=sys.stderr)
        return 1

    print(f"   OK ({len(after)} interfaces after {INSTALLS_GLOBAL}, none clear "
          f"the global handler)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
