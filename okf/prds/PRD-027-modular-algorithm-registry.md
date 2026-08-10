# PRD-027 — A modular algorithm registry: compile, register, provenance

> **PRD #:** 027 · **Status:** 🟡 In Progress · **Created:** 2026-08-08 · **Updated:** 2026-08-10 · **Owner:** tpeulen
>
> **Implementation (2026-08-09):** The `register_operation` ABI is now live.
> `tttrlib_operation_v1` struct added to `tttrlib_plugin.h` (append-only,
> struct_size gated). `PluginHost::operations()` / `operations_json()`
> implemented with full journal rollback. Registry splices plugin-provided
> operations into the `operation` category alongside the 8 built-in entries.
> Build clean, verified: `registry_category_json("operation")` returns 8
> operations.
>
> **Implementation (2026-08-10):** Parts 1 and 3 are in for the families that
> had no registry at all. `AlgorithmDescriptor` and `register_algorithm` live in
> `modules/registry/include/AlgorithmRegistry.h`; `algorithms_json(capability)`,
> `algorithm_capabilities()`, `find_algorithm()` and `algorithm_operations_json()`
> serve the live registrations, and `registry_json()` assembles `fcs`, `hmm` and
> `pda` from them. **The PRD's first stated problem is closed**: FCS, HMM and PDA
> are no longer invisible. Each entry carries full prose `description` and a
> `references` array (criteria 13/14); replayable ones reach the `operation`
> category, so the provenance system resolves them without a source edit
> (criteria 4/5, for these families). Covered by
> `test/python/test_algorithm_registry.py` (30 cases) and a new conformance case
> `registry.live_algorithm_categories` so every binding has to serve them.
>
> Merging is deliberately additive during the migration: a live registration
> never displaces a hand-authored entry of the same name, because a collision is
> a mistake to surface rather than to resolve by load order.
>
> **Criterion 6 (2026-08-10):** done. `TTTR::burst_search` dispatches through
> `find_burst_search(name)` (`BurstSearchDispatch.h`) instead of a chain of
> `if (mode == "...")`. The chain was not merely inelegant: it lived inside the
> one function every burst search has to be reachable from, so adding a search
> meant editing that function, and a search contributed from anywhere else could
> not be reached by name at all however completely it was implemented. Adding
> one is now a `register_burst_search(name, fn)` call from the search's own
> translation unit. Behaviour is unchanged and pinned by
> `test/python/burstfilter/test_burst_search_dispatch.py` (17 cases), including
> the two things easiest to lose: the `T` reinterpretation the narrow
> `(L, m, T)` signature forces on `kalman`, `maxtree` and `bayesian_blocks`, and
> the fallback that runs the sliding window for an unrecognised mode rather than
> raising.
>
> **Still open, each its own piece of work:**
> - Criterion 2 — **`kBurstSearchRegistry` is retired (2026-08-10)**; all seven
>   burst searches declare descriptor and dispatch function in one call, and the
>   category kept its shape (0 entries removed or changed, 63 fields added).
>   `kOperationRegistry` and `kFitRegistry` remain. See PRD-032. The constraint
>   that made this more than a move — the `burst_search` and `fit` categories
>   have a consumer-visible entry shape (`method`, `params_schema`) differing
>   from the descriptor's — is now discharged for `burst_search` by
>   `dispatch_name` and the shape-compatible aliases.
> - Criteria 7/8 — `TTTRLIB_MODULAR_ALGORITHMS` shared-library split.
> - **Criterion 10 — done for operation types (2026-08-10).** The gap was wider
>   than the four new names: `mmfdb.dic` had **no operation category at all**, so
>   none of the `_mmfdb_operation.*`, `_mmfdb_artifact.*` or `_mmfdb_edge.*` tags
>   the `.pto` writer has always emitted were defined anywhere — including the
>   eight hand-authored pipeline operations. Added: the three categories, an
>   `_mmfdb_operation.operation_type` item whose enumeration is the controlled
>   vocabulary (all 12 operations), controlled vocabularies for
>   `_mmfdb_artifact.row_grain` and `_mmfdb_artifact.data_format`, definitions
>   for `settings_json`, `parent_operation`, `relationship_type` and
>   `source_node_id`, and a save block per operation carrying its label, grain,
>   format and replayability.
>   Enforced **in both directions** by `test/python/test_registry_matches_mmfdb.py`:
>   a registered operation missing from the dictionary fails, and a dictionary
>   entry nobody registers fails too. Both directions were verified to fire by
>   perturbing the dictionary, because a conformance check that cannot fail is
>   decoration. Settings keys and output column names are **not** yet checked
>   against the dictionary — that is the remainder of criterion 10.
> - Criteria 16-18 — `PtoFile.citations*`.
> - The PDA citation (Antonik et al. 2006) was taken from the literature, not
>   transcribed from the source — PDA carries no reference of its own. It is
>   marked as needing confirmation in `BuiltinAlgorithms.cpp`.
>
> **Next:** PRD-032 (migrate burst_search and decay_fit onto the same path and
> delete the literals), then the C++ port of PRD-026 is unblocked.
>
> **BLOCKER for** [PRD-026](PRD-026-mfd-sim-to-ndx-pto-pipeline.md) C++ port.
> The burst pipeline C++ port cannot proceed until this PRD is implemented.
>
> **Audit (2026-08-08):** The plugin host infrastructure is solid (discovery,
> dlopen, quarantine, rollback, JSON emission). Three capability types work
> end-to-end (container, decay_fit, burst_search). The gap is that the set of
> capability types is **closed** — each is a bespoke C struct with a bespoke
> `register_*` slot. A `.dll` cannot register a new algorithm type (BVA, 2CDE,
> IRF extraction) without editing tttrlib source. This PRD generalises it.

