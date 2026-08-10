# PRD-032 — Migrate existing algorithms onto register_algorithm

> **PRD #:** 032 · **Status:** 🟡 In Progress · **Created:** 2026-08-09 · **Updated:** 2026-08-10 · **Owner:** tpeulen
>
> **Depends on:** [PRD-027](PRD-027-modular-algorithm-registry.md) — the
> `register_operation` ABI is now live (2026-08-09). This PRD is the
> follow-through: every existing algorithm must use it.
>
> **Progress (2026-08-10):**
>
> - **Criterion 3 — done.** `TTTR::burst_search` dispatches through
>   `find_burst_search(name)` rather than a chain of `if (mode == "...")`. See
>   PRD-027 criterion 6.
> - **Criterion 4 — done.** A burst search a plugin contributes is now callable
>   through `TTTR::burst_search(name, ...)`, the same door every built-in uses.
>   This was not merely unsupported before, it was **silently wrong**: the
>   `if/else` chain fell through to the sliding window for any name it did not
>   recognise, so calling `burst_search` with a plugin's name returned
>   sliding-window bursts while the registry listed the plugin's search. Covered
>   by `test_a_plugin_burst_search_is_reachable_through_burst_search`.
> - **Steps 4-6 of the sequencing (FCS, HMM, PDA) — done** as part of PRD-027
>   Parts 1/3: all three register and appear in the registry.
> - **Criterion 5 verified**: the whole `registry_json()` blob is byte-identical
>   to a capture taken before this work (0 differences), and the fast lane is
>   green at 2363 passed.
>
> **Criterion 1 obstacle 1 (shape) — removed 2026-08-10.** `AlgorithmDescriptor`
> gained `dispatch_name` (emitted as `method`, and *omitted* when empty, because
> its absence is what routes a plugin's search through the by-name path in every
> consumer that reads this) and `provider`; every entry now also carries
> `params_schema` alongside `settings_schema`. A category can therefore migrate
> onto registrations without its entries changing shape. Verified against a
> capture of the whole registry taken before the work: 16 differences, **all
> additions, none removed or changed**.
>
> The mechanism also moved to its own module, `modules/algorithm`, depending on
> nothing but a JSON writer. It had to: `registry` already depends on `burst`,
> so an algorithm module that wanted to register itself could not depend on
> `registry` without closing a cycle. Nothing could have migrated while the
> mechanism lived above the algorithms.
>
> **Criterion 1 obstacle 2 (ownership) still stands** for `kOperationRegistry`:
>    retiring it faithfully means each
>    operation's descriptor moving next to the code that performs it — that is
>    the point of the exercise, and the only thing that stops the entry drifting
>    from the implementation again. But several of those operations
>    (`burst_selection`, `mle_green`, `bva`, `kde_cde`) have no single C++ home
>    yet: they are the pipeline steps PRD-026 is still specifying, and some are
>    Python prototypes. Moving the eight entries into eight descriptor blocks in
>    the same registry module would satisfy the letter of "no `const char*` JSON
>    literals remain" and none of its purpose, while adding regression risk to a
>    consumer-visible blob. **Do this with PRD-026, not before it.**
>
> **`kBurstSearchRegistry` is migrated and deleted (2026-08-10).** Its seven
> searches now declare description and dispatch function in a single
> `register_burst_search(AlgorithmDescriptor, BurstSearchFn)` call in
> `BurstSearchRegistry.cpp` — one registration, one place, which is what "one
> registration path" was supposed to mean. The overload registers the descriptor
> first and the dispatch entry second, so a search that is described but not
> runnable is unrepresentable rather than merely discouraged.
>
> `BurstSearchDispatch.cpp` is now the mechanism only: it names no burst search
> and includes none of their headers.
>
> **Criterion 5 verified again, the same way**: against a capture of the whole
> registry taken before the change — **0 entries removed, 0 changed, 63 added**,
> the 63 being exactly the 9 descriptor fields (`capability`, `operation_type`,
> `settings_schema`, `row_grain`, `inputs`, `outputs`, `references`, `provider`,
> `can_replay`) × 7 searches. Behaviour re-checked directly: `bocpd` still
> dispatches to its own entry point, `coincident` still raises and names
> `burst_search_coincident`, and an unknown name still falls back to the sliding
> window. 2463 fast-lane tests pass and all four SWIG wrappers generate.
>
> Two things only running it revealed, both worth keeping:
>
> 1. **`algorithms_json("burst_search")` came back empty.** It primes the
>    *algorithm* module's built-ins, which know nothing about burst searches, so
>    the category silently lost all seven entries rather than failing.
>    `burst_search_algorithms_json()` now calls `register_builtin_burst_searches()`
>    itself. A category that empties quietly is the failure mode to watch for in
>    the remaining migrations.
> 2. **`PluginHost::burst_searches_json()` returns a brace-less fragment**, not a
>    document — it was written to be spliced into the middle of a literal. Parsing
>    it as JSON needs it braced first. The splice it replaced located the
>    insertion point with `find_last_of('}')`, a parser written in string search;
>    a built-in whose description ended in a brace would have moved it.
>
> **Two bugs found while doing this, both the same shape as criterion 4's:**
> `bocpd` and `coincident` were advertised in the registry with a `method`, and
> `burst_search("bocpd", ...)` returned **sliding-window** bursts — the
> unrecognised-name fallback, silently, from the wrong algorithm. `bocpd` is now
> dispatched (`T` is its bin width). `coincident` cannot be: its essential input
> is the channel grouping and `(L, m, T)` has nowhere to put it, so it now
> raises and names `burst_search_coincident` instead of answering with something
> else. A name the registry lists is not an unknown name, so failing for it does
> not touch the fallback callers rely on.

