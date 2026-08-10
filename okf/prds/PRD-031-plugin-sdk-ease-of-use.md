# PRD-031 — Plugin SDK: easy install, clear interfaces, templates, and docs

> **PRD #:** 031 · **Status:** ⚪ Draft · **Created:** 2026-08-08 · **Owner:** tpeulen
>
> Builds on PRD-027 (modular algorithm registry), PRD-029 (drop-in
 verification), PRD-018 (ABI stability).

## Summary

The plugin system works, but only someone who reads the source can use it.
The C ABI header (`tttrlib_plugin.h`) is 455 lines of densely commented C
structs. There is no template project, no high-level helper, no "start here"
guide, and no way for a Python user to install a plugin without finding the
right filesystem directory.

This PRD makes the plugin system **approachable**:

1. **`tttrlib.add_plugin(path)`** — a one-call API (Python, R, JS) that
   copies or links a shared library into the plugin directory and loads it
   immediately. No finding directories, no environment variables, no restart.
2. **`tttrlib_plugin.hpp`** — a C++ helper header that wraps the raw C ABI in
   type-safe RAII templates so a plugin author writes a decay fit or a burst
   search in 20 lines, not 200.
3. **A cookie-cutter template project** (`plugins/template/`) — a minimal,
   compilable plugin with CMake, a sample decay fit, a sample burst search,
   and a test harness. Copy it, rename, compile, drop in.
4. **End-to-end documentation** — a tutorial in `okf/` for plugin authors
   (developer-facing) and a user-facing guide in `doc/` for users who just
   want to `add_plugin` and go.

## Problem / motivation

### What a plugin author faces today

To write a plugin, you must:

1. Read `tttrlib_plugin.h` (455 lines) and understand the five contracts,
   the struct layout, the `struct_size` forward-compat scheme, and the
   host callback table.
2. Write C function pointers for every capability you want — `sniff`, `open`,
   `read`, `close` for a container; `create`, `evaluate`, `destroy` for a
   decay fit; `search` for a burst search.
3. Hand-author a `params_schema` JSON string inside your C code.
4. Write a `tttrlib_plugin_init_v1` function that fills the info struct and
   calls the host's `register_*` callbacks.
5. Name your output `tttrlib_<name>.{so,dylib,dll}` and figure out where to
   put it.
6. Discover that there is no example to copy from.

### What a plugin user faces today

To install a plugin, you must:

1. Know that plugins exist and that the search path is
   `$TTTRLIB_PLUGIN_PATH`, `<package>/plugins/`, or a per-user directory.
