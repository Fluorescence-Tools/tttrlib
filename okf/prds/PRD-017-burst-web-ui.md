# PRD-017 — A Node.js burst-analysis web UI, driven by the registry

> **PRD #:** 017 · **Status:** Proposed · **Created:** 2026-08-05 · **Owner:** tpeulen
> **Groundwork done:** PRD-016 is implemented, and `examples/js/ptu-webapp/` is a
> working reference application on top of it — worker-thread isolation, the
> containment-checked data root, registry-driven container naming and C++-side
> binning are all demonstrated there. This PRD's M1 starts from that shape.
> **Related:** PRD-016 (JavaScript bindings — hard dependency), PRD-015 (conformance), the `burst_search` registry category
> **Reference implementation being mirrored:** chisurf's burst pipeline (`../chisurf`), specifically `chisurf/plugins/burst/burst_selection/api/selection.py`, `chisurf/plugins/burst/burst_analysis/api/workflow.py` and the `.bur` writers in `chisurf/core/fio/fluorescence/burst.py`

## Summary

A single-page web application, served by a Node.js process that calls tttrlib
through the PRD-016 bindings, which runs the burst-analysis pipeline chisurf
runs — read → pre-filter → search → merge → per-burst features → `.bur` export —
and renders it in a browser. Its parameter forms are generated from
`tttrlib.registry()`, so the UI has **no hard-coded knowledge of any burst-search
algorithm**.

## Problem / motivation

Two problems, one application.

**tttrlib has no shop window for the JS binding.** A binding with no non-trivial
consumer accumulates unexercised typemaps. Burst analysis is the right consumer:
it touches file I/O, per-photon arrays in both directions, 64-bit macro times,
the registry, and a real numeric result that can be compared against another
implementation.

**Burst analysis is desktop-only today.** chisurf's pipeline is excellent and
requires a Qt install and a Python environment. A collaborator who wants to see
whether a measurement has bursts at all cannot.

And a third thing this settles: chisurf already proved that a UI should not
hard-code algorithm lists. `chisurf/core/tttrlib_registry.py` says it plainly —
before the registry existed, chisurf "hard-coded those lists in several places
and drifted out of step whenever tttrlib gained a feature." tttrlib publishes six
burst searches (`sliding_window`, `cusum_sprt`, `kalman`, `coincident`,
`maxtree`, `bayesian_blocks`) with a JSON Schema of parameters, types, defaults,
units and ranges. A web form is a *pure function of that schema*. Demonstrating
that is the most valuable thing this app does, and it is the part that will still
be true when tttrlib adds a seventh search.

## Goals

1. Open a TTTR file server-side, pick a detector setup, run a burst search, see
   the result — time trace with bursts marked, burst count, per-burst tables and
   histograms — in a browser, with no Python anywhere.
2. **Zero hard-coded algorithm knowledge**: the algorithm list, every parameter
   widget, its default, unit and range come from `registry("burst_search")`.
   Adding a search to tttrlib adds it to the UI with no UI change. The same holds
   for the file-container list, from `registry("file_container")`.
3. Reproduce chisurf's pipeline stages and its `.bur` output format closely
   enough that a `.bur` written here opens in chisurf and agrees numerically.
4. Reproducible: the exact settings that produced a result are exportable and
   re-importable as JSON.
5. Stay a *reference application* — small enough to read in an evening, and
   exercised in CI.

## Non-goals

- Replacing chisurf, or its GUI. No fitting, no PDA, no H2MM, no MMFDB, no
  project files. Burst selection and its immediate observables only.
- Multi-user, authentication, or an internet-facing deployment. This binds to
  localhost and reads from one configured data root. Said in the README, enforced
  in code, not left to a deployment decision later.
- Uploading large files through the browser. The server reads from disk; the
  browser sends a path relative to the data root.
- Browser-side (WASM) tttrlib. All computation is server-side native; see
  PRD-016's fork decision.

## The pipeline being mirrored

From chisurf, with the tttrlib call each stage maps to:

| # | Stage | chisurf | tttrlib |
|--:|---|---|---|
| 1 | **Setup** — named detectors: routing channels + optional micro-time windows (PIE/ALEX gating) | `Setup` / `Detector` in `burst_analysis/api/workflow.py` | `TTTR.get_selection_by_channel` + micro-time ranges |
| 2 | **Read** | `tttrlib.TTTR(path)` | container from `registry("file_container")`, content-sniffed |
| 3 | **Photon pre-filters** — channel, micro-time range, Δmacro-time (`dT_max`) | `apply_photon_filters` | selections; the Δ-filter reduces the stream the search then runs on |
| 4 | **Search** — one of the registry algorithms, or count-rate threshold | `_run_burst_search`, `BurstFilterMode` | `burst_search_by_name(algorithm, **params)` |
| 5 | **Merge / gap-fill** (`max_gap`) → burst start/stop | `find_bursts` | `BurstFilter` |
| 6 | **Per-burst features** — per-detector counts, duration, mean macro time, mean micro time (ns), count rate | `summarize_bursts` | `BurstFeatureExtractor` |
| 7 | **Export** `.bur` + settings manifest | `burst.py`, `burst_manifest.py` | — (formatting is app-side) |

**One inherited quirk must be inherited, not fixed.** chisurf's `.bur` column
`Mean Macro Time (ms)` is the **midpoint of the burst's first and last photon**,
`(t_first + t_last)/2` — not the mean of the photon macro times. It is documented
at the top of `chisurf/core/fio/fluorescence/burst.py` as deliberate: the
reference format and every tool reading these files expect that value. This app
writes the same midpoint under the same column name, documents it in the same
words, and — since it is not bound by an existing format — *also* offers a
correctly-named `Mean Macro Time (true) (ms)` column, off by default. Detectors a
burst has no photons in carry `-1.0` and `0`, matching the sentinel convention.

