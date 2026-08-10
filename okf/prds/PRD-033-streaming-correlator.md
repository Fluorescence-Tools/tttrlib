# PRD-033 — Streaming Correlator

> **STATUS: ✅ FIXED (2026-08-10).** `StreamingCorrelator` now agrees with the
> batch `Correlator` (Wahl) to 1.0000 on every cascade. The diagnosis in the
> "Why it was broken" section below turned out to be wrong in an instructive
> way — it was not a normalization problem — and is kept for that reason.
> Of the other three streaming consumers, two were correct as claimed and one
> was not — see "The other three consumers" below. All four are now checked
> against their batch equivalents rather than asserted to be right.

## The fix (2026-08-10)

**Root cause: the lag axis and the accumulator disagreed.** `get_correlation`
read every level `b >= 1` at coarse lags `n_bins/2 .. n_bins/2 + n_bins - 1`,
a constant. But the axis the class publishes — the same one
`CorrelatorCurve::update_axis` builds, and the one the batch correlator
computes at — requires level `b` to start at

    offset(b) = x[b * n_bins] / 2^b

which for `n_bins = 16` is 0, 8, 12, 14, 15, 15, ... converging to
`n_bins - 1`. Only level 1 happens to equal `n_bins/2`, which is exactly why
cascades 0 and 1 agreed with the batch and everything above did not. Reporting
the value from a *shorter* lag than the label claims reads, for a decaying
G(tau), as an inflated G — and the inflation grows with the cascade because the
mismatch does. That is the whole of the 1.19 / 1.25 / 1.12 / 2.39 pattern; the
non-constant ratio, read at the time as evidence of a normalization subtlety,
was evidence of the opposite.

Two smaller defects went with it:

- The normalization used `2^(j / n_bins)` for the coarse bin width where the
  batch uses `2^((j-1) / n_bins)`, so every block-boundary lag was off by a
  factor of two.
- The two-channel entry point `push_photon(mt1, w1, mt2, w2)` accumulated `w1`
  only and ignored the second channel entirely: it computed an
  autocorrelation and called it a cross-correlation. It is replaced by
  `push_photon(mt, w, channel)`, and each level now carries a history per
  channel, so the lag runs channel 0 -> channel 1 as it does in the batch.

Each level now stores its own first lag and accumulates only the `n_bins` lags
it will be asked for, which also makes the inner loop and the history shorter
than they were.

**Verification** — `test/python/streaming/test_streaming_correlator.py`, 8
tests: axis equality with the batch, Poisson flatness, per-cascade agreement on
two correlated streams, cross-correlation, flush idempotence, rejection of a
photon pushed after flush, and chunked-vs-whole equality. Mean stream/batch
ratio per cascade on 40k photons from an OU-modulated stream:

| Cascade | Lag range | Before | After |
|--------:|-----------|-------:|------:|
| 0 | 1–16 | 1.031 | **1.0000** |
| 1 | 18–48 | 1.031 | **1.0000** |
| 2 | 52–112 | 1.19–1.27 | **1.0000** |
| 3 | 120–240 | 1.25 | **1.0000** |
| 4 | 256–496 | 1.12 | **1.0000** |
| 5 | 528–1008 | 2.39 | **1.0000** |
| 6–11 | up to 65520 | not measured | **0.9999–1.0000** |

## The other three consumers

The PRD recorded `StreamingBurstDetector`, `StreamingDecayHistogram` and
`StreamingPhasor` as "✅ works" beside the broken correlator. Two of the three
hold up; the burst detector does not, and it was wrong in the same *shape* as
the correlator — an untested claim, plausible on the common case.

| Class | Claimed | Checked against | Verdict |
|---|---|---|---|
| `StreamingDecayHistogram` | works | `np.bincount`, `TTTR.get_microtime_histogram` | ✅ exact |
| `StreamingPhasor` | works | `DecayPhasor.compute_phasor_bincounts` | ✅ to 1e-12, and on the universal semicircle |
| `StreamingBurstDetector` | works | `TTTR::burst_search_sliding_window` | ❌ four defects, now fixed |

`StreamingBurstDetector` implements the same criterion as the batch sliding-
window search, so on the same photons it must return the same boundaries. It
did not:

1. **Every burst ended one photon late.** The window that *fails* the test ends
   the burst at the last photon of the preceding window — `i + m - 2` in the
   batch loop — not at the photon just pushed. Measured as a uniform `+1` on
   every end index, which adds a background photon to every burst.
2. **A burst of coincident photons was reported as no burst.** The detector
   divided `m` by the window span to get a rate, and guarded a zero span with
   `rate := 0` — so `m` photons in a single macro-time tick, the highest rate it
   can ever see, fell below the threshold. The batch search finds that burst.
   The comparison is now on the span directly, which is what the batch does and
   needs no special case.
3. **It kept every photon's macro time** — `std::vector` growing without bound,
   commented "all photon macro times" — in a consumer whose entire purpose is a
   live acquisition of unbounded length. Only the last `m` are needed; it is a
   ring buffer now, O(m).