2. Find the per-user directory for your platform
   (`~/Library/Application Support/tttrlib/plugins/` on macOS,
   `~/.local/share/tttrlib/plugins/` on Linux,
   `%LOCALAPPDATA%\tttrlib\plugins\` on Windows).
3. Copy the file there.
4. Restart the Python session (plugin loading is `call_once`).

None of this is discoverable from the API. A user who has a `.dll` from a
collaborator has no way to install it without reading the source or the
header comments.

## Goals

### For the user

- `tttrlib.add_plugin("/path/to/tttrlib_cool.dll")` copies the plugin into
  the per-user directory and loads it in the current session. The plugin's
  capabilities are available immediately. Returns a status dict (loaded,
  failed, quarantined) with diagnostics.
- `tttrlib.list_plugins()` shows what is installed and their status —
  without reading `registry("plugin")` and parsing JSON.
- `tttrlib.remove_plugin("cool")` unlinks the plugin from the directory.
  Takes effect on next session (never `dlclose` during a running session —
  per the existing design).
- The doc site has a user-facing page: "Installing plugins" with one example.

### For the developer

- **`tttrlib_plugin.hpp`** — a C++ header that wraps the C ABI. A plugin
  author subclasses `tttrlib::plugin::DecayFit` or
  `tttrlib::plugin::BurstSearch` and implements 2–3 methods. The header
  handles the struct filling, the `params_schema`, the `create`/`destroy`
  lifecycle, and the `init` function. No raw C function pointers.
- **Template project** — `plugins/template/` with:
  - `CMakeLists.txt` that finds tttrlib's installed headers and links
    against nothing but the host.
  - `my_decay_fit.hpp` — a sample decay fit using the C++ helper.
  - `my_burst_search.hpp` — a sample burst search.
  - `my_container.hpp` — a sample file format plugin.
  - `my_flrcif_ext.dic` — a sample extension dictionary (PRD-029).
  - `test_plugin.py` — a test that loads the compiled plugin and exercises it.
- **Documentation** — an `okf/` tutorial that walks through writing,
  compiling, and installing a plugin, step by step. Covers the C ABI (for
  non-C++ authors), the C++ helper, and the verification flow.

### For everyone

- **Clear interface documentation.** Every capability type's struct,
  callback signature, and expected behaviour is documented in one place with
  examples — not buried in 455 lines of header comments. The existing header
  comments in `tttrlib_plugin.h` are excellent reference material but poor
  tutorial material.

## Non-goals

- **A plugin package manager.** No central registry, no `pip install
  tttrlib-plugin-foo`. Distribution is still "share the `.dll`". The
  `add_plugin` call accepts a local path.
- **Removing the C ABI.** The raw C structs stay. The C++ helper is layered
  on top, not a replacement. A plugin can still be written in pure C.
- **Hot-unloading.** A loaded plugin's library is never `dlclose`d during a
  session (PRD-018, the existing design). `remove_plugin` removes the file;
  the library stays loaded until the process exits.
- **A GUI plugin installer.** This is an API and CLI level feature. ndx or
  ChiSurf can build a UI on top.

## Part 1 — `add_plugin`, `list_plugins`, `remove_plugin`

### API (Python, exposed via SWIG)

```python
import tttrlib

# Install and load in one call
result = tttrlib.add_plugin("/home/user/downloads/tttrlib_cool.dylib")
# result == {
#     "name": "cool",
#     "status": "Loaded",        # or "Failed", "Quarantined"
#     "version": "1.0.0",
#     "description": "A cool FLIM fitter",
#     "capabilities": ["decay_fit"],
#     "nomenclature": "verified",
#     "message": ""
# }

# If the plugin failed nomenclature verification:
# result == {
#     "name": "cool",
#     "status": "Failed",
#     "message": "operation_type 'cool_flim' is not in the flrCIF dictionaries..."
# }

# List what is installed
tttrlib.list_plugins()
# [{"name": "cool", "status": "Loaded", ...}, ...]

# Remove (takes effect next session)
tttrlib.remove_plugin("cool")
```

### Behaviour

**`add_plugin(path)`:**

1. Validate the file exists and matches the `tttrlib_<name>.{so,dylib,dll}`
   naming convention.
2. Copy it into the per-user plugin directory
   (`per_user_directory()`, already implemented in `PluginHost`).
3. Force a reload of that one plugin — **not** `ensure_loaded` (which is
   `call_once`), but a new `PluginHost::load_one(path)` call that loads the
   library, runs init, runs nomenclature verification (PRD-029), and
   registers it. This must be safe to call after the initial `ensure_loaded`
   has already run.
4. Return the result as a structured dict.

**`list_plugins()`:**

Returns a list of dicts — one per installed plugin — with name, status,
version, description, capabilities, and nomenclature status. This is the
same data as `registry("plugin")` but in a friendly format.

**`remove_plugin(name)`:**

1. Find `tttrlib_<name>.{ext}` in the per-user directory.
2. Delete it.
3. The plugin stays loaded in the current session (no `dlclose`); it is gone
   on the next session.
4. Returns `True` if the file was deleted, `False` if not found.

### C++ side

```cpp
namespace tttrlib {

struct PluginInstallResult {
    std::string name;
    std::string status;       // "Loaded", "Failed", "Quarantined"
    std::string version;
    std::string description;
    std::vector<std::string> capabilities;
    std::string nomenclature;  // "verified", "permissive", ""
    std::string message;       // diagnostic on failure
};

/// Copy a shared library into the per-user plugin directory and load it.
/// Safe to call after ensure_loaded(). Thread-safe.
PluginInstallResult add_plugin(const std::string& path);

/// List all installed plugins and their status.
std::vector<PluginInstallResult> list_plugins();

/// Remove a plugin from the per-user directory. Loaded in-session until
/// process exit. Returns false if not found.
bool remove_plugin(const std::string& name);

}  // namespace tttrlib
```

`PluginHost` gains a `load_one_explicit` method that is not gated by
`call_once` — it acquires the state mutex, loads one library, runs init +
verification, and commits or rolls back. The existing `load_all` path is
unchanged.

## Part 2 — `tttrlib_plugin.hpp` (C++ helper)

A header that lives alongside `tttrlib_plugin.h` and wraps the raw C structs
in RAII templates. A plugin author includes this instead of writing C.

### Decay fit plugin (the 20-line version)

```cpp
#include <tttrlib_plugin.hpp>

class MyFlimFit : public tttrlib::plugin::DecayFit<MyFlimFit> {
public:
    static constexpr const char* name = "phase_domain_flim";
    static constexpr const char* schema = R"({
        "type": "object",
        "properties": {
            "lifetime": {"type": "number", "default": 3.5, "units": "ns"}
        }
    })";

    double evaluate(const double* params,
                    const tttrlib_fit_problem_v1* problem,
                    double* model) const {
        // fill model[], return chi-square
    }

    int n_parameters(const tttrlib_fit_problem_v1* problem) const {
        return 1;
    }
};

