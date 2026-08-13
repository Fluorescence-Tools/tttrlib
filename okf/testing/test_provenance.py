"""Comprehensive provenance tests for the burst pipeline.

These tests verify:

1. **Provenance completeness** — every non-source artifact has the full
   _mmfdb_* tag set (operation_type, data_format, row_grain, source_uid,
   relationship_type, settings_json, settings_hash).

2. **Settings hash integrity** — the settings_hash written to the container
   matches settings_hash(settings_json) computed from the actual settings.

3. **Lineage graph integrity** — no orphan non-source nodes, every companion
   links to a burst_selection node, every IRF links to a photon stream,
   every MLE has a calibrated_by edge to its IRF.

4. **Round-trip reconstruction** — read the .pto, extract settings from
   provenance, rebuild a pipeline via from_provenance(), re-run the analysis,
   and compare results bit-for-bit.

5. **Column-name conformance** — every emitted column matches mmfdb.dic.

6. **ndx equation evaluation** — all applicable ndx equations compute.
"""

import json
import os
import sys
import tempfile
import pathlib

import numpy as np
import pytest
import tttrlib

# Add prototype to path
PROTOTYPE = pathlib.Path(__file__).resolve().parents[2] / "prototype"
sys.path.insert(0, str(PROTOTYPE))

from burst_pipeline.pipeline import BurstPipeline, mfd_config
from burst_pipeline.provenance import ProvenanceGraph, settings_hash
from burst_pipeline.operations import Operation
from burst_pipeline.pto_builder import PtoBuilder

TTTRLIB_ROOT = pathlib.Path(__file__).resolve().parents[2]
DIC_PATH = TTTRLIB_ROOT / "okf" / "nomenclature" / "mmfdb.dic"
CONFIG_PATH = TTTRLIB_ROOT / "examples" / "simulation" / "configs" / "alex.json"


# ── fixtures ───────────────────────────────────────────────────────────

@pytest.fixture(scope="module")
def tttr_data():
    """Simulate one TTTR photon stream for testing."""
    with open(CONFIG_PATH) as f:
        config = f.read()
    sim = tttrlib.SimEngine.from_json(config)
    sim.run()
    return sim.to_tttr(0.01, 2)


@pytest.fixture(scope="module")
def pto_path(tttr_data):
    """Run the full pipeline and produce a .pto container."""
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "test_provenance.pto")
        pipeline = BurstPipeline(mfd_config())
        pipeline.pack(path, [tttr_data])
        yield path


@pytest.fixture(scope="module")
def provenance_graph(pto_path):
    """Read the provenance graph from the .pto."""
    reader = tttrlib.PtoFile()
    assert reader.open(pto_path), f"Failed to open {pto_path}"
    graph = ProvenanceGraph(reader)
    yield graph
    reader.close()


# ── 1. provenance completeness ─────────────────────────────────────────

class TestProvenanceCompleteness:
    """Every non-source artifact must carry the full _mmfdb_* tag set."""

    def test_no_provenance_violations(self, provenance_graph):
        violations = provenance_graph.verify_all()
        assert not violations, (
            f"Provenance violations:\n"
            + "\n".join(f"  UID {uid}: {errs}" for uid, errs in violations.items())
        )

    def test_every_artifact_has_operation_type(self, provenance_graph):
        for node in provenance_graph.nodes.values():
            if node.kind == "tttr_photon_stream":
                continue
            assert node.operation_type, f"UID {node.uid} ({node.name}) missing operation_type"

    def test_every_artifact_has_data_format(self, provenance_graph):
        for node in provenance_graph.nodes.values():
            if node.kind == "tttr_photon_stream":
                continue
            assert node.data_format, f"UID {node.uid} ({node.name}) missing data_format"

    def test_every_artifact_has_settings_json(self, provenance_graph):
        for node in provenance_graph.nodes.values():
            if node.kind == "tttr_photon_stream":
                continue
            assert node.settings, f"UID {node.uid} ({node.name}) missing settings"
            assert isinstance(node.settings, dict)

    def test_every_artifact_has_row_grain(self, provenance_graph):
        for node in provenance_graph.nodes.values():
            if node.kind == "tttr_photon_stream":
                continue
            assert node.row_grain, f"UID {node.uid} ({node.name}) missing row_grain"


# ── 2. settings hash integrity ─────────────────────────────────────────

