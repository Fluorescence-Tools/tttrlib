# A minimal TTTR file viewer, in a browser

Open a PTU (or HT3, SPC, Photon-HDF5, …) file and see its header, its time trace
and its micro-time decay — with no Python anywhere. It exists to be the smallest
honest consumer of the tttrlib JavaScript binding: enough to prove the binding
works end to end on real data, small enough to read in one sitting.

```
cmake -S . -B build -DBUILD_JAVASCRIPT_INTERFACE=ON     # from the repository root
cmake --build build -j
node examples/js/ptu-webapp/server.mjs
```

Then open <http://127.0.0.1:8030>.

The file list comes from `tttr-data/` (or `$TTTRLIB_DATA`). If it is empty, run
`python test/download_test_data.py` to fetch the reference files.

## What it does, and where

| | |
|---|---|
| `server.mjs` | HTTP, the file list, path containment, and a small worker pool |
| `worker.mjs` | **every** tttrlib call — reading, binning, histogramming |
| `web/` | the page: plain ES modules and `<canvas>`, no framework, no build step |

Two decisions are worth stating, because everything built on this binding will
face them and both are load-bearing rather than stylistic.

**All tttrlib calls run on a worker thread.** `new TTTR(path)` reads and decodes
the whole file before it returns; for a 200 MB PTU that is seconds. On Node's
main thread that blocks the event loop and the server answers nothing at all
during a read — including the request that would report progress. This is the
operational risk PRD-016 names for a synchronous binding, and a worker is its
mitigation.

**Photon arrays never leave the worker.** Binning and histogramming happen in
C++; what crosses the thread boundary, and then the wire, is a few thousand
points. A time trace built by shipping a hundred million macro times to a browser
and reducing them in JavaScript would be slower, would round the 64-bit times to
doubles, and would produce a different answer from every other tttrlib tool.

## Scope

This is a *viewer*. Burst analysis, correlation, fitting and the rest of the
tttrlib surface are all reachable from the same binding — see PRD-017 for the
burst web UI this is the groundwork for — but they are not here.

## Not for a network

The server binds to `127.0.0.1` and reads only from the configured data root,
with containment enforced server-side (`path.resolve` plus a `path.relative`
escape check, symlinks not followed). It has no authentication, no rate limiting
and no CSRF protection, and it is not written for exposure to a network.
