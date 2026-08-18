"""A pipeline is a document: JSON, `.pto`, Python -- and an mmfdb workflow.

The point is reproducibility: the analysis that produced a result travels with
it, names its format and the tttrlib version that wrote it, and can be re-run
somewhere else. These tests pin the round trips (Python -> JSON -> Python,
Python -> .pto -> Python, Python -> mmfdb -> Python), the version/format
stamping and what happens when a document is too new or names an operation
this build does not have, and that a reloaded pipeline computes the same
numbers as the one that was written.
"""
import json
import warnings

import numpy as np
import pytest

import tttrlib


@pytest.fixture
def pipeline():
    return (tttrlib.Pipeline("segmentation", description="regions and their outlines")
            .then("region_segmentation")
            .then("iso_contours", level=1.5, vertex_connect_high=False))


@pytest.fixture
def image():
    rng = np.random.default_rng(3)
    img = rng.random((24, 24))
    markers = np.zeros((24, 24), np.int32)
    markers[4, 4], markers[18, 18] = 1, 2
    return img, markers


def _run(p, image):
    img, markers = image
    return np.asarray(p.run(img, adapters={
        "region_segmentation": lambda v: ((v, markers), {}),
        "iso_contours": lambda v: ((np.asarray(v, dtype=float),), {}),
    }))


# ------------------------------------------------------------- composition --

def test_a_pipeline_is_built_by_naming_registered_operations(pipeline):
    assert len(pipeline) == 2
    assert [s["operation"] for s in pipeline.steps] == ["region_segmentation", "iso_contours"]
    assert pipeline.steps[1]["params"] == {"level": 1.5, "vertex_connect_high": False}


def test_steps_are_values_not_mutations(pipeline):
    longer = pipeline.then("clustering", n_clusters=3)
    assert len(pipeline) == 2 and len(longer) == 3


def test_the_pipe_operator_composes(pipeline):
    p = tttrlib.Pipeline("x") | "region_segmentation" | ("iso_contours", {"level": 1.5,
                                                                          "vertex_connect_high": False})
    assert [s["operation"] for s in p.steps] == [s["operation"] for s in pipeline.steps]
    joined = pipeline | tttrlib.Pipeline("y").then("clustering")
    assert len(joined) == 3


def test_an_unregistered_operation_is_refused_when_the_step_is_added():
    with pytest.raises(ValueError):
        tttrlib.Pipeline("x").then("no_such_operation")


def test_duplicate_operations_get_distinct_ids():
    p = tttrlib.Pipeline("x").then("region_segmentation").then("region_segmentation")
    assert [s["id"] for s in p.steps] == ["region_segmentation", "region_segmentation_2"]


# ---------------------------------------------------------------- the doc --

def test_the_document_states_its_format_and_the_writing_version(pipeline):
    doc = pipeline.to_dict()
    assert doc["format"] == "tttrlib.pipeline"
    assert doc["format_version"] == tttrlib.PIPELINE_FORMAT_VERSION
    assert doc["version"] == tttrlib.MMFDB_WORKFLOW_VERSION      # mmfdb schema
    assert doc["software"] == {"package": "tttrlib", "version": tttrlib.__version__}
    for step in doc["steps"]:
        assert step["software"]["version"] == tttrlib.__version__
        assert step["python"] == "tttrlib.pipeline:run_step"


def test_each_step_carries_the_mmfdb_operation_type(pipeline):
    """The registry's `operation_type` -- mmfdb's controlled vocabulary -- is
    what a provenance reader validates against, so it is in the document."""
    doc = pipeline.to_dict()
    assert doc["steps"][0]["operation_type"] == \
        tttrlib.describe("region_segmentation")["operation_type"]


def test_json_round_trip_is_exact(pipeline):
    assert tttrlib.Pipeline.from_json(pipeline.to_json()) == pipeline


def test_a_saved_pipeline_reloads_and_computes_the_same_numbers(pipeline, image, tmp_path):
    path = pipeline.save(tmp_path / "pipeline.json")
    reloaded = tttrlib.Pipeline.load(path)
    assert reloaded == pipeline
    assert np.array_equal(_run(reloaded, image), _run(pipeline, image))


def test_a_document_from_a_newer_format_is_refused_by_default(pipeline):
    doc = pipeline.to_dict()
    doc["format_version"] = tttrlib.PIPELINE_FORMAT_VERSION + 1
    with pytest.raises(ValueError) as excinfo:
        tttrlib.Pipeline.from_dict(doc)
    assert "newer" in str(excinfo.value)
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        tttrlib.Pipeline.from_dict(doc, strict=False)
    assert caught, "non-strict reading must still warn"


def test_a_step_this_build_cannot_run_is_named_in_the_error(pipeline):
    doc = pipeline.to_dict()
    doc["steps"][0]["operation"] = "operation_from_a_plugin_we_do_not_have"
    with pytest.raises(ValueError) as excinfo:
        tttrlib.Pipeline.from_dict(doc)
    assert "operation_from_a_plugin_we_do_not_have" in str(excinfo.value)