## Summary

A new algorithm lands every month — burst searches, lifetime fitters, FCS
models, HMM variants, PDA likelihoods — and every one arrives ad hoc: a C++
class, a SWIG wrapper, maybe a hand-authored JSON literal in the registry, and
no way for the `.pto`/mfdb provenance system to know it exists unless someone
also edits `OperationRegistry.cpp`. FCS, HMM, and PDA have no registry entry at
all and no plugin path.

Fix all of that with one mechanism. Every algorithm — built-in or plugin —
implements a **single interface** (the *algorithm descriptor*), compiles into a
**self-contained unit** (either the core library or an independent shared
library), and **registers itself** at load time. The registry discovers what
registered. The provenance system reads the registry to know each operation's
inputs, outputs, settings schema, and whether it can replay. No hand-authored
JSON literals. No hardcoded dispatch switches. No special-casing.

Existing algorithms are refactored onto this path. Core compatibility to 0.26
is preserved: the public C++ classes and SWIG-wrapped methods keep their
signatures; the new path is additive.

Names follow the mmfdb/flrCIF vocabulary throughout: operation types, settings
keys, column items.

## Problem / motivation

### Today's landscape

```
                    COMPILE TIME                          RUNTIME
                    ───────────                           ───────
Built-in burst  ─→  BurstSearchRegistry.cpp            ─→ hardcoded method switch
searches            (JSON literal)                        on TTTR (string → if/else)

Built-in decay  ─→  FitRegistry.cpp                    ─→ explicit register_decay_fit()
fits                (JSON literal)                        called by name

Plugin decay    ─→  tttrlib_decay_fit_v1 C table       ─→ PluginDecayFitModel adapter
fits                                                      (wraps C table → C++ class)

Plugin burst    ─→  tttrlib_burst_search_v1 C table   ─→ PluginHost::burst_search()
searches

Pipeline ops    ─→  OperationRegistry.cpp             ─→ schema only
(.pto provenance)   (JSON literal)                        (replay is a Python prototype)

FCS / HMM / PDA ─→  direct C++ classes                 ─→ NO registry, NO plugin path
```

Five things are wrong with this:

1. **FCS, HMM, PDA are invisible.** They work, but the registry does not list
   them, the provenance system cannot describe them, and no plugin can
   contribute a competing implementation.

2. **The registry is descriptive, not dispatchive.** Built-ins are dispatched
   by hardcoded `if (method == "name")` switches in C++. The registry JSON
   describes the algorithm but does not *call* it. Plugin algorithms are
   dispatched by runtime table lookup. Two dispatch paths for the same concept.

3. **Registry entries are hand-authored literals.** A new algorithm means
   editing a `const char*` JSON string in `OperationRegistry.cpp`, a second
   one in `FitRegistry.cpp` or `BurstSearchRegistry.cpp`, and keeping them
   in sync. They drift.

