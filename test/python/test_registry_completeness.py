"""Every algorithm the library exposes is in the one registry.

The registry (core, `Registry.h`) is assembled from what registered, next to
its code. That makes it complete only if every module actually registers -- so
this test walks the Python surface (`dir(tttrlib)`: every public class and
function) and requires each symbol to be either

* named by a registry entry -- in its ``api`` list (``"BVA"``,
  ``"CLSMImage.get_mean_lifetime"``), as its ``method`` or ``name``; or
* declared *plumbing* below, in a named group with the reason.

A new class or function that is neither fails here, which is the point: the
question "did the new algorithm register?" is answered by the suite, not by
someone remembering. Data structures, I/O and helpers go in the plumbing list;
an algorithm, a model, an estimator or an operation goes in the registry.
"""
import inspect
import json

import pytest

import tttrlib

# ----------------------------------------------------------- the surface --

def _public_surface():
    names = []
    for n in dir(tttrlib):
        if n.startswith("_"):
            continue
        if n.startswith(("Vector", "Map", "Pair", "Swig", "vector_", "Set")) or n.endswith("Vector"):
            continue                     # SWIG container templates
        obj = getattr(tttrlib, n)
        if inspect.isclass(obj) or inspect.isbuiltin(obj) or inspect.isfunction(obj):
            names.append(n)
    return sorted(set(names))


# --------------------------------------------------------- the plumbing --
#
# Not algorithms. Each group says why. Adding to this list is a reviewable
# statement that the symbol computes nothing a user would look up by name.
PLUMBING = {
    "photon-stream data model": {
        "TTTR", "TTTRHeader", "TTTRDecodeState", "TTTRStreamWriter", "RecordStreamWriter",
        "ContainerRecords", "PhotonSink", "PhotonStreamHub",
        "concat", "unit", "version", "experimental", "mark_experimental", "ExperimentalWarning",
        "dataclass",
    },
    "file formats and I/O (the file_container / table_format catalogs)": {
        "open_file", "replace_file", "get_supported_filetypes", "inferTTTRFileType",
        "inferTTTRContainerTypeFromExtension", "tttrContainerCanonicalExtension",
        "isBH132File", "isBHSPCQCFile", "isCZConfocor3File", "isFlimLabsITT1File",
        "isFlimLabsSTT1File", "isHDF5File", "isHT3File", "isPTUFile", "isPhotonsFile", "isSMFile",
        "is_pto_file", "is_store_file", "can_stream", "container_chunks", "container_events",
        "container_n_records", "container_read_events", "container_read_records", "container_records",
        "container_supports_ranged_reads", "decodable_record_types", "decode_records",
        "record_bytes", "record_stream_supported", "record_type_is_decodable", "record_type_name",
        "bh_set", "parse_set", "read_set_file", "BhSetParameter",
        "read_hdf5", "write_hdf5", "hdf5_bytes_read", "read_hdf5_table", "read_hdf5_table_columns",
        "read_hdf5_table_into", "write_hdf5_table", "hdf5_table_available", "hdf5_table_groups",
        "hdf5_table_has", "hdf5_table_remove",
        "read_csv", "read_csv_column_names", "read_csv_into", "infer_csv_columns", "write_csv",
        "write_csv_string", "CsvOptions", "CsvWriteOptions",
        "read_table", "read_table_into", "write_table", "table_columns", "table_format_name",
        "table_format_of", "table_groups", "table_has",
        "imread", "imwrite", "tiff_dtype", "tiff_dtype_name", "tiff_info", "tiff_metadata", "TiffInfo",
        "compress_on_read_enabled", "init_auto_compress_on_read",
        "native_to_utf8", "utf8_to_native", "safe_getenv", "Path", "StorePath",
    },
    "DataStore / PTO containers (columnar storage, not analysis)": {
        "DataStore", "DataStoreInfo", "DataStoreRegistry", "Column", "BitMask", "NaRange",
        "column_type_name", "column_type_size", "data_store_report", "data_stores",
        "datastore_index_out_of_range", "tttr_index_out_of_range", "live_data_store_bytes",
        "live_data_stores", "load_store", "load_store_region", "read_store", "read_store_into",
        "save_store", "store_bytes_read", "store_columns", "store_groups", "store_has", "write_store",
        "PtoAnnotation", "PtoCue", "PtoExtent", "PtoFile", "PtoFileType", "PtoObject",
        "PtoPhotonStream", "PtoTag", "pto_add_store", "pto_bundle", "pto_bundle_files",
        "pto_classify_path", "pto_events", "pto_mark_sidecar", "pto_read_events", "pto_read_store",
        "pto_store", "pto_store_columns", "pto_store_groups", "pto_store_region", "pto_update_store",
        "is_floating", "is_false_value", "is_feature_enabled_by_env", "edge_less",
    },
    "the registry itself": {
        "AlgorithmDescriptor", "algorithm_capabilities", "algorithm_key", "algorithm_operations_json",
        "algorithms_json", "api_index", "find_algorithm", "fit_models_json", "fit_objectives_json",
        "fit_setup_json", "operation_registry_json", "register_algorithm", "register_algorithm_json",
        "registry", "registry_categories", "registry_category_json", "registry_json",
        "describe", "defaults", "resolve", "compose",
        "Pipeline", "run_step",   # the pipeline document API (constants are not callables)
    },
    "runtime / build introspection": {
        "configure_openmp", "detect_features", "get_avx_compiled", "get_avx_enabled",
        "get_fma_enabled", "get_neon_compiled", "get_neon_enabled", "get_openmp_enabled",
        "get_openmp_num_threads",
    },
    "random sampling primitives (used by the simulator and PDA, not analyses)": {
        "sample_from_cdf", "weighted_choice",
    },
}


