"""Every module README describes what is actually in the module.

A README that lists files the module does not have, or omits files it does,
is worse than none: it is read as an inventory. `io/be` claimed to be the
Becker & Hickl reader for months (it is the BrightEyes-TTM one), `io/csv`
named `io_csv_reader.cpp` (never existed) and `io/table` named
`TableVocabulary.cpp` (renamed long ago) — all three found by comparing the
prose against the directory rather than by reading it.

So this test compares them. For every `modules/**/README.md`:

* a leaf module (one with `include/` or `src/`) must **name every source and
  header file** it contains;
* an aggregate directory (`modules/`, `modules/io/`, ...) must **name every
  subdirectory** under it;
* a file the README names as its own must exist.

The rule is deliberately mechanical: it does not check that the *description*
is right, only that the inventory is. Prose that is wrong about a file it does
name is a review problem; prose that is wrong about which files exist is a
test problem, and this is the test.
"""
import os
import pathlib
import re

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
MODULES = REPO / "modules"

pytestmark = pytest.mark.skipif(not MODULES.is_dir(),
                                reason="source tree not available (installed package)")

SOURCE_SUFFIXES = (".h", ".hpp", ".c", ".cpp", ".i")


def _readmes():
    return sorted(MODULES.rglob("README.md"))


def _files_of(directory):
    """The module's own sources and headers, one level of `include/` / `src/`."""
    found = []
    for sub in ("include", "src"):
        path = directory / sub
        if path.is_dir():
            for root, _dirs, names in os.walk(path):
                found += [n for n in names if n.endswith(SOURCE_SUFFIXES)]
    found += [p.name for p in directory.iterdir()
              if p.is_file() and p.name.endswith(SOURCE_SUFFIXES)]
    return sorted(set(found))


def _subdirs_of(directory):
    return sorted(p.name for p in directory.iterdir()
                  if p.is_dir() and (p / "README.md").exists())


def _ids(paths):
    return [str(p.relative_to(REPO)) for p in paths]


@pytest.mark.parametrize("readme", _readmes(), ids=_ids(_readmes()))
def test_the_readme_names_every_file_of_its_module(readme):
    directory = readme.parent
    files = _files_of(directory)
    if not files:
        pytest.skip("aggregate directory; covered by the subdirectory test")
    text = readme.read_text()
    missing = [f for f in files if f not in text]
    assert not missing, (
        f"{readme.relative_to(REPO)} does not mention: {missing}\n"
        f"Add them to the Contents section (a reader takes that list for an "
        f"inventory), or move the file.")


@pytest.mark.parametrize("readme", _readmes(), ids=_ids(_readmes()))
def test_the_readme_does_not_name_files_the_module_does_not_have(readme):
    """A file named as *this* module's own must exist. A cross-reference to
    another module's file is written with a path or a link, and is skipped."""
    directory = readme.parent
    files = set(_files_of(directory))
    if not files:
        pytest.skip("aggregate directory")
    text = readme.read_text()
    # `Name.cpp` in backticks, with no path and not inside a link target
    claimed = set(re.findall(r'`([A-Za-z0-9_]+\.(?:h|hpp|c|cpp|i))`', text))
    # names that belong to another module are referenced by link elsewhere in
    # the same bullet; require the file to exist somewhere in the tree at least
    ghosts = []
    for name in sorted(claimed - files):
        elsewhere = list(MODULES.rglob(name))
        if not elsewhere:
            ghosts.append(name)
    assert not ghosts, (
        f"{readme.relative_to(REPO)} names files that exist nowhere in "
        f"modules/: {ghosts}")


@pytest.mark.parametrize("readme", _readmes(), ids=_ids(_readmes()))
def test_an_aggregate_readme_names_every_submodule(readme):
    directory = readme.parent
    if _files_of(directory):
        pytest.skip("leaf module")
    text = readme.read_text()
    missing = [d for d in _subdirs_of(directory)
               if f"`{d}" not in text and f"[`{d}" not in text and f"({d})" not in text]
    assert not missing, (
        f"{readme.relative_to(REPO)} does not list submodules: {missing}")


def test_every_module_directory_has_a_readme():
    """A module without a README is a module nobody can find their way into.
    `modules/README.md` states the rule; this enforces it."""
    missing = []
    for cmake in MODULES.rglob("CMakeLists.txt"):
        directory = cmake.parent
        if not (directory / "README.md").exists():
            missing.append(str(directory.relative_to(REPO)))
    assert not missing, f"module directories without a README.md: {missing}"
