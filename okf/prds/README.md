# PRDs

Product Requirements Documents for tttrlib. One `.md` per initiative,
`PRD-<NNN>-<slug>.md`. Each file's header block shows its **number** and
**status**.

## Status lifecycle

| Status | Meaning |
|---|---|
| ⚪ `Draft` | Being written; not yet agreed. |
| 🔵 `Proposed` | Complete, awaiting review/decision. |
| 🟣 `Accepted` | Approved; ready to implement. |
| 🟡 `In Progress` | Implementation started. |
| 🟢 `Done` | Shipped and verified. |
| ⚫ `Deferred` | Valid but parked. |
| 🚫 `Superseded` | Overtaken by events; do not implement. Kept for the record. |

## Index

| PRD | Title | Status |
|---|---|---|
| [001](PRD-001-cross-language-test-parity.md) | Cross-language test parity (Python ⇄ R ⇄ Java) | 🟢 Done |
| [002](PRD-002-java-nd-output-array-marshalling.md) | Java N-dimensional / generic output-array marshalling | 🟢 Done |
| [003](PRD-003-docs-deploy-via-rattler.md) | Documentation deploy via rattler | 🚫 Superseded |
| [004](PRD-004-single-frame-flim-ptu-clsm.md) | Single-frame FLIM PTU CLSM reconstruction | 🟢 Done |
| [005](PRD-005-photon-simulator.md) | Photon simulator (+ [RNG benchmark](PRD-005-rng-benchmark.md)) | ⚪ Draft |
| [006](PRD-006-tttr-file-roundtrip-io.md) | TTTR file round-trip I/O for all supported containers | 🟢 Done |
| [007](PRD-007-burstnet-simulator-integration.md) | BurstNet integration for the photon simulator | 🟡 In Progress |
| [008](PRD-008-simulator-openmm-api.md) | Simulator OpenMM-style API, manual & use-case coverage | 🟡 In Progress |
| [009](PRD-009-sim-driven-feature-docs.md) | Documenting every tttrlib feature by simulated example | 🔵 Proposed |
| [010](PRD-010-neural-net-and-surrogate-models.md) | Reusable neural network + surrogate models, AD gradients | 🟢 Done |
| [011](PRD-011-photon-hmm.md) | Photon-by-photon HMM: one class, three inference paths | 🟢 Done |
| [012](PRD-012-flimlabs-brighteyes-readers.md) | FLIM LABS and BrightEyes-TTM native readers | 🟢 Done |
| [013](PRD-013-gaussian-emission-hmm.md) | Gaussian emissions: the binned-trace HMM on the photon HMM's core | ⚪ Draft |
| [014](PRD-014-example-gallery-taxonomy.md) | Example gallery: one taxonomy, nothing invisible | 🔵 Proposed |
| [015](PRD-015-conformance-suite-language-parity.md) | A conformance suite: one case list, four languages | 🟢 Done |
| [016](PRD-016-javascript-node-api-bindings.md) | JavaScript bindings via SWIG Node-API | 🟢 Complete |
| [017](PRD-017-burst-web-ui.md) | Node.js burst-analysis web UI, driven by the registry | 🔵 Proposed |
| [018](PRD-018-abi-stability.md) | A stable ABI within a minor series | ⚪ Draft |
| [019](PRD-019-data-groups.md) | Data groups: a store is a tree, in memory and in the file | 🟢 Done |
| [020](PRD-020-pto-streaming-and-targeted-reads.md) | PTO: streaming and targeted reads | 🟢 Done |
| [021](PRD-021-record-decoding-streams-and-the-set-sidecar.md) | Decoding a buffer, reading a stream, and the whole `.set` sidecar | 🟢 Done |
| [022](PRD-022-column-metadata.md) | A column is described, not just named | 🟢 Done |
| [023](PRD-023-one-table-access-vocabulary.md) | One vocabulary for a table in a file, whatever the file is | 🟢 Done |
| [024](PRD-024-one-way-to-open-a-file.md) | What is this file, and open it as that | 🔵 Proposed |
| [025](PRD-025-executable-pto-containers.md) | A container you can read | 🔵 Proposed |
| [026](PRD-026-mfd-sim-to-ndx-pto-pipeline.md) | Simulated MFD burst pipeline to an ndx-conformant `.pto` | 🟢 Done |
| [027](PRD-027-modular-algorithm-registry.md) | Modular algorithm registry: compile, register, provenance | 🔵 Proposed |
| [028](PRD-028-spectroscopy-data-standard.md) | A data standard for decay curves, FCS, PDA, and PCH in `.dstore` / `.pto`-mmfdb | ⚪ Draft |
| [029](PRD-029-drop-in-algorithm-verification.md) | Drop-in algorithms: auto-register, verify against nomenclature, provenance-ready | ⚪ Draft |
| [030](PRD-030-burst-plugin-migration.md) | Migrate chiSurf burst plugins to `tttr` CLI calls | ⚪ Draft |
| [031](PRD-031-plugin-sdk-ease-of-use.md) | Plugin SDK: easy install, clear interfaces, templates, and docs | ⚪ Draft |
| [032](PRD-032-migrate-algos-to-registry.md) | Migrate existing algorithms onto register_algorithm | 🟡 In Progress |
| [033](PRD-033-streaming-correlator.md) | Streaming correlator agrees with the batch correlator | 🟢 Done |
| [034](PRD-034-pto-native-tttr-sink.md) | .pto as its own TTTR sink — native photon tables + header definitions | 🔵 Proposed |
| [035](PRD-035-generic-log-domain-hmm-lattice.md) | Generic log-domain HMM lattice (forward / backward+xi / Viterbi) over a caller-supplied frame-probability matrix | 🟢 Done |
| [036](PRD-036-2d-flc-photon-kernels.md) | 2D-FLC photon kernels — the fluorescence-decay correlation (2D-FDC) pass | 🟢 Done |
| [037](PRD-037-kernels-to-finish-chisurfs-numba-retirement.md) | Everything tttrlib needs so ChiSurf can drop numba entirely | 🔵 Proposed |
| [038](PRD-038-general-pattern-fit-and-maxent-consolidation.md) | General N-pattern fit (NNLS/Tikhonov/MaxEnt) and one MaxEnt engine instead of two | 🟢 Done |
| [039](PRD-039-historic-maxent-target-chisq.md) | Historic MaxEnt: opt-in joint chi²+nu optimization (auto-nu via target chi-square) | 🟢 Done |

