.. _fit_guide:

Fluorescence Decay Fitting
==========================

Every decay fit in tttrlib is reached the same way: name the model, hand it the
measurement, read the answer back. There is one interface, not one per
estimator, and what a model's parameters, setup values and results *mean* is
described in the registry rather than in the shape of a function signature.

.. code-block:: python

   import tttrlib

   setup = tttrlib.setup_vector("fit23", dt=0.032, period=32.0)
   fit = tttrlib.DecayFit2("fit23", setup, irf)

   problem = tttrlib.DecayFitProblem(2, n_bins, 0.032)
   problem.irf = tttrlib.VectorDouble(irf)
   problem.background = tttrlib.VectorDouble(background)
   problem.data = tttrlib.VectorDouble(data)

   constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, -1, -1, -1]))
   outcome = fit.fit([2.0, 0.0, 0.38, 1.0], constraints, problem)

   print(outcome.parameters[0])                                   # the lifetime
   print(tttrlib.results_as_dict("fit23", list(outcome.results)))  # named results

The worked version of this, with plots and a batch fit, is
:doc:`auto_examples/fluorescence_decay/plot_decay_fit_interface`.

The four pieces
---------------

``DecayFit2``
   The model, built by registry name. It is immutable and caches what it derives
   from the IRF, so build it once and reuse it for many decays — with the
   corollary that **changing the IRF means building a new model**, which is
   enforced rather than left to discipline.

``DecayFitProblem``
   The measurement — data, instrument response, background, plus any fixed
   reference patterns. The channel count is stored explicitly, so single-channel
   and polarisation-resolved data are the same type.

``DecayFitConstraints``
   What the optimiser may move (see `Fixing, freeing and linking`_).

The outcome
   ``parameters``, ``results`` and ``objective``. The fitted curve is left in
   ``problem.model`` rather than returned, because carrying it per row would
   make a batch result thousands of columns wide.

Which fit should I use?
-----------------------

.. list-table::
   :widths: 14 44 42
   :header-rows: 1

   * - Fit
     - Use when
     - Parameters
   * - ``fit23``
     - One lifetime from a polarisation-resolved decay, modelling the anisotropy
       so both channels are described together. The single-molecule burst and
       FLIM-pixel workhorse.
     - ``tau``, ``gamma``, ``r0``, ``rho``
   * - ``fit24``
     - A decay one exponential cannot describe — two states with distinct
       lifetimes, plus scatter and a constant offset.
     - ``tau1``, ``gamma``, ``tau2``, ``A2``, ``offset``
   * - ``fit25``
     - The sample is known to occupy one of a few discrete states and the goal
       is to *classify* rather than to measure: four candidate lifetimes are
       scored and the best is reported.
     - ``tau1``–``tau4``, ``gamma``, ``r0``
   * - ``fit26``
     - The pure-component decays are already known and only their mixing
       fraction is wanted (species fractioning against measured references).
     - ``x1``
   * - ``fit_nexp``
     - Any number of lifetimes, with the amplitudes profiled rather than
       searched. Set ``tail_start`` to fit a tail without reconvolution.
     - ``lifetimes[]``, ``amplitudes[]``

Ask the library rather than this table when writing code — the registry is the
authority, and it cannot go stale:

.. code-block:: python

   tttrlib.fit_names()                          # every registered fit
   tttrlib.decay_fit_parameter_names("fit24")   # its parameters, in order
   tttrlib.decay_fit_result_names("fit24")      # its result columns
   tttrlib.registry("fit")["fit24"]             # units, ranges, descriptions

Setup, parameters and results
-----------------------------

All three cross the boundary as flat ``double`` arrays. That keeps the hot path
free of string handling and gives every language binding the same wire — but a
flat array is only usable if something turns names into slots, and that is what
the registry does. **Never build these vectors by hand.**

.. code-block:: python

   setup = tttrlib.setup_vector("fit23", dt=0.032, period=32.0, g_factor=1.05)

Every slot takes its documented default unless you name it, so you supply only
what you care about and cannot get the order wrong. The same builder exists in
every binding (``decay_fit_setup_vector(name, json)``), so R and Java are not
left counting positions either.

