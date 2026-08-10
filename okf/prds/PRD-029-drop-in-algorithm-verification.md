# PRD-029 — Drop-in algorithms: auto-register, verify against nomenclature, provenance-ready

> **PRD #:** 029 · **Status:** ⚪ Draft · **Created:** 2026-08-08 · **Owner:** tpeulen
>
> Builds on PRD-027 (modular algorithm registry) and PRD-018 (ABI stability).

## Summary

PRD-027 defined the algorithm descriptor and the registration path. This PRD
completes the user-facing promise: **a developer drops a shared library into a
directory and everything else happens automatically.** No recompilation. No
configuration file. No manual registry entry. The host discovers the library,
loads it, validates its declared vocabulary against the mmfdb/flrCIF
nomenclature, and — if it passes — registers it as a first-class algorithm.
The `.pto`-mfdb provenance system then knows the algorithm's operation type,
settings schema, inputs, and outputs, and can replay or validate results that
reference it.

If the library declares names that do not exist in the flrCIF dictionaries, it
is **rejected with a diagnostic** — not silently registered with unknown
identifiers that break provenance tracking.

## Problem / motivation

### Today's plugin loading is unverified

The plugin host (`PluginHost.cpp`) loads any `tttrlib_<name>.{so,dylib,dll}`,
calls `tttrlib_plugin_init_v1`, and registers whatever the plugin declares. The
checks are structural (name not empty, function pointers present,
`params_schema` non-null) but **not semantic**: a plugin can declare
`operation_type = "my_cool_algo"` and `outputs = ["my_intensity"]`, and the
host accepts them. Those names appear in the registry and in `.pto`
provenance tags, but no flrCIF dictionary defines them — a reader that opens
the `.pto` a year later cannot validate or interpret them.

### What that costs

1. **Provenance rot.** A `.pto` file references `operation_type = "my_cool_algo"`,
   but the algorithm is gone, and the flrCIF dictionaries never knew it existed.
   The provenance chain is unbroken in structure and broken in meaning.

2. **No way to catch typos at load time.** A plugin that types
   `_mmfdb_burst_colum.first_photon` (missing an `n`) registers a phantom
   column name that silently fails to match anything downstream.

3. **Third-party plugins cannot interoperate.** Two plugins that compute "FCS
   correlation" declare different `operation_type` strings because nothing
   forces them to use the canonical name. A reader cannot tell they are the
   same operation.

## Goals

- **Zero-config discovery.** Drop `tttrlib_myalgo.so` into the plugin
  directory. On the next tttrlib use, it is found, loaded, and — if valid —
  registered. No config file, no manifest, no rebuild.

- **Nomenclature verification at load time.** Every `operation_type`,
  settings key, and output column name the plugin declares is checked against
  the flrCIF dictionaries (`mmfdb.dic`, `mmfdb_flr_ext.dic`, and any
  registered extension dictionaries). Unknown names reject the plugin with a
  precise diagnostic.

- **Provenance-ready.** A successfully registered algorithm is immediately
  visible to the `.pto`-mfdb provenance system. An artifact tagged with its
  `operation_type` validates against the live registry. Settings validate
  against the plugin's schema. Replay is possible if `can_replay` is set.

- **Graceful failure.** A plugin that fails nomenclature verification is
  quarantined like any other failed plugin — its registrations are rolled
  back, it appears in `registry("plugin")` as `Failed`, and the diagnostic
  says exactly which identifier is unknown.

- **Extensible vocabulary.** A plugin that introduces genuinely new
  operations can ship its own flrCIF extension dictionary alongside the
  binary. The host loads extension dictionaries before verifying, so a
  self-contained plugin that declares both its vocabulary and its
  implementation is accepted.

## Non-goals

- **Sandboxing.** A loaded shared library runs native code. The existing
  mitigations (quarantine markers, `RTLD_NOW`, `TTTRLIB_PLUGINS=0`) stand;
  this PRD does not add a sandbox.

- **Signed plugins.** Not yet. The infrastructure for verifying a plugin's
  SHA256 is already in `PluginHost`; a trust model can be layered on later.

- **A new plugin ABI version.** `_v1` is unchanged. The verification layer
  sits above the C ABI, in the host, after init returns.

- **Online plugin marketplaces.** Discovery is local filesystem only.

## Part 1 — the loading sequence

```
1. DISCOVER     scan plugin directories for tttrlib_<name>.{so,dylib,dll}
2. QUARANTINE   write marker file (existing — survives a crash)
3. LOAD         dlopen, find tttrlib_plugin_init_v1, call it
4. REGISTER     host callbacks register capabilities into the journal
5. VERIFY       check every declared name against the flrCIF dictionaries
6. COMMIT       if verification passes: keep registrations, delete marker
7. ROLLBACK     if verification fails: undo registrations, keep marker,
                record diagnostic
```

Steps 1–4 are the existing plugin host. This PRD adds **step 5** and makes
step 6/7 conditional on its result.

