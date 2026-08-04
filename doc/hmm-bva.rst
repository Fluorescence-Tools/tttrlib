.. _hmm_bva_guide:

Dynamic FRET: HMM and BVA
=========================

tttrlib ships two photon-level tools for detecting and modelling **sub-burst
FRET dynamics** — conformational changes that happen while a single molecule is
inside the confocal volume:

``BVA`` (Burst Variance Analysis)
   A fast model-free test for dynamics. It compares the measured per-burst
   spread of the FRET proximity ratio against the shot-noise limit.

:class:`HMM` (photon-by-photon Hidden Markov Model)
   A generative model that infers the number of hidden FRET states, their
   emission (FRET) values, and the transition rates between them — directly
   from photon arrival times, with no time binning.

   The class is named for what it is; **H2MM** names the *algorithm* it runs by
   default — the maximum-likelihood photon-by-photon EM of Pirchi et al. — and
   that name is used throughout this guide when the algorithm, rather than the
   class, is meant.

Both start from a burst search (:class:`BurstFilter`) and photon streams
defined by :class:`Channel` (routing channel + micro-time windows), so they
compose with the rest of the tttrlib burst pipeline.

Burst Variance Analysis
-----------------------

Each burst is split into slices — either a fixed number of photons per slice or
fixed-duration time windows. For every slice the proximity ratio
:math:`PR = n_A / (n_A + n_D)` is computed from the donor/acceptor photon
counts, and the burst's BVA statistic is the standard deviation of :math:`PR`
across its slices. A *static* species sits on the shot-noise line
:math:`\sigma = \sqrt{p(1-p)/n}`; a species that interconverts within the burst
sits above it.

.. code-block:: python

   import tttrlib

   data = tttrlib.TTTR("example.ptu", "PTU")
   bf = tttrlib.BurstFilter(data)
   bf.set_burst_parameters(min_photons=50, window_photons=10, window_time_max=0.5e-3)
   bf.find_bursts()

   bva = tttrlib.BVA(bf)                     # reuse the filter's bursts + TTTR
   bva.set_donor([0])                        # donor routing channel(s)
   bva.set_acceptor([1])                     # acceptor routing channel(s)
   bva.compute(bf.get_bursts(), 5)           # burst bounds, photons per slice

   mean = bva.proximity_ratio_mean           # per-burst mean PR (NumPy array)
   std = bva.proximity_ratio_std             # per-burst std of PR (NumPy array)
   # shot-noise-limited reference: proximity-ratio bins and the std line
   bins, line = tttrlib.BVA.compute_static_bva_line([0.1, 0.5, 0.9], 5)

Photon-by-photon HMM (H2MM)
---------------------------

H2MM (Pirchi *et al.*, J. Phys. Chem. B 2016) models the photon stream as a
hidden Markov chain observed one photon at a time. The model
:math:`\lambda = \{\pi, A, B\}` has an initial-state vector :math:`\pi`, a
one-tick transition matrix :math:`A`, and an emission matrix :math:`B`
(:math:`B_{ik} = P(\text{stream } k \mid \text{state } i)`). Because photons
arrive at irregular macro-times, propagating the chain across a gap of
:math:`\Delta t` ticks uses :math:`A^{\Delta t}`.

.. code-block:: python

   green = tttrlib.Channel("green"); green.add_component(0, 0, 65535)
   red = tttrlib.Channel("red");    red.add_component(1, 0, 65535)

   eng = tttrlib.HMM()
   eng.set_bursts_from_filter(bf, [green, red], min_photons=10)

   # fit 1..3 states, pick the best by BIC
   best = min((eng.fit(k, n_restarts=3) for k in (1, 2, 3)), key=lambda m: m.bic())
   print(best.n_states(), best.obs_np)     # emission (FRET) per state

   path, icl = eng.viterbi_path(best)      # most-likely state per photon

The engine also provides :func:`HMM.factory_model` and
:func:`HMM.simulate_bursts` for generating test data, and
:func:`HMM.set_bursts` to load pre-extracted per-burst photon streams directly.

.. _hmm_one_molecule:

One molecule per burst — the assumption that matters most
---------------------------------------------------------

.. warning::

   A burst holding **two** molecules is a superposition of two independent
   chains, not one chain. A single-chain model has exactly one way to explain
   interleaved photons from two sources — rapid switching — so multi-molecule
   coincidence manufactures the very signal these tools exist to detect.

Measured on two **static** species at :math:`E = 0.25` and :math:`0.75`, so the
truth contains no dynamics at all and every reported transition is an artifact.
Median over 8 independent datasets:

==================  =========================  ====================
coincident bursts   apparent switching / tick   BIC picks 3 states
==================  =========================  ====================
0 %                 none (at the floor)         0 of 8
**5 %**             **7.5e-6**                  **8 of 8**
10 %                1.3e-5                      8 of 8
25 %                5.4e-5                      8 of 8
50 %                7.9e-5                      8 of 8
==================  =========================  ====================

**5% coincidence -- ordinary at typical burst concentrations -- already invents
kinetics and adds a state that is not there, in every dataset tried.** That is a
stronger confound than crosstalk or background, which bias :math:`E` by
0.03-0.05 but create neither states nor dynamics.

The intuitive failure mode is not the dangerous one. With *full* overlap the two
species stop being resolved at all -- a superposition at every timescale has no
time structure to separate, so the fit returns something built from the mixture
rather than from the species, and the states are destroyed rather than the
kinetics invented. With *partial* overlap, which is what diffusion actually
produces, the burst contains a genuine change-point that is simply not a
conformational one.