## Proposed approach

### Layout

```
apps/burst-webui/
  server/       fastify app, routes, worker pool
  worker/       worker_thread: the entire tttrlib pipeline, one message per stage
  web/          front end: ES modules, no framework, no build step
  schema/       the settings JSON Schema (also the export format)
  test/         node:test — API tests + a headless pipeline test
```

Under `apps/`, not `examples/` — it is a Node project with a `package.json` and a
test suite, and PRD-014's gallery guard governs `examples/` only.

### Server

Fastify, three routes plus a stream:

- `GET  /api/registry` — `registry()` verbatim. The front end's only source of
  algorithm and container knowledge.
- `GET  /api/files` — the data root, listed, with the sniffed container type per
  file (from the registry's detection rules — the same content-sniffing the C++
  reader does, so the UI never guesses from an extension).
- `POST /api/analyze` — settings JSON in; job id out.
- `GET  /api/jobs/:id/events` — Server-Sent Events: stage progress, then the
  result summary. SSE rather than WebSockets because progress is one-directional
  and SSE survives a proxy without configuration.

**Every tttrlib call happens in a `worker_thread`.** A synchronous 200 MB read on
the main thread stalls the whole server — PRD-016 lists this as its main
operational risk, and this app is where the mitigation is demonstrated. One
worker per job, a small pool, and the photon arrays never cross the thread
boundary: only bursts and summaries do.

### The registry-driven form

The front end fetches `/api/registry` and, for the selected algorithm, walks its
JSON Schema:

| schema | widget |
|---|---|
| `integer` with `minimum`/`maximum` | number input with bounds |
| `number` with `unit` | number input, unit as suffix label |
| `boolean` | checkbox |
| `enum` | select |
| `default` | initial value |
| `description` | tooltip / help line |

That is the entire form layer, and it is the same trick
`chisurf.core.tttrlib_registry.entry_form_view` plays for Qt. If the UI ever
needs a `switch` on an algorithm name, something has gone wrong and should be
fixed in the registry instead.

### Views

1. **Time trace** — binned intensity per detector, bursts shaded. Binning done in
   C++ (`Histogram`), not in JavaScript; the browser receives a few thousand
   points, never a few hundred million.
2. **Burst table** — virtualised, sortable, the `.bur` columns.
3. **Histograms** — per-burst count, duration, count rate; and, when the setup
   declares two detectors, the proximity-ratio histogram, plus the 2D E/S plot
   when an ALEX micro-time gating is declared.
4. **Settings panel** — registry-generated, with export/import JSON.

Plain ES modules and `<canvas>`. No framework and no build step, so the app is
readable as source and cannot rot through a toolchain upgrade.

### Interop as the acceptance test

The strongest thing this app can assert is that it agrees with chisurf. A CI test
(or, while chisurf is not in tttrlib's CI, a documented `make interop` target):

1. same file, same settings JSON;
2. chisurf's `analyze_file` writes `a.bur`; the web UI's worker writes `b.bur`;
3. assert identical burst count, identical start/stop indices, and every shared
   column equal within `1e-9` relative.

Any disagreement is a real finding — either a binding bug or a genuine pipeline
difference — and this is the only way to find it before a user does.

## Milestones

- **M1 — headless pipeline.** `worker/pipeline.mjs`: settings JSON in, bursts +
  `.bur` out, no server, no UI. Tested with `node --test`. This is the piece that
  proves PRD-016's burst surface (its M4) and can be reviewed on its own.
- **M2 — interop.** The chisurf comparison above, green on one reference file
  with the default `sliding_window` settings. Do this *before* any UI work: a UI
  over numbers that do not match is worse than no UI.
- **M3 — server.** Fastify, the four endpoints, worker pool, SSE progress.
- **M4 — UI, registry-driven.** Settings panel generated from the schema; file
  picker; run; burst count. Deliberately ugly, deliberately complete: a new
  tttrlib search must appear with no diff to `web/`.
- **M5 — views.** Time trace with shading, burst table, histograms, E/S plot.
- **M6 — ship.** README with the localhost-only statement, a `docker run` for
  people without a toolchain, and a CI job that builds the addon, runs the
  headless pipeline test and hits the API with a smoke request.

## Risks

- **Blocked on PRD-016.** Nothing here starts before its M4 (burst surface). M1
  is written against that API and is the reason to prioritise it.
- **Interop drift.** chisurf's pipeline moves; a pinned expected output goes
  stale. Mitigation: pin the chisurf commit used for the interop test and treat a
  mismatch as a review item rather than a build break, with the pin updated
  deliberately.
- **Reference app grows into a product.** Every "can it also do H2MM" is a step
  toward maintaining a second chisurf. Mitigation: the non-goals list is in the
  README, and anything beyond burst selection is a chisurf feature request.
- **A path parameter that reads any file on the host.** Real, and the reason the
  data root is resolved and containment-checked server-side (`path.resolve` +
  prefix check, symlinks rejected) rather than trusted from the client.
- **Big files, small browser.** A 10-million-burst table cannot go over the wire.
  Mitigation: the table is paginated server-side from the start, not retrofitted;
  histograms are computed in C++ and sent as bin counts.

## Acceptance

- `npm start` in `apps/burst-webui/`, open `localhost:8030`, pick a `.spc`, run:
  bursts shaded on the time trace and a `.bur` downloadable.
- The `.bur` opens in chisurf and matches its own output on the same settings.
- `grep -rE "sliding_window|maxtree|kalman|cusum" apps/burst-webui/web/` returns
  nothing — every algorithm name reaches the UI through the registry.
- A tttrlib build with a new `burst_search` registry entry shows the new
  algorithm, with its parameters, with no change to this app.
