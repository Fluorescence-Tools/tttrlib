.. _h2mm_bva_guide:

Dynamic FRET: H2MM and BVA
==========================

tttrlib ships two photon-level tools for detecting and modelling **sub-burst
FRET dynamics** — conformational changes that happen while a single molecule is
inside the confocal volume:

``BVA`` (Burst Variance Analysis)
   A fast model-free test for dynamics. It compares the measured per-burst
   spread of the FRET proximity ratio against the shot-noise limit.

``H2MM`` (photon-by-photon Hidden Markov Model)
   A generative model that infers the number of hidden FRET states, their
   emission (FRET) values, and the transition rates between them — directly
   from photon arrival times, with no time binning.

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
   bva.compute(number_of_photons_per_slice=5)

   mean = bva.proximity_ratio_mean           # per-burst mean PR
   std = bva.proximity_ratio_std             # per-burst std of PR
   # shot-noise-limited reference line
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

   eng = tttrlib.H2MM()
   eng.set_bursts_from_filter(bf, [green, red], min_photons=10)

   # fit 1..3 states, pick the best by BIC
   best = min((eng.fit(k, n_restarts=3) for k in (1, 2, 3)), key=lambda m: m.bic())
   print(best.n_states(), best.obs_np)     # emission (FRET) per state

   path, icl = eng.viterbi_path(best)      # most-likely state per photon

The engine also provides :func:`H2MM.factory_model` and
:func:`H2MM.simulate_bursts` for generating test data, and
:func:`H2MM.set_bursts` to load pre-extracted per-burst photon streams directly.

.. _h2mm_performance:

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
