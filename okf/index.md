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

# Language bindings

Cross-cutting first, then the JavaScript binding (PRD-016).

* [The Python seam costs ~50 ns per element](bindings/marshalling-cost.md) - why `std::vector<double>` bindings make the wrapper set the runtime, and the granularity rule that follows: **a loop stays whole in C++**
* [tttrlib for JavaScript (Node.js)](bindings/javascript-binding.md) - the binding itself: scope, layout, conventions, how to build and what has been verified
* [What is still open](bindings/open-items.md) - the honest gap list, ordered by what would bite first; **read this before continuing the work**
* [SWIG Node-API backend traps](bindings/swig-node-api-traps.md) - five behaviours that fail silently or misreport where the error is; check before debugging `ext/js/*.i`
* [shared_ptr for the Node-API backend](bindings/shared-ptr-design.md) - why `ext/js/js_shared_ptr.i` is not a transcription of SWIG's, and the lifetime contract that follows
* [PTU web viewer](bindings/ptu-webapp.md) - the reference application, and the two decisions anything built on this binding will face

# Testing

* [Test workload tiers and the fast lane](testing/test-workload-tiers.md) - what the suite actually costs (29 tests are 85% of it), the `--lane` tiers and the audit that keeps them honest, and the cold-cache trap that makes a first measurement wrong by 4x
* [How to run benchmarks and document performance changes](testing/benchmarking.md) - the competitor harness, what to update after a perf change, and the SWIG vector-marshalling trap that cost 400 ms. **Read before closing a perf task.**
* [Pruning CI: workflow runs, and the 10 GB cache](testing/pruning-ci-history.md) - what must never be pruned (releases, tags, main), the run-pruning script, and the two findings that matter: the space goes to **five branch-scoped copies of the same 1.03 GB test download**, not to sccache's 189 tiny entries; and `actions/cache` reads `path:` from the **`env` context**, which does not contain the runner image's own variables — so a path written as `${{ env.VCPKG_INSTALLATION_ROOT }}` cached nothing at all, silently, for as long as it was there

# Specifications

* [Every algorithm must work on photons](specs/photon-native-algorithms.md) - **the API-shape rule for anything added here**: a standard form and a photon form, the jitter bridge for algorithms that have no event-wise formulation, and the three defects the first case shipped with anyway (kernel interpolation broadens by `t(1-t)`, truncation sets positional accuracy, normalise on the comb)
* [Two registers: the code, and the documentation](specs/documentation-register.md) - **normative for anything a user reads**: source comments may be ugly, published documentation may not. What "sloppy" and "detailed" mean concretely, with the failure modes that prompted the rule
* [PTU binary decoding](specs/pto-binary-decoding.md)
* [tttr runner](specs/tttr-runner.md)

# Design

* [Folding chisurf's PDA3c into tttrlib's `pda` module](design/pda-3colour-fold-in.md) - implemented as `PdaBurstLikelihood`; the plan, what was done, and what stays in chisurf
* [Photon Simulator subsystem plan (PRD-005)](design/plan-005-photon-simulator.md) - architecture and guardrails for the additive `Sim`-prefixed simulation engine
* [Sim\* integration notes and gotchas](design/sim-integration-notes.md) - what driving the simulator from an outside project actually cost: anisotropy parameters silently destroying channel routing on a non-polarized instrument, a TAC window narrower than the laser period eating the decay, and the macOS OpenMP import failure. Includes the old→new API rename table.

# ImageJ / Fiji plugin

* [Fiji plugin — where to continue](handover/fiji-plugin-handover.md) - earlier handover note, kept verbatim; predates this bundle

# Handovers

* [FLIM performance optimization — DONE](handover/flim-performance-opt.md) - the 3.3× `fit_map` speedup, what was changed and why, the build-safety warning, and suggested next steps
* [ChiSurf → tttrlib compute-core port](handover/chisurf-compute-core-port.md) - what moved into the compute core, and what was left behind
* [The Seidel / redRobin harvest](handover/seidel-redrobin-harvest.md) - what was taken from the two legacy toolchains, and what was deliberately not

# Known defects

* [BUGS.md](BUGS.md) - open defects with reproductions, newest first. **An entry is never deleted, only stubbed FIXED** — the file's own rule, and the one concurrent sessions break most often.
* [MODULE-DEBT.md](MODULE-DEBT.md) - what the module split still owes, item by item, each with the exit that closes it. `CMakeLists.txt` points here for why `tttrlibShared` and `tttrlibStatic` still compile every source a second and third time.

# History

* [log.md](log.md) - what changed, newest first

# Vocabulary

* [mmfdb is the naming repository](specs/mmfdb-is-the-vocabulary.md) — **normative, and it applies to every repository**: one vocabulary, no copies, and a term that is missing is added to mmfdb rather than worked around. Records the eighteen-term drift that made the rule necessary, and why every test passed while it was happening.
