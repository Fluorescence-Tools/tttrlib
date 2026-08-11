#!/usr/bin/env python
# SPDX-License-Identifier: BSD-3-Clause
"""Print the failures out of a pytest --report-log file.

pytest writes its FAILURES section when the session ends, and on Windows that
section never arrives: the run reaches 100%, returns 1, and the log jumps
straight to the exit code with no traceback for any of the seventy tests that
failed. Whatever eats it -- the documented heap corruption at interpreter
shutdown is the suspect -- it takes the one thing worth having with it.

--report-log writes one JSON object per test *as that test finishes*, so a
report that dies at the end costs nothing. This reads that file back and prints
what the terminal did not.

Usage:  python tools/print_report_log.py <report.jsonl> [more.jsonl ...]

Exit status is 0 whatever it finds: the pytest run has already decided whether
the build fails, and this only reports.
"""
import json
import sys
from pathlib import Path


def failures(path):
    """Yield (nodeid, when, longrepr) for every failing phase in the log."""
    for line in Path(path).read_text(errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            entry = json.loads(line)
        except ValueError:
            continue  # a half-written last line is exactly what a crash leaves
        if entry.get("$report_type") != "TestReport":
            continue
        if entry.get("outcome") != "failed":
            continue
        yield entry.get("nodeid", "?"), entry.get("when", "?"), entry.get("longrepr")


def text_of(longrepr):
    """A traceback out of the several shapes longrepr takes."""
    if longrepr is None:
        return "(no traceback recorded)"
    if isinstance(longrepr, str):
        return longrepr
    if isinstance(longrepr, dict):
        for key in ("longrepr", "reprcrash", "message"):
            value = longrepr.get(key)
            if isinstance(value, str):
                return value
            if isinstance(value, dict) and isinstance(value.get("message"), str):
                return value["message"]
        return json.dumps(longrepr)[:2000]
    return str(longrepr)


def main(argv):
    if not argv:
        print(__doc__)
        return 0
    total = 0
    for path in argv:
        if not Path(path).exists():
            print("no report log at %s" % path)
            continue
        found = list(failures(path))
        total += len(found)
        print("=" * 72)
        print("%d failing phases in %s" % (len(found), path))
        print("=" * 72)
        for nodeid, when, longrepr in found:
            print("\n---- %s (%s) ----" % (nodeid, when))
            print(text_of(longrepr))
    print("\n%d failing phases in total" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
