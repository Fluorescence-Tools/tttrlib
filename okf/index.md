---
okf_version: "0.2"
---

# tttrlib knowledge bundle

Curated notes on tttrlib that do not belong in the source tree or the user
documentation — design decisions and their reasons, hard-won facts about the
toolchain, and honest status of work in progress.

# JavaScript binding (PRD-016)

* [tttrlib for JavaScript (Node.js)](javascript-binding.md) - the binding itself: scope, layout, conventions, how to build and what has been verified
* [What is still open](open-items.md) - the honest gap list, ordered by what would bite first; **read this before continuing the work**
* [SWIG Node-API backend traps](swig-node-api-traps.md) - five behaviours that fail silently or misreport where the error is; check before debugging `ext/js/*.i`
* [shared_ptr for the Node-API backend](shared-ptr-design.md) - why `ext/js/js_shared_ptr.i` is not a transcription of SWIG's, and the lifetime contract that follows
* [PTU web viewer](ptu-webapp.md) - the reference application, and the two decisions anything built on this binding will face

# Testing

* [Test workload tiers and the fast lane](test-workload-tiers.md) - what the suite actually costs (29 tests are 85% of it), the `--lane` tiers and the audit that keeps them honest, and the cold-cache trap that makes a first measurement wrong by 4x

# ImageJ / Fiji plugin

* [Fiji plugin — where to continue](fiji-plugin-handover.md) - earlier handover note, kept verbatim; predates this bundle

# History

* [log.md](log.md) - what changed, newest first