Results are read by name:

.. code-block:: python

   named = tttrlib.results_as_dict("fit23", list(outcome.results))
   named["twoIstar"], named["converged"], named["r_scatter"]

Every model reports at least ``twoIstar`` — the Poisson deviance against a
perfectly fitting model, near 1 for a good fit to counting data — plus
``converged`` and ``iterations``.

Fixing, freeing and linking
---------------------------

One integer per parameter says what may move:

.. list-table::
   :widths: 16 84
   :header-rows: 1

   * - Code
     - Meaning
   * - ``0``
     - **Free** — optimised on its own.
   * - ``-1``
     - **Fixed** — held at the value you supplied.
   * - ``k > 0``
     - **Linked** — every slot carrying group ``k`` is optimised as one shared
       value.

Linking is what makes fitting several measurements together worth doing: some
parameters are properties of the *instrument* and must be common (a g-factor, a
fundamental anisotropy, a timeshift), while others are properties of each
*sample* and must not be.

The registry marks parameters that are not identifiable from a short decay, and
``default_links`` honours that:

.. code-block:: python

   tttrlib.default_links("fit23")                  # [0, -1, -1, -1]
   tttrlib.default_links("fit23", free=["gamma"])  # [0, 0, -1, -1]

Scatter and anisotropy are barely identifiable against an auto-extracted
background, so freeing them by default produces confident nonsense. Free them
when a *measured* IRF and background make them meaningful.

Bounds are priors
-----------------

A hard box constraint is the degenerate case of a prior — uniform inside the
interval, impossible outside — so a parameter needs one concept, not two. A
prior is attached per slot and serialised as JSON, in the same form ChiSurf
uses, so it crosses between the two losslessly:

.. code-block:: python

   constraints.set_prior_json(0, '{"kind": "lognormal", "mu": 0.7, "sigma": 0.3}')

Available kinds: ``uniform``, ``normal``, ``truncated_normal``, ``half_normal``,
``lognormal``, ``exponential``, ``gamma``, ``beta`` and ``product``. A uniform
prior contributes hard bounds to the optimiser; the others contribute a residual,
so a least-squares fit performs maximum-a-posteriori estimation with no change to
the optimiser itself.

Fitting many decays
-------------------

Batching belongs to the interface, not to each model, so it works for every fit:

.. code-block:: python

   batch = fit.fit_many(problem, matrix.ravel().tolist(), n_rows, n_cols,
                        start, constraints)
   taus = np.asarray(batch.parameters).reshape(n_rows, n_parameters)[:, 0]

Rows are spread across workers with the GIL released; the model is shared
because it is immutable, and only per-row working state is copied.
``TTTRLIB_USE_OPENMP=0`` forces serial, and ``TTTRLIB_NUM_THREADS`` /
``OMP_NUM_THREADS`` cap the worker count.

Fitting them *together*
-----------------------

``fit_many`` fits each row on its own, so the rows never meet and a link group
spanning them does nothing. ``fit_linked`` is the other thing: one parameter
vector covering every row, groups resolved across the whole of it, and a single
objective minimised jointly.

.. code-block:: python

   # tau free per row; gamma and r0 held; rho tied across every row (group 1).
   codes = []
   for _ in range(n_rows):
       codes += [0, -1, -1, 1]

   out = fit.fit_linked(problem, matrix.ravel().tolist(), n_rows, n_cols,
                        x0, tttrlib.DecayFitConstraints(tttrlib.VectorInt32(codes)))

   parameters = np.asarray(out.parameters).reshape(n_rows, n_parameters)
   print(out.n_variables)   # 12 slots -> 3 free lifetimes + 1 shared rho

Constraints are defined over the **concatenated** vector — slot
``row * n_parameters + i`` — and group ids are global, so a group spans rows
naturally. A linked slot comes back *identical* in every row, because it is one
value written into each of them, not several values that happen to agree.

This is the reason to fit several measurements together. A rotational
correlation time, a g-factor or a timeshift is a property of the instrument or
the sample and should be informed by every measurement at once; a lifetime or a
species fraction belongs to each measurement alone. Tie the first kind and leave
the second free.