**Neither selection nor concentration is a cheap fix, and per-burst detection
does not work at all.** An earlier version of this guide reported that photon
count separates coincident bursts at AUC 0.87 and recommended filtering on it.
That was wrong twice over: the bursts were made coincident by *concatenating*
photon lists, so the coincident ones held more photons by construction, and the
number came from a single dataset. Re-measured against ground truth from
:class:`tttrlib.SimEngine` — molecules diffusing through a focus, with
``emitting_molecule()`` naming the emitter of every photon — and replicated over
independent seeds:

==================  ==================  ==================
statistic           AUC at 5.3 %        AUC at 17.7 %
==================  ==================  ==================
burst duration      0.53 ± 0.04         0.542 ± 0.006
photon count        0.53 ± 0.04         0.548 ± 0.005
peak count rate     0.47 ± 0.04         0.537 ± 0.009
==================  ==================  ==================

**None of these is a detector.** Peak count rate lands below chance in the
sparse sample, and nothing improves in the crowded one — at high occupancy the
"clean" bursts are contaminated too, so the contrast the statistic depends on
washes out and the filter is weakest where it is most needed. A statistic that
*ought* to work fails as well: a second molecule arriving part-way should step
the count rate at the moment the apparent :math:`E` changes, and the correlation
between local rate and local :math:`E` also measures at chance.

.. warning::

   **Replicate before believing an AUC here.** At 5% coincidence a single run
   holds only a handful of coincident bursts, and single-run AUCs ranged from
   **0.36 to 0.63** across seeds. One seed makes the statistic look publishable,
   another makes it anti-correlated. Every number in this section is a mean over
   independent seeds for that reason.

Selection is therefore a bad trade. Discarding the longest half of all bursts
moves contamination only from 5.3% to 4.8%, at roughly **25 clean bursts
discarded per coincident burst removed**.

Occupancy is the better lever, but it saturates:

================  ====================  ==================
population        bursts / 1e6 ticks    coincident
================  ====================  ==================
0.125             60                    2.5 %
0.25              115                   5.3 %
0.5               239                   9.3 %
1.0               530                   17.7 %
================  ====================  ==================

Halving the occupancy costs a doubling of acquisition time for the same burst
yield. It buys a factor of ~1.9 where the sample is crowded (17.7% → 9.3%) but
only ~2.1 at the sparse end (5.3% → 2.5%), and spurious switching remains
measurable even at the lowest occupancy tried. **There is no setting at which
coincidence goes away**, so this is an acquisition-design problem rather than an
analysis one: choose the occupancy before measuring, expect a floor, and treat a
marginal extra state as suspect whenever the sample was crowded.

The coincidence *rate* is predictable even though the per-burst label is not.
Burst arrivals are Poisson, so the coincident fraction is
:math:`1 - e^{-\lambda \tau}` — but :math:`\tau` is the **transit** time, not
the detected burst duration. A detected burst is only the above-threshold part
of a transit, and a molecule keeps emitting sub-threshold photons into its
neighbours' bursts for far longer, so using the burst duration underestimates
coincidence by more than an order of magnitude. Take :math:`\tau` from the FCS
diffusion time.

All of this is worked end to end, against engine ground truth, in
``plot_hmm_coincidence.py``.

Why there is no per-state brightness read-out
----------------------------------------------

A recurring request is to report how bright each state is. Only the *ratio*
between two states is identifiable — conditioning on photon arrivals removes any
overall scale — and the natural estimator is
:math:`(\text{photons in state}) / (\text{time in state})`. The PSF envelope
was expected to cancel from that ratio, since a molecule's conformational state
and its position in the focus are independent.

**It does not cancel**, and that is why the feature is not shipped. Two
independent errors bite, measured on a two-state system with a true ratio of
3.00 and replicated over seeds:

===========================  ==========================  ===============
regime                       estimate (truth 3.00)       error source
===========================  ==========================  ===============
true tick-level path         3.00–3.09 at every rate     none — unbiased
slow switching, flat field   2.96 ± 0.02                 none
slow switching, 55× field    2.66 ± 0.06                 envelope
slow switching, 10⁶× field   2.19 ± 0.07                 envelope
fast switching, flat field   1.99                        gap attribution
fast switching, 55× field    1.65                        both
===========================  ==========================  ===============

The first row matters most: with the **true tick-level** path the estimator is
unbiased at every switching rate, so there is no information-theoretic ceiling.
What is lost is *attribution* — the state is known only at photons, so a gap
gets charged to the state seen at its start even when the chain switched inside
it. That error grows with switching rate.

The envelope error is separate and worse, because it is present at **any**
switching rate and grows monotonically with the depth of the focus. A guard on
switching rate — the obvious way to ship this safely — would therefore have left
users with a number that looks trustworthy and is 10–27% low.

The tick-level bridge already in :func:`HMM.sample` does not rescue it either:
it conditions on the endpoint states but not on the fact that the gap contained
**no photons**, and an empty gap is evidence of the dim state. Using that
evidence is precisely a rate-aware likelihood, which for diffusing data would
attribute the PSF transit to state changes — the confound described above. The
circularity is real, and the honest conclusion is that per-state brightness is
not a quantitative read-out for freely-diffusing data.

.. note::

   This does not contradict the invariance described earlier. The envelope is
   genuinely harmless to what the HMM infers, because :math:`P(\text{symbol} |
   \text{state})` does not depend on the photon rate. It is a brightness
   read-out *bolted on afterwards* that the envelope breaks.


Checking a converged fit with phasors
-------------------------------------