def _plumbing():
    out = set()
    for group in PLUMBING.values():
        out |= group
    return out


# --------------------------------------------------------- the registry --

@pytest.fixture(scope="module")
def covered():
    reg = json.loads(tttrlib.registry_json())
    names = set()
    for category, entries in reg.items():
        if category in ("file_container", "table_format", "plugin"):
            continue
        for key, e in entries.items():
            names.add(key)
            if e.get("method"):
                names.add(e["method"])
            names.update(e.get("api", []))    # "CLSMImage.fill" covers that method only;
                                              # an entry names the class itself when it owns it
    return names


def test_every_public_symbol_is_registered_or_declared_plumbing(covered):
    surface = set(_public_surface())
    plumbing = _plumbing()
    uncovered = sorted(surface - covered - plumbing)
    assert not uncovered, (
        "public symbols that neither a registry entry names (api / method) nor the "
        f"PLUMBING list declares:\n  " + "\n  ".join(uncovered))


def test_plumbing_list_is_current(covered):
    """A plumbing symbol that no longer exists, or that some entry now claims,
    is a stale line here."""
    surface = set(_public_surface())
    stale = sorted(_plumbing() - surface)
    assert not stale, f"PLUMBING names symbols that do not exist: {stale}"
    claimed = sorted(_plumbing() & covered)
    assert not claimed, f"declared plumbing but a registry entry names it: {claimed}"


def test_every_api_symbol_exists(covered):
    """An `api` name a registry entry claims must resolve in Python -- a
    renamed method would otherwise leave the entry pointing at nothing.

    The names are **Python** paths resolved with `getattr` from the `tttrlib`
    module: `Class.method`, with a dot. C++ notation (`Class::method`) is the
    mistake this catches most often -- it resolves to nothing, so the entry
    silently documents an API that cannot be reached. A nested C++ type is
    named by what SWIG calls it, not by its C++ path: `NeuralNet::Backward`
    crosses as `NeuralNetBackward`."""
    reg = json.loads(tttrlib.registry_json())
    missing = []
    for category, entries in reg.items():
        for key, e in entries.items():
            for a in e.get("api", []):
                obj = tttrlib
                try:
                    for part in a.split("."):
                        obj = getattr(obj, part)
                except AttributeError:
                    hint = ""
                    if "::" in a:
                        hint = (f"  <- C++ notation; write "
                                f"'{a.replace('::', '.')}' (a method) or "
                                f"'{a.replace('::', '')}' (a nested type)")
                    missing.append(f"{category}/{key}: {a}{hint}")
    assert not missing, (
        "registry entries name API symbols that do not resolve in Python:\n"
        + "\n".join(missing))


def test_every_module_registered_something():
    """Each compiled module contributes at least one entry -- the coarse check
    that a module's static registration actually ran in this build."""
    reg = json.loads(tttrlib.registry_json())
    cats = set(reg)
    for expected in ("burst_search", "burst", "fit", "objective", "prior", "operation",
                     "fcs", "correlation_method", "hmm", "pda", "clsm", "superres",
                     "localization", "kinetics", "fluctuation", "corrections", "decay",
                     "math", "simulation", "histogram", "selection", "calibration"):
        assert expected in cats and reg[expected], f"category {expected!r} is missing or empty"
