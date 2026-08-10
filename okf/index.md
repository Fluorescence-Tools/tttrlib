---
okf_version: "0.2"
---

# tttrlib knowledge bundle

Curated notes on tttrlib that do not belong in the source tree or the user
documentation — design decisions and their reasons, hard-won facts about the
toolchain, and honest status of work in progress.

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

# Specifications

* [Every algorithm must work on photons](specs/photon-native-algorithms.md) - **the API-shape rule for anything added here**: a standard form and a photon form, the jitter bridge for algorithms that have no event-wise formulation, and the three defects the first case shipped with anyway (kernel interpolation broadens by `t(1-t)`, truncation sets positional accuracy, normalise on the comb)
* [PTU binary decoding](specs/pto-binary-decoding.md)
* [tttr runner](specs/tttr-runner.md)

# Design

* [Folding chisurf's PDA3c into tttrlib's `pda` module](design/pda-3colour-fold-in.md) - implemented as `PdaBurstLikelihood`; the plan, what was done, and what stays in chisurf
* [Photon Simulator subsystem plan (PRD-005)](design/plan-005-photon-simulator.md) - architecture and guardrails for the additive `Sim`-prefixed simulation engine

# ImageJ / Fiji plugin

* [Fiji plugin — where to continue](handover/fiji-plugin-handover.md) - earlier handover note, kept verbatim; predates this bundle

# History

* [log.md](log.md) - what changed, newest first