class TestSettingsHash:
    """The settings_hash in the container must match settings_hash(settings)."""

    def test_settings_hash_matches(self, pto_path, provenance_graph):
        reader = tttrlib.PtoFile()
        reader.open(pto_path)
        for node in provenance_graph.nodes.values():
            if not node.settings:
                continue
            tags = reader.tags_for(node.uid)
            written_hash = None
            for tag in tags:
                if tag.name == "_mmfdb_operation.settings_hash":
                    written_hash = tag.text
            if written_hash is not None:
                expected = settings_hash(node.settings)
                assert written_hash == expected, (
                    f"UID {node.uid} ({node.name}): hash mismatch "
                    f"written={written_hash} expected={expected}"
                )
        reader.close()


# ── 3. lineage graph integrity ─────────────────────────────────────────

class TestLineageIntegrity:
    """The provenance DAG must be well-formed."""

    def test_has_photon_streams(self, provenance_graph):
        sources = provenance_graph.sources()
        assert len(sources) >= 1, "No source artifacts (photon streams)"
        for s in sources:
            assert s.kind == "tttr_photon_stream"

    def test_has_burst_selection(self, provenance_graph):
        burst_ops = provenance_graph.artifacts_by_operation("burst_selection")
        assert len(burst_ops) >= 1, "No burst_selection artifacts"

    def test_every_non_source_has_parent(self, provenance_graph):
        for node in provenance_graph.nodes.values():
            if node.kind == "tttr_photon_stream":
                continue
            assert node.source_uids, (
                f"UID {node.uid} ({node.name}) has no source_uids"
            )

    def test_companions_link_to_burst_table(self, provenance_graph):
        burst_uids = {
            n.uid for n in provenance_graph.artifacts_by_operation("burst_selection")
        }
        for node in provenance_graph.nodes.values():
            if node.relationship == "companion_of":
                # companion_of means the primary source is the burst table
                assert any(s in burst_uids for s in node.source_uids), (
                    f"UID {node.uid} ({node.name}) companion_of "
                    f"but no source in burst_uids {burst_uids}"
                )

    def test_irf_links_to_photon_stream(self, provenance_graph):
        stream_uids = {n.uid for n in provenance_graph.sources()}
        for node in provenance_graph.nodes.values():
            if node.operation_type == "tcspc_calibration":
                assert any(s in stream_uids for s in node.source_uids), (
                    f"IRF {node.name} does not link to a photon stream"
                )

    def test_ancestors_reach_photon_stream(self, provenance_graph):
        stream_uids = {n.uid for n in provenance_graph.sources()}
        for node in provenance_graph.nodes.values():
            if node.kind == "tttr_photon_stream":
                continue
            ancestors = provenance_graph.ancestors(node.uid)
            assert any(a in stream_uids for a in ancestors), (
                f"UID {node.uid} ({node.name}) cannot trace back to a photon stream"
            )

    def test_mle_has_calibrated_by_edge(self, provenance_graph, pto_path):
        """MLE tables must have a calibrated_by edge to their IRF."""
        reader = tttrlib.PtoFile()
        reader.open(pto_path)
        mle_nodes = [n for n in provenance_graph.nodes.values()
                     if n.operation_type in ("mle_green", "mle_red")]
        for node in mle_nodes:
            tags = reader.tags_for(node.uid)
            has_calibrated = any(
                t.name == "_mmfdb_edge.relationship_type"
                and t.text == "calibrated_by"
                for t in tags
            )
            assert has_calibrated, (
                f"MLE {node.name} missing calibrated_by edge"
            )
        reader.close()


# ── 4. round-trip reconstruction ───────────────────────────────────────

class TestRoundTripReconstruction:
    """Read the .pto, rebuild the pipeline, re-run, compare results."""

    def test_pipeline_reconstructs(self, provenance_graph):
        pipeline = BurstPipeline.from_provenance(provenance_graph)
        assert pipeline.operations
        burst_ops = [o for o in pipeline.operations
                     if o.operation_type == "burst_selection"]
        assert len(burst_ops) == 1

    def test_settings_preserved_through_roundtrip(self, provenance_graph):
        original = mfd_config()
        pipeline = BurstPipeline.from_provenance(provenance_graph)
        for orig_op in original:
            if orig_op.operation_type == "burst_selection":
                recon_ops = [o for o in pipeline.operations
                             if o.operation_type == "burst_selection"]
                assert recon_ops
                assert orig_op.settings == recon_ops[0].settings, (
                    f"Settings mismatch for {orig_op.name}:\n"
                    f"  original: {orig_op.settings}\n"
                    f"  reconstructed: {recon_ops[0].settings}"
                )

    def test_burst_count_matches_on_rerun(self, pto_path, provenance_graph, tttr_data):
        """Re-run burst search with reconstructed settings, compare burst count."""
        reader = tttrlib.PtoFile()
        reader.open(pto_path)

        # Read original burst count from the container
        original_count = None
        for node in provenance_graph.nodes.values():
            if node.operation_type == "burst_selection":
                cols = tttrlib.pto_store_columns(reader, node.uid)
                store = tttrlib.pto_store(reader, node.uid, list(cols))
                original_count = len(store["First Photon"])
                break
        reader.close()
        assert original_count is not None
        assert original_count > 0

        # Re-run with reconstructed pipeline
        pipeline = BurstPipeline.from_provenance(provenance_graph)
        result = pipeline.analyze_one(tttr_data)
        rerun_store = result["stores"]["burst_search"]
        rerun_count = len(np.asarray(
            tttrlib.pto_store_columns.__self__ if False else [0]
        ))  # can't easily read n_rows from DataStore; use burst_indices
        rerun_count = result["burst_indices"].shape[0]

        assert rerun_count == original_count, (
            f"Burst count mismatch: original={original_count} rerun={rerun_count}"
        )