4. **The provenance system cannot discover new operations.** An algorithm
   that is not catalogued in `kOperationRegistry` is invisible to the `.pto`
   provenance tags — a reader sees `_mmfdb_operation.operation_type =
   "fcs_correlation"` and has no schema to validate or replay it.

5. **Built-ins and plugins are structurally different.** A built-in decay
   fit is a `DecayFitModel` subclass registered via a C++ factory. A plugin
   decay fit is a C ABI table wrapped in `PluginDecayFitModel`. They end up
   indistinguishable to the consumer, but the code paths, the registration
   mechanisms, and the registries are separate. Adding a new *capability
   type* (e.g. "FCS correlator" or "HMM inference") means touching both
   paths.

### What the current plugin system gets right

The plugin C ABI (`tttrlib_plugin_init_v1`, PRD-018) is sound: version-by-
symbol-name, forward-compatible structs, host-allocated buffers, no exceptions
across the boundary, and three capability types already working (container,
decay fit, burst search). This PRD does **not** redesign the C ABI. It
generalises the registration and dispatch layer above it, and makes built-in
algorithms travel the same road.

## Goals

### One interface, one registration path

- Every algorithm — burst search, decay fit, FCS correlator, HMM inference,
  PDA likelihood, and any future type — implements a common **algorithm
  descriptor**: a self-describing struct that carries its name (mmfdb/flrCIF
  canonical), its capability type, its settings schema (JSON Schema), its
  input/output specification, and a dispatch function pointer.

- Every algorithm registers itself through the same call, whether it is
  compiled into the core library or loaded as a plugin shared library.

- The registry is built from live registrations, not hand-authored literals.
  `registry_json()` reflects what is actually loaded.

### Independently compilable

- A built-in algorithm's source can be compiled into a **separate shared
  library** (e.g. `libtttrlib_fcs.{so,dylib,dll}`) that registers at load
  time, without touching the aggregate `libtttrlib`. The build system
  supports both: monolithic (everything in one lib, the default for Python
  wheels) and modular (each algorithm family in its own shared lib).

- A third-party algorithm compiles against the public headers and the plugin
  C ABI, links nothing but the host, and registers at load.

### Provenance knows what to do

- When an algorithm registers, it declares its `operation_type` (the mmfdb
  identifier), its `settings_schema`, and its input/output grain. The
  operation registry category is built from these registrations — the
  hand-authored `kOperationRegistry` is retired.

- The `.pto`/mfdb provenance system reads the registry to validate an
  `operation_type` tag, resolve its settings schema, and determine whether
  the operation can replay. A registered algorithm that has not been seen
  before is *known* the moment the registry is queried — no source edits.

### Compatibility

- Core compatibility to 0.26: the public C++ and SWIG-wrapped API surface
  from 0.26 keeps working. New algorithms may use the new path; old call
  sites keep calling.

- The plugin C ABI (`tttrlib_plugin_init_v1`) is unchanged. Existing v1
  plugins keep loading.

## Non-goals

- **Removing the C++ class hierarchy.** `DecayFitModel`, `HMM`, `Pda`,
  `Correlator` stay as C++ types. The algorithm descriptor wraps them; it
  does not replace them.

- **Rewriting algorithm internals.** The numerics are untouched. This PRD is
  about how algorithms are discovered, registered, and dispatched — not how
  they compute.

- **A new plugin ABI version.** `_v1` stands. If the descriptor needs
  something v1 does not offer, a `_v2` entry point is added and v1 keeps
  loading — per PRD-018's forward-compat rule.

- **Mandating shared-library splitting for the default build.** The default
  build (Python wheel, conda) remains a single aggregate library. The
  *capability* to split is the deliverable; splitting is opt-in.

- **A general-purpose service locator.** The registry is for algorithms
  with settings schemas and provenance semantics. It is not a generic
  plugin-injection framework for arbitrary C++ objects.

## Part 1 — the algorithm descriptor

### What an algorithm declares

Every algorithm that wants to be discoverable, dispatchable, and provenance-
tracked declares itself through one registration call. The declaration carries
everything the registry and the provenance system need:

```cpp
// The descriptor — one per algorithm, built at registration time.
// Lives in a public header so both built-ins and external plugins can use it.
struct AlgorithmDescriptor {
    // -- identity (mmfdb/flrCIF canonical names) --
    const char* operation_type;      // e.g. "fcs_correlation", "mle_green"
    const char* display_name;        // human label
    const char* summary;            // one-line prose (for lists, tooltips)

    // -- human documentation --
    const char* description;         // full prose: what it does, when to use
                                     // it, assumptions and limitations. Ends
                                     // up in API docs and UI help, so write it
                                     // for a user who does not read source.
    const char* references_json;     // JSON array of citation objects — so a
                                     // user knows what to cite. See Part 1b.

    // -- classification --
    const char* capability;          // "burst_search", "decay_fit", "fcs",
                                     // "hmm", "pda", or a future type

    // -- schema (the contract the registry serves to consumers) --
    const char* settings_schema;     // JSON Schema of parameters
    const char* inputs_json;         // required/optional inputs
    const char* outputs_json;        // output column names (mmfdb items)
    const char* row_grain;           // "burst", "curve_point", "photon", ...
    bool can_replay;                 // can re-execute from settings + inputs

    // -- dispatch --
    // Opaque. The host never calls this directly; the capability-specific
    // registrar (Part 2) wraps it. This is the seam that lets a C ABI
    // plugin and a C++ built-in share one registration path.
    void* impl;                      // capability-specific (see Part 2)
};
```

### Part 1b — description and references

Every algorithm descriptor carries two documentation fields that the registry
serves to consumers:

**`description`** — full prose, not a one-liner. It says what the algorithm
does, when it is the right choice, what assumptions it makes, and where its
limits are. This text ends up in the generated API docs, in a UI's algorithm
help panel, and in the `operation` registry category alongside the settings
schema. A user who reads it should not need to open the source to decide
whether the algorithm is appropriate for their data.

**`references_json`** — a JSON array of citation objects so a user knows what
to cite when they use the algorithm in published work:

```json
[
  {
    "type": "journal",
    "authors": "Felekyan, S., Kalinin, S., Valeri, A., et al.",
    "title": "Filtered FlCS and 2-color filtered FCS: ...",
    "journal": "Microscopy Research and Technique",
    "year": 2005,
    "volume": "69",
    "pages": "186--194",
    "doi": "10.1002/jemt.20188"
  }
]
```

Fields:

| Field | Required | Description |
|---|---|---|
| `type` | yes | `journal`, `conference`, `preprint`, `software`, `url` |
| `authors` | yes | Full author list, as it should appear in a bibliography |
| `title` | yes | Full title |
| `year` | yes | Publication year |
| `journal` / `venue` | if applicable | Journal or conference name |
| `volume`, `pages` | if applicable | |
| `doi` | if applicable | DOI without the `https://doi.org/` prefix |
| `url` | if applicable | A canonical URL (for software / preprints) |

The registry JSON includes both fields per entry. A UI (ndx, ChiSurf, the web
app) renders the description as help text and the references as a citation
list with DOI links. A `.pto` file that stores results from this algorithm
carries the `operation_type`; a reader that looks it up in the registry finds
the references, so provenance reaches the academic literature.

Built-in algorithms populate these from their module's registry code. Plugin
algorithms populate them through the C ABI's info struct or the descriptor —
the host copies them into the registry entry at registration time.

### Registration

```cpp
// one call, used by built-ins and plugins alike
bool register_algorithm(const AlgorithmDescriptor& desc);
```

Built-in algorithms call `register_algorithm` from their module's init function
(explicit, by name — the same lesson from `DecayFitModelRegistration.h`: static
initialisers are dropped by static archives).

Plugin algorithms call it through the existing `tttrlib_plugin_init_v1` entry
point; the host's `register_*` callbacks populate the same descriptor fields and
call `register_algorithm` on the plugin's behalf.

### What retires

| Before | After |
|---|---|
| `kOperationRegistry` (hand-authored JSON literal in `OperationRegistry.cpp`) | built from live `register_algorithm` calls |
| `kFitRegistry` (hand-authored JSON literal in `FitRegistry.cpp`) | built from live registrations |
| `kBurstSearchRegistry` (hand-authored JSON literal) | built from live registrations |
| hardcoded `if (method == "name")` dispatch on `TTTR` | table lookup on the capability registry |
| `PluginHost::decay_fit_models_json()` textual splice | registrations go through `register_algorithm`; splice is unnecessary |