## Part 2 — nomenclature verification

### What is checked

After a plugin's `init` returns `TTTRLIB_OK`, the host walks every capability
the plugin registered and checks each declared identifier:

| Field | Checked against | What "known" means |
|---|---|---|
| `operation_type` | `_mmfdb_operation.operation_type` entries | exact string match in a registered dictionary |
| `settings_schema` property names | `_mmfdb_parameter.*` items | each JSON Schema property name has a matching flrCIF item |
| `outputs` column names | `mmfdb_burst_column`, `mmfdb_derived_column`, `mmfdb_decay_curve`, `mmfdb_fcs_curve`, `mmfdb_pda_histogram`, `mmfdb_pch` | exact match |
| `row_grain` | `_mmfdb_artifact.row_grain` enum | one of: `photon`, `burst`, `curve_point`, `histogram_cell`, `histogram_bin`, `pixel`, `molecule`, `frame`, `dwell`, `track` |
| `capability` | the host's known capability types | one of: `burst_search`, `decay_fit`, `fcs`, `hmm`, `pda` (or a future registered capability) |

### How the dictionaries are loaded

The host maintains a **nomenclature index** — an in-memory set of valid
identifiers, keyed by category, loaded at first use from:

1. **Built-in dictionaries** bundled with tttrlib (`okf/nomenclature/mmfdb.dic`
   and `mmfdb_flr_ext.dic`).
2. **Extension dictionaries** placed alongside a plugin:
   `tttrlib_<name>.dic` in the same directory. Loaded before that plugin's
   verification, so a self-contained plugin can introduce new vocabulary.
3. **Environment-pointed dictionaries**: `$TTTRLIB_NOMENCLATURE_PATH`, a
   path list of additional `.dic` files, loaded before any plugin.

The index is built once (beh the same `std::call_once` that triggers plugin
loading) and is immutable after construction. A plugin that needs a new
identifier ships the extension dictionary that defines it.

### What rejection looks like

```
-- plugin: tttrlib_custom_fcs: verification failed
   operation_type "fcs_correlate" is not in the flrCIF dictionaries.
     Did you mean "fcs_correlation"?
   output column "g_tau" is not a known mmfdb item.
     Register it in an extension dictionary (tttrlib_custom_fcs.dic)
     or use the canonical name "correlation".
   rolling back 1 registration.
```

The diagnostic is precise: each unknown identifier is listed, with the closest
known match if one is within edit distance. The plugin appears in
`registry("plugin")` with status `Failed` and the diagnostic in its `message`.

### Strictness policy

Verification is **strict by default**: one unknown identifier rejects the
entire plugin. This is the right default because a partially-registered
algorithm with some unknown names is worse than none — it creates the illusion
of provenance tracking while leaving gaps.

A developer working on a plugin can relax this with
`TTTRLIB_NOMENCLATURE=permissive`, which logs warnings for unknown names but
still registers. This is a development escape hatch, not a production mode,
and the registry marks plugins loaded in permissive mode with
`"nomenclature": "permissive"`.

## Part 3 — the extension dictionary format

A plugin that introduces a new operation type or new output columns ships a
flrCIF-format extension dictionary:

```
# tttrlib_custom_fcs.dic — extension vocabulary for the custom_fcs plugin

save__mmfdb_operation.fcs_correlate_modulated
    _mmfdb_operation.operation_type      fcs_correlate_modulated
    _mmfdb_operation.description         "FCS with modulated excitation"
    _mmfdb_operation.can_replay          yes
    _mmfdb_operation.row_grain           curve_point
save_

save__mmfdb_fcs_curve.modulation_amplitude
    _mmfdb_fcs_curve.modulation_amplitude .
    _mmfdb_column.units                  dimensionless
    _mmfdb_column.description            "Modulation-depth-normalised amplitude"
save_
```

The host parses it, adds the items to the nomenclature index, and then the
plugin that declares `fcs_correlate_modulated` and outputs
`modulation_amplitude` passes verification.

Extension dictionaries are **namespaced by the plugin**: their items are only
visible to plugins in the same directory. This prevents two unrelated plugins
from colliding on a name by each shipping an extension dictionary.

## Part 4 — the descriptor enrichment

The `AlgorithmDescriptor` from PRD-027 gains one field that the verification
layer populates:

```cpp
struct AlgorithmDescriptor {
    // ... all fields from PRD-027 ...

    // Filled by the verification layer after successful validation.
    // Empty until verification passes; never filled for a rejected plugin.
    const char* nomenclature_status;  // "verified", "permissive", ""
};
```

The registry JSON includes this per entry:

```json
{
  "fcs_correlation": {
    "name": "fcs_correlation",
    "operation_type": "fcs_correlation",
    "nomenclature": "verified",
    "params_schema": { ... },
    "outputs": ["correlation_time", "correlation"],
    ...
  }
}
```

