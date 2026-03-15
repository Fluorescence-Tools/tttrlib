"""
Windows pytest runner that isolates the pytest exit code from any
subsequent heap-corruption crash during Python interpreter shutdown.

Usage: python run_pytest_windows.py [pytest args...] --exitcode-file=<path>

The script runs pytest in a subprocess, captures the return code at the
Python level (before interpreter shutdown), writes it to a file, then
calls os._exit(0) to skip Python's shutdown machinery entirely.

This prevents the heap-corruption crash (0xC0000374) that occurs in
tttrlib's C extension destructors from affecting the step exit code.
The calling shell reads the exitcode file for the real pytest result.
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

    # Write exit code to file BEFORE any Python shutdown that could crash.
    with open(exitcode_file, "w") as f:
        f.write(str(code))
        f.flush()
        os.fsync(f.fileno())

    # os._exit() bypasses all atexit handlers, __del__ methods, and the
    # Python interpreter shutdown sequence entirely. This prevents the
    # C extension destructor heap crash from killing this wrapper process.
    os._exit(0)

if __name__ == "__main__":
    main()