A fit can converge, return the right :math:`E`, and still rest on the wrong
decay model. The **phasor** — the sine and cosine moments of a micro-time
histogram at one frequency — gives a check that fits nothing and so cannot be
talked into agreeing with the model it is checking.

The recipe needs no new API. :func:`HMM.evaluate` returns
``gamma_obs``, the posterior-weighted per-state micro-time histogram, and the
fitted model carries its own table; run
:func:`tttrlib.DecayPhasor.compute_phasor_bincounts` on both and compare::

    ev = eng.evaluate(fit)
    data  = ev.gamma_obs_np(n_states)[k].reshape(n_streams, n_bins)[0]
    model = fit.obs_micro_np[k, 0] / fit.obs_micro_np[k, 0].sum() * data.sum()
    # (1, 0) is the identity IRF phasor, and the default -- (0, 0) is not
    # "no IRF" but a division by zero, and now raises rather than giving nan.
    gd, sd = DecayPhasor.compute_phasor_bincounts(counts(data),  freq)
    gm, sm = DecayPhasor.compute_phasor_bincounts(counts(model), freq)

Measured on data generated with a **multi-exponential donor** and fitted once
with a mono-exponential spec and once with the generating one, over five
datasets:

====================  ====================  ====================
fitted model          recovered :math:`E`   phasor distance
====================  ====================  ====================
mono-exponential      0.247 / 0.700         **0.0435 ± 0.0040**
multi-exponential     0.247 / 0.700         **0.0095 ± 0.0017**
truth                 0.25 / 0.70           —
====================  ====================  ====================

Both fits recover :math:`E` to within 0.003, so the headline number gives no
warning; the phasor distances differ by 4.6× with no overlap across datasets.
The reason is structural — :math:`E` is set by the *stream split* while the
misspecification lives in the *decay shape*.

**Use it as a relative diagnostic.** The correctly specified model does not
score zero, and should not be expected to: the distance carries sampling noise,
and binning and truncation of the micro-time window move data and model points
slightly differently. For the same reason, distance from the universal
semicircle is *not* a clean multi-exponentiality test here — a binned, truncated
mono-exponential does not lie exactly on the circle either. Compare candidate
parameterisations on the same data and prefer the smaller distance.

The likelihood remains the thing that *ranks* models, and it does rank these
correctly (+91 nats for the multi-exponential fit). The phasor adds something
the likelihood cannot: it says **which state** is failing and in which direction
on the plane, from a statistic that never saw the model.

.. note::

   Use ``gamma_obs`` — the posterior-weighted histogram — not a hard Viterbi
   assignment. Hard assignment discards the fit's own uncertainty and pulls the
   data phasor toward the model that produced the assignment, which makes a
   badly specified model look self-consistent.

Worked in ``plot_hmm_phasor_diagnostic.py``.


Error bars on the maximum-likelihood path
------------------------------------------

:func:`HMM.fit` returns a point estimate and no uncertainty. :func:`HMM.sample`
gives a calibrated posterior but needs priors and costs sweeps. Between them is
the **non-parametric bootstrap**: bursts are conditionally independent given the
model, so resampling them with replacement and refitting gives a frequentist
interval with no prior at all.

**No new API is needed.** A replicate is the same dataset with its burst list
resampled, which both loaders already accept — including duplicate rows, which
is exactly what sampling with replacement means::

    rows = bursts[rng.integers(0, len(bursts), size=len(bursts))]
    eng = tttrlib.HMM()
    eng.set_bursts_from_tttr(data, rows, channels, min_photons=40)

On 313 bursts and ~32k photons that runs at about **7 ms per replicate**, so a
200-replicate interval costs a couple of seconds and copies no photon data.

Measured coverage of a nominal 95% interval, 40 datasets × 100 replicates:

=========================  ==================  ==================
interval                   pooled coverage     verdict
=========================  ==================  ==================
burst bootstrap            **94.4 % ± 1.8 %**  honest
``posterior_sd_analytic``  ~63 %               **do not quote**
=========================  ==================  ==================

The analytic gap is not a defect — it is what that function's own
documentation says. Conditioning on the state path drops
:math:`\mathrm{Var}(E[\theta \mid y, \text{path}])`, so it is a *lower bound*
on the width, measured at roughly half. Quoting it as an error bar overstates
precision about twofold.

Two sample sizes govern the bootstrap, each with a floor:

- **bursts** — coverage runs 90 % / 94 % / 93 % at 20 / 40 / 120 bursts, so
  below a few tens of bursts the interval is mildly optimistic. That is a
  small-sample property of the percentile bootstrap, not of this engine.
- **replicates** — coverage runs 89.4 % at 40 and 94.4 % at both 100 and 250.
  **Use at least 100**; there is nothing to gain past a few hundred.

.. warning::

   **Order the states before summarising.** States are exchangeable, so
   replicate fits can return the same model relabelled. Without a canonical
   order the spread measures label switching and every interval comes out far
   too wide — the same trap that applies to Gibbs draws.

Which interval to use: :func:`HMM.sample` when priors are wanted or the
posterior shape matters; the bootstrap for the plain maximum-likelihood path;
the analytic width only for a quick relative comparison between parameters.
Worked in ``plot_hmm_bootstrap.py``.

ALEX stoichiometry filtering is the other standard route. It is independent of
all of these statistics — it detects a donor-only and an acceptor-only molecule
overlapping by their combined :math:`S`, not by their brightness — so it
composes with them and is likely the better lever where ALEX is available.

Modelling coincidence instead requires a product state space over the two
molecules, which is not implemented.

.. note::

   This is specific to freely-diffusing burst data. The single-photon smFRET
   literature that this engine otherwise follows (Sgouralis & Pressé) analyses
   **immobilized** molecules, where coincidence does not arise — so it offers no
   ready-made treatment to borrow here.