The hand-authored literals may be kept temporarily as a fallback during
migration, but the goal is to delete them.

## Part 2 — capability registrars

The `impl` field in `AlgorithmDescriptor` is opaque to the host. Each
capability type has a **registrar** that knows how to wrap `impl` into the
appropriate C++ object or function call. This is the same pattern already used
by `PluginHost::set_decay_fit_registrar` — generalised.

### Existing capabilities (already have registrars)

| Capability | Registrar today | What it wraps |
|---|---|---|
| `decay_fit` | `set_decay_fit_registrar` | `tttrlib_decay_fit_v1*` C table → `PluginDecayFitModel` |
| `burst_search` | inline in `PluginHost` | `tttrlib_burst_search_v1*` C table → direct call |

These are extended: the registrar accepts both C ABI tables (from plugins) and
C++ factory lambdas (from built-ins), producing the same result type.

### New capabilities (need registrars)

| Capability | C++ result type | Dispatch |
|---|---|---|
| `fcs` | `Correlator` factory | `create(name, params_json) → Correlator` |
| `hmm` | `HMM` factory | `create(name, params_json) → HMM` |
| `pda` | `Pda` factory | `create(name, params_json) → Pda` |
| *(future)* | | |

A new capability type is added by writing one registrar — not by editing
multiple registry files, the provenance system, and the dispatch switches.

### How a built-in registers

```cpp
// in modules/spectroscopy/fcs/src/fcs_registry.cpp
void register_fcs_algorithms() {
    register_algorithm({
        .operation_type = "fcs_correlation",
        .display_name   = "FCS multi-tau correlation",
        .description    = "Multi-tau correlation of two photon streams",
        .capability     = "fcs",
        .settings_schema = kFcsSettingsSchema,  // JSON Schema literal
        .inputs_json    = R"(["tttr_photon_stream"])",
        .outputs_json   = R"(["correlation_time", "correlation_amplitude"])",
        .row_grain      = "curve_point",
        .can_replay     = true,
        .impl           = &create_correlator    // factory function ptr
    });
    // ... one per correlation method: wahl, felekyan, laurence
}
```

The settings schema and output column names use mmfdb/flrCIF item identifiers.
This is the alignment required by the naming rule.

### How a plugin registers

Unchanged from today: the plugin's `tttrlib_plugin_init_v1` calls
`host->register_decay_fit()` (or `register_burst_search()`, or a future
`register_fcs()`). The host fills the `AlgorithmDescriptor` and calls
`register_algorithm`. The plugin does not know about `AlgorithmDescriptor`;
the host translates.

## Part 3 — the registry is built from registrations

### What `registry_json()` returns

Today, `registry_json()` assembles hand-authored literals + textual splices.
After this PRD, it assembles from the live algorithm registry:

```cpp
std::string registry_json() {
    json root;
    root["burst_search"] = algorithms_json("burst_search");
    root["decay_fit"]    = algorithms_json("decay_fit");
    root["fcs"]          = algorithms_json("fcs");
    root["hmm"]          = algorithms_json("hmm");
    root["pda"]          = algorithms_json("pda");
    root["operation"]    = operations_json();  // from can_replay descriptors
    root["file_container"] = file_container_entries();
    root["table_format"]   = table_format_entries();
    root["plugin"]         = plugin_entries();
    root["fit_setup"]      = fit_setup_json();
    root["objective"]      = fit_objectives_json();
    return root.dump(2);
}
```

`algorithms_json(capability)` walks the live registry, filters by capability,
and serialises each descriptor. No hand-authored literals.

`operations_json()` walks the registry, filters by `can_replay == true`, and
produces the operation registry — the schema the `.pto` provenance system
consumes. This replaces `kOperationRegistry`.

### Backward compatibility of the registry JSON

The existing registry categories (`burst_search`, `fit`, `operation`) keep
their current shape — consumers that parse them (ChiSurf, ndx, the web UI)
are unaffected. New categories (`fcs`, `hmm`, `pda`) appear as additive
keys. A consumer that does not know about them ignores them.

The `fit` category is renamed to `decay_fit` internally but aliased: a query
for `registry_category_json("fit")` returns the same result as
`registry_category_json("decay_fit")`.

