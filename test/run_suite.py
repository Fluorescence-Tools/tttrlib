#!/usr/bin/env python
# SPDX-License-Identifier: BSD-3-Clause
"""Run the Python suite one directory at a time.

pytest prints its FAILURES section when the session ends. A segmentation fault
in the C++ layer ends the process instead, and every failure the session had
already collected dies with it -- which is how a CI log comes back with twenty
red test names and not one traceback to explain any of them.

Running each top-level directory as its own pytest session costs a few seconds
of interpreter startup per group and keeps the report of every group that did
not crash. A crash is still a failure: the group's exit status is reported and
the overall status is non-zero.

Every argument is passed through to each session:

    python test/run_suite.py -v --tb=short
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent / "python"

# pytest's "no tests were collected"; a group can be empty without that being
# an error (a data-gated directory, a platform that skips everything).
EXIT_NO_TESTS = 5


def groups():
    """The directories, then whatever sits directly in test/python."""
    for path in sorted(ROOT.iterdir()):
        if path.is_dir() and not path.name.startswith((".", "__")):
            yield path.name, [str(path)]
    loose = sorted(str(p) for p in ROOT.glob("*.py"))
    if loose:
        yield "toplevel", loose


def main(argv):
    failed = []
    for name, targets in groups():
        print(f"\n===== {name} =====", flush=True)
        status = subprocess.call([sys.executable, "-m", "pytest", *argv, *targets])
        if status not in (0, EXIT_NO_TESTS):
            failed.append((name, status))

    if failed:
        print("\nfailed groups:", flush=True)
        for name, status in failed:
            # 139/-11 is a segfault; the group's own log says where it stopped.
            print(f"  {name}: pytest exited {status}", flush=True)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