.. _hmm_rate_blind:

Photon rate is ignored, and that is deliberate
-----------------------------------------------

The likelihood scores :math:`P(\text{symbol} \mid \text{state})` — a
distribution *conditional on a photon having arrived*. Macro-times only
propagate the chain through :math:`A^{\Delta t}`; the photon **rate** carries no
likelihood weight. That reads like an omission and is a robustness property.

A diffusing molecule's brightness depends on its position in the focal spot, so
the photon rate rises and falls during every burst as it transits the PSF. A
state-dependent rate term would attribute that transit to state changes — the
same way multi-molecule coincidence does above. Conditioning on photon arrivals
removes the envelope entirely, because :math:`P(\text{symbol}\mid\text{state})`
does not depend on the overall rate.

Measured on a **static** molecule (one state, no dynamics), BIC scan over
:math:`k = 1 \ldots 3`, four datasets each:

====================  =================  =====================
brightness envelope   intensity range     BIC picks
====================  =================  =====================
flat (control)        1x                  1 state, 4 of 4
Gaussian PSF          26x                 1 state, 4 of 4
narrow PSF            **425,000x**        1 state, 4 of 4
====================  =================  =====================

A 425,000-fold intensity swing across the burst produces no spurious state and
no more apparent switching than the flat control.

.. warning::

   **The invariance covers position, not states.** It holds when brightness
   varies for reasons unrelated to the hidden state — the PSF transit above. If
   the *states themselves* differ in brightness, gap durations become
   informative about the state, conditioning on the arrival times discards that
   information, and the **kinetics** are biased. Emission values are not:

   ==================  ==============  =============  ====================
   true switching      brightness       k01 / k10      truth
   ==================  ==============  =============  ====================
   0.0003              1:1              1.09           1.0
   0.0003              3:1              1.13           1.0
   0.0030              1:1              0.95           1.0
   0.0030              **3:1**          **1.71**       1.0
   0.0100              1:1              0.98           1.0
   0.0100              **3:1**          **2.69**       1.0
   ==================  ==============  =============  ====================

   With equal brightness the rates stay symmetric even when switching is fast.
   With a 3:1 brightness ratio they reach 2.69, so the inferred *equilibrium
   populations* are wrong by that factor. Recovered :math:`E` stays correct
   throughout (0.25 / 0.75). The bias grows with the switching rate, so it is
   worst exactly where kinetics are the point.

   Two consequences worth stating plainly: states differing **only** in
   brightness are invisible to this likelihood, and reported rates should be
   treated with caution whenever the states are known to differ in brightness —
   a dark or partially quenched state, for instance.

.. warning::

   Do not "fix" this by adding a state-dependent rate to the likelihood. It is
   sound only where brightness is constant during observation — immobilized or
   surface-tethered molecules — which is why the generator-based formulations in
   the literature can use it and this engine should not. For diffusing data,
   making rate informative requires joint inference of the trajectory, which is
   a switching state-space model rather than an HMM. The bias above is a real
   cost of that choice, not an argument against it: modelling rate without the
   trajectory trades a kinetics bias for a worse one.

Background: predictable dilution
--------------------------------

Uncorrelated background — scatter, or a constant "dirt" pattern in the decay —
arrives at a steady rate regardless of the molecule's state, so it dilutes every
state's emission toward the background's own colour. The dilution is **exact**:

.. math::

   E_{\text{obs}} = (1 - f)\,E + f\,E_{\text{bg}}

Measured against that formula, with background colour 0.5 and true states at
0.25 / 0.75:

=============  =================  ==================
background     measured E         predicted
=============  =================  ==================
5 %            0.260 / 0.735      0.263 / 0.738
15 %           0.288 / 0.716      0.288 / 0.713
30 %           0.323 / 0.675      0.325 / 0.675
=============  =================  ==================

Transition rates are essentially unaffected, and the state count is usually
right, though BIC occasionally picks one extra. Because the relationship is
exact, background is *correctable* once :math:`f` is known — which is what
:attr:`HmmEmissionSpec.background_fraction` and its per-stream ``background``
pattern are for. On a micro-time alphabet the pattern matters as well as the
fraction: scatter follows the IRF, while "dirt" carries a long lifetime, and the
two dilute different parts of the decay.

Per-state **brightness** can still be read out *after* decoding, since state and
position are independent and the unknown envelope cancels in a ratio. That is a
post-hoc measurement, not a likelihood term, and it is only trustworthy while
switching is slow compared with the burst transit.

.. _hmm_micro_alphabet:

Adding the lifetime axis
------------------------

A symbol does not have to be a detection stream. Load the data with
``n_micro_bins > 1`` and each photon becomes the **product symbol**
``stream * n_micro_bins + micro_bin``, so the emission table is
:math:`P(\text{stream}|k) \cdot f_k(\text{bin})` — a state is then constrained by
*when* its photons arrive as well as by *where*:

.. code-block:: python

   eng = tttrlib.HMM()
   eng.set_bursts_from_filter(bf, [green, red], min_photons=10, n_micro_bins=32)
   eng.get_n_symbols()        # 2 * 32

This costs the recursions nothing. They read ``obs[i*p + y]`` with ``p`` a
runtime integer and never ask what ``y`` means, so the micro-time axis is a
wider emission table rather than a second code path — and ``n_micro_bins == 1``
is bit-identical to the stream-only engine.

