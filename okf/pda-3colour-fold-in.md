# Folding chisurf's PDA3c into tttrlib's `pda` module

Status: **implemented** as `PdaBurstLikelihood` (`modules/pda`), and chisurf's
`burst_log_likelihood` now delegates to it. Steps 1-4 of the plan below are
done; step 5 (the three-colour physics layer) stays in chisurf, as proposed.

Outcome against the plan:

- 14-920x faster than the NumPy path on matched inputs, agreeing to ~1e-13;
  140x end-to-end at chisurf's `total_log_likelihood`.
- Improvements (a) burst-side caching and (b) the fused reduction landed as
  designed. (c) went further than proposed: rather than *choosing* between box
  and convolution, the box is gone. Grouping by the total background count makes
  the coefficients a convolution of the per-channel series, `O(K b m_max)`
  against `O(prod b_c)`, with no box in memory and no chunking. The crossover
  analysis below (K=3 keeps the GEMM, K=4 does not) is superseded: the
  convolution is never worse, and at K=2 it is the same work.
- A second trick that is not in the Python at all: peak-shifting per channel
  rather than per box row leaves the inner loop free of transcendentals.
  `sum_c b_c` exponentials per burst instead of `prod_c b_c` -- 90 against 3375
  at a 15-wide three-channel box. This was worth 37x on its own, on top of the
  convolution.
- One correctness fix beyond the port: the NumPy fast path returns `-inf` where
  a channel has `p_c = 0` but collected photons, because `L = Multinom(F;p) x
  correction` divides by a zero leading term. tttrlib evaluates those bursts in
  a form that never divides by `p`. chisurf's own untruncated reference agrees
  with tttrlib; a test in `test_pda3c_likelihood.py` pins both behaviours.
- The `(int)` cast of the effective truncation rate is undefined once a
  near-zero `p_c` sends it past 1e11, which silently collapsed the box to
  nothing -- caught by the cross-validation, not by any unit test. Guarded now.

chisurf side, once the numbers matched: the NumPy fast path and its private
helpers (`_burst_log_likelihood_numpy`, `_channel_boxes`, `_background_factors`,
`_tail_cutoff`, `_KERNEL_ELEMENT_BUDGET`) are **deleted**, not deprecated --
nothing public was removed, so there is no deprecation cycle to run.
`burst_log_likelihood` keeps its signature and semantics and is now a thin
adapter; tttrlib is a hard requirement with a named `ImportError` rather than a
silent fallback. Net -181 lines. What stays in chisurf is only what tttrlib does
not provide in the same shape: the broadcasting `log_multinomial_pmf`, the
untruncated per-burst oracle, and `collapse_bursts`.

Two chisurf tests had quietly stopped testing anything: they monkeypatched
`_KERNEL_ELEMENT_BUDGET` to force chunking, and once the tttrlib path took over
that knob was inert, so both assertions were trivially true. Replaced with the
invariants they were actually after -- per-burst answers independent of the
batch, and a peak allocation measured against the result size.

The rest of this document is the original proposal, kept as the rationale.

## What PDA3c actually is

`chisurf/core/fluorescence/pda3c` (1826 lines of NumPy/SciPy) is **not** a
three-channel version of tttrlib's `Pda`. It is a different evaluation
strategy for the same physics:

| | tttrlib `Pda` | chisurf `pda3c` |
|---|---|---|
| channels | 2, hard-coded | K, currently 3 |
| representation | dense `(Nmax+1)²` matrix | none — per burst |
| objective | χ² against a binned 1-D histogram | burst-wise log-likelihood |
| photon cap | `hist2d_nmax` | none |
| background | 2-D Poisson convolution of the matrix | per-burst Poisson series |

The count matrix is the thing that does not generalise. At `Nmax = 300` a
three-channel dense simplex is 2.7·10⁷ doubles (217 MB) and a four-channel one
is 65 GB. So the fold-in is **not** "add an axis to `S1S2`". It is: add a
burst-wise likelihood evaluator alongside the existing histogram path, and let
the 2-channel case fall out of it as `K = 2`.

The existing histogram path stays. It is what people plot, and it is the only
thing that produces a figure.

## The algorithm worth taking

The likelihood of one burst with per-channel counts `F` under channel
probabilities `p` and Poisson backgrounds `B`:

```
L(F | p, B) = Σ_{b ≤ F} [Π_c Pois(b_c; B_c)] P(n) Multinom(F − b; p),   n = N − Σb_c
```

Evaluated literally this is a nested sum over every channel's background count.
chisurf's docstring notes the incumbent implementation of that "needs threaded C
and a GPU kernel". Three transformations remove the need:

**1. Only the total background count couples the channels.** The multinomial's
leading `n!` depends on `m = Σ_c b_c`, not on how the background is
distributed. Divide through by the zero-background term and group by `m`:

```
L = Multinom(F;p) · Σ_m w_m c_m,    w_m = P(N−m)(N−m)!/N!
c_m = Σ_{Σb_c = m} Π_c u_c(b_c),    u_c(b) = Pois(b;B_c) · F_c!/(F_c−b)! · p_c^{−b}
```

`c_m` is a discrete convolution of the per-channel series.

**2. Burst and model separate.** Writing `u_c(b)` as a burst-determined factor
times `p_c^{−b}`, the sum over the background box becomes a matrix product
between a `(bursts × box)` array and a `(points × box)` one. Background then
costs the same *kind* of operation as the zero-background term instead of a
different one, and the two compose rather than being separate code paths.

**3. Peak-shift before exponentiating.** The model half is the unscaled
`Π_c p_c^{−b_c}`, cancelled only later by the burst factor's falling
factorials. It overflows to `inf` — and `out + log(inf)` is a `+inf`
log-likelihood, i.e. a "perfect" fit — as soon as `b·log(1/p_c) > 709`. That is
reachable in an ordinary fit. Each half is shifted onto `(0,1]` first and the
shifts added back in log space; exact, because the shifts are per row.

### The one trap to carry across verbatim

**The background series may not be truncated on Poisson tail mass.** The summand
behaves like `Pois(b;B_c)·(F_c/(N p_c))^b`. That ratio is ≈1 near the optimum —
which is why a naive cutoff looks fine on well-fitting data — but where a channel
collected far more photons than the model allows it *grows* for many steps before
the Poisson factor turns it over, and there the background explanation is the
entire likelihood. chisurf cuts at an **effective rate** `B_c · max(F_c/(N p_c))`
capped at the largest count the channel saw.

Note this does **not** apply to tttrlib's existing `conv_pF`, which truncates at
`1e-15` kernel mass. There the other operand is a probability ≤ 1, so the
discarded mass is bounded. I have added a comment saying so, because the two look
like the same optimisation and only one of them is safe.

## Algorithmic improvements available in the port

These are things chisurf's version does not do, not just a language change. Its
inner loop is already GEMM-shaped, so a naive transcription would win little.

**(a) Cache the burst-side factors across fit iterations — the big one.**
`_background_factors` builds the `(bursts × box)` log-kernel on every call, with
`gammaln` and `poisson.logpmf` over the whole box. But it depends only on
`counts`, `background`, `P(n)` and the box — *none of which change during a
fit*. Only `log_model = −log(p) @ exponentsᵀ` changes. Hoisting the burst half
into object state makes each iteration two GEMMs and nothing else. Same
structure as the `get_1dhistogram_per_species` win already landed on the
2-colour path: precompute everything that does not depend on the fitted
parameter.

**(b) Fuse the reduction.** `burst_log_likelihood` returns the full
`(points × bursts)` grid. A fit almost always wants `Σ_bursts`. At 1000 points ×
50 000 bursts that array alone is 400 MB, and chisurf chunks specifically to
survive it. Accumulating the sum in the GEMM epilogue removes both the
allocation and the chunking logic.

**(c) Pick box vs. convolution by K.** The fast path uses the full product box,
size `Π_c box_c` — exponential in K. The convolution form from point 1 above is
`O(K · m_max²)` instead, and chisurf only uses it on the per-burst reference
path. Rough crossover at box ≈ 15/channel: K=3 gives 3375 (box) vs 6075 (conv),
so GEMM wins; K=4 gives 50 625 vs 14 400, so convolution wins. Three colours
should keep the GEMM; a four-channel future should not.

**(d) No SciPy.** `gammaln` → `std::lgamma`; `poisson.logpmf(b,λ)` →
`b·log(λ) − λ − lgamma(b+1)`. Both are std-only, so this stays inside the
project's no-third-party-dependencies rule.

## Proposed shape

A new class in `modules/pda`, K-channel from the start, with the burst-side
precompute in the constructor:

```cpp
class PdaBurstLikelihood {
public:
    // counts: (n_bursts x K) row-major. Precomputes the multinomial constant
    // and the (bursts x box) background kernel once.
    PdaBurstLikelihood(const int* counts, int n_bursts, int n_channels,
                       std::vector<double> background = {},
                       std::vector<double> photon_number_pmf = {},
                       double tolerance = 1e-12);

    // log L per burst for one probability vector p (length K).
    void log_likelihood(double* p, int n_p, double** out, int* n_out);

    // Σ_bursts log L, for a grid of probability vectors: p is (n_points x K).
    void total_log_likelihood(double* p, int n_p1, int n_p2,
                              double** out, int* n_out);
};
```

