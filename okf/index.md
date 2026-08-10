---
okf_version: "0.2"
---

# tttrlib knowledge bundle

Curated notes on tttrlib that do not belong in the source tree or the user
documentation — design decisions and their reasons, hard-won facts about the
toolchain, and honest status of work in progress.

# Agent message board

* [agent-board.md](agent-board.md) - **read this before starting work**. Shared coordination channel for agents across tttrlib and chisurf. Claim work, post blockers, hand off. The same file is symlinked at `chisurf/okf/agent-board.md` so both projects see one board.

# Product requirements

* [PRD index](prds/README.md) - one document per initiative, with a status lifecycle (Draft → Done). Covers cross-language parity, the simulator, HMM, PTO containers, the table-access vocabulary, ABI stability and more.

# JavaScript binding (PRD-016)

* [tttrlib for JavaScript (Node.js)](bindings/javascript-binding.md) - the binding itself: scope, layout, conventions, how to build and what has been verified
* [What is still open](bindings/open-items.md) - the honest gap list, ordered by what would bite first; **read this before continuing the work**
* [SWIG Node-API backend traps](bindings/swig-node-api-traps.md) - five behaviours that fail silently or misreport where the error is; check before debugging `ext/js/*.i`
* [shared_ptr for the Node-API backend](bindings/shared-ptr-design.md) - why `ext/js/js_shared_ptr.i` is not a transcription of SWIG's, and the lifetime contract that follows
* [PTU web viewer](bindings/ptu-webapp.md) - the reference application, and the two decisions anything built on this binding will face

# Testing

* [Test workload tiers and the fast lane](testing/test-workload-tiers.md) - what the suite actually costs (29 tests are 85% of it), the `--lane` tiers and the audit that keeps them honest, and the cold-cache trap that makes a first measurement wrong by 4x
* [How to run benchmarks and document performance changes](testing/benchmarking.md) - the competitor harness, what to update after a perf change, and the SWIG vector-marshalling trap that cost 400 ms. **Read before closing a perf task.**

# Specifications

* [Every algorithm must work on photons](specs/photon-native-algorithms.md) - **the API-shape rule for anything added here**: a standard form and a photon form, the jitter bridge for algorithms that have no event-wise formulation, and the three defects the first case shipped with anyway (kernel interpolation broadens by `t(1-t)`, truncation sets positional accuracy, normalise on the comb)
* [PTU binary decoding](specs/pto-binary-decoding.md)
* [tttr runner](specs/tttr-runner.md)

# Design

* [Folding chisurf's PDA3c into tttrlib's `pda` module](design/pda-3colour-fold-in.md) - implemented as `PdaBurstLikelihood`; the plan, what was done, and what stays in chisurf
* [Photon Simulator subsystem plan (PRD-005)](design/plan-005-photon-simulator.md) - architecture and guardrails for the additive `Sim`-prefixed simulation engine

# ImageJ / Fiji plugin

* [Fiji plugin — where to continue](handover/fiji-plugin-handover.md) - earlier handover note, kept verbatim; predates this bundle

# Handovers

* [FLIM performance optimization — DONE](handover/flim-performance-opt.md) - the 3.3× `fit_map` speedup, what was changed and why, the build-safety warning, and suggested next steps

# History

* [log.md](log.md) - what changed, newest first

# Vocabulary

* [mmfdb is the naming repository](specs/mmfdb-is-the-vocabulary.md) — **normative, and it applies to every repository**: one vocabulary, no copies, and a term that is missing is added to mmfdb rather than worked around. Records the eighteen-term drift that made the rule necessary, and why every test passed while it was happening.
