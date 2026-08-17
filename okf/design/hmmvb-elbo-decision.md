# `HmmVB.elbo`: engine number (E) or header derivation (H)? — decision brief

Status: **open decision**, numbers in hand. Filed from `okf/BUGS.md` ("`HmmVB`'s
ELBO data term is not the one its header derives"). Nothing in the kernel was
changed for this brief. Scripts and raw output:
`okf/design/scripts/hmmvb_elbo_decision.py` (`.out`) and
`okf/design/scripts/hmmvb_elbo_fixture.py` (`.out`), ~10 min on a laptop.

## 1. What the two numbers are

The tick-level photon HMM has hidden state at every macro-time tick and an
observation only at photon ticks; θ = (π, A, B) with Dir(1) priors. Mean-field
VB (MacKay 1997, Beal 2003) with q(θ) = product of Dirichlets gives
Ã = exp E_q[log A] (and π̃, B̃ likewise). Ã is **sub-stochastic** — the
geometric mean of a Dirichlet row sums to less than 1 by Jensen; for row i
with total concentration α_i and K states, row mass ≈ 1 − (K−1)/(2α_i).

* **H (header / Beal / the Python prototype `prototype/hmm/vb.py`)**:
  q(z) ∝ p̃(y, z | θ̃) from forward–backward with the sub-stochastic weights,
  a gap of Δt ticks marginalising to Ã^Δt. Then
  F = log Z̃ − Σ KL(q(θ)‖p(θ)) is Beal's bound: **F ≤ log p(y) for every
  q(θ)**, so H is a valid ELBO at the engine's converged q(θ) even though the
  engine did not iterate on it. This is what `HMMVB.h`'s comment derives.
* **E (engine, `HMMVB.cpp` via `HMM::evaluate`)**: the A^Δt cache
  (`pair_pow`/`matmul_norm`) row-normalises Ã before every squaring, so the
  data term is a forward pass with π̃, B̃ sub-stochastic and A stochastic.
  E = that − ΣKL. It is not Beal's F, and not the plug-in likelihood of any
  θ̂ either (π̃, B̃ still enter un-normalised). No theorem makes E a bound.

**The gap has a closed form.** With Dir(1) priors, α_i = N_i + K where N_i is
the expected number of ticks spent in state i, and the sub-stochastic pass
loses log(row mass) ≈ −(K−1)/(2α_i) on each of those N_i ticks, so

  E − H ≈ Σ_i N_i (K−1)/(2α_i) → **K(K−1)/2 nat**  (1 for K=2, 3 for K=3, 6 for K=4),

independent of the data once every state is populated (an unused state
contributes ~0). Measured |gap − prediction| ≤ 0.07 nat over the 100 fits below.
So H is not "E with a data-dependent bias": it is E minus half a nat per free
transition parameter — a fixed, K-only penalty. It cannot depend on tick
count or posterior width beyond that; the BUGS entry's worry on that point is
withdrawn.

Note the same normalisation changes q(z) only at relative order (K−1)/(2α_i)
~1e-4 on realistic tick counts, so the engine's fixed point is Beal's fixed
point to that order; the *number* is what differs, not the posterior.

## 2. Numbers

`logZ` is an importance-sampling estimate of the exact log marginal likelihood
(proposal = the VB posterior with (α−1) scaled by 0.7, p(y|θ) from the engine's
exact forward pass, 3000 draws; ESS printed — for K ≥ 3 the ESS is ~10, which
biases the estimate *low* by 1–3 nat, i.e. the true shortfalls of E and H are if
anything larger). `logZ` is single-mode; the full evidence adds ln K! for the
label permutations (0.69 / 1.79 / 3.18 nat for K = 2/3/4). BIC and ICL are the
ML fit's (`HMM::optimize`, `viterbi_path`). All fits: near-diagonal A₀ (0.99),
4 restarts, best objective kept, up to 3000 iterations.

### 2a. The BUGS.md case — H2MM_C fixture `two_state_2det` (30 bursts, 3600 photons, 90k ticks, true K = 2)

| K | E (engine) | H (header) | E−H | K(K−1)/2 | logZ (IS) | ESS | ML loglik | BIC | ICL |
|---|-----------:|-----------:|----:|---------:|----------:|----:|----------:|----:|----:|
| 1 | −2391.57 | −2391.57 | 0.000 | 0 | −2391.57 | 2862 | −2387.67 | 4783.5 | 4783.5 |
| 2 | **−2293.76** | **−2294.76** | 1.000 | 1 | **−2291.84** | 117 | −2272.38 | **4585.7** | **5318.3** |
| 3 | −2312.79 | −2315.78 | 2.996 | 3 | −2306.68 | 10 | −2270.43 | 4630.9 | 5820.8 |
| 4 | −2334.11 | −2340.09 | 5.980 | 6 | −2323.91 | 8 | −2269.28 | 4694.1 | 6273.8 |

Every criterion picks K = 2, by 19 (E), 21 (H), 15 (logZ) nat. K = 1 is exact
(conjugate: E = H = logZ to 0.01) — a check on the IS estimator. Shortfall
from logZ at K = 2/3/4: E −1.9/−6.1/−10.2, H −2.9/−9.1/−16.2 nat; both are
below the evidence at every K ≥ 2 and both fall further behind as K grows.

### 2b. Simulated sweeps (12 seeds each; K_true = 2 with 40 bursts ≈ 2400 photons, K_true = 3 with 80 bursts ≈ 4800 photons; 2 detectors, mean gap 8 ticks)

Correct picks out of 12 seeds (a pick = the K maximising E / H / logZ, or minimising BIC / ICL):

| truth | E | H | logZ | logZ + ln K! | BIC | ICL |
|-------|---|---|------|--------------|-----|-----|
| K = 2 | 12 | 12 | 12 | 12 | 12 | 12 |
| K = 3 | 11 | 11 | 12 | 12 | 11 | 0 (always 2) |

E and H picked the same K on all 24 seeds. The one miss (K_true = 3, seed 7)
had ΔlogZ(2→3) = +0.16 (a tie for the exact evidence, +1.3 with ln K!),
ΔE = −2.76, ΔH = −4.75, ΔBIC also wrong: an ELBO of either kind is more
conservative than the evidence, H by the extra K nat per step.

Mean shortfall from logZ (nat; 12 seeds each; the K = 1 row is exact and
checks the estimator):

| case | K | E − logZ | H − logZ | E − H | K(K−1)/2 | mean ESS |
|------|---|---------:|---------:|------:|---------:|---------:|
| K_true = 2 | 1 | 0.00 | 0.00 | 0.000 | 0 | 2862 |
|            | 2 | −0.47 | −1.47 | 0.999 | 1 | 908 |
|            | 3 | −4.85 | −7.84 | 2.98 | 3 | 16 |
|            | 4 | −10.29 | −16.22 | 5.94 | 6 | 7 |
| K_true = 3 | 1 | 0.00 | 0.00 | 0.000 | 0 | 2862 |
|            | 2 | −0.65 | −1.65 | 0.999 | 1 | 526 |
|            | 3 | −3.57 | −6.56 | 2.99 | 3 | 22 |
|            | 4 | −8.86 | −14.82 | 5.95 | 6 | 5 |

Neither E nor H exceeded logZ + 0.05 in any of the 96 fits. |E − H − K(K−1)/2|
≤ 0.07 nat throughout (unpopulated states account for the residual).

K → K+1 steps of E, per seed (nat): K_true = 2: 1→2 +190…+256, 2→3 −16…−20,
3→4 −15…−21. K_true = 3: 1→2 +488…+602, **2→3 −2.8…+26 (median +9.3)**,
3→4 −22…−30. The E–H penalty difference at the 2→3 step is 2 nat, at 3→4 it is
3 nat — smaller than every wrong-direction step observed and smaller than all
but one right-direction step.

## 3. Reading

1. **Validity.** H is a lower bound on log p(y) by construction; E is not,
   but on 100 fits E never exceeded logZ either — E sits K(K−1)/2 nat above
   H and still 2–10 nat (growing with K) below the evidence, because the
   mean-field losses on π and B, the ln K! label term and the coupling are
   larger than the transition-row Jensen term E discards.
2. **Model selection.** E and H rank K identically unless
   |E(K+1) − E(K)| < K nat, since the E–H gap grows by exactly K nat from K to
   K+1. In the sweeps the wrong-direction steps were 15–30 nat and the right-direction
   2 → 3 steps at K_true = 3 were −3…+26 nat (median 9), so E and H disagreed on
   no seed at all; where an ELBO missed (one seed), the exact evidence was
   itself tied. Both ELBOs are **conservative** relative to logZ (they under-count
   states more often, H by one extra ½-nat-per-parameter), and both agree with
   BIC far more often than with ICL, which is the most conservative of the set.
3. **The Pressé/Gonzalez caution stands**: an ELBO is a bound whose gap grows
   with K, so it is a *biased-low* model-selection number whichever way this
   decision goes; the honest statement in the header is "conservative,
   ln K! + mean-field gap not recovered", not "the model-selection number".

## 4. Recommendation

**Report H as `elbo`; keep the engine's iteration untouched; keep the
E data term available.** Concretely:

* At convergence, run one extra sub-stochastic forward pass (no A^Δt cache
  normalisation — an `A^Δt` power without `matmul_norm`, scaled per burst as the
  forward recursion already is) and set `elbo = logZ̃ − ΣKL`. Cost: one E-step
  (~ 1/n_iter of the fit).
* Keep `loglik` = the normalised-row data term (what `HMM::evaluate` returns
  today) and add `elbo_normalised` (= today's `elbo`) so anyone who saved
  numbers can compare; document the K(K−1)/2 relation.
* No flag: both numbers are returned; nothing else in the fit changes (the
  posterior moves at 1e-4 relative if the E-step were made sub-stochastic too —
  not worth touching `pair_pow`).

Why this way round: H is the quantity every VB-HMM paper reports and the header
derives; E is a number no derivation licences and its only virtue — being
closer to logZ — is accidental (it discards one Jensen loss and keeps the
others). Users comparing K see values drop by K(K−1)/2 nat; ranking changes
only in the borderline (< K nat) cases, where the exact evidence itself is
within its IS error of a tie.

Alternative if the user prefers **no reported-value change**: keep `elbo` = E,
rename the header's claim to what the engine does ("rows normalised; E − F =
K(K−1)/2 nat, not a bound"), add `elbo_beal` = H. Same code, only the default
name differs. Not recommended: silently keeping E as "the ELBO".

Either way the pinned test
`test_ab_hmm_reference.py::TestVariationalBayes::test_elbo_data_term_against_the_header_derivation`
flips from documenting the discrepancy to asserting whichever choice is made,
and the header gets its `// Validation:` block.

## 5. Sources — what is literature, what is ours, and the independent A/B

Asked "where does this come from" — the honest split:

**Literature.** Mean-field VB for HMMs, the bound F = E_q[log p(y, z | θ)] −
KL(q‖p) and the geometric-mean weights θ̃ = exp E_q[log θ] in the E-step:
MacKay 1997, *Ensemble learning for hidden Markov models* (Cavendish tech
report); Beal 2003, *Variational algorithms for approximate Bayesian
inference*, PhD thesis UCL, ch. 3 (VB-HMM; Ã sub-stochastic, forward-backward
run with the unnormalised weights). Dirichlet expectations E[ln θ_j] = ψ(α_j) −
ψ(Σα): Bishop 2006, *PRML*, ch. 10. VB-HMM in smFRET: vbFRET (Bronson et al.
2009, Biophys J 97:3196), ebFRET (van de Meent et al. 2014, Biophys J
106:1327). The photon-by-photon (Δt-dependent) transition model: H2MM (Pirchi
et al. 2016, J Phys Chem B 120:13065), scored by ML + ICL/BIC in H2MM_C /
burstH2MM (Harris et al. 2022, Nat Commun 13:1000) — the reason ICL/BIC are
the practical comparison here. Comparing a VB bound to the exact evidence by
importance sampling from a proposal near the posterior: Beal & Ghahramani 2003,
*The variational Bayesian EM algorithm for incomplete data*, Bayesian
Statistics 7 (they use AIS; here plain IS from the VB posterior, ESS reported).

**Ours, not literature.** (i) The extension of Beal's bound to the tick chain
observed only at photon ticks (`HMMVB.h` header, `prototype/hmm/vb.py`, PRD-011):
Jensen over the full tick path, so the sub-stochastic Ã^Δt marginalisation is
still a bound — no paper states this form. (ii) The engine's E (row-normalising
Ã before the tick power) — what `HMMVB.cpp`'s cache does; no derivation.
(iii) The closed form E − H = K(K−1)/2 nat — a first-order expansion of the
Dirichlet row mass, derived in §1 and measured in §2. Until today (i)–(iii)
had only been checked against our own NumPy transcription, i.e. against
ourselves.

**Independent A/B (2026-08-17, later).** On streams with a photon at every tick
(dt = 1) the model is exactly a categorical VB-HMM, which
`hmmlearn.vhmm.VariationalCategoricalHMM` (0.3.3) implements with Dirichlet
factors and Beal's lower bound. Same data, Dir(1) priors, same posterior seed:

| | K = 2, 3 700 ticks | K = 3, 5 500 ticks (fixture) | K = 3, 49 780 ticks (bench) |
|---|---|---|---|
| hmmlearn bound at tttrlib's converged posterior − H | 1.4e-11 | — | 2.0e-10 |
| converged posterior α, max rel diff | 4e-4 | ≤ 2e-3 (asserted) | 1.1e-4 |
| hmmlearn's optimum bound − its bound at our posterior | 1.6e-5 nat | — | 1.0e-5 nat |
| tttrlib `elbo` (E) − hmmlearn bound | 0.998 | 2.999 | 2.99906 |
| K(K−1)/2 | 1 | 3 | 3 |

So (i) is confirmed by an upstream package at Δt = 1 (the Δt > 1 step is the
Ã^Δt identity, checked numerically in the pinned test), the engine's fixed
point is Beal's to 1e-4, and (iii) holds against the upstream bound. tttrlib
runs 13× faster than hmmlearn on the bench case (128 vs 1698 ms). Permanent:
fixture `test/data/reference/hmm_vb_hmmlearn_reference.npz`
(`test/python/hmm/gen_ab_hmm_vb_hmmlearn_reference.py`, sciref venv),
`test_ab_hmm_reference.py::TestVariationalBayesAgainstHmmlearn`, benchmark
pair `hmm_vb` (`bench_sciref.py` / `competitors/bench_sciref.py` /
`check_sciref.py`).
