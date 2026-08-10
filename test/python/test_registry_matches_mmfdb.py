"""PRD-027 criterion 10: every name the registry publishes is defined in the
mmfdb dictionary.

The registry is the live source of what tttrlib can do; `okf/nomenclature/mmfdb.dic`
is the controlled vocabulary the `.pto` provenance graph is written in. They are
two halves of one contract, and nothing but a test keeps them together — a
registry entry whose `operation_type` is not in the dictionary produces `.pto`
files a reader cannot validate or replay, and a dictionary entry with no
registration describes an operation that no longer exists.

Both directions are checked, because both have failed before: FCS, HMM and PDA
were registered with no dictionary entry, and the eight pipeline operations the
`.pto` writer has always emitted had no dictionary category at all.
"""
import json
import pathlib
import re

import pytest

import tttrlib

import mmfdb_dictionary

mmfdb_dictionary.require()
pytestmark = pytest.mark.skipif(not mmfdb_dictionary.dictionaries(),
                                reason=mmfdb_dictionary.WHERE_WE_LOOKED)


def _dic_text():
    """mmfdb's dictionaries, not a copy in this repository.

    This used to read `okf/nomenclature/mmfdb.dic`, and that is what let the two
    drift together: the registry was checked against a local file, the local
    file was maintained beside the registry, and neither was checked against
    mmfdb. See okf/specs/mmfdb-is-the-vocabulary.md.
    """
    return mmfdb_dictionary.text()


def _defined_operation_types():
    """mmfdb's controlled vocabulary for the operation type.

    Read from the `_item_enumeration` of `_mmfdb_operation.operation_type`.
    This used to scan for per-operation save blocks, which existed only in
    tttrlib's own copy of the dictionary -- mmfdb states the vocabulary once,
    as an enumeration, which is the thing an outside reader validates against.
    """
    return mmfdb_dictionary.enumeration("_mmfdb_operation.operation_type")


def _enumeration_values(item_name):
    """The controlled vocabulary of one item, from its _item_enumeration loop."""
    return mmfdb_dictionary.enumeration(f"_{item_name}")


@pytest.fixture(scope="module")
def operations():
    return json.loads(tttrlib.registry_json())["operation"]


# ----------------------------------------------------------- both directions --

def _registered_operation_types(operations):
    """The *terms*, not the registry keys.

    A registry entry has two names and they are not the same thing. Its key is
    tttrlib's own identifier for an algorithm -- `mle_green`, `bva` -- and may
    be anything, because it is what a caller dispatches on. Its
    `operation_type` is the controlled provenance vocabulary, and is the value
    that goes into a `.pto`. Comparing the *key* against the dictionary, which
    is what this used to do, forces the two to be equal and so quietly requires
    tttrlib's internal names to be mmfdb's -- which is how `bva`, `kde_cde` and
    `mle_green` came to be in a dictionary that has `burst_variance_analysis`,
    `burst_2cde` and `burst_lifetime_fitting`.
    """
    return {e["operation_type"] for e in operations.values()
            if e.get("operation_type")}


def test_every_registered_operation_is_defined_in_the_dictionary(operations):
    defined = _defined_operation_types()
    missing = sorted(_registered_operation_types(operations) - defined)
    assert not missing, (
        f"registered but not defined in mmfdb.dic: {missing}. A .pto tagged "
        f"with one of these cannot be validated or replayed by a reader.")


def test_no_registered_operation_names_a_term_mmfdb_retired(operations):
    """One direction only, now that the dictionary is mmfdb's.

    The reverse check -- every defined operation is registered -- made sense
    while the dictionary was tttrlib's own and described exactly what tttrlib
    did. mmfdb's vocabulary is the whole ecosystem's: it declares operations
    ChiSurf performs and tttrlib does not, and requiring tttrlib to implement
    all of them would be requiring the library to be the vocabulary. What is
    still worth asserting is that nothing is registered under a term mmfdb has
    dropped, which is the direction that produces unreadable files.
    """
    assert not sorted(_registered_operation_types(operations)
                      - _defined_operation_types())


# ------------------------------------------------------ controlled vocabulary --

def test_every_row_grain_is_in_the_controlled_vocabulary(operations):
    allowed = _enumeration_values("mmfdb_artifact.row_grain")
    assert allowed, "row_grain has no enumeration in the dictionary"
    used = {e["row_grain"] for e in operations.values() if e.get("row_grain")}
    assert used <= allowed, f"row_grain values not in the dictionary: {sorted(used - allowed)}"


def test_the_data_format_vocabulary_is_about_storage_not_content():
    """`data_format` says how an artifact is stored, and its terms come from
    mmfdb (see `test_vocabulary_matches_mmfdb.py`, which fails on an invented
    one).

    The MFD companion suffixes — .bg4, .bv4, .2c4, .td4 — are deliberately not
    values here: they name what a table *is*, which is `operation_type`'s job,
    and an object inside a container has no suffix at all. PRD-026 settled that
    every artifact the .pto writer emits is stored as `dstore`.

    The `data_format` field on the registry's *operation* entries still carries
    those suffixes and is mid-rename, so it is not asserted against this
    vocabulary here — that check belongs with the rename, not against a moving
    target."""
    allowed = _enumeration_values("mmfdb_artifact.data_format")
    assert "dstore" in allowed, "the format every .pto artifact uses is missing"
    for suffix in ("bg4", "bv4", "2c4", "td4", "fu4"):
        assert suffix not in allowed, (
            f"{suffix} names a kind of table, not a storage format")


# ------------------------------------------------------------- the tags used --

def _tags_the_source_emits():
    """Every `_mmfdb_*` tag name that appears as a literal in the C++.

    Discovered from the source rather than listed here: a hand-maintained list
    is exactly the kind of thing that stops matching the code, which is the
    failure this whole file exists to prevent."""
    root = pathlib.Path(__file__).resolve().parents[2] / "modules"
    names = set()
    pattern = re.compile(r'"(_mmfdb_[a-z_]+\.[a-z_]+)"')
    for path in root.rglob("*"):
        if path.suffix in (".cpp", ".h", ".hpp"):
            names |= set(pattern.findall(path.read_text(errors="ignore")))
    return names


@pytest.mark.slow
def test_every_tag_the_source_emits_is_defined():
    """A tag written into a .pto with no definition anywhere is a name only its
    author understands — the reader on the other end has nothing to look up."""
    emitted = _tags_the_source_emits()
    assert emitted, "found no _mmfdb tags in the source; the scan is broken"
    text = _dic_text()
    missing = sorted(t for t in emitted
                     if not re.search(r'_item\.name\s+"' + re.escape(t) + r'"', text))
    assert not missing, (
        f"emitted into .pto containers but not defined in mmfdb.dic: {missing}")
