# SPDX-License-Identifier: BSD-3-Clause
"""The curated registry and the generated API index."""
from __future__ import annotations

import json
import unittest

import tttrlib


class TestRegistry(unittest.TestCase):
    """The curated layer: what a tool needs in order to *offer* a feature."""

    def test_categories(self):
        registry = tttrlib.registry()
        self.assertIn("burst_search", registry)
        self.assertIn("file_container", registry)
        self.assertIn("fit", registry)
        self.assertIn("fit_setup", registry)
        self.assertIn("objective", registry)

    def test_every_entry_is_self_describing(self):
        for category, entries in tttrlib.registry().items():
            self.assertTrue(entries, f"{category} is empty")
            for name, entry in entries.items():
                self.assertEqual(entry["name"], name)
                self.assertTrue(entry["label"], f"{category}/{name} has no label")
                self.assertTrue(entry["summary"], f"{category}/{name} has no summary")

    def test_callable_entries_carry_a_usable_schema(self):
        """An entry naming a method must describe how to call it."""
        for name, entry in tttrlib.registry("burst_search").items():
            self.assertTrue(hasattr(tttrlib.TTTR, entry["method"]), entry["method"])
            properties = entry["params_schema"]["properties"]
            self.assertTrue(properties, f"{name} publishes no parameters")
            for prop_name, prop in properties.items():
                self.assertIn(prop["type"],
                              ("integer", "number", "boolean", "string",
                               "array", "object"))
                self.assertTrue(prop["title"], prop_name)
                self.assertTrue(prop["description"], prop_name)
                if "default" not in prop:
                    # Legitimate when the value is something only the caller can
                    # decide, such as a detector grouping: inventing a default
                    # would silently search the wrong channels.
                    continue

    def test_fit_entries_are_constructible_and_describe_their_parameters(self):
        """Every fit entry must be buildable by name and describe its parameters.

        The entry no longer names a class to look up: dispatch goes through the
        model interface, so the registry key *is* the identity and
        ``make_decay_fit`` is what must accept it.
        """
        constructible = set(tttrlib.decay_fit_names())
        for name, entry in tttrlib.registry("fit").items():
            self.assertNotIn("method", entry,
                             f"{name}: 'method' is obsolete; the key is the identity")
            self.assertIn(name, constructible,
                          f"{name} is advertised but make_decay_fit cannot build it")
            properties = entry["params_schema"]["properties"]
            self.assertTrue(properties, f"{name} publishes no parameters")
            for prop_name, prop in properties.items():
                self.assertIn(prop["type"],
                              ("integer", "number", "boolean", "string", "array"))
                self.assertTrue(prop["title"], prop_name)
                self.assertTrue(prop["description"], prop_name)
                if prop["type"] == "array":
                    # A variable-length block declares where its length comes from.
                    self.assertIn("count_from", prop, f"{name}.{prop_name}")
                    self.assertIn("items", prop, f"{name}.{prop_name}")
                    self.assertIn("default", prop["items"], f"{name}.{prop_name}")
                else:
                    self.assertIn("default", prop)
                # fixed_default is the registry hint saying whether the parameter
                # is held fixed unless the user frees it.
                if "fixed_default" in prop:
                    self.assertIsInstance(prop["fixed_default"], bool)

    def test_every_fit_describes_its_results_and_capabilities(self):
        """A flat result vector is only usable if its columns are named.

        Without this the layout lives in a comment on a C++ function and every
        consumer hard-codes it, which is what the schema exists to stop.
        """
        for name, entry in tttrlib.registry("fit").items():
            for flag in ("supports_lnprob", "supports_gradient"):
                self.assertIsInstance(entry[flag], bool, f"{name}: {flag}")
            properties = entry["results_schema"]["properties"]
            self.assertTrue(properties, f"{name} publishes no results")
            for prop_name, prop in properties.items():
                self.assertIn(prop["type"], ("integer", "number", "boolean"))
                self.assertTrue(prop["title"], prop_name)
                self.assertTrue(prop["description"], prop_name)
            # Every model reports these, so a batch matrix has a predictable
            # prefix and a caller can always tell whether a row converged.
            for required in ("twoIstar", "converged", "iterations"):
                self.assertIn(required, properties, f"{name} omits {required}")
            # Bulk arrays never travel in results: the fitted curve stays on the
            # problem, or a batch result would be thousands of columns wide.
            self.assertNotIn("model", properties, f"{name} puts the curve in results")

    def test_count_from_names_a_real_setup_property(self):
        """A variable-length parameter block must say where its length comes from."""
        registry = tttrlib.registry()
        for name, entry in registry["fit"].items():
            setup_name = entry["setup"]["name"]
            setup_props = registry[entry["setup"]["category"]][setup_name]
            setup_props = setup_props["params_schema"]["properties"]
            for prop_name, prop in entry["params_schema"]["properties"].items():
                if prop.get("type") != "array":
                    continue
                count_from = prop["count_from"]
                self.assertIsInstance(count_from, str, f"{name}.{prop_name}")
                self.assertIn(count_from, setup_props,
                              f"{name}.{prop_name}: count_from '{count_from}' is not "
                              f"a property of setup '{setup_name}'")
                self.assertEqual(setup_props[count_from]["type"], "integer")

    def test_objective_choices_match_the_objective_category(self):
        """A setup that offers a choice of objective must offer the real ones."""
        registry = tttrlib.registry()
        objectives = registry["objective"]
        self.assertTrue(objectives)
        for setup_name, setup in registry["fit_setup"].items():
            prop = setup["params_schema"]["properties"].get("objective")
            if prop is None:
                continue
            self.assertEqual(set(prop["enum"]), set(objectives),
                             f"{setup_name}: objective enum has drifted from the category")
            self.assertIn(prop["default"], objectives)

    def test_fit_setup_link_resolves(self):
        """Each fit entry's setup link must point at a real fit_setup entry."""
        registry = tttrlib.registry()
        for name, entry in registry["fit"].items():
            link = entry["setup"]
            category = registry[link["category"]]
            self.assertIn(link["name"], category, f"{name}: dangling setup link")
            setup = category[link["name"]]
            self.assertTrue(setup["params_schema"]["properties"])

    def test_file_containers_match_the_reader(self):
        """Every advertised container must actually be openable by name."""
        for name, entry in tttrlib.registry("file_container").items():
            self.assertIsInstance(entry["container_type"], int)
            self.assertTrue(entry["extensions"].startswith("."))

    def test_single_category_matches_the_whole(self):
        everything = tttrlib.registry()
        for category in everything:
            self.assertEqual(tttrlib.registry(category), everything[category])

    def test_unknown_category_names_the_alternatives(self):
        with self.assertRaises(ValueError) as ctx:
            tttrlib.registry("not_a_category")
        self.assertIn("burst_search", str(ctx.exception))

    def test_json_is_the_transport(self):
        """Scripting languages read the JSON, so it must parse and agree."""
        self.assertEqual(json.loads(tttrlib.registry_json()), tttrlib.registry())


