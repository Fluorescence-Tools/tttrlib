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

## Index

| PRD | Title | Status |
|---|---|---|
| [001](PRD-001-cross-language-test-parity.md) | Cross-language test parity (Python ⇄ R ⇄ Java) | 🟢 Done |
| [002](PRD-002-java-nd-output-array-marshalling.md) | Java N-dimensional / generic output-array marshalling | 🟢 Done |
| [003](PRD-003-docs-deploy-via-rattler.md) | Documentation deploy via rattler | 🟡 In Progress |
| [004](PRD-004-single-frame-flim-ptu-clsm.md) | Single-frame FLIM PTU CLSM reconstruction | 🟢 Done |
| [005](PRD-005-photon-simulator.md) | Photon simulator (+ [RNG benchmark](PRD-005-rng-benchmark.md)) | ⚪ Draft |
| [006](PRD-006-tttr-file-roundtrip-io.md) | TTTR file round-trip I/O for all supported containers | 🟢 Done |
| [007](PRD-007-burstnet-simulator-integration.md) | BurstNet integration for the photon simulator | 🟡 In Progress |
| [008](PRD-008-simulator-openmm-api.md) | Simulator OpenMM-style API, manual & use-case coverage | 🟡 In Progress |
| [009](PRD-009-sim-driven-feature-docs.md) | Documenting every tttrlib feature by simulated example | 🔵 Proposed |
| [010](PRD-010-neural-net-and-surrogate-models.md) | Reusable neural network + surrogate models, AD gradients | 🟡 In Progress |
| [011](PRD-011-photon-hmm.md) | Photon-by-photon HMM: one class, three inference paths | 🟢 Done |
| [012](PRD-012-flimlabs-brighteyes-readers.md) | FLIM LABS and BrightEyes-TTM native readers | 🟡 In Progress |
| [013](PRD-013-gaussian-emission-hmm.md) | Gaussian emissions: the binned-trace HMM on the photon HMM's core | ⚪ Draft |
| [014](PRD-014-example-gallery-taxonomy.md) | Example gallery: one taxonomy, nothing invisible | 🔵 Proposed |
| [015](PRD-015-conformance-suite-language-parity.md) | A conformance suite: one case list, four languages | 🟢 Done |
| [016](PRD-016-javascript-node-api-bindings.md) | JavaScript bindings via SWIG Node-API | 🟡 In Progress |
| [017](PRD-017-burst-web-ui.md) | Node.js burst-analysis web UI, driven by the registry | 🔵 Proposed |
| [018](PRD-018-abi-stability.md) | A stable ABI within a minor series | ⚪ Draft |
| [019](PRD-019-data-groups.md) | Data groups: a store is a tree, in memory and in the file | 🟡 In Progress |
| [020](PRD-020-pto-streaming-and-targeted-reads.md) | PTO: streaming and targeted reads | 🟢 Done |
| [021](PRD-021-record-decoding-streams-and-the-set-sidecar.md) | Decoding a buffer, reading a stream, and the whole `.set` sidecar | 🟢 Done |
| [022](PRD-022-column-metadata.md) | A column is described, not just named | 🟢 Done |
| [023](PRD-023-one-table-access-vocabulary.md) | One vocabulary for a table in a file, whatever the file is | 🟢 Done |
| [024](PRD-024-one-way-to-open-a-file.md) | What is this file, and open it as that | 🔵 Proposed |

**019** is yellow rather than green for one reason: criterion 21 asks for the
same assertions from Python, R, Java and JavaScript in the PRD-015 conformance
suite, and that suite does not exist yet -- nor do R and Java wrap `DataStore`
at all. Everything else is done, and there is a third piece the PRD did not ask
for: a native `.dstore` file, because the PRD's premise that HDF5 is the only
way to serialise a store is what made it slow. See `doc/saving-tables.rst`.

**012** is yellow rather than green for one reason: no FLIM LABS sample file is
published anywhere, so its `STT1` reader is spec-conformant but has never seen a
file the instrument wrote. Everything else in it is implemented and tested. It
turns green when a real file arrives — see the PRD's *Implementation status*.
