"""Conformance test: every ndx equation reference resolves in mmfdb.dic.

Parses the central mmCIF naming dictionary (okf/nomenclature/mmfdb.dic) and
the ndx equations file, then asserts that every quoted name in every equation
expression is either an input column, a constant, or a derived column defined
in the dictionary.

If a name appears in the equations but not in the dictionary, the naming
contract is broken and this test fails — telling you exactly which equation
and which name.
"""

import re
import pathlib

import pytest
import yaml

TTTRLIB_ROOT = pathlib.Path(__file__).resolve().parents[2]
DIC_PATH = TTTRLIB_ROOT / "okf" / "nomenclature" / "mmfdb.dic"

CHISURF_ROOT = pathlib.Path("/Users/tpeulen/dev/chisurf")
EQUATIONS_PATH = CHISURF_ROOT / "modules" / "ndxplorer" / "ndxplorer" / "settings" / "mfd.equations.yaml"
CONSTANTS_PATH = CHISURF_ROOT / "modules" / "ndxplorer" / "ndxplorer" / "settings" / "mfd.constants.json"

pytestmark = pytest.mark.skipif(
    not EQUATIONS_PATH.exists(),
    reason="chiSurf ndx equations not found",
)


def _parse_dic_column_items(dic_text: str) -> dict[str, dict]:
    """Parse an mmCIF .dic file, returning {column_name: metadata}.

    Extracts every _mmfdb_burst_column.column, _mmfdb_constant.name, and
    _mmfdb_derived_column.column value, along with associated symbol/units.
    """
    columns: dict[str, dict] = {}

    # mmCIF save blocks: save__mmfdb_burst_column.xxx ... save_
    # The column name appears as a quoted value after the _mmfdb_*.column key
    # within each save block.
    for category_prefix, column_key in [
        ("burst_column", "column"),
        ("constant", "name"),
        ("derived_column", "column"),
    ]:
        pattern = rf"_mmfdb_{category_prefix}\.{column_key}\s+(?:\"([^\"]+)\"|(\S+))"
        for m in re.finditer(pattern, dic_text):
            name = m.group(1) or m.group(2)
            columns[name] = {"category": category_prefix}
    return columns


def _load_dic() -> dict[str, dict]:
    with open(DIC_PATH) as f:
        return _parse_dic_column_items(f.read())


def _load_equations():
    with open(EQUATIONS_PATH) as f:
        return yaml.safe_load(f)


def _load_constants():
    import json
    with open(CONSTANTS_PATH) as f:
        return json.load(f)


def _extract_refs(expr: str) -> list[str]:
    """Return every quoted single-quoted name in an equation expression."""
    return re.findall(r"'([^']+)'", expr)


class TestNomenclatureConformance:

    @pytest.fixture(scope="class")
    def dic(self):
        return _load_dic()

    @pytest.fixture(scope="class")
    def equations(self):
        return _load_equations()

    @pytest.fixture(scope="class")
    def constants(self):
        return _load_constants()

    def test_constants_match_dic(self, dic, constants):
        """Every constant in mfd.constants.json must appear in mmfdb.dic."""
        missing = set(constants) - set(dic)
        assert not missing, (
            f"Constants in mfd.constants.json not defined in mmfdb.dic: {missing}"
        )

    def test_equation_outputs_in_dic(self, equations, dic):
        """Every equation output key must be a derived column in the dictionary."""
        dic_derived = {k for k, v in dic.items() if v.get("category") == "derived_column"}
        missing = []
        for eq in equations:
            for out_key in eq:
                if out_key not in dic_derived:
                    missing.append(out_key)
        assert not missing, (
            f"Equation outputs not in mmfdb.dic derived_columns: {missing}"
        )

    def test_all_equation_refs_resolve(self, equations, dic, constants):
        """Every quoted reference in every equation must resolve.

        A reference resolves if it is:
        1. an input column or derived column in the dictionary, or
        2. a constant in the dictionary / mfd.constants.json, or
        3. the output of an earlier equation.
        """
        all_constants = set(constants)
        eq_output_keys: set[str] = set()
        dic_all = set(dic)

        unresolved: list[tuple[str, str]] = []
        for eq in equations:
            for out_key, expr in eq.items():
                refs = _extract_refs(expr)
                for ref in refs:
                    resolves = (
                        ref in dic_all
                        or ref in all_constants
                        or ref in eq_output_keys
                    )
                    if not resolves:
                        unresolved.append((out_key, ref))
                eq_output_keys.add(out_key)

        assert not unresolved, (
            f"Equation references not found in mmfdb.dic or constants: {unresolved}"
        )

    def test_prototype_columns_in_dic(self, dic):
        """Columns emitted by prototype/mfd_sim_to_pto.py must be in the dictionary."""
        proto_path = TTTRLIB_ROOT / "prototype" / "mfd_sim_to_pto.py"
        if not proto_path.exists():
            pytest.skip("prototype not found")
        source = proto_path.read_text()
        proto_cols = set(re.findall(r'bur_store\.add\("([^"]+)"', source))
        missing = proto_cols - set(dic)
        assert not missing, (
            f"Prototype burst columns not in mmfdb.dic: {missing}"
        )

    def test_table_of_symbols(self, dic):
        """Print the full symbol table for human review."""
        header = f"{'Column':40s} {'Symbol':15s} {'Units':10s} Category"
        sep = "=" * len(header)
        print(f"\n{sep}\n{header}\n{sep}")
        for name, meta in sorted(dic.items()):
            print(f"{name:40s} {'':15s} {'':10s} {meta.get('category', '?')}")
        assert len(dic) > 20, "Dictionary seems too small"
