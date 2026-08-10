"""
Tests for the ``tttr`` command-line tool.

The CLI was migrated from a Python/click script (``bin/tttrlib``) to a native
C++ binary (``tttr``). These tests exercise the binary the same way the
bioconda recipe does: ``tttr --help`` must exit 0.

In a development tree the binary lives in ``build/bin/tttr``; in an installed
environment (conda, pip wheel with the cli recipe) it is on ``PATH``.
"""

import os
import shutil
import subprocess
import sys

import pytest


# ---------------------------------------------------------------------------
# Locate the tttr binary
# ---------------------------------------------------------------------------

_REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
)

_BUILD_BIN = os.path.join(_REPO_ROOT, "build", "bin", "tttr")


def _find_tttr():
    """Return the path to the ``tttr`` binary, or None if not found.

    Prefer the build tree binary so a developer's local changes are tested
    rather than a possibly-stale installed copy.
    """
    if os.path.isfile(_BUILD_BIN) and os.access(_BUILD_BIN, os.X_OK):
        return _BUILD_BIN
    return shutil.which("tttr")


TTTR_BIN = _find_tttr()


def _run(*args):
    """Run ``tttr <args>`` and return CompletedProcess."""
    env = dict(os.environ)
    # In a dev build the shared lib sits next to the binary in build/modules.
    if TTTR_BIN and TTTR_BIN == _BUILD_BIN:
        lib_dir = os.path.join(_REPO_ROOT, "build")
        env["DYLD_LIBRARY_PATH"] = (
            lib_dir + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
        )
        env["LD_LIBRARY_PATH"] = (
            lib_dir + os.pathsep + env.get("LD_LIBRARY_PATH", "")
        )
    return subprocess.run(
        [TTTR_BIN] + list(args),
        capture_output=True,
        text=True,
        env=env,
    )


pytestmark = pytest.mark.skipif(
    TTTR_BIN is None,
    reason="tttr binary not found (not on PATH and not in build/bin)",
)


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestCLI:
    """
    Subprocess-based tests that mirror the bioconda recipe test section.

    bioconda recipe tests:
        commands: tttr --help
    """

    def test_help_exits_zero(self):
        """``tttr --help`` must exit 0 — this is the bioconda CI test."""
        result = _run("--help")
        assert result.returncode == 0, (
            f"tttr --help exited {result.returncode}\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

    def test_help_shows_subcommands(self):
        """``tttr --help`` output must list correlate and image subcommands."""
        result = _run("--help")
        assert result.returncode == 0
        combined = result.stdout + result.stderr
        assert "correlate" in combined
        assert "image" in combined

    def test_correlate_help(self):
        """``tttr correlate --help`` must exit 0 and mention channel options."""
        result = _run("correlate", "--help")
        assert result.returncode == 0, f"stderr: {result.stderr}"
        combined = result.stdout + result.stderr
        assert "ch1" in combined or "channel" in combined.lower()

    def test_image_help(self):
        """``tttr image --help`` must exit 0 and list export."""
        result = _run("image", "--help")
        assert result.returncode == 0, f"stderr: {result.stderr}"
        assert "export" in result.stdout + result.stderr

    def test_image_export_help(self):
        """``tttr image export --help`` must exit 0."""
        result = _run("image", "export", "--help")
        assert result.returncode == 0, f"stderr: {result.stderr}"

    def test_no_args_shows_usage(self):
        """No arguments prints usage and exits non-zero."""
        result = _run()
        assert result.returncode != 0
        combined = result.stdout + result.stderr
        assert "usage" in combined.lower() or "subcommand" in combined.lower()

    def test_correlate_missing_required_option(self):
        """``correlate`` without required --ch1 must fail non-zero."""
        result = _run("correlate")
        assert result.returncode != 0

    def test_unknown_subcommand(self):
        """An unknown subcommand must fail non-zero."""
        result = _run("nonexistent_subcommand")
        assert result.returncode != 0

    def test_pto_help(self):
        """``tttr pto --help`` must exit 0."""
        result = _run("pto", "--help")
        assert result.returncode == 0, f"stderr: {result.stderr}"

    def test_formats_exits_zero(self):
        """``tttr formats`` must exit 0 and list supported containers."""
        result = _run("formats")
        assert result.returncode == 0, f"stderr: {result.stderr}"