## Part 4 — independently compilable algorithm modules

### Build modes

**Monolithic (default).** All algorithm families compile into the aggregate
`libtttrlib`. Registration is called explicitly from a single init function
that the SWIG module loads. This is today's wheel/conda build.

**Modular (opt-in).** Each algorithm family compiles into its own shared
library:

```
libtttrlib_core.{so,dylib}         — TTTR, DataStore, PTO, registry, plugin host
libtttrlib_burst.{so,dylib}        — burst search algorithms
libtttrlib_decay.{so,dylib}        — decay fit models
libtttrlib_fcs.{so,dylib}          — FCS correlators
libtttrlib_hmm.{so,dylib}          — HMM inference
libtttrlib_pda.{so,dyml}           — PDA likelihood
```

Each modular lib calls `register_algorithm` in its init function. The plugin
host discovers them in the same search path it uses today
(`$TTTRLIB_PLUGIN_PATH`, `<package>/plugins/`, per-user dir) — a modular
algorithm lib is, from the host's perspective, a plugin with a known
capability.

**Why this matters:** a user who needs only FCS does not link the HMM and
decay code. A third party can ship `libtttrlib_custom_fret.{so}` without
recompiling tttrlib.

### What the build system does

- `tttrlib_add_module` gains a `MODULE_GROUP` option: `core` or `algorithm`.
  Algorithm-group modules can be built into either the aggregate lib or a
  standalone shared lib, controlled by a CMake toggle
  (`TTTRLIB_MODULAR_ALGORITHMS=ON`).

- When `TTTRLIB_MODULAR_ALGORITHMS=OFF` (default), algorithm modules are
  linked into `libtttrlib` as today. When `ON`, each algorithm module
  produces its own shared library and a thin stub that calls
  `register_algorithm` at load.

- The SWIG wrapper calls a single `tttrlib_init_algorithms()` that, in
  monolithic mode, calls every built-in registration function, and in
  modular mode, triggers `PluginHost::ensure_loaded()` which discovers the
  modular libs.

## Part 5 — provenance integration

### What the provenance system gains

Today, `_mmfdb_operation.operation_type` is validated against
`kOperationRegistry` — a static list. After this PRD, it is validated against
the **live registry**. This means:

1. **A new algorithm is provenance-ready the moment it registers.** No edit
   to `OperationRegistry.cpp`. The settings schema, inputs, outputs, and
   replay capability are declared in the descriptor.

2. **A reader can validate an unknown operation type.** If
   `operation_type = "fcs_correlation"` appears in a `.pto` tag, the reader
   queries `registry_category_json("operation")`, finds the entry, reads the
   settings schema, and validates the `settings_json` against it — or
   reports "unknown operation type" if nothing registered that name.

3. **Replay is driven by the descriptor.** If `can_replay == true`, the
   settings schema + inputs are sufficient to re-execute the operation. The
   replay engine (currently a Python prototype in
   `prototype/burst_pipeline/`) dispatches through the registry:
   look up `operation_type`, find the capability, call the factory.

### The provenance tag contract (unchanged)

| Tag | Source |
|---|---|
| `_mmfdb_operation.operation_type` | `AlgorithmDescriptor.operation_type` |
| `_mmfdb_operation.settings_json` | caller-supplied, validated against `settings_schema` |
| `_mmfdb_operation.settings_hash` | SHA256[:16] of canonical settings JSON |
| `_mmfdb_artifact.data_format` | `AlgorithmDescriptor.capability`-derived |
| `_mmfdb_artifact.row_grain` | `AlgorithmDescriptor.row_grain` |
| `_mmfdb_edge.source_uid` | caller-supplied (parent artifact) |

### Part 5b — automatic citation list for a pipeline

A `.pto`-mmfdb file records which algorithms produced each artifact (via
`operation_type` tags on the provenance graph). Each registered algorithm
carries a `references_json` citation array (Part 1b). The pipeline's
citation list is the **union of references across every operation in the
graph** — built automatically, no manual curation.

**API:**

```python
import tttrlib, json

# Returns a JSON string: deduplicated citation list for every algorithm
# that touched any artifact in the container.
citations = tttrlib.PtoFile.citations("experiment.pto")

# Or formatted as a text bibliography (numbered, with DOIs)
text = tttrlib.PtoFile.citations_text("experiment.pto")

# Or a BibTeX stub
bibtex = tttrlib.PtoFile.citations_bibtex("experiment.pto")
```

