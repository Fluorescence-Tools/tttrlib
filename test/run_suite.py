#!/usr/bin/env python
# SPDX-License-Identifier: BSD-3-Clause
"""Run the Python suite one directory at a time, several at once.

pytest prints its FAILURES section when the session ends. A segmentation fault
in the C++ layer ends the process instead, and every failure the session had
already collected dies with it -- which is how a CI log comes back with twenty
red test names and not one traceback to explain any of them.

Running each top-level directory as its own pytest session costs a few seconds
of interpreter startup per group and keeps the report of every group that did
not crash. A crash is still a failure: the group's exit status is reported and
the overall status is non-zero.

Because every group is already a separate process, running them concurrently
costs nothing in isolation -- and the groups are what made this suite take
half an hour on a four-core CI runner. Each group's output is captured and
printed as one block when that group finishes, so concurrency does not
interleave the logs into noise. `--jobs 1` restores the old serial order when
a failure is easier to read that way.

Every other argument is passed through to each session:

    python test/run_suite.py -v --tb=short
    python test/run_suite.py --jobs 1 -v
"""
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
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


def split_jobs(argv):
    """Pull `--jobs N` / `--jobs=N` out of argv; the rest is pytest's."""
    default = os.cpu_count() or 1
    rest, jobs, expecting = [], default, False
    for arg in argv:
        if expecting:
            jobs, expecting = int(arg), False
        elif arg == "--jobs":
            expecting = True
        elif arg.startswith("--jobs="):
            jobs = int(arg.split("=", 1)[1])
        else:
            rest.append(arg)
    if expecting:
        raise SystemExit("run_suite.py: --jobs needs a number")
    return rest, max(1, jobs)


def run_group(name, targets, argv):
    """One pytest session, output captured so it can be printed in one piece."""
    proc = subprocess.run(
        [sys.executable, "-m", "pytest", *argv, *targets],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
    )
    return name, proc.returncode, proc.stdout


def main(argv):
    argv, jobs = split_jobs(argv)
    work = list(groups())
    failed = []

    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(run_group, n, t, argv) for n, t in work]
        for future in futures:
            name, status, output = future.result()
            print(f"\n===== {name} =====", flush=True)
            print(output, end="", flush=True)
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
