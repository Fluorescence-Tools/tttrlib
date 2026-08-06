---
type: Application
title: PTU web viewer
description: A minimal browser viewer for TTTR files, and the reference consumer of the JavaScript binding.
resource: /examples/js/ptu-webapp
tags: [javascript, nodejs, webapp, example, prd-017]
status: stable
generated: { by: "claude-code/claude-opus-5", at: 2026-08-06T07:20:00Z }
sources:
  - id: prd-017
    resource: PRDs/PRD-017-burst-web-ui.md
    title: PRD-017 — A Node.js burst-analysis web UI, driven by the registry
    author: human:tpeulen
---

# What it is

Open a PTU (or HT3, SPC, Photon-HDF5, …) and see its header, time trace and
micro-time decay in a browser, with no Python anywhere. One file of server, one
page of front end, no framework and no build step.

```bash
node examples/js/ptu-webapp/server.mjs      # http://127.0.0.1:8030
```

It exists to be the smallest honest consumer of the binding: enough to prove it
works end to end on real data, small enough to read in one sitting. It is also
the groundwork for the burst web UI.[^prd-017]

# Two decisions worth inheriting

Both are load-bearing, not stylistic, and anything built on this binding will
face them.

**Every tttrlib call runs on a worker thread.** `new TTTR(path)` reads and
decodes the whole file before returning; for a 200 MB PTU that is seconds. On
Node's main thread that blocks the event loop and the server answers nothing at
all during a read — including the request that would report progress.

**Photon arrays never leave the worker.** Binning and histogramming happen in
C++; a few thousand points cross the thread boundary and then the wire. Doing it
the other way would be slower, would round the 64-bit macro times to doubles, and
would produce a different answer from every other tttrlib tool.

# Registry-driven, not hard-coded

Container names come from `registry("file_container")` rather than a table in the
app, so a reader added to tttrlib shows up with its proper label and no code
change. That is the property PRD-017 cares most about demonstrating.

# Verified

Read end to end, against the built addon:

| File | Result |
|---|---|
| `pq/ptu/pq_ptu_hh_t3.ptu` | PicoQuant PTU · 3,506,476 events · 2 channels · 123 header tags |
| `bh/bh_spc132.spc` | Becker & Hickl SPC-130 · 183,657 events · 4 channels · decay peak 676 |
| `imaging/pq/ht3/pq_ht3_clsm.ht3` | PicoQuant HT3 · 15,604,430 events · 5 channels |

Every event is accounted for in the time trace, and the decay sums to the event
count. The SPC decay peak of 676 is the same `REF_HIST_PEAK_VAL` the
cross-language reference test asserts, so the application and the parity suite
agree on the same file through independent paths.

Path traversal is rejected: `?path=../../etc/passwd` returns
`{"error":"path escapes the data root"}`.

# Not for a network

Binds to `127.0.0.1`, reads only from one configured data root with containment
enforced server-side (`path.resolve` plus a `path.relative` escape check,
symlinks not followed). No authentication, no rate limiting, no CSRF protection.

# Scope

A viewer. Burst analysis, correlation and fitting are all reachable from the same
binding but are not here — that is PRD-017's job.

[^prd-017]: PRD-017 — A Node.js burst-analysis web UI, driven by the registry