def test_a_foreign_document_is_refused(pipeline):
    with pytest.raises(ValueError):
        tttrlib.Pipeline.from_json(json.dumps({"format": "something.else", "steps": []}))


# ---------------------------------------------------------------- the pto --

def test_a_pipeline_round_trips_through_a_pto_container(pipeline, image, tmp_path):
    path = str(tmp_path / "run.pto")
    pipeline.to_pto(path)
    reloaded = tttrlib.Pipeline.from_pto(path)
    assert reloaded == pipeline
    assert np.array_equal(_run(reloaded, image), _run(pipeline, image))


def test_the_pto_names_the_workflow_the_way_mmfdb_does(pipeline, tmp_path):
    """`_mmfdb_workflow.definition` is mmfdb's item for the verbatim workflow,
    so a reader that knows mmfdb finds it without knowing tttrlib."""
    path = str(tmp_path / "run.pto")
    pipeline.to_pto(path)
    f = tttrlib.PtoFile()
    assert f.open(path)
    try:
        tags = {t.name: t.text for t in f.tags()}
    finally:
        f.close()
    assert tags["_mmfdb_workflow.name"] == "segmentation"
    assert tags["_mmfdb_workflow.version"] == str(tttrlib.PIPELINE_FORMAT_VERSION)
    assert json.loads(tags["_mmfdb_workflow.definition"])["steps"][0]["operation"] \
        == "region_segmentation"


def test_load_dispatches_on_the_extension(pipeline, tmp_path):
    pto = str(tmp_path / "run.pto")
    pipeline.to_pto(pto)
    assert tttrlib.Pipeline.load(pto) == pipeline


def test_writing_a_pipeline_into_an_existing_pto_keeps_its_contents(pipeline, tmp_path):
    path = str(tmp_path / "data.pto")
    f = tttrlib.PtoFile()
    assert f.create(path, "measurement")
    store = tttrlib.DataStore("bursts")
    store.set_n_rows(4)
    store.add("Tau", np.linspace(1.0, 4.0, 4))
    tttrlib.pto_add_store(f, "table", "bursts", store)
    assert f.commit()
    f.close()
    del store        # freed here, not in whatever test runs next

    pipeline.to_pto(path)

    assert tttrlib.Pipeline.from_pto(path) == pipeline
    f = tttrlib.PtoFile()
    assert f.open(path)
    try:
        assert f.n_objects() >= 1        # the burst table survived
    finally:
        f.close()


# -------------------------------------------------------------- the mmfdb --

def test_the_mmfdb_document_is_the_workflow_schema(pipeline):
    """mmfdb's workflow schema v1: sources + steps with id / operation_type /
    software / inputs / params / python / outputs (mmfdb/workflow/spec.py)."""
    doc = pipeline.to_mmfdb(sources={"raw": {"path": "data.spc", "kind": "raw_measurement"}})
    assert doc["version"] == 1
    assert set(doc) == {"version", "name", "description", "sources", "steps"}
    assert doc["sources"]["raw"]["path"] == "data.spc"
    for step in doc["steps"]:
        assert set(step) == {"id", "operation_type", "software", "inputs", "params",
                             "python", "outputs"}
        assert step["software"] == {"package": "tttrlib", "version": tttrlib.__version__}
        assert step["python"] == "tttrlib.pipeline:run_step"
        assert step["params"]["tttrlib_operation"]


def test_the_operation_types_are_mmfdb_vocabulary(pipeline):
    import mmfdb_dictionary
    if not mmfdb_dictionary.dictionaries():
        pytest.skip(mmfdb_dictionary.WHERE_WE_LOOKED)
    defined = set(mmfdb_dictionary.enumeration("_mmfdb_operation.operation_type"))
    for step in pipeline.to_mmfdb()["steps"]:
        assert step["operation_type"] in defined, \
            f"{step['operation_type']} is not in mmfdb's vocabulary"


def test_an_mmfdb_workflow_round_trips(pipeline):
    doc = pipeline.to_mmfdb()
    assert tttrlib.Pipeline.from_mmfdb(doc) == pipeline


def test_foreign_steps_of_an_mmfdb_workflow_are_skipped(pipeline):
    """A workflow that stitches tttrlib and another tool loads here as the
    tttrlib steps only -- running someone else's tool is their business."""
    doc = pipeline.to_mmfdb()
    doc["steps"].insert(0, {"id": "fretbursts", "operation_type": "burst_selection",
                            "software": {"package": "FRETBursts", "version": "0.8.3"},
                            "inputs": {"photons": "raw"}, "params": {"F": 6, "m": 10},
                            "python": "my_adapters:run_fretbursts", "outputs": {}})
    loaded = tttrlib.Pipeline.from_mmfdb(doc)
    assert [s["operation"] for s in loaded.steps] == \
        [s["operation"] for s in pipeline.steps]


def test_the_run_target_is_importable():
    """`tttrlib.pipeline:run_step` is what an mmfdb `python` step names; the
    module path must resolve, or mmfdb cannot run a tttrlib step."""
    import importlib
    module = importlib.import_module("tttrlib.pipeline")
    assert callable(module.run_step)