**019** is green: criterion 21 (the same assertions from Python, R, Java and
JavaScript in the PRD-015 conformance suite) was the last one open, and it
landed once R and Java wrapped `DataStore` and the eleven `datastore.*` cases
went in. There is also a third piece the PRD did not ask for: a native
`.dstore` file, because the PRD's premise that HDF5 is the only way to
serialise a store is what made it slow. See `doc/saving-tables.rst`.

**003** is superseded, not done: the 2026-08-13 CI cost rework deleted the
rattler docs job instead of wiring its artifact into the deploy — the
measured reason (the library compile for autodoc is the expensive part, not
the Sphinx render) is in the PRD's resolution note and where the job used to
sit in `ci.yml`. Its underlying goal — one doc build path, not two — is true
by construction now.

**036** is green: the ChiSurf half landed with chisurf's numba removal
(`f1290e84b` — `flc_2d/core.py` delegates to `fdc_*`, parity fixture green).
Beyond the PRD's own scope it was later proven against the original author's
MATLAB in Octave (log-axis tick quantization fixed and pinned by a
MATLAB-recorded fixture) and benchmarked end to end — dynamics resolution
from simulated experiments, microsecond recovery on an immobilized single
FRET molecule, and the same with freely diffusing molecules at
τ_diff = 2 ms. See the PRD's closing sections.

**012** is green with one criterion **waived, not met** (2026-08-10): no FLIM
LABS sample file is published anywhere and none is expected, so its `STT1`
reader is spec-conformant but has never seen a file the instrument wrote.
Everything else in it is implemented and tested. If a real file ever turns up,
verify before trusting — see the PRD's *Implementation status*.