``out`` carries the joint ``objective`` (the sum over rows), ``row_objective``
for each row, the per-row ``results``, and ``n_variables`` — worth checking,
since it tells you how much the links actually collapsed the search.

Simulating a decay
------------------

``model_curve`` returns the curve a model predicts, with no reference to any
data:

.. code-block:: python

   curve = fit.model_curve([2.0, 0.0, 0.0, 1.0], problem)

Use it to simulate, or to plot a model before a measurement exists. It checks the
problem first — same sizing rules as ``fit`` — and raises rather than reading
past the end of a mis-sized response. ``evaluate``
is the other thing: it scores parameters *against* the data, and for models that
profile their amplitude against the observed counts it scales the curve to them
— so on empty data it returns zeros. ``supports_model_curve()`` reports whether a
model has a data-free curve (``fit25`` does not: a selection among four
candidates has no single curve).

Convolving: the recursion or the transform?
-------------------------------------------

A decay is compared against data only after it has been convolved with the
instrument response, and tttrlib can do that two ways. The choice is exposed
because the two are not interchangeable — but the reason to pick one is *not* the
one most people reach for.

.. code-block:: python

   recursive = tttrlib.dfa_convolve(rates, weights, irf, n_bins, 0.0, 0)
   spectral  = tttrlib.dfa_convolve(rates, weights, irf, n_bins, 0.0, 1)

**Use the recursion. It is the default and it is faster.** "Convolution is a
multiplication in frequency space, so use an FFT" is sound advice when the
response must be convolved with an arbitrary signal, and misleading here. Both
paths cost ``O(n_bins x n_rates)``: the recursion does one multiply–add per rate
and bin, while the closed-form periodic spectrum does one complex *division* per
rate and frequency. The transform is not what dominates — evaluating the closed
form is — so the frequency domain buys no better scaling, only worse constants,
and the recursion is SIMD-optimised on top of that.

Measured by ``benchmarks/bench_convolution.py`` on an M1 Pro:

.. list-table::
   :widths: 20 20 20 20 20
   :header-rows: 1

   * - bins
     - rates
     - recursion
     - spectral
     - spectral is
   * - 1024
     - 1
     - 28 µs
     - 47 µs
     - 1.7× slower
   * - 1024
     - 16
     - 55 µs
     - 253 µs
     - 4.6× slower
   * - 1024
     - 64
     - 147 µs
     - 911 µs
     - 6.2× slower
   * - 4096
     - 64
     - 581 µs
     - 3569 µs
     - 6.1× slower

The gap *widens* with the rate count, which is exactly the regime a
donor ⊗ FRET ⊗ anisotropy outer product lives in — so the frequency domain is at
its worst where a rate-spectrum model needs it most.

Reach for the spectral path for the two things the recursion genuinely cannot
do:

* convolving an **arbitrary measured pattern** — an autofluorescence or scatter
  reference is not a sum of exponentials;
* an **independent check**, because it is a different algorithm computing the
  same physics;
* a **response that wraps** around the period boundary. The recursion starts at
  bin 0 as though nothing preceded it, so it cannot see the part of a broad
  instrument response that has wrapped. With a compact response — near zero at
  both ends of the period, which is the normal case — the two agree to machine
  precision; with a broad one only the spectral path is right.

A **sub-bin timeshift** needs neither choice: pass a fractional ``shift_bins``
and the recursion borrows one spectral transform of the *response*, which costs
about 24% (53 → 66 µs at 1024 bins and 16 rates) and does not grow with the rate
count. The shift is applied to the response and never to the decay — a phase ramp
is band-limited interpolation, and the decay steps at the period boundary where
the next pulse arrives, so shifting it makes the tail ring and go **negative**,
which a Poisson likelihood cannot take the logarithm of.

