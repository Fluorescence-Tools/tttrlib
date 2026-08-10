"""PRD-027 Parts 1-3: algorithms register themselves, and the registry reflects
what registered.

The gap this closes is the PRD's first stated problem: FCS, HMM and PDA worked
and were invisible. Invisible is not cosmetic — a UI cannot offer an algorithm
it cannot enumerate, the `.pto` provenance system has no schema to validate an
`operation_type` against or to replay from, and a plugin has no name to register
a competing implementation under.
"""
import json

import pytest

import tttrlib


LIVE_CAPABILITIES = ("fcs", "hmm", "pda")


@pytest.fixture(scope="module")
def registry():
    return json.loads(tttrlib.registry_json())


# --------------------------------------------------------------- categories --

@pytest.mark.parametrize("capability", LIVE_CAPABILITIES)
def test_capability_is_a_registry_category(registry, capability):
    assert capability in registry, (
        f"'{capability}' is missing from the registry; "
        f"present: {sorted(registry)}")
    assert registry[capability], f"'{capability}' is registered but empty"


@pytest.mark.parametrize("capability", LIVE_CAPABILITIES)
def test_algorithms_json_matches_the_registry(registry, capability):
    """The direct accessor and the assembled registry must not drift."""
    direct = json.loads(tttrlib.algorithms_json(capability))
    for name, entry in direct.items():
        assert name in registry[capability]
        assert registry[capability][name] == entry


def test_unknown_capability_is_empty_not_an_error():
    assert json.loads(tttrlib.algorithms_json("no_such_capability")) == {}


# ------------------------------------------------------------ entry content --

@pytest.mark.parametrize("capability", LIVE_CAPABILITIES)
def test_every_entry_is_self_describing(registry, capability):
    for name, e in registry[capability].items():
        assert e["name"] == name
        assert e["capability"] == capability
        assert e["operation_type"] == name
        for field in ("label", "summary", "description", "row_grain"):
            assert e[field], f"{name}: empty '{field}'"
        assert isinstance(e["settings_schema"], dict)
        assert isinstance(e["inputs"], dict)
        assert isinstance(e["outputs"], dict)


@pytest.mark.parametrize("capability", LIVE_CAPABILITIES)
def test_description_is_prose_not_a_restatement_of_the_name(registry, capability):
    """Criterion 13. A description that repeats the label tells a user nothing
    they did not already have, which is the failure mode worth testing for."""
    for name, e in registry[capability].items():
        desc = e["description"]
        assert len(desc) > 200, f"{name}: description is {len(desc)} chars"
        assert desc.strip().lower() != e["label"].strip().lower()
        assert name.replace("_", " ") not in desc[:40].lower(), (
            f"{name}: description opens by restating its own name")


@pytest.mark.parametrize("capability", LIVE_CAPABILITIES)
def test_every_algorithm_carries_at_least_one_citation(registry, capability):
    """Criterion 13/15: provenance has to reach the literature."""
    for name, e in registry[capability].items():
        refs = e["references"]
        assert isinstance(refs, list) and refs, f"{name}: no references"
        for r in refs:
            for field in ("type", "authors", "title", "year"):
                assert r.get(field), f"{name}: citation missing '{field}'"
            assert isinstance(r["year"], int)


@pytest.mark.parametrize("capability", LIVE_CAPABILITIES)
def test_settings_schema_is_usable_json_schema(registry, capability):
    """A form builder renders this without tttrlib-specific code, so every
    property needs a type and the object needs to say it is one."""
    for name, e in registry[capability].items():
        schema = e["settings_schema"]
        assert schema.get("type") == "object", f"{name}: schema is not an object"
        props = schema.get("properties", {})
        assert props, f"{name}: no parameters declared"
        for key, spec in props.items():
            assert "type" in spec, f"{name}.{key}: no type"


# ---------------------------------------------------------- operation category --

@pytest.mark.parametrize("capability", LIVE_CAPABILITIES)
def test_replayable_algorithms_reach_the_operation_category(registry, capability):
    """Criterion 4/5: an operation_type the provenance system meets in a .pto
    resolves against the live registry, with no source edit to teach it."""
    ops = registry["operation"]
    for name, e in registry[capability].items():
        if e["can_replay"]:
            assert name in ops, f"{name} can replay but is not an operation"
            assert ops[name]["settings_schema"] == e["settings_schema"]


def test_hand_authored_operations_are_still_there(registry):
    """The literals have not been retired yet, and the live registrations are
    additive — a consumer that reads the operation category must not lose the
    entries it already depended on."""
    for name in ("burst_selection", "tcspc_calibration", "mle_green", "bva",
                 "kde_cde", "burst_fusion", "burst_fcs"):
        assert name in registry["operation"], f"{name} disappeared"


def test_operation_names_are_unique_across_both_sources(registry):
    """A live registration must never silently displace a hand-authored entry
    of the same name; the merge keeps the existing one, so a collision would
    show up here rather than as a changed schema at a call site."""
    ops = registry["operation"]
    live = json.loads(tttrlib.algorithms_json("fcs"))
    live.update(json.loads(tttrlib.algorithms_json("hmm")))
    live.update(json.loads(tttrlib.algorithms_json("pda")))
    for name in live:
        assert ops[name]["capability"] in LIVE_CAPABILITIES


# ----------------------------------------------------------- no regressions --

def test_existing_categories_are_untouched(registry):
    for category in ("burst_search", "fit", "fit_setup", "objective",
                     "file_container", "table_format", "plugin", "operation"):
        assert category in registry, f"{category} lost from the registry"


def test_registry_categories_lists_the_new_ones():
    cats = list(tttrlib.registry_categories())
    for capability in LIVE_CAPABILITIES:
        assert capability in cats


@pytest.mark.parametrize("capability", LIVE_CAPABILITIES)
def test_registry_category_json_serves_the_new_categories(capability):
    entries = json.loads(tttrlib.registry_category_json(capability))
    assert entries
    assert entries == json.loads(tttrlib.algorithms_json(capability))


def test_registry_helper_returns_the_new_categories():
    """The Python `registry()` convenience wrapper, which is what callers use."""
    for capability in LIVE_CAPABILITIES:
        entries = tttrlib.registry(capability)
        assert isinstance(entries, dict) and entries