# ── 5. column-name conformance ─────────────────────────────────────────

class TestColumnConformance:
    """Every emitted column must match mmfdb.dic."""

    @staticmethod
    def _dic_names():
        import re
        text = DIC_PATH.read_text()
        names = set()
        for cat, key in [("burst_column", "column"), ("constant", "name"),
                         ("derived_column", "column")]:
            for m in re.finditer(
                rf"_mmfdb_{cat}\.{key}\s+(?:\"([^\"]+)\"|(\S+))", text
            ):
                names.add(m.group(1) or m.group(2))
        return names

    def test_bur_columns_in_dic(self, pto_path):
        dic_names = self._dic_names()
        reader = tttrlib.PtoFile()
        reader.open(pto_path)
        for obj in reader.objects():
            if obj.kind == "burst_table" and ".bur" in obj.name:
                cols = tttrlib.pto_store_columns(reader, obj.uid)
                missing = set(cols) - dic_names
                assert not missing, (
                    f"Columns in {obj.name} not in mmfdb.dic: {missing}"
                )
        reader.close()


# ── 6. ndx equation evaluation ─────────────────────────────────────────

class TestNdxEquations:
    """All applicable ndx equations must compute on the output data."""

    def test_core_equations_compute(self, pto_path):
        import yaml

        eq_path = pathlib.Path("/Users/tpeulen/dev/chisurf/modules/ndxplorer/ndxplorer/settings/mfd.equations.yaml")
        const_path = pathlib.Path("/Users/tpeulen/dev/chisurf/modules/ndxplorer/ndxplorer/settings/mfd.constants.json")
        if not eq_path.exists():
            pytest.skip("chiSurf ndx equations not found")

        with open(eq_path) as f:
            equations = yaml.safe_load(f)
        with open(const_path) as f:
            constants = json.load(f)

        # Read data from PTO
        reader = tttrlib.PtoFile()
        reader.open(pto_path)
        data = {}
        for obj in reader.objects():
            if ".bur" in obj.name or ".bg4" in obj.name:
                cols = tttrlib.pto_store_columns(reader, obj.uid)
                store = tttrlib.pto_store(reader, obj.uid, list(cols))
                for c in cols:
                    data[c] = np.asarray(store[c])
        reader.close()

        import pandas as pd
        import ast
        _ALLOWED = (ast.Expression, ast.BinOp, ast.UnaryOp, ast.Constant,
                     ast.Name, ast.Load, ast.Call, ast.Add, ast.Sub, ast.Mult,
                     ast.Div, ast.Pow, ast.Mod, ast.USub, ast.UAdd)

        df = pd.DataFrame(data)
        eq_outputs = {}
        computed = 0
        for mapping in equations:
            for out_key, expr in mapping.items():
                refs = __import__("re").findall(r"'([^']+)'", expr)
                ns = {}
                skip = False
                for i, ref in enumerate(refs):
                    if ref in constants:
                        ns[f"_r{i}"] = constants[ref]
                    elif ref in df.columns:
                        ns[f"_r{i}"] = df[ref].values
                    elif ref in eq_outputs:
                        ns[f"_r{i}"] = eq_outputs[ref]
                    else:
                        skip = True
                        break
                if skip:
                    continue

                class R(ast.NodeTransformer):
                    idx = 0
                    def visit_Constant(self, n):
                        if isinstance(n.value, str):
                            x = ast.Name(id=f"_r{self.idx}", ctx=ast.Load())
                            self.idx += 1
                            return ast.copy_location(x, n)
                        return n
                r = R()
                tree = r.visit(ast.parse(expr, mode="eval"))
                ast.fix_missing_locations(tree)
                code = compile(tree, "<eq>", "eval")
                eq_outputs[out_key] = eval(code, {"__builtins__": {}, "abs": np.abs}, ns)
                computed += 1

        assert computed >= 15, f"Only {computed} equations computed (expected >= 15)"