**How it works:**

1. Walk the provenance graph — every artifact's `_mmfdb_operation.operation_type`.
2. Look up each `operation_type` in the live algorithm registry.
3. Collect the `references_json` from each matched descriptor.
4. Deduplicate (same DOI → one entry).
5. Return as JSON, text, or BibTeX.

**Embedded in the `.pto`:**

The citation list is also **written into the container** as a dedicated
attachment — an ASCII text file named `citations` in the `Attachments`
section, alongside the README and instrument file. This makes the file
self-contained: a reader who opens it a year later, on a machine without
tttrlib installed, finds the citations without querying a registry.

```python
# On write: embeds the citation list derived from the provenance graph
tttrlib.PtoFile.embed_citations("experiment.pto")
```

The embedded list is regenerated whenever the container is written, so it
tracks the operations actually present — not a snapshot from an earlier
version.

**What a user gets:**

```
$ cat citations
This file was produced by the following algorithms:

1. Burst search (sliding window)
   Fries, J. R., et al. "Quantitative ray-tracing..." Biophys. J. 1998.

2. Maximum-likelihood estimator (green channel)
   Margeat, E., et al. "Direct single-molecule..." JACS 2006.

3. Burst variance analysis (BVA)
   Nevskyi, O., et al. "BVA..." ChemPhysChem 2018.
```

Or as JSON for programmatic consumption, or as BibTeX for LaTeX integration.

## Part 6 — refactoring existing algorithms

### Sequencing

The refactor is additive: each algorithm family migrates independently, and
the library works at every intermediate step.

1. **Decay fits** (already closest). The explicit `register_decay_fit_models_*`
   calls build descriptors and call `register_algorithm`. `kFitRegistry` is
   generated from registrations. The `DecayFitModel` hierarchy is untouched.

2. **Burst searches.** The hardcoded method-name switch on `TTTR` is replaced
   by a table lookup on the `burst_search` capability registry. Each existing
   method registers a descriptor. `kBurstSearchRegistry` is generated.

3. **FCS.** `Correlator` gains a factory + descriptor. The three methods
   (`wahl`, `felekyan`, `laurence`) register individually. The `fcs` category
   appears in the registry for the first time.

4. **HMM.** `HMM` gains a factory + descriptor. The `hmm` category appears.

5. **PDA.** `Pda` gains a factory + descriptor. The `pda` category appears.

6. **Operation registry.** `kOperationRegistry` is deleted. `operations_json()`
   is built from `can_replay` descriptors. Existing operation types
   (`burst_selection`, `mle_green`, `bva`, `kde_cde`, etc.) are preserved
   exactly — their `operation_type` strings, settings schemas, and I/O specs
   do not change.

### What does not change

- The `DecayFitModel`, `HMM`, `Pda`, `Correlator` class hierarchies.
- The public SWIG-wrapped methods on `TTTR` (`burst_search`, etc.).
- The plugin C ABI (`tttrlib_plugin_init_v1`).
- The `.pto`/mfdb tag vocabulary and format.
- The `registry_json()` return shape (additive only).

### Compatibility surface

| Surface | 0.26 | After PRD-027 |
|---|---|---|
| `TTTR::burst_search(name, ...)` | works | works (dispatch via table lookup) |
| `DecayFitModel` subclasses | works | works |
| `registry_json()` | returns JSON | returns JSON (same shape + new keys) |
| `registry_category_json("fit")` | returns JSON | returns JSON (aliased to `decay_fit`) |
| Plugin v1 (`tttrlib_plugin_init_v1`) | works | works |
| `Correlator` direct construction | works | works |
| `HMM` direct construction | works | works |
| `Pda` direct construction | works | works |

A consumer written against 0.26 sees no difference. A consumer that opts into
the new path (registry-driven dispatch, provenance replay) gets the new
capabilities.

## Part 7 — naming alignment with mmfdb / flrCIF