**Why it is worth the width.** Intensity alone cannot tell a *dark acceptor*
from real FRET: both lower the acceptor fraction. Only transfer also shortens
the donor lifetime. On simulated data where the two states have an identical
donor/acceptor ratio by construction, per-photon Viterbi accuracy against the
known state path is 0.53 — chance — on the stream alphabet, and 0.77 once
micro-time is included.

Choosing ``n_micro_bins``
~~~~~~~~~~~~~~~~~~~~~~~~~

A micro-time is a TAC channel, so it is *already* discrete; ``n_micro_bins``
applies a **second, coarser** binning on top. That is a compute knob, not a
modelling choice, and specifically it is not about lookup: the E-step reads
``obs[i*p + y]`` once per photon whatever the resolution. What scales is the
**table build**, which the M-step repeats for every candidate lifetime.

Because each bin's probability is the exact *integral* over the bin (see the IRF
note below), coarsening aggregates rather than approximates. Simulating at 4096
native channels and then binning the data down:

=========  ================  ==============  ===============  ==========
fit bins   bin width (ns)    width / IRF     fitted τ (ns)    build
=========  ================  ==============  ===============  ==========
16         1.000             11.1            3.84 / 2.05      0.01 ms
128        0.125             1.4             3.85 / 2.06      0.02 ms
1024       0.016             0.2             3.85 / 2.06      0.11 ms
4096       0.004             0.04            3.85 / 2.06      0.45 ms
=========  ================  ==============  ===============  ==========

(truth 4.0 / 2.0 ns). Even one-nanosecond bins — eleven times the IRF width —
recover the lifetimes, because a **mono-exponential** lifetime is a *slope*
across the whole decay rather than a narrow feature.

.. warning::

   **That robustness does not carry to multi-exponential decays.** Separating a
   fast component from a slow one depends on the first few bins, so the cost
   scales with *bin width ÷ fastest lifetime*. Photons needed to resolve a 10%
   change, 8 bins (2 ns) against native resolution:

   =====================  ==============  ==============  =============
   quantity               τ_fast = 0.3    τ_fast = 1.0    τ_fast = 3.0
   =====================  ==============  ==============  =============
   fast lifetime          1.65x           1.86x           1.05x
   fast amplitude         **2.7x**        1.2x            1.04x
   =====================  ==============  ==============  =============

   Negligible when the components are comparable (τ_fast = 3 against a 4 ns
   slow), and expensive when they are well separated — which is exactly the case
   a spectrum is fitted for. Size the bins against the **fastest component**,
   not against the slow one and not against the IRF.

   Note also the absolute scale: resolving a 10% change in a 0.3 ns component
   needs ~41 000 photons *even at native resolution*, and its amplitude ~4 900.
   Fast components are expensive to pin down however finely the axis is binned,
   which is a property of the physics rather than of the discretisation.

Fine binning otherwise earns its cost only when something **narrow** is being
estimated: IRF position or width, or a lifetime comparable to the bin width. It
is not needed merely because the instrument offers 4096 channels.

.. note::

   This supersedes earlier guidance to default to 1024 bins. That was measured
   on a table built by *point-sampling* the decay, where coarse bins carried a
   real discretisation bias; bin integration removed it, and with it the reason
   to pay for fine bins.

Building the emission table from lifetimes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

:class:`HmmEmissionSpec` generates the table from a per-state, per-stream
lifetime spectrum instead of leaving all ``n_streams * n_micro_bins`` columns
free:

.. code-block:: python

   spec = tttrlib.HmmEmissionSpec.uniform(2, 2, 32, dt=0.5, tau=4.0)
   spec.set_spectrum(0, 0, tttrlib.HmmLifetimeSpectrum(4.0))       # donor, dark acceptor
   spec.set_spectrum(1, 0, tttrlib.HmmLifetimeSpectrum(2.0))       # donor, E = 0.5
   spec.irf_center, spec.irf_fwhm = 1.2, 0.09    # analytic Gaussian IRF, ns
   spec.background_fraction = 0.05

   # factory_model(n_states, n_symbols, trans_scale, seed, n_micro_bins)
   model = tttrlib.HMM.factory_model(2, eng.get_n_symbols(), 1e-4, 0, 32)
   spec.set_model(model)

The parameter count is the point. A free categorical over a 4-stream,
1024-bin alphabet carries 4095 numbers per state and EM does not find its good
optimum; a spectrum carries a handful whatever the binning, and does. The
decays are evaluated through :class:`SimDecay`, so the object that *draws*
micro-times in the simulator is the object that *scores* them here.

**The IRF has two routes.** ``irf_center`` / ``irf_fwhm`` is the analytic one: a
photon's micro-time is the sum of the memoryless excited-state time and whatever
the instrument adds, so its density is an exponential convolved with a Gaussian
— the exponentially-modified Gaussian — and each bin's probability is a
difference of its CDFs. That is *exact at any resolution*: a 32-bin table equals
an 8192-bin one summed over the bins it merges. Passing a sampled pattern to
``set_irf`` instead is the route for a **measured** IRF of arbitrary shape, and
costs a discrete convolution plus its aliasing (~1.7e-3 at 256 bins, needing
~4096 bins to reach 1e-6). Setting both is rejected — it would convolve the
Gaussian in twice.

.. _hmm_counting_statistics:

Counting statistics: where they bite, and where they do not
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Scoring needs no Poisson term, and this is structural rather than an
approximation.** The likelihood is a product over photons of
:math:`P(\text{symbol}\mid\text{state})`; no micro-time histogram is ever
formed, so there are no bin counts to carry Poisson noise. Conditioned on the
photon count :math:`N`, a Poisson likelihood factorises as