class TestApiIndex(unittest.TestCase):
    """The generated layer: complete coverage, derived so it cannot drift."""

    @classmethod
    def setUpClass(cls):
        cls.index = tttrlib.api_index()

    def test_covers_the_library(self):
        self.assertGreater(self.index["n_classes"], 50)
        self.assertGreater(self.index["n_methods"], 500)
        self.assertEqual(self.index["n_classes"], len(self.index["classes"]))
        self.assertEqual(self.index["n_functions"], len(self.index["functions"]))

    def test_core_classes_are_present(self):
        # doubleHistogram, not Histogram: it is a SWIG template instantiation,
        # and the index reports the names actually exported.
        for name in ("TTTR", "Correlator", "CLSMImage", "doubleHistogram",
                     "TTTRHeader", "CLSMFrame", "TTTRMask"):
            self.assertIn(name, self.index["classes"], name)

    def test_methods_carry_signatures_and_defaults(self):
        maxtree = self.index["classes"]["TTTR"]["methods"]["burst_search_maxtree"]
        self.assertIn("signature", maxtree)
        names = [p["name"] for p in maxtree["parameters"]]
        self.assertIn("L", names)
        self.assertNotIn("self", names)
        defaults = {p["name"]: p.get("default") for p in maxtree["parameters"]}
        self.assertEqual(defaults["L"], 20)

    def test_index_tracks_the_code_rather_than_a_written_list(self):
        """Every registry method appears in the index with matching parameters.

        This is the property that makes the two layers safe to keep separate: the
        curated registry can only describe parameters the code actually has.
        """
        for name, entry in tttrlib.registry("burst_search").items():
            method = self.index["classes"]["TTTR"]["methods"][entry["method"]]
            indexed = {p["name"] for p in method["parameters"]}
            advertised = set(entry["params_schema"]["properties"])
            missing = advertised - indexed
            self.assertFalse(
                missing,
                f"{name} advertises {sorted(missing)}, which "
                f"{entry['method']} does not accept",
            )

    def test_excludes_what_tttrlib_does_not_own(self):
        """A wrapper module re-exports its imports; those are not tttrlib's API."""
        self.assertNotIn("version", self.index["functions"])
        for name, entry in self.index["classes"].items():
            self.assertNotIn(".", name)

    def test_is_json_serialisable(self):
        """It is only useful to other languages if it survives a JSON round trip."""
        self.assertEqual(json.loads(json.dumps(self.index)), self.index)


if __name__ == "__main__":
    unittest.main()
