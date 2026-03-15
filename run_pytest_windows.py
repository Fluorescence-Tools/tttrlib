"""
Windows pytest runner that isolates the pytest exit code from any
subsequent heap-corruption crash during Python interpreter shutdown.

Usage: python run_pytest_windows.py [pytest args...] --exitcode-file=<path>

The script runs pytest in a subprocess, writes the exit code to a file,
then exits with code 0. The caller reads the file to get the real result.
"""
import subprocess
import sys
import os

def main():
    args = sys.argv[1:]
    exitcode_file = "pytest_exitcode.txt"

    # Pull out --exitcode-file=<path> if present
    filtered = []
    for a in args:
        if a.startswith("--exitcode-file="):
            exitcode_file = a.split("=", 1)[1]
        else:
            filtered.append(a)

    cmd = [sys.executable, "-m", "pytest"] + filtered
    result = subprocess.run(cmd)
    code = result.returncode

    with open(exitcode_file, "w") as f:
        f.write(str(code))

    # Exit cleanly so any shutdown crash doesn't corrupt the file write above.
    # The calling shell reads the file for the real code.
    sys.exit(0)

if __name__ == "__main__":
    main()
