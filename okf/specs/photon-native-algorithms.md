---
title: Every algorithm must work on photons
type: spec
status: active
tags: [algorithms, photons, jitter, quantisation, api-shape]
---

# Every algorithm must work on photons

This is a rule about the *shape* of the API, and it applies to anything added to
this library from here on.

The data this library exists for is a list of detections. A grid — a pixel, a
histogram bin, a lag channel, a TAC channel — is something a reader **imposed**
afterwards. So an algorithm that only accepts the grid is an algorithm that
cannot be used on the measurement; it can only be used on somebody's summary of
it.

## Where to pick this up

1. **Apply the rule to the algorithms that do not yet follow it.** The status
   table below is the worklist. `wiener_deconvolve` is the nearest one: it has
   no event-wise formulation (closed form in Fourier space), so what it needs is
   not a `_events` twin but a documented jitter path and a test that the two
   agree. Anything new added to `modules/math` starts here.

2. **The 3-D event-mode entry point does not exist.** `richardson_lucy_events`
   is rank-generic in C++ but only `richardson_lucy_events_2d` is bound. An
   axial stack is where deconvolution gains most, so this is the largest single
   gap. The typemaps are the only work — see `Deconvolution.i`, and note that a
   3-D `ARGOUTVIEWM` cannot reuse the 2-D output parameter names.

3. **Weights are implemented and unbound.** `richardson_lucy_events` takes a
   per-photon weight and the flat entry point passes `nullptr`. That is how a
   caller applies a detection-efficiency correction, or bins coarsely on one
   axis while staying event-wise on another. Cheap to expose; nothing depends on
   it yet, which is why it was left.

4. **The sensitivity term is computed and discarded.** It is the fraction of
   each pixel's PSF that falls inside the frame, which is directly useful to a
   caller — it says which pixels are poorly observed — and it is what makes the
   bare sum not conserve flux near a border. Returning it would let callers
   check `sum(f * s) == n_events`, which is the exact invariant and currently
   only assertable indirectly.

5. **Do not test the photon form against the standard form.** They are different
   estimators — list mode divides by a sensitivity the grid form has no term for
   — so a comparison measures only how far apart they are supposed to be. Test
   against a transcription of the formula, as `test_deconvolution.py` does.

## The two forms

Every algorithm ships **both**, and neither is optional:

| | takes | why it exists |
|---|---|---|
| **standard form** | the binned array | what a caller with an image or a histogram already in hand expects, and what a reviewer will compare against the reference implementation. Keep it numerically identical to that reference. |
| **photon form** | the detections, with fractional coordinates | the one that is *correct* on photon data. It never forms the grid. |

Naming: the photon form takes the standard name with `_events` appended —
`richardson_lucy` / `richardson_lucy_events`.

## When the algorithm cannot be reformulated

Some algorithms are closed-form on a grid and on nothing else. The fallback is
**not** to bin and pretend. It is to *jitter*: give each photon a continuous
position drawn uniformly across the interval its quantised coordinate stands
for, then run the continuous algorithm on that.
[`Jitter.h`](../../modules/math/include/Jitter.h) is the shared implementation —
`jitter_coordinates`, `events_from_counts`, `counts_from_events`.

### Why jitter, stated correctly

The tempting justification is that binning *biases* estimates. For a mean it
does not — the bin centre is unbiased — and starting from a wrong reason tends
to produce a wrong tolerance somewhere downstream. The real reason is that
binning puts distinct photons at **identical coordinates**, and every algorithm
that measures a distance is degenerate on ties rather than merely degraded by
them. Four thousand photons of a σ = 3 px spot, binned to that grid:

| | true | binned | jittered |
|---|---:|---:|---:|
| nearest-neighbour distances exactly zero | 0 % | **98.4 %** | 0 % |
| mean nearest-neighbour distance | 0.1170 | 0.0175 | 0.1169 |

That is what clustering, density estimation and nearest-neighbour methods see.

### The price, and that it is charged twice

The dither adds variance `w² / 12` — Sheppard's correction, known and
subtractable, which is what makes it payable rather than hidden. But a photon
that has been through a histogram *and back* pays it **twice**: once when
rounding threw the position away, once when the dither put a random one back.
Only the original coordinates avoid both. This is why jitter is the fallback
and an event-wise formulation is the answer wherever one exists.

The dither is uniform across the bin and **not** Gaussian: quantisation is a
rectangle, so a rectangle is what reproduces the distribution the value came
from. A Gaussian of matched variance puts a third of its photons outside the bin
the instrument said they were in.

### Reproducibility

Jitter draws from the central RNG ([`Random.h`](../../modules/math/include/Random.h))
via the counter-based `deterministic(seed, index)` path, per photon and per axis
— never from sequential state. So `TTTR_RNG_SEED` fixes a run, and the answer
does not depend on thread count or scheduling. A jittered analysis gives the
same result on 1 core and on 32.

## What this cost to get right in the first case

Deconvolution was the first algorithm built to this rule, and two defects in it
are the kind the rule invites. Both are worth reading before adding the next
one, because both were invisible to every obvious test.

**Interpolating the kernel is itself a convolution.** The photon form evaluates
the PSF at each photon's fractional offset by interpolating between samples.
Linear interpolation between taps `1-t` and `t` has variance `t(1-t)` — up to
0.25 px² — so the reconstruction was broadened by an amount that *swung with
each photon's sub-pixel position*, which is precisely the quantity working
event-wise was meant to preserve. Flux, centroid and non-negativity were all
exactly right while this was happening. Sampling the kernel `K` times finer
divides it by `K²`; the entry point takes a `psf_oversampling` parameter and
callers pass 8.

It also flattered the benchmark: before the fix, event mode "beat" binning by
more than it really does, because an over-wide forward model over-sharpens.

**Where the kernel is truncated sets positional accuracy.** For a photon at a
fractional position the kernel is sampled at offsets that are *not* symmetric
about it, so cutting the tails cuts unequally and drags the centroid. It does
not improve with finer sampling — only with support:

| support | 3.7 σ | 5.0 σ | 6.3 σ | 7.7 σ |
|---|---:|---:|---:|---:|
| centroid error | 8·10⁻⁴ px | 3·10⁻⁶ px | 2·10⁻⁹ px | 1·10⁻¹³ px |

**Five sigma** is the number to remember.

A third, smaller one: normalise the kernel on the **comb** it will actually be
sampled at — every `K`-th sample from the centre — not on the fine array's
Riemann sum. The two differ by the truncation and by aliasing, which was worth
1·10⁻⁴ in flux and 8·10⁻⁴ px in centroid.

## Status

| algorithm | standard | photon | notes |
|---|---|---|---|
| Richardson–Lucy | `richardson_lucy` | `richardson_lucy_events` | list-mode MLEM with a sensitivity term the grid form has no analogue for; the two are *different estimators* and must not be tested against each other |
| Wiener | `wiener_deconvolve` | — | linear and closed-form in Fourier space; use the jitter bridge |
| HDBSCAN / MST | `Cluster.h` | inherently event-wise | takes points, never a grid |

## See also

* [`Jitter.h`](../../modules/math/include/Jitter.h) — the bridge and the full argument
* [`Deconvolution.h`](../../modules/math/include/Deconvolution.h) — the worked first case
* `test/python/misc/test_jitter.py`, `test/python/misc/test_deconvolution.py`
