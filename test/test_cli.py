"""
Tests for the tttrlib command-line interface (bin/tttrlib).

Uses subprocess to mirror exactly what bioconda does:
  ``chmod 0755 $SRC_DIR/bin/*``
  ``cp -f $SRC_DIR/bin/* $PREFIX/bin``
  test command: ``tttrlib --help``
"""

import subprocess
import sys
import os

import pytest


# ---------------------------------------------------------------------------
# Path to the CLI script
# ---------------------------------------------------------------------------

BIN_SCRIPT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "bin", "tttrlib"
)


def _run(*args):
    """Run ``python bin/tttrlib <args>`` and return CompletedProcess."""
    return subprocess.run(
        [sys.executable, BIN_SCRIPT] + list(args),
        capture_output=True,
        text=True,
    )


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestCLI:
    """
    Subprocess-based tests that mirror the bioconda recipe test section.

    bioconda master recipe tests:
        imports: tttrlib
        commands: tttrlib --help
    """

    def test_help_exits_zero(self):
        """``tttrlib --help`` must exit 0 — this is the bioconda CI test."""
        result = _run("--help")
        assert result.returncode == 0, (
            f"tttrlib --help exited {result.returncode}\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

    def test_help_shows_subcommands(self):
        """``tttrlib --help`` output must list trace and image subcommands."""
        result = _run("--help")
        assert result.returncode == 0
        combined = result.stdout + result.stderr
        assert "trace" in combined
        assert "image" in combined

    def test_trace_help(self):
        """``tttrlib trace --help`` must exit 0 and list correlate."""
        result = _run("trace", "--help")
        assert result.returncode == 0, f"stderr: {result.stderr}"
        assert "correlate" in result.stdout + result.stderr

    def test_image_help(self):
        """``tttrlib image --help`` must exit 0 and list export."""
        result = _run("image", "--help")
        assert result.returncode == 0, f"stderr: {result.stderr}"
        assert "export" in result.stdout + result.stderr

    def test_trace_correlate_help(self):
        """``tttrlib trace correlate --help`` must exit 0."""
        result = _run("trace", "correlate", "--help")
        assert result.returncode == 0, f"stderr: {result.stderr}"
        combined = result.stdout + result.stderr
        assert "ch1" in combined or "channel" in combined.lower()

    def test_image_export_help(self):
        """``tttrlib image export --help`` must exit 0."""
        result = _run("image", "export", "--help")
        assert result.returncode == 0, f"stderr: {result.stderr}"

    def test_no_args_shows_usage(self):
        """No arguments prints usage to stderr (Click groups exit 2)."""
        result = _run()
        # Click group with no subcommand prints help to stderr and exits 2
        combined = result.stdout + result.stderr
        assert "Usage:" in combined or "Commands:" in combined

    def test_trace_correlate_missing_required_option(self):
        """``trace correlate`` without required -ch1 must fail non-zero."""
        result = _run("trace", "correlate")
        assert result.returncode != 0

    def test_bin_script_exists(self):
        """bin/tttrlib must exist as a file (required for bioconda build.sh cp)."""
        assert os.path.isfile(BIN_SCRIPT), f"Missing: {BIN_SCRIPT}"

    def test_bin_script_has_shebang(self):
        """bin/tttrlib must start with a Python shebang (for bioconda chmod/cp)."""
        with open(BIN_SCRIPT, "r") as f:
            first_line = f.readline()
        assert first_line.startswith("#!"), "Missing shebang line"
        assert "python" in first_line.lower(), "Shebang must reference python"