## Summary

The `register_operation` ABI (PRD-027) lets a plugin `.dll` register a new
algorithm type without recompiling tttrlib. But the built-in algorithms —
burst searches, decay fits, FCS correlators, HMMs, PDAs — still use
hand-authored JSON literals and hardcoded dispatch switches. This PRD
migrates them onto the same `register_algorithm` path so there is **one**
registration mechanism, not two.

## What migrates

| Algorithm family | Current dispatch | Current registry | After this PRD |
|---|---|---|---|
| Burst searches (7 methods) | ~~`if (method == "name")` on `TTTR`~~ | ~~`kBurstSearchRegistry` literal~~ | ✅ **done** — `register_burst_search(descriptor, fn)` + table lookup |
| Decay fits (5 models) | `set_decay_fit_registrar` factory | `kFitRegistry` literal | `register_algorithm` + factory from descriptor |
| FCS correlators (3 methods) | direct C++ construction | none | `register_algorithm` + factory |
| HMM inference | direct C++ construction | none | `register_algorithm` + factory |
| PDA likelihood | direct C++ construction | none | `register_algorithm` + factory |
| Pipeline operations (8 ops) | `kOperationRegistry` literal | `OperationRegistry.cpp` | `register_algorithm` — literals retired |

## Rules

1. **No hand-authored JSON literals.** `kFitRegistry` and `kOperationRegistry`
   are deleted (`kBurstSearchRegistry` already is, 2026-08-10). The registry is built from live
   `register_algorithm` calls.

2. **No hardcoded dispatch switches.** `TTTR::burst_search(name, ...)` looks
   up the name in the capability registry and calls the registered function
   pointer. Adding a new burst search method does not require editing `TTTR`.

3. **One registration call per algorithm.** Whether compiled into the core
   library or loaded as a plugin `.dll`, every algorithm calls
   `register_algorithm(AlgorithmDescriptor{...})`.

4. **Backward compatible.** The public API surface from 0.26
   (`TTTR::burst_search`, `DecayFitModel` subclasses, `Correlator`,
   `registry_json()`) works without modification. Old call sites keep calling.

## Sequencing

1. **Pipeline operations** (easiest — already have descriptors in
   `OperationRegistry.cpp`). Convert each static literal to a
   `register_algorithm` call at module init. Delete `kOperationRegistry`.

2. **Burst searches.** Replace the `if/else` dispatch on `TTTR` with a
   table lookup on the `burst_search` capability registry. Each of the 6
   methods registers a descriptor.

3. **Decay fits.** The explicit `register_decay_fit_models_*` calls build
   descriptors and call `register_algorithm`. `kFitRegistry` is generated
   from registrations.

4. **FCS.** `Correlator` gains a factory + descriptor. Three methods register.

5. **HMM.** `HMM` gains a factory + descriptor.

6. **PDA.** `Pda` gains a factory + descriptor.

The library works at every intermediate step — each family migrates
independently.

## Criteria

1. `kBurstSearchRegistry`, `kFitRegistry`, and `kOperationRegistry` are
   deleted from the source. No `const char*` JSON literals remain.

2. `registry_json()` returns the same categories as before, populated from
   live registrations.

3. `TTTR::burst_search("sliding_window", ...)` dispatches via table lookup,
   not `if/else`.

4. A new burst search method added as a plugin `.dll` (not compiled into
   tttrlib) appears in `registry("burst_search")` and is callable via
   `TTTR::burst_search("plugin_method", ...)`.

5. All existing tests pass without modification.

6. The C++ burst pipeline (PRD-026) dispatches companion analyses
   exclusively through the registry — no hardcoded operation list.

## Non-goals

- Removing the `DecayFitModel`, `HMM`, `Pda`, `Correlator` class hierarchies.
- Rewriting algorithm internals.
- A new plugin ABI version (`_v1` stands).
- Mandating shared-library splitting for the default build.
