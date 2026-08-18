"""Algorithms register themselves, and the registry reflects
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


def test_the_former_literal_operations_are_still_there(registry):
    """The eight pipeline operations used to be a hand-authored literal; they
    now register themselves next to their code. A consumer that reads the
    operation category must not have lost one in the move."""
    for name in ("burst_selection", "tcspc_calibration", "mle_green", "mle_red",
                 "bva", "kde_cde", "burst_fusion", "burst_fcs"):
        assert name in registry["operation"], f"{name} disappeared"
        assert registry["operation"][name]["provider"] == "builtin"


# ---------------------------------------------------------- one registry ------

ONE_REGISTRY_CATEGORIES = ("burst_search", "fit", "fit_setup", "objective",
                           "operation", "fcs", "hmm", "pda",
                           "correlation_method", "prior")


@pytest.mark.parametrize("capability", ONE_REGISTRY_CATEGORIES)
def test_every_algorithm_category_is_served_by_the_one_registry(registry, capability):
    """There is one registry. Every category that describes an algorithm, a
    fit model, a setup block, an objective or an operation is exactly what
    `algorithms_json(capability)` returns -- nothing is spliced in from a
    literal or a side table, so nothing can drift from what registered."""
    direct = json.loads(tttrlib.algorithms_json(capability))
    assert direct, f"{capability} has no registrations"
    for name, entry in direct.items():
        assert registry[capability][name] == entry
    # and nothing in the category came from anywhere else -- `operation` is
    # the union with every can_replay registration of any capability, which
    # is still the one registry (algorithm_operations_json)
    others = set()
    if capability == "operation":
        others = set(json.loads(tttrlib.algorithm_operations_json()))
    assert set(registry[capability]) == set(direct) | others, (
        f"{capability} carries entries that did not register: "
        f"{sorted(set(registry[capability]) - set(direct) - others)}")


def test_no_registry_literal_remains_in_the_tree():
    """Rule 1 of the migration: no hand-authored registry JSON literal. The
    registry module assembles; it declares nothing. (Entries declared next to
    their code as raw JSON strings are registrations, not a parallel table --
    they go through register_algorithm_json.)"""
    import pathlib
    registry_src = pathlib.Path(tttrlib.__file__).resolve().parents[3] / "modules" / "registry" / "src"
    if not registry_src.exists():
        pytest.skip("source tree not available")
    for f in registry_src.glob("*.cpp"):
        assert 'R"JSON(' not in f.read_text(), f"{f.name} still holds a registry literal"


def test_fit_parameter_order_is_the_declared_order(registry):
    """The flattening rule: `params_schema.properties` order IS the flat
    initial_values layout. The entries are registered next to the models
    (DecayFitModelFit2x.cpp); a round trip through a sorting JSON type would
    silently reorder them, so pin the order here."""
    assert list(registry["fit"]["fit23"]["params_schema"]["properties"]) == ["tau", "gamma", "r0", "rho"]
    assert list(registry["fit_setup"]["fit2x"]["params_schema"]["properties"])[:2] == ["dt", "period"]


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


def test_registry_describes_exactly_the_dispatch_tables(registry):
    """The registry describes what the dispatch tables can run -- no more, no
    less: every correlation method `set_correlation_method` accepts has an
    entry and vice versa, and the same for the prior kinds `from_json_string`
    builds. A plugin adds to both at once (the host registers the entry, the
    layer looks the table up), so this holds with plugins loaded too."""
    assert set(registry["correlation_method"]) == set(tttrlib.Correlator.correlation_method_names())
    assert set(registry["prior"]) == set(tttrlib.DecayFitPrior.kinds())