`Pda` is untouched. `K = 2` on this class is an MLE alternative to the existing
χ² histogram fit — no binning, no `hist2d_nmax` cap, and correct where a bin
holds a handful of bursts, which is exactly where χ² on a histogram is not.

## Order of work

1. `PdaBurstLikelihood` for K channels, no background (the multinomial term
   alone). Cross-check against chisurf's `log_multinomial_pmf`.
2. Background series + product box + peak-shifted GEMM. Cross-check against
   chisurf's `burst_log_likelihood_reference`, which evaluates the nested sum
   directly and is deliberately untruncated.
3. Burst-side caching (improvement **a**) and the fused reduction (**b**).
4. SWIG exposure; `K = 2` regression against the existing `Pda` on simulated
   data where both are valid.
5. Only then: the three-colour physics layer (`transfer_efficiencies`,
   cascading pathways, crosstalk matrices). That is `physics.py`, ~540 lines,
   and it is pure bookkeeping on top of the likelihood — it can stay in Python
   in chisurf, or move later.

Steps 1–3 are the part that is slow in chisurf and the part that is reusable
outside it. Step 5 is neither.

## Next: generate the validation data with the sim engine

Not a speed idea — the likelihood is fast now. This is about what the fit is
tested against.

chisurf's `simulate_bursts` "generates by the definition rather than by the
likelihood's factorisation". True, and it does catch algebra errors. But it
draws one distance triple per burst and multinomially splits an independently
drawn photon budget — which is *the fitted model, sampled*. It cannot detect an
error in the model's **assumptions**, only in its arithmetic. Every assumption
below is put in by hand and so is never tested:

- one fixed distance for the whole burst (no dynamics during transit),
- photon budget independent of distance (no brightness change with FRET),
- burst sizes drawn from a distribution rather than emerging from the optics,
- background exactly Poisson with a known rate,
- bursts handed over pre-segmented, so burst selection never enters.

`SimEngine` breaks all five, and three-colour is directly expressible in it —
verified, not assumed:

```python
sp.q_alex = [[40.0, 25.0, 12.0],   # laser 0 (blue): B, G, R brightness
             [ 0.0, 30.0, 18.0]]   # laser 1 (green): G, R
eng = tttrlib.SimEngine(system, [exc, exc], [det, det, det], integrator)
```

Two excitation grids give exactly the blue and green excitation periods that
`BurstCounts.blue` (3 wide) and `.green` (2 wide) already model; `n_channels = 3`
and `alex_period = 2` run it; `to_tttr()` hands back a photon stream a burst
search can be run on. A 3-channel two-laser run produces per-channel counts and
a TTTR object today.

What that buys, in order of value:

1. **A forward model that is independent of the fitted one.** Recovering known
   distances from diffusing molecules tests the model, not the algebra.
2. **Assumption violations on purpose.** `k_nrad` between two species at
   different distances gives dynamics *within* a burst; the fit assumes there are
   none. How much does a known switching rate bias the recovered distance? That
   question has no answer today.
3. **A real `P(n)`.** pda3c takes `photon_number_pmf` as an input and the docs
   note P(F) ≈ P(S) "is invalid for small photon counts". The simulator knows the
   true signal photon number, so the approximation can be quantified instead of
   asserted.
4. **Burst selection in the loop.** The sim produces a photon stream, so the
   selection bias the guide warns about becomes measurable end to end.

Cost: the physics layer (`channel_probabilities`) already converts distances to
per-channel probabilities, and `SimSpecies.q` is per-channel brightness — so the
bridge is one function, distances → `q_alex` rows. The work is in the harness and
the analysis, not the plumbing.

## Risks

- **Numerical agreement is the whole deliverable.** Every step needs the
  chisurf reference path as an oracle, not a pinned array — the same lesson as
  the S1S2 orientation bug, where a symmetric test model stayed green through a
  transposition.
- The overflow behaviour (point 3) is the failure mode that produces a
  *plausible* answer — a `+inf` log-likelihood reads as a perfect fit. Any port
  needs an explicit test that a short-distance node with a few photons in a
  near-zero-probability channel does not score `+inf`.
- chisurf's `pda3c` is under active development (`likelihood.py` touched
  2026-08-04). Port against a pinned commit and record it here.