TTTRLIB_REGISTER_DECAY_FIT(MyFlimFit)
TTTRLIB_PLUGIN_INFO("myflim", "1.0.0", "Phase-domain FLIM fitter")
```

That is the whole plugin. The header generates:

- The `tttrlib_decay_fit_v1` C struct with the right function-pointer
  trampolines.
- The `create`/`destroy` lifecycle (default-construct, store in a `void*`).
- The `tttrlib_plugin_init_v1` entry point that calls
  `host->register_decay_fit`.
- The `params_schema` from the `schema` constant.

### Burst search plugin

```cpp
#include <tttrlib_plugin.hpp>

class MySearch : public tttrlib::plugin::BurstSearch<MySearch> {
public:
    static constexpr const char* name = "dual_scale_burst";
    static constexpr const char* schema = R"({
        "type": "object",
        "properties": {
            "window": {"type": "number", "default": 500e-6, "units": "s"},
            "threshold": {"type": "integer", "default": 60}
        }
    })";

    void search(const uint64_t* macro_times,
                const int8_t* routing_channels,
                uint64_t n,
                double macro_time_resolution,
                const char* params_json,
                std::vector<int64_t>& bursts) const {
        // fill bursts with [start, stop, start, stop, ...]
    }
};

TTTRLIB_REGISTER_BURST_SEARCH(MySearch)
TTTRLIB_PLUGIN_INFO("mysearch", "1.0.0", "Dual-scale burst search")
```

### What the helper handles

| Concern | Raw C ABI | With `tttrlib_plugin.hpp` |
|---|---|---|
| Struct filling | manual | automatic from `constexpr` fields |
| `params_schema` | raw `const char*` | extracted from `schema` constant |
| `create`/`destroy` | manual `void*` management | automatic (placement new on the subclass) |
| `init` function | hand-written | generated by `TTTRLIB_REGISTER_*` macros |
| `info` struct | hand-filled | generated by `TTTRLIB_PLUGIN_INFO` |
| Exception safety | manual `try/catch` | automatic (trampoline catches, calls `set_error`) |
| `struct_size` | must set correctly | set by the template |

A plugin author who does not use C++ still has the raw C ABI — it is not
deprecated, just supplemented.

## Part 3 — template project (`plugins/template/`)

```
plugins/template/
├── CMakeLists.txt          # finds tttrlib headers, builds tttrlib_template.so
├── README.md               # "Copy this directory, rename, compile"
├── my_decay_fit.hpp        # sample decay fit (20 lines)
├── my_burst_search.hpp     # sample burst search (20 lines)
├── my_container.hpp        # sample file format plugin (30 lines)
├── template_dic.dic        # sample flrCIF extension dictionary
├── test_plugin.py          # loads the compiled plugin, verifies it works
└── .github/                # CI workflow that compiles + tests on 3 platforms
```

The CMakeLists finds tttrlib's installed headers via `find_package(tttrlib)` or
`find_path(TTTRLIB_INCLUDE tttrlib_plugin.h)`, compiles the `.hpp` sources into
`tttrlib_template.{so,dylib,dll}`, and links nothing — the plugin is
self-contained except for the tttrlib headers.

The `test_plugin.py` uses `tttrlib.add_plugin()` to load the compiled plugin,
runs a fit or a burst search through it, and asserts the result. This doubles
as the CI test for the plugin SDK.

## Part 4 — documentation

### Developer-facing: `okf/bindings/plugin-sdk.md`

A tutorial covering:

1. **The plugin model** — what a plugin is, how discovery works, the three
   capability types, the five ABI contracts (one paragraph each, not 455
   lines).
2. **Quick start with the C++ helper** — copy the template, write a decay fit
   in 20 lines, compile, `add_plugin`, test. The 80% case.
3. **Quick start with the C ABI** — for non-C++ authors (Rust, C, Fortran).
   The minimal container example from `tttrlib_plugin.h`, extracted into a
   standalone tutorial.
4. **The nomenclature verification flow** — what names are accepted, how to
   ship an extension dictionary, how to read a rejection diagnostic.
5. **Testing a plugin** — the test harness, the CI workflow.
6. **Debugging** — `TTTRLIB_PLUGINS=0`, quarantine markers, `verbose=True`,
   common failure modes and their causes.
7. **Reference** — link to `tttrlib_plugin.h` for the full ABI, and
   `tttrlib_plugin.hpp` for the helper.

### User-facing: `doc/plugins.rst`

A page in the Sphinx docs:

1. **What plugins are** (one paragraph).
2. **Installing a plugin**: `tttrlib.add_plugin("path/to/tttrlib_cool.dll")`.
3. **Checking what is installed**: `tttrlib.list_plugins()`.
4. **Removing a plugin**: `tttrlib.remove_plugin("cool")`.
5. **Where plugins live** (the per-user directory, for manual management).
6. No mention of the C ABI, struct layout, or `init` functions — that is
   developer documentation.

## Part 5 — CLI support

The `tttrlib` CLI (modules/cli/) gains:

```bash
tttrlib plugin install /path/to/tttrlib_cool.dylib
tttrlib plugin list
tttrlib plugin remove cool
```

So a user who is not in Python can manage plugins from the shell.

## Criteria

1. `tttrlib.add_plugin(path)` copies a shared library into the per-user
   plugin directory, loads it in the current session (not gated by
   `call_once`), and returns a result dict with status, capabilities, and
   nomenclature verification outcome.

2. `tttrlib.list_plugins()` returns a list of installed plugins with name,
   status, version, and capabilities — in a friendly format, not raw JSON.

3. `tttrlib.remove_plugin(name)` deletes the plugin file from the per-user
   directory. Returns `False` if not found. The plugin stays loaded in the
   current session.

4. `tttrlib_plugin.hpp` exists and provides `DecayFit<T>`, `BurstSearch<T>`,
   and `Container<T>` CRTP templates. A decay fit plugin written with the
   helper is under 30 lines including schema and registration.

5. `TTTRLIB_REGISTER_DECAY_FIT`, `TTTRLIB_REGISTER_BURST_SEARCH`, and
   `TTTRLIB_PLUGIN_INFO` macros generate the C struct, the init function, and
   the info struct from the subclass's `constexpr` fields.

6. The trampoline in the C++ helper catches exceptions and calls
   `host->set_error` — a plugin author cannot accidentally throw through the
   C boundary.

7. `plugins/template/` contains a compilable project with CMake, a sample
   decay fit, a sample burst search, a sample container, a sample extension
   dictionary, and a test script. Copying, renaming, and compiling produces
   a working plugin.

8. The template's `test_plugin.py` uses `add_plugin` to load the compiled
   plugin and exercises a fit and a burst search end-to-end. This runs in CI
   on Linux, macOS, and Windows.

9. `okf/bindings/plugin-sdk.md` documents the full plugin development flow:
   the model, the C++ helper quick start, the C ABI quick start, nomenclature
   verification, testing, debugging, and reference links.

10. `doc/plugins.rst` is a user-facing page covering install, list, remove,
    and where plugins live — with no mention of the C ABI.

11. `tttrlib plugin install/list/remove` CLI commands work from the shell.

12. The existing raw C ABI (`tttrlib_plugin.h`) is unchanged. A plugin
    written today against the raw ABI still loads, now also passing
    nomenclature verification (PRD-029) if its names are canonical.