.. math::

   P(\{n_k\}) = \underbrace{\mathrm{Poisson}(N)}_{\text{photon rate}}
                \times \underbrace{N!\textstyle\prod_k f_k^{n_k}/n_k!}_{\text{what this engine scores}}

so the counting statistics *are* handled exactly. Binning resolution is a
scoring-accuracy knob with no statistical cost. The one piece genuinely omitted is the information in :math:`N`
itself: H2MM's macro-times only propagate the chain, so a state's **brightness**
carries no likelihood weight. Making it count needs a state-dependent
:math:`\lambda_k` and an inhomogeneous-Poisson term per gap — a likelihood
extension, not a correction to what is here.

**The problem is in estimation, and it is structural.** Micro-time bins are not
free parameters — they are one smooth decay, of about two parameters, sampled
onto the TAC grid. Fitting them as independent columns discards that structure
and admits models no decay can produce: an exact zero in the *interior* of an
exponential, declaring a photon impossible at 3 ns while allowing it at 2 and 4.

So the following table should be read as *free parameters per state*, not as a
bin threshold — more bins only widen a family that was already wrong. Measured
on two states whose only difference is the donor lifetime (20 bursts x 300
photons):

=========  ==================  ==================  ==================
bins       photons/bin         free ``fit()``      ``HmmEmissionSpec``
=========  ==================  ==================  ==================
16         188                 0.715               0.784
32         94                  0.531               0.794
128        23                  0.533               0.797
1024       2.9                 0.521               0.797
=========  ==================  ==================  ==================

(per-photon decoding accuracy against the known state path; 0.5 is chance).
The free fit collapses between 16 and 32 bins and leaves ~46% of its emission
table *exactly* zero — a sparse histogram's MLE, asserting with infinite
confidence that no photon can arrive there. The spectrum is flat throughout.

**Smoothing is not the fix.** Dirichlet restraints on the emission rows remove
every zero, so the :math:`\log 0` hazard genuinely disappears — and accuracy
stays near chance (0.55). The obstacle is the parameter count, not the
sparsity. Cutting the parameters is what works.

.. warning::

   **A free optimiser can destroy a correct emission table.** This is not only
   a search failure. Started at the *exact generating model*, free EM sometimes
   walks away from it: over four macro-time seeds × four bin counts, three of
   sixteen runs fell from ~0.78 per-photon accuracy to ~0.51, converging to a
   "decay" with holes in its interior. The collapse is **not monotone in bins**
   (128 fine, 256 collapsed, 512 mixed, 1024 fine), so there is no safe bin
   count to retreat to — the family itself is wrong.

   So on a product alphabet, never use :func:`HMM.fit` or a bare
   :func:`HMM.optimize` — both re-estimate the emission freely.

Fitting with a parameterised emission
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Pass the spec to :func:`HMM.optimize` and the M-step re-fits *its* parameters
instead of the ``n_streams * n_micro_bins`` free columns:

.. code-block:: python

   spec = tttrlib.HmmEmissionSpec.uniform(2, 2, 256, dt=0.0625, tau=3.0)
   init = tttrlib.HmmModel(prior, trans, spec.build())
   init.n_micro_bins = 256

   fit = eng.optimize(init, 200, 1e-9, 1e-12, True, False, None, None, spec)
   spec.spectrum[0].lifetimes[0]      # the fitted lifetime, read back out

The M-step factorises exactly, so this costs almost nothing: with
``obs[i][k,b] = p_ik · f_ik(b)``,

.. math::

   Q = \underbrace{\sum_{i,k} \Big(\sum_b g_{i,k,b}\Big) \log p_{ik}}_{\text{closed form}}
     + \underbrace{\sum_{i,k} \sum_b g_{i,k,b} \log f_{ik}(b)}_{\text{1-D search per } (i,k)}

The stream split keeps the categorical M-step's closed form; only the lifetime
needs a bounded golden-section search, one scalar per (state, stream).

On the configuration where free EM falls from 0.775 to 0.509, this reaches
**0.776 starting from a deliberately wrong seed** (8.0 / 0.8 ns against a truth
of 4.0 / 2.0), and the fitted table has no interior holes by construction.

.. note::

   Restraints and constraints on ``obs`` do not apply here — the family is
   itself the constraint, and a far stronger one. A prior on a *lifetime*
   belongs on the lifetime, not on the table it generates. SQUAREM is also
   disabled, because an extrapolated table need not be reachable from any
   parameters. Currently only mono-exponential components are re-fitted; a
   spectrum with several components keeps its shape and contributes through the
   stream split.

.. note::

   This is the same problem, and the same class of answer, that the **phasor
   transform** provides elsewhere: a phasor coordinate is a mean over photons —
   an unbinned sufficient statistic — so it never forms the sparse histogram in
   the first place. For scoring, tttrlib's HMM already has that property by
   construction, and a per-photon phasor would be a lossless reparameterisation
   of the micro-time that adds nothing to the likelihood. Where a phasor *is*
   useful here is upstream: as a cheap model-free lifetime estimate to seed a
   :class:`HmmEmissionSpec`, and as a diagnostic for whether a fitted state sits
   on the universal semicircle.

Measured decays: patterns, not spectra
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A decay usually arrives as a **measured pattern**, not as amplitudes and
lifetimes — a donor-only reference, a scatter pattern, an IRF. That is why
:class:`SimDecay` treats a pattern as its first-class representation and the
multi-exponential helpers as optional constructors, and
:class:`HmmEmissionSpec` accepts the same:

.. code-block:: python

   spec.set_pattern(0, 0, donor_only_decay)     # any length; e.g. 4096 TAC channels
   spec.set_pattern(1, 0, quenched_reference)