A consumer that sees `"nomenclature": "verified"` knows every name is backed
by a flrCIF dictionary entry. A consumer that sees `"permissive"` knows the
plugin was loaded in development mode and some names may be unrecognised.

## Part 5 — end-to-end: the user drops a DLL

```
$ cp tttrlib_myflim.dylib ~/Library/Application\ Support/tttrlib/plugins/
$ python -c "import tttrlib; import json; print(json.dumps(
    json.loads(tttrlib.registry_json())['plugin'], indent=2))"
[
  {
    "name": "myflim",
    "status": "Loaded",
    "version": "1.0.0",
    "description": "Phase-domain FLIM fitter",
    "containers": [],
    "nomenclature": "verified",
    "sha256": "a1b2c3..."
  }
]
```

The algorithm appears in the registry:

```python
regs = json.loads(tttrlib.registry_json())
# regs["decay_fit"]["phase_domain_flim"] exists
# regs["operation"]["phase_domain_flim_fitter"] exists with settings schema
```

A `.pto` file that stores a result from this algorithm carries
`_mmfdfb_operation.operation_type = "phase_domain_flim_fitter"`, and a reader
that opens it a year later queries the registry (or the flrCIF dictionary) and
finds the schema — because the name was verified at load time.

## Part 6 — the nomenclature index API

```cpp
namespace tttrlib {

class NomenclatureIndex {
public:
    // Load built-in dictionaries + extension dictionaries + env-pointed ones.
    // Called once, behind the same call_once as plugin loading.
    static void ensure_loaded();

    // Is this identifier known in this category?
    static bool is_known(const std::string& category,
                         const std::string& identifier);

    // The closest known identifier (edit distance), or "" if none is close.
    static std::string closest_match(const std::string& category,
                                     const std::string& identifier);

    // All known identifiers in a category.
    static std::vector<std::string> items(const std::string& category);

    // Load one extension dictionary from a file path.
    // Called by the host before verifying a plugin that shipped a .dic.
    static bool load_extension(const std::string& dic_path);
};

}  // namespace tttrlib
```

Exposed through SWIG so a Python caller can query the vocabulary:

```python
tttrlib.NomenclatureIndex.is_known("mmfdb_burst_column", "first_photon")  # True
tttrlib.NomenclatureIndex.closest_match("mmfdb_operation", "burst_serch")  # "burst_selection"
```

## Part 7 — CI and testing

### Plugin SDK test

A **minimal plugin** is compiled in CI against only the public headers
(`tttrlib_plugin.h` + the algorithm descriptor header). It:

1. Declares a known `operation_type` and known output columns → loads and
   registers successfully.
2. Declares an unknown `operation_type` → is rejected with a diagnostic.
3. Ships an extension `.dic` that defines the unknown type → loads
   successfully.
4. Declares a known type but an unknown output column → rejected.

This test runs on every platform (Linux, macOS, Windows) and exercises the
full path: compile → drop → discover → verify → register.

### Nomenclature drift test

A CI job loads the built-in dictionaries and checks that every
`operation_type`, settings key, and output column declared by every built-in
algorithm in tttrlib passes verification. This catches naming drift: if a
developer renames a column without updating the dictionary, CI fails.

## Criteria

1. `NomenclatureIndex::ensure_loaded()` loads the built-in flrCIF dictionaries
   and makes them queryable through `is_known(category, identifier)`.

2. A plugin whose declared `operation_type`, settings keys, and output columns
   all exist in the flrCIF dictionaries loads and registers successfully. Its
   registry entry carries `"nomenclature": "verified"`.

3. A plugin that declares an identifier not in any flrCIF dictionary is
   rejected. All its registrations are rolled back. Its `registry("plugin")`
   entry has status `Failed` and a message listing each unknown identifier
   with the closest known match.

4. A plugin that ships an extension dictionary (`tttrlib_<name>.dic`)
   defining its new identifiers passes verification after the extension is
   loaded. Extension items are only visible to plugins in the same directory.

5. `TTTRLIB_NOMENCLATURE=permissive` logs warnings for unknown names but
   still registers. Plugins loaded in this mode carry
   `"nomenclature": "permissive"` in the registry.

6. `TTTRLIB_NOMENCLATURE_PATH` adds additional `.dic` files to the index
   before any plugin is verified.

7. A `.pto` file that references a verified plugin's `operation_type` is
   validated by the provenance system — the settings schema is found in the
   live registry and the settings JSON validates against it.

8. The CI plugin SDK test compiles a minimal plugin against only the public
   headers on all three platforms and exercises the accept, reject, and
   extension-dictionary paths.

9. The CI nomenclature drift test verifies that every built-in algorithm in
   tttrlib passes nomenclature verification against the current dictionaries.

10. `NomenclatureIndex::closest_match(category, identifier)` returns the
    nearest known identifier by edit distance, so a diagnostic can suggest
    "Did you mean ...?".

11. The extension dictionary format is a subset of flrCIF/mmCIF `save_` blocks
    and is parseable by the same parser that reads `mmfdb.dic`.