4. **A non-positive macro-time resolution silently returned the whole stream as
   one burst.** `T/dt` and the rate threshold both go negative and the
   comparison inverts. This is reachable by accident: a `TTTR` whose header has
   not been read reports a resolution of `-1.0`. The constructor now rejects it.

The return value of `push_photon` was also documented as "true if a burst just
completed" and computed as `!in_burst && !bursts_.empty() && &bursts_.back() !=
nullptr` — where the last term is the address of a reference and so always
true. It now means what it says.

Verified by `test/python/streaming/test_streaming_burst_detector.py`, which
compares boundaries index for index against the batch over nine
seed/parameter combinations, plus
`test/python/streaming/test_streaming_decay_and_phasor.py` for the other two.

## Performance, while the file was open

The correlator cost one cascade step per macro-time bin — including the empty
ones, which at a native resolution outnumber the photons by a factor of
hundreds to thousands. Empty runs are now skipped in closed form: level *b*
emits `n0 / 2^b` times, so a run of `k` empty samples covers a difference of
two divisions, and only the first emission can carry a non-zero accumulator.
80k photons over a 4.0 M-bin span: 0.136 s → 0.063 s; over a 200 M-bin span the
old code would have spent about 4 s and it now takes 0.069 s. The cost no
longer scales with the length of the acquisition.

## Historical record — the state before the fix

## Goal

Provide tttrlib consumers that accept photons one at a time and maintain
incremental state, so the library can run in a live/acquisition environment
where the full photon record is not yet available.

## Current state (Aug 2026)

A `streaming` module exists at `modules/streaming/` with four header-only
classes:

| Class | Status | Notes |
|-------|--------|-------|
| `StreamingCorrelator` | **BROKEN** | Wrong G values at cascade ≥ 2 |
| `StreamingBurstDetector` | ✅ works | Sliding-window burst search |
| `StreamingDecayHistogram` | ✅ works | Incremental TCSPC histogram |
| `StreamingPhasor` | ✅ works | Incremental FLIM g, s |

Python bindings are wired via `ext/python/Streaming.i` and `%include` in
`tttrlib.i`. All classes import and basic round-trips work.

## Why `StreamingCorrelator` is broken

The correlator uses the Schätzel multi-tau intensity-trace architecture:
photons are binned into a uniform macro-time intensity trace, and each sample
feeds a cascade of correlation levels, each at 2× coarser resolution.

**Lag geometry (correct now):**
- level 0: fine lags 0..15 → output [0..15]
- level 1: coarse lags 8..23 → output [16..31]
- level 2: coarse lags 12..27 → output [32..47]

**Agreement vs batch Wahl correlator (measured on 42k photons, τ_D=15.6 ms):**

| Cascade | Lag range (ms) | ratio stream/batch |
|---------|----------------|--------------------|
| 0 | 0.6–1.9 | **1.031** ✅ |
| 1 | 5–10 | **1.031** ✅ |
| 2 | 15–30 | 1.19–1.27 ❌ |
| 3 | 50 | 1.25 ❌ |
| 4 | 100 | 1.12 ❌ |
| 5 | 195 | 2.39 ❌ |

Cascade 0 and 1 match batch within 3%. Cascade 2+ overestimate G. The
normalization was changed from `pw²` to `pw` to fix cascade 1, but cascades
2+ remain off. The residual error is likely a **border/overlap handling
issue** at cascade boundaries, not a simple normalization constant.

## What had to be fixed (all done)

1. ~~**Resolve cascade ≥ 2 normalization.**~~ Done — it was not normalization;
   see "The fix" above. The ratio is not constant (1.19,
   1.25, 1.12, 2.39), so it is not a single missing constant. Investigate
   whether coarsening accumulates `pending` correctly across multiple levels
   (the `phase`/`pending` logic in `feed_cascade`).
2. ~~**Verify against batch for a pure Poisson stream**~~ Done —
   `test_poisson_is_flat_at_one`.
3. ~~**Add a unit test** comparing `StreamingCorrelator` to `Correlator`~~ Done —
   `test/python/streaming/test_streaming_correlator.py`, asserted per cascade
   rather than in aggregate, because an aggregate tolerance is what would have
   hidden this defect.

## Files

- `modules/streaming/include/StreamingCorrelator.h`
- `modules/streaming/include/StreamingBurstDetector.h`
- `modules/streaming/include/StreamingDecayHistogram.h`
- `ext/python/Streaming.i`
- `test/python/streaming/test_streaming_correlator.py` — the batch comparison
- `benchmarks/fcs_ab_fast.py` — A/B test harness (reuses cached photons)
- `benchmarks/fcs_ab_compare.png` — comparison plot
- `benchmarks/fcs_test_photons.npy` — cached synthetic diffusion data

## Reference

- Schätzel, K. (1985). Fast digital techniques for the photon-counting
  correlator. *Proc. SPIE* 492, 82–90.
- Felekyan et al. multi-tau architecture (as in `Correlator::ccf_wahl`).