Patterns are aggregated onto the emission axis, which is **exact** — a bin's
probability *is* the sum of the source channels inside it — so an
instrument-resolution decay can be passed straight in and coarsened by
``build()``. Verified against the analytic table to 1e-17.

**A supplied pattern is the shape, not a starting guess.** Neither the fit nor
the sampler touches it; only the stream split stays free. That is *more* robust
than fitting a lifetime rather than less, because there are no decay parameters
left to be unidentifiable — and it sidesteps the multi-exponential binning cost
above entirely, since nothing about the shape is being estimated. On the
dark-state example it decodes marginally better than fitting the lifetimes
(0.781 against 0.776), simply because the shape is known.

This is the natural route when reference decays are available: measure the
donor-only and quenched decays from pure samples, hand them over, and let the
HMM fit only the mixture — which is the pattern-fitting approach familiar from
MFD and PDA analyses.

Refining the physics against the photons
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A fixed pattern is the right answer when reference decays are measured. When
they are not, the thing to refine is **not the pattern** — that is the free
categorical again, with all its failure modes — but the *physical parameter that
generates it*. :func:`HMM.evaluate` is the contract for that: it returns one
E-step's sufficient statistics, so an external optimiser can take over the
M-step entirely::

    while not converged:
        ev = eng.evaluate(model)        # C++: one forward-backward pass
        A  = row_normalize(ev.xi)       # closed-form M-step for the kinetics
        R  = refine(R, ev.gamma_obs)    # 1-D search per state -- the physics

Nothing physical enters the engine. In the worked example
(``plot_hmm_distance_refinement.py``) each of three states is a **distance
distribution**, and 30 000 photons recover

===================  ===============  ==========================  ============
quantity             start            recovered                   truth
===================  ===============  ==========================  ============
distances (Å)        35 / 55 / 75     **42.11 / 52.68 / 63.95**   42 / 52 / 64
:math:`k_{01}`       1e-3             0.00097                     0.00080
:math:`k_{12}`       1e-3             0.00059                     0.00060
===================  ===============  ==========================  ============

with the log-likelihood increasing monotonically — which is what distinguishes a
genuine EM step from a heuristic run alongside one.

Two things this makes concrete. **The E–τ coupling**: one scalar per state sets
both the acceptor fraction and the donor decay rate, which is precisely what a
free table cannot respect and why a dark acceptor is separable from real
transfer. And **why the fitted quantity is a distance rather than a lifetime**:
a state is a *distribution* over distances, so its donor decay is a sum of
exponentials across quadrature nodes, which no single lifetime can represent.

The loop accepts any parameterisation with a tractable :math:`Q` — a different
distance distribution, a multi-exponential donor, a crosstalk-aware stream split
— and nothing below it changes.

A blinking acceptor, and the limit of what is separable
-------------------------------------------------------

``plot_hmm_blinking_acceptor.py`` runs that same loop on the hardest confusion in
two-colour smFRET. A **dark acceptor** emits no acceptor photons, so it reports
:math:`E = 0`; a **distant** acceptor reports a small :math:`E`. By intensity the
two are the same observation, and they differ only in the donor lifetime — a dark
acceptor leaves the donor unquenched, a distant one still drains it.

The example makes every part of that realistic at once: a **multi-exponential
donor** (:math:`a = 0.6/0.4`, :math:`\tau = 3.8/1.4` ns), quenched
**homogeneously** so that :math:`1/\tau_{i,DA} = 1/\tau_i + k_{FRET}(R)`; three
conformations, each a **Gaussian distance distribution** of width 6 Å; and an
acceptor blinking on a **10 µs** timescale, faster than a burst, so it toggles
several times *within* the observation. A state's donor decay is then a double
sum — 2 components × 15 quadrature nodes = **30 exponentials** — generated from
one scalar.

The state space is a product, *conformation* × *photophysics*, the same
superstate construction the photon-by-photon literature uses. While the acceptor
is dark the conformation is unobservable, so it collapses to **one** dark state:
three dark states would be emission-identical and no amount of data would
separate them.

====================  ===============  ==========================  ============
quantity              start            recovered                   truth
====================  ===============  ==========================  ============
distances (Å)         45 / 50 / 58     **41.0 / 50.6 / 60.6**      40 / 52 / 65
blink-off rate        0.10 / tick      **0.0986**                  0.10
dark-state recall     —                0.91                        —
dark-state precision  —                0.72                        —
====================  ===============  ==========================  ============

**The 65 Å state comes back at 61 Å, and that is the physics rather than the
fit.** At 65 Å the transfer is weak, so the state's donor decay sits close to the
unquenched one and its separation from the dark state rests on a small lifetime
difference; pulling the distance in buys donor photons the dark state would
otherwise have to explain. The one-sided confusion is why precision (0.72) trails
recall (0.91).

That is an identifiability limit of the *measurement*, and it generalises: **a
dark acceptor and a sufficiently distant one converge as** :math:`R` **grows**,
and no estimator separates them once the quenching falls below the lifetime
resolution. A distance reported out there needs the interval :func:`HMM.sample`
gives, not a point estimate.

.. warning::

   **No IRF is applied to a supplied pattern.** A measured decay already
   contains the instrument response, so convolving again would broaden it twice.
   ``irf`` and ``irf_fwhm`` therefore affect only the cells still described by a
   spectrum — which means the two can be mixed in one spec (a measured donor
   reference alongside an analytic acceptor decay) without the IRF being applied
   inconsistently.

.. note::

   ``HmmEmissionSpec`` knows no photophysics — no Förster radius, no linker
   width, no crosstalk matrix. A state is described by what scoring needs, a
   lifetime spectrum and a stream split; the map from a structure onto those
   quantities lives outside tttrlib and enters as a prior on the parameters.