Every `operation_type`, every settings key, every output column name in a
descriptor must use the canonical mmfdb/flrCIF identifier. This is the rule
from `BUGS.md` made enforceable: the registry is now the single source of
truth for what names an algorithm uses, and a CI check can compare descriptor
fields against the flrCIF dictionaries (`okf/nomenclature/mmfdb.dic`,
`mmfdb_flr_ext.dic`).

Specifically:

- `operation_type` values match `_mmfdb_operation.operation_type` entries.
- `outputs_json` column names match `mmfdb_burst_column` / `mmfdb_derived_column` /
  future `mmfdb_decay_curve` / `mmfdb_fcs_curve` items.
- `settings_schema` property names match the ChiSurf parameter registry's
  `flrcif_item_id` linkage.

New flrCIF categories that this PRD will require (and that do not yet exist in
`mmfdb.dic`):

| Category | What it describes |
|---|---|
| `mmfdb_fcs_curve` | FCS correlation curve columns (correlation time, amplitude, normalised amplitude) |
| `mmfdb_decay_curve` | TCSPC decay curve columns (channel, parallel, perpendicular, IRF, model) |
| `mmfdb_pda_histogram` | PDA 2D histogram and 1D projection columns |

These are defined in the mmfdb repository, not here — but this PRD blocks on
their existence, because without them the descriptors cannot name their
outputs canonically.

## Criteria

1. `register_algorithm(const AlgorithmDescriptor&)` exists in a public header
   and is callable from both core and plugin code.

2. Every built-in algorithm family (burst search, decay fit, FCS, HMM, PDA)
   registers through `register_algorithm`. No hand-authored `kOperationRegistry`,
   `kFitRegistry`, or `kBurstSearchRegistry` literals remain in the source.

3. `registry_json()` returns a JSON object that includes `fcs`, `hmm`, and
   `pda` categories, each populated from live registrations.

4. `registry_category_json("operation")` returns a JSON object built from
   `can_replay` descriptors, with the same entries the hand-authored
   `kOperationRegistry` had.

5. A `.pto` file tagged with `_mmfdb_operation.operation_type = "fcs_correlation"`
   is validated by the provenance system against the live registry — no source
   edit needed to teach the system about the new operation type.

6. `TTTR::burst_search(name, ...)` dispatches through a table lookup, not a
   hardcoded `if/else` chain. Adding a new burst search method does not require
   editing `TTTR`.

7. With `TTTRLIB_MODULAR_ALGORITHMS=ON`, each algorithm family compiles into a
   separate shared library. The library loads and registers correctly when
   placed in the plugin search path.

8. With `TTTRLIB_MODULAR_ALGORITHMS=OFF` (default), the aggregate library
   behaves exactly as before — same symbols, same registry output, same
   performance.

9. Every public API surface from 0.26 (`TTTR`, `DecayFitModel`, `Correlator`,
   `HMM`, `Pda`, `registry_json`, `registry_category_json`, plugin v1) works
   without modification.

10. Every `operation_type`, settings key, and output column name in every
    descriptor matches an entry in the flrCIF dictionaries. A CI check
    verifies this.

11. A third-party plugin that implements `tttrlib_decay_fit_v1` (or a future
    `tttrlib_fcs_v1`) compiles against the public headers, loads without
    recompiling tttrlib, and appears in the registry.

12. The replay prototype (`prototype/burst_pipeline/`) dispatches through the
    live registry: given an `operation_type`, it finds the descriptor and
    calls the factory — no hardcoded Python dispatch table.

13. Every algorithm descriptor carries a non-empty `description` (full prose,
    not a restatement of the name) and a `references_json` array. Every
    built-in algorithm has at least one citation.

14. The registry JSON for each algorithm entry includes `description` and
    `references` fields. A consumer (ndx, ChiSurf) can render them as help
    text and a citation list without reading source or external docs.

15. A `.pto` artifact tagged with an algorithm's `operation_type` can be
    traced to its citations: a reader queries the registry, finds the
    `references_json`, and knows what to cite.

16. `PtoFile.citations(path)` returns a deduplicated JSON citation list for
    every algorithm that produced an artifact in the container.

17. `PtoFile.citations_text(path)` and `PtoFile.citations_bibtex(path)`
    return formatted bibliography outputs (numbered text and BibTeX).

18. `PtoFile.embed_citations(path)` writes the citation list into the `.pto`
    container as a dedicated `citations` attachment. A reader without tttrlib
    installed can read it directly.
