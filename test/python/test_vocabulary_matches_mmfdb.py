"""Every controlled term tttrlib writes is defined by **mmfdb**, not by tttrlib.

tttrlib used to keep its own `okf/nomenclature/mmfdb.dic`, and it had drifted by
**eighteen terms** — `bva`, `kde_cde`, `mle_green`, `mle_red`, `burst_fcs`,
`hmm_photon_by_photon`, `tcspc_calibration`, `pda_histogram`, `companion_of`,
`histogram_bin` and seven `…4` data formats, none of which mmfdb declares. The
registry agreed with the local copy, the local copy agreed with the writer, and
a conformance test checked them against each other and passed: a closed loop of
agreement that says nothing about the vocabulary the rest of the world reads
these files in.

The copy is gone (see `okf/specs/mmfdb-is-the-vocabulary.md`). This validates
against **mmfdb's own dictionaries**, which is the only check that can catch a
term this repository invented — because it is the only one that reads a file
this repository does not own.

Skipped, loudly, when mmfdb cannot be found — a vocabulary that cannot be
checked is a finding, so the skip reason says where it looked.
"""
import pathlib

import pytest

import tttrlib

import mmfdb_dictionary

REPO = pathlib.Path(__file__).resolve().parents[2]

#: The items whose enumeration constrains what a `.pto` may carry.
_ITEMS = (
    "_mmfdb_operation.operation_type",
    "_mmfdb_operation.algorithm",
    "_mmfdb_artifact.row_grain",
    "_mmfdb_artifact.data_format",
    "_mmfdb_edge.relationship_type",
)

mmfdb_dictionary.require()
pytestmark = pytest.mark.skipif(
    not mmfdb_dictionary.dictionaries(),
    reason=mmfdb_dictionary.WHERE_WE_LOOKED,
)


def test_this_repository_keeps_no_dictionary_of_its_own():
    """Rule 1 of okf/specs/mmfdb-is-the-vocabulary.md, enforced.

    A copy of a vocabulary drifts from it. This one did, by eighteen terms,
    while the registry, the copy and the writer all agreed with each other and
    every test passed -- which is why the check is for the *file*, not for its
    contents. There is no version of a local dictionary that is safe.
    """
    stray = [p for p in REPO.rglob("*.dic")
             if "build" not in p.parts and ".git" not in p.parts
             and "thirdparty" not in p.parts]
    assert not stray, (
        f"{[str(p.relative_to(REPO)) for p in stray]} — mmfdb is the naming "
        "repository; a term this library needs is added there, not here."
    )


@pytest.mark.parametrize("item", _ITEMS)
def test_mmfdb_declares_every_item_this_library_writes(item):
    """The items themselves must exist, before their values can be checked."""
    assert mmfdb_dictionary.enumeration(item), (
        f"mmfdb declares no enumeration for {item}. If tttrlib writes it, the "
        "term belongs in mmfdb — improve the standard rather than working "
        "around it."
    )


def test_every_operation_the_registry_publishes_is_an_mmfdb_term():
    """The registry is the live source of what tttrlib can do; a `.pto` it
    produces is tagged with these values, and a reader validates them against
    mmfdb rather than against anything here."""
    import json

    theirs = mmfdb_dictionary.enumeration("_mmfdb_operation.operation_type")
    if not theirs:
        pytest.skip("mmfdb declares no operation_type enumeration")

    registry = json.loads(tttrlib.registry_json())
    published = {
        entry["operation_type"]
        for entry in registry.get("operation", {}).values()
        if isinstance(entry, dict) and entry.get("operation_type")
    }
    assert published, "the registry publishes no operation_type at all"
    invented = sorted(published - theirs)
    assert not invented, (
        f"the registry publishes {invented}, which mmfdb does not declare. "
        "The registry's `name` is tttrlib's own identifier and may be anything; "
        "`operation_type` is the controlled provenance vocabulary and may not."
    )
