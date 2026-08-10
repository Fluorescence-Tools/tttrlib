# `registry` — Global Algorithm and Model Registry

Central registry for decay fit models, burst algorithms, and format schemas.

## Contents

- **`Registry.h` / `Registry.cpp`**: Global algorithm registry, schema validator, and factory functions.

## Dependencies

- Depends on `util`.

## Two sources, one output (PRD-027)

The registry is assembled from two places, and is migrating from one to the
other:

- **Hand-authored literals** — `kFitRegistry`, `kOperationRegistry`. A
  `const char*` of JSON, kept in sync with the code by hand. This is what decay
  fits and pipeline operations still use.
- **Live registrations** — `register_algorithm(AlgorithmDescriptor)` in
  `AlgorithmRegistry.h`. The algorithm declares itself; the registry serves
  what registered. FCS, HMM, PDA and **burst searches** use this.

`kBurstSearchRegistry` is gone (PRD-032). Its seven searches now declare
description and dispatch function in one `register_burst_search(descriptor, fn)`
call in `BurstSearchRegistry.cpp`, so the two cannot drift — which they had:
`bocpd` and `coincident` were advertised with a `method` the dispatcher had
never heard of, and calling them ran the sliding window instead.

The literals are the reason FCS, HMM and PDA were missing entirely: not a
design decision, just three families nobody wrote a literal for. Being missing
is not cosmetic — a UI cannot enumerate what is not listed, `.pto` provenance
has no schema to validate an `operation_type` against or to replay from, and a
plugin has no name under which to offer a competing implementation.

`build()` merges the two **additively**: a live registration is added to a
category only under a name the literal does not already use. A collision is
left visible rather than resolved, because during a migration both sides are
real and picking a winner by load order turns a mistake into a behaviour.

### Adding an algorithm

Fill in an `AlgorithmDescriptor` and call `register_algorithm` from
`register_builtin_algorithms()` (or, for a plugin, through
`tttrlib_plugin_init_v1`). Two fields are not optional in practice:

- `description` — prose a user reads *instead of* the source when deciding
  whether the algorithm suits their data. A restatement of the name is checked
  for, and fails the test suite.
- `references_json` — at least one citation, so a `.pto` artifact can be traced
  to the literature. Transcribe from the algorithm's own source where it
  carries a reference list; where it does not, say so in the entry rather than
  presenting an unverified citation as if it came from the code.

Registration is explicit, never a static initialiser: a static initialiser in a
translation unit nothing references is dropped when the library is linked as a
static archive, and the algorithm then silently does not exist. That lesson is
already recorded in `DecayFitModelRegistration.h`.