.. important::

   ``viterbi_path`` answers "what is the single most likely state sequence".
   If your next step is instead "how do the photons distribute over the
   states" — occupancies, per-state decays, populations — the argmax is a
   **biased** answer to that question: it inflates well-separated states and
   erases ambiguous and short-lived ones. See
   :ref:`hmm_state_decoding` for the per-photon posterior and the two
   decoders that reproduce the distribution faithfully, plus how to persist a
   decoded assignment.

.. _hmm_performance:

Why the tttrlib H2MM engine is fast
-----------------------------------

The reference implementation of H2MM is Harris's ``H2MM_C`` (a pthreads C
library). The tttrlib engine implements the same Baum-Welch EM and Viterbi
decoding but reaches the **identical** optimum several-fold faster. On a
simulated 3-state, 200k-photon dataset (macOS, Apple silicon, CPU only):

============================  ==========  ===================
Engine                        EM to tol   vs. tttrlib
============================  ==========  ===================
``H2MM_C`` (reference C)         842 ms    1.0× (baseline C)
ChiSurf numba                    402 ms    —
**tttrlib (plain EM)**           345 ms    **2.4× vs H2MM_C**
**tttrlib (SQUAREM)**            103 ms    **8.2× vs H2MM_C**
============================  ==========  ===================

Viterbi decoding runs at ~150 M photons/s. The speed comes from four
algorithmic choices, not from micro-optimising the reference's inner loops:

**1. Sparse unique-Δt caches.**
   Between two consecutive photons the chain performs :math:`\Delta t`
   unobserved transitions, so the E-step needs :math:`A^{\Delta t}` and the
   expected-transition tensor :math:`\rho(\Delta t)`. ``H2MM_C`` builds a
   **dense** cache for *every* tick from 1 to the largest inter-photon gap in
   the dataset — so a single long dark gap forces an
   :math:`n_{states}^4 \times \Delta t_{max}` allocation and that many tensor
   builds. tttrlib builds the caches only for the **observed unique** gaps
   (typically a few dozen), keyed by a per-photon slot index. Cost scales with
   the number of *distinct* gaps, not the clock range.

**2. Deferred ρ contraction (O(N·n²) hot loop).**
   The naive E-step contracts the full :math:`\rho` tensor against the
   forward/backward messages at *every photon*, costing
   :math:`O(N \cdot n_{states}^4)` and streaming the large tensor from memory
   once per gap. tttrlib instead accumulates a small per-slot transition weight
   ``W[slot,k,m]`` in an :math:`O(N \cdot n_{states}^2)` hot loop, and performs
   the single expensive contraction
   :math:`\xi = \sum_{slot} W \cdot \rho` **once** in the serial reduction
   (:math:`O(n_{slots} \cdot n_{states}^4)` with :math:`n_{slots} \ll N`).
   Mathematically identical, but the per-photon work drops from :math:`n^4` to
   :math:`n^2` and the :math:`\rho` cache is read only :math:`n_{slots}` times.

**3. SQUAREM acceleration.**
   Plain Baum-Welch converges linearly. tttrlib wraps the EM map in SQUAREM
   (Varadhan & Roland 2008, scheme S3), a squared-extrapolation accelerator with
   a monotonicity safeguard. It reaches the *same* EM fixed point in far fewer
   maps — 54 vs. 172 on the benchmark above — which is the difference between
   the 345 ms and 103 ms rows. ``H2MM_C`` has no equivalent.

**4. Allocation-free, persistently-threaded kernels.**
   The :math:`A^{\Delta t}` / :math:`\rho` caches are rebuilt every EM map, so
   the pair-power (binary-exponentiation) build reuses per-thread scratch
   buffers instead of allocating on each call — this alone cut the cache-build
   time ~4×. Bursts are processed in parallel over a **persistent** fork-join
   thread pool (``std::thread``, no OpenMP dependency) created once per
   optimisation, with thread-local Baum-Welch accumulators to avoid false
   sharing. ``H2MM_C`` spawns and joins its pthreads *every* EM iteration.

The engine is a direct C++ port of the ChiSurf numba engine and reproduces its
single-EM-iteration log-likelihood and full-convergence Viterbi path to
~1e-9, so results are numerically interchangeable.

**Approximate float32 fast mode.** ``optimize(..., single_precision=True)`` runs
the :math:`A^{\Delta t}` / :math:`\rho` caches and the forward-backward hot loop
in float32 (model parameters and Baum-Welch reductions stay double), roughly
halving the memory bandwidth of the caches. Because the log-likelihood then
carries float32 round-off (~1e-2), the convergence threshold is floored at 1e-3.
The benefit grows with the cache size (more states, more distinct gaps), so it is
intended for exploratory fits on very large datasets, not for final numbers.
(ChiSurf's other approximate estimator — a neural *surrogate* that predicts a
model in one forward pass — is not ported: it needs a trained network and so
falls outside tttrlib's dependency-free C++ core.)

Reproducing the benchmark
-------------------------

The comparison lives under ``benchmarks/``. It writes one shared simulated
dataset and a fixed initial model, then optimises the identical problem in each
engine's isolated environment::

   cd benchmarks
   python bench_h2mm.py                                   # tttrlib (base env)
   .venvs/h2mm_c/bin/python competitors/bench_h2mm_c.py       # reference C
   .venvs/h2mm_numba/bin/python competitors/bench_h2mm_numba.py  # numba

(Build the competitor venvs first with ``benchmarks/build_envs.sh``.)