The two agree to machine precision, which is a stronger statement than it looks.
The recursion applies the trapezoid rule to the convolution integral, and working
out which kernel that leaves gives ``exp(-k L)`` at every lag except ``L = 0``,
where it leaves one half. Halving one sample is subtracting half a delta, and a
delta has a flat spectrum, so the whole difference is a constant ``1/2``
subtracted from the periodic spectrum. Without that correction the two differ by
``(1 + exp(-k)) / 2`` — a factor that depends on the *rate*, so it does not
divide out of a rate spectrum but reweights it (0.5% at ``k = 0.01`` against 5%
at ``k = 0.1``). Relative weights are the measurement in a FRET-rate
distribution, so that would be a systematic error in the answer rather than in
the last digit.

Common failure modes
--------------------

.. list-table::
   :widths: 30 70
   :header-rows: 1

   * - Symptom
     - What it usually is
   * - ``ValueError: irf has N samples, expected 2N``.
     - The instrument response is sized for **one** detection channel while the
       problem describes two. A shared response is only shared when there is one
       channel to share it with: a polarisation-resolved model reads
       ``n_channels * n_bins`` samples straight out of the array, so a
       half-length response is an out-of-bounds read rather than a shorthand.
       Stack the two responses (``[VV, VH]``) as you do for the data. This is
       refused rather than tolerated because it used to be *silent* — the curve
       came back looking plausible and the process died later, elsewhere.
   * - The lifetime comes back in the thousands, and ``2I*`` looks *good*.
     - Sparse data. Below a few hundred photons the likelihood barely
       distinguishes a real lifetime from one far longer than the recorded
       window, and the objective can fall monotonically all the way to
       ``tau -> infinity`` — so the physical answer is only a *local* minimum,
       with a basin that a starting value outside it will miss. The fit reports
       success either way. **Compare the fitted lifetime against the excitation
       period**; nothing in the result will do it for you.
   * - The lifetime sits *exactly* on the excitation period.
     - The decay is convolved over one period, so the search is bounded by it and
       a longer lifetime rails onto that bound. **This is quiet**: ``converged``
       still reports ``True`` and ``2I*`` can look plausible. Lengthen the period
       rather than believing the number.
   * - ``2I*`` is NaN and the parameters did not move.
     - The likelihood was undefined — commonly ``fit24``/``fit25``/``fit26``
       against an all-zero background, whose background term then has no weight.
       Give a small flat background.
   * - A freed ``gamma`` runs to ~1.
     - The background pattern is not area-normalised, so ``gamma`` is not a 0..1
       fraction. Normalise it, or hold ``gamma``.
   * - ``fit24``'s two lifetimes trade against each other.
     - A bi-exponential model is not identifiable from data a single exponential
       describes. A second lifetime that wanders freely is evidence the data do
       not support it.
   * - ``fit25`` always picks the same candidate.
     - The candidates do not bracket the decay, or ``gamma`` is held at a value
       that forces the choice.
   * - A polarisation-resolved fit is nonsense.
     - Check the channel order: parallel first, then perpendicular, in one array.

Related decay tools
-------------------

``DecayConvolution``
   Convolves model decays with an instrument response. Use it when building or
   validating custom models outside these fits.

``DecayPhasor``
   Phasor coordinates for decay histograms — fast, model-free lifetime screening
   and FLIM inspection.

Deprecated API
--------------

The former per-estimator classes (``Fit23``/``Fit24``/``Fit25``/``Fit26``, the
``fit23``-style helpers, ``DecayFitData`` and the per-model ``fit_matrix``) are
kept as a **Python-only** compatibility layer that warns on use and will be
**removed in 0.29**. They are reconstructed on top of the interface above and
return the same numbers. Port to ``DecayFit2``: the parameter, setup and result
layouts then come from the registry instead of from a per-model convention, and
the same code works for every model.

Examples
--------

* :doc:`auto_examples/fluorescence_decay/plot_decay_fit_interface` — start here
* :doc:`auto_examples/fluorescence_decay/plot_convolution_methods` — which
  convolution backend, and why the FFT is not the fast one
* :doc:`auto_examples/fluorescence_decay/plot_fit23`
* :doc:`auto_examples/fluorescence_decay/plot_fit24`
* :doc:`auto_examples/fluorescence_decay/plot_fit25`
* :doc:`auto_examples/fluorescence_decay/plot_fit26`
* :doc:`auto_examples/flim/plot_mle_lifetime`
