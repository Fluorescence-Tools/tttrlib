# PRD-013 — Gaussian emissions: the binned-trace HMM on the photon HMM's core

> **PRD #:** 013 · **Status:** Draft · **Created:** 2026-08-04 · **Owner:** tpeulen
> **Related:** PRD-011 (photon-by-photon HMM — supplies the inference core this reuses)
> **Paired with:** ChiSurf **PRD-77**, `okf/prds/prd-77.md`. **The two must land together**;
> neither is useful alone. See *Sequencing* below.

## Summary

PRD-011 built one HMM class with three inference paths — EM, Viterbi, FFBS — over a
**categorical emission on detection streams**: one observation is one photon. That is the
right model for confocal single-molecule data and it is done.

A second model is used just as often on the same instruments and is **not** that: a
**binned trace**, where one observation is a time bin carrying a continuous value (an
intensity, a FRET efficiency), and the emission is Gaussian. ebFRET is this. ChiSurf
carries its own implementation of it — a `GaussianHMM` written to drop `hmmlearn` — and
that implementation is now the only HMM in ChiSurf that does not delegate here.

This PRD adds a **Gaussian emission** to the existing core rather than a second HMM.

## Why this is a likelihood and not an HMM

PRD-011 records the property that makes this cheap, and it is the reason to do it here
rather than anywhere else:

> `forward_burst`, the backward pass, Viterbi and FFBS touch emission through exactly one
> [point] … The shared core is the same code for both — no emission policy, no virtual
> dispatch.

So the scaffolding — scaling, log-domain accumulation, the interval propagator, the
constraint and prior machinery, FFBS — is already emission-agnostic and already tested.
What is missing is one `log p(observation | state)` and the M-step that goes with it.

**This must be verified before the estimate is trusted.** If the stream/micro-bin alphabet
turns out to be baked into the forward pass rather than reached through that one point,
"add a likelihood" becomes "add an HMM" and this PRD is a different size. That check is
G0.

## Goals

* **G0 — confirm the seam.** Establish that the emission is genuinely reached through one
  point for all three inference paths. If not, stop and re-scope.
* **G1 — Gaussian emission.** Diagonal and full covariance, per state. Log-domain
  density; the M-step is the standard weighted mean/covariance from the posterior.
* **G2 — observations are bins, not photons.** A trace is a sequence of real-valued
  observations with an implicit uniform spacing, so the interval propagator collapses to a
  single one-step matrix. The API must not require a photon time axis to express that.
* **G3 — multivariate.** ebFRET-style traces are often two-channel (donor, acceptor);
  the emission is a vector per bin.
* **G4 — the same three inference paths.** EM, Viterbi and FFBS, reached the same way as
  the categorical model, so a caller switching emission changes one argument.
* **G5 — priors and constraints reused,** not reimplemented: the existing
  `HmmConstraints`/`HmmRestraints` machinery must apply to a Gaussian model.
* **G6 — a sampler,** matching `simulate_bursts`: draw traces from a model, for tests and
  for training data.

## Non-goals

* Variational Bayes / automatic state-number selection. ChiSurf's `burst_ebfret` has a
  VBEM path; whether that follows is a later question and is **not** required for PRD-77.
* Replacing the categorical model, or unifying the two behind one class name.

## Acceptance

* Recovers a known two-state Gaussian model from simulated traces — means, covariances and
  transition matrix — over a range of separations, including one where the states overlap
  enough that the answer is genuinely uncertain, asserted as uncertain rather than as
  wrong.
* Agrees with ChiSurf's `GaussianHMM` on the **same input and the same initial model** to
  within optimiser tolerance. Not a substitute for the ground-truth test above: two
  implementations agreeing is not evidence when they share an assumption, and this project
  has been bitten by exactly that twice.
* **Benchmarked against it, honestly.** ChiSurf's implementation is 1.1–18× faster per
  E-step than the `hmmlearn` it replaced, so it is not a straw man. If this is not faster,
  PRD-77 says so and does not migrate on tidiness alone.

## Sequencing — why the two PRDs are simultaneous

ChiSurf PRD-77 removes an implementation that currently works. It cannot merge before this
one ships, or ChiSurf loses a capability. This one has no consumer until PRD-77 adopts it,
so shipping it alone leaves untested surface.

They land together: implement here, adopt there, and keep ChiSurf's implementation until
the benchmark and the ground-truth recovery both pass in this repository.
