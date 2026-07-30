.. _simulator-guide:

======================================
Photon-simulation guide (smFRET / MFD)
======================================

tttrlib ships an **OpenMM-style photon simulator**: it propagates diffusing (or
immobile) fluorophores through a confocal excitation focus, lets them switch
photophysical/conformational states, and emits a time-resolved single-photon
stream (macro-time, routing channel, micro-time/TAC) that you can analyse or
write to a TTTR file exactly like real data.

This guide is written so that a **FRET expert can configure any standard
experiment from the manual alone** — single-molecule FRET, dynamic exchange,
proximity-ratio histograms, anisotropy, MFD, PIE, CLSM/FLIM imaging, and
different point-spread functions. The central idea is that all of these are
obtained by *composing routing channels, species, and decays* — not by special
engine modes.

.. contents:: On this page
   :local:
   :depth: 2


The OpenMM analogy
==================

If you know OpenMM, the object model maps directly:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - OpenMM
     - tttrlib simulator
   * - ``System`` (model)
     - :class:`~tttrlib.SimSystem` — species, kinetics, box, population, background, field references
   * - ``Integrator`` (propagator)
     - :class:`~tttrlib.SimIntegrator` — ``dt``, RNG, stop conditions, throughput knobs, micro-time axis
   * - ``Simulation`` (driver)
     - :class:`~tttrlib.SimEngine` — ``run()`` / ``step()`` / ``run_scan()`` and the output arrays
   * - ``Force`` / fields
     - :class:`~tttrlib.SimGrid` — excitation & per-channel detection fields (PSF/CEF)
   * - ``State`` (snapshot)
     - :class:`~tttrlib.SimState` — from ``get_state()``

There is one **single entry point** — a JSON/dict configuration — documented and
validatable against ``examples/simulation/sim.schema.json``.


Quick start
===========

.. code-block:: python

    import tttrlib

    config = {
        "settings": {"dt": 0.01, "n_ph_max": 200000, "n_channels": 2},
        "box": {"xy": 2.0, "z": 4.0},
        "species": [{"D": 3.0, "q": [50.0, 50.0]}],   # one channel-pair, equal brightness
        "k_rad": [0.0], "k_nrad": [0.0],
        "background": [0.0, 0.0],
        "population": [2.0],                            # ~2 molecules in the box (open volume)
        "excitation": {"type": "gaussian3d", "w0": 0.3, "z0": 2.0,
                       "extent_xy": 2.0, "extent_z": 4.0, "spacing": 0.1},
    }

    sim = tttrlib.SimEngine.from_dict(config)   # single entry point
    sim.run()

    ph = sim.photons()          # dict of numpy arrays: macro_window, arrival_time,
                                # channel, micro_time, species, molecule, event_type
    tttr = sim.to_tttr(dt=0.01, n_channels=2)   # -> a tttrlib.TTTR you can analyse

``sim.photons()`` returns numpy arrays directly (no ``VectorDouble`` wrapping),
and ``sim.to_tttr(...)`` gives you a first-class :class:`~tttrlib.TTTR` object so
correlation, burst search, decay fitting, imaging, etc. all just work.


The unit contract
=================

Two unit systems that **never mix**:

* **Macro-time.** One abstract unit shared by ``dt``, the diffusion coefficient
  ``D`` (length²/unit), the rate matrices ``k_rad``/``k_nrad`` (per unit), the
  brightness ``q`` and ``background`` (photons/unit), and the outputs
  ``macro_window``/``arrival_time``. The common convention is **milliseconds**,
  so ``D = 3`` is 3 µm²/ms and ``q = 50`` is 50 kHz peak brightness.
* **Micro-time (nanoseconds).** ``microtime_resolution``, ``laser_period``, each
  ``SimDecay`` ``dt``/``t0``, and ``D_rot`` (rad²/ns). The output ``micro_time``
  is a TAC channel index.

Lengths are micrometres throughout. Do **not** enlarge ``dt`` for speed — it is
sub-stepped internally; use the throughput knobs (see :ref:`sim-performance`).


Geometry and concentration
==========================

Molecules diffuse in an ellipsoidal open-volume ``box`` (half-extents
``xy``, ``z`` in µm). ``population[i]`` is the *expected* number of molecules of
species ``i`` in that box, which fixes the concentration
:math:`C = N / (N_A \cdot V)`; molecules are injected across the box surface at
the matching flux and removed when they leave. The confocal focus is a much
smaller region inside the box, so single-molecule work is dilute
(``population`` of order 1). Use discrete ``emitters`` instead of ``population``
for fixed/immobile fluorophores (e.g. CLSM samples).


.. _sim-channel-model:

The routing-channel model (read this first)
===========================================

Every "advanced" observable — FRET, PIE, MFD, per-channel lifetimes, anisotropy
— is produced by **composition**, not by a dedicated engine feature. The number
of detection channels is the *product* of the physical routings present:

.. math::

   n_\text{channels} = (\text{spectral: green/red/…}) \times
                       (\text{polarization: } \parallel/\perp) \times
                       (\text{PIE window: prompt/delay})

and you compose three primitives:

* **Species** = a photophysical / conformational *state* (:class:`~tttrlib.SimSpecies`),
  with a diffusion coefficient ``D``, a per-channel brightness vector ``q``
  (which encodes *where* its photons go), an optional micro-time decay
  (:class:`~tttrlib.SimDecay`, its *lifetime*), and anisotropy parameters.
* **Rate matrices** ``k_rad`` / ``k_nrad`` = *transitions* between species.
* **Fields** = the excitation and per-channel detection PSFs.

The recipes below all follow from this table:

.. list-table::
   :header-rows: 1
   :widths: 28 72

   * - Observable
     - How to configure it
   * - FRET / proximity
     - per-channel ``q`` sets the donor/acceptor (green/red) split; ``q_green ∝ (1−E)``, ``q_red ∝ E``. Static states → distinct species → an E-histogram with peaks.
   * - Dynamic FRET / exchange
     - transitions between FRET-state species via ``k_nrad`` (spontaneous). Photo-induced transfer/bleaching via ``k_rad`` (scaled by the local excitation intensity).
   * - Per-channel lifetime (MFD)
     - model each emitting state as its own species with its own ``SimDecay`` and channel-routing ``q`` (donor→green with donor τ; acceptor→red with acceptor τ).
   * - PIE
     - place a species' decay in the *prompt* vs *delayed* TAC window via ``SimDecay.t0`` within the ``laser_period``.
   * - Anisotropy
     - a parallel/perpendicular channel pair per spectral band; ``r0``/``D_rot``/``l1``/``l2`` on the species.
   * - ISM
     - several detection fields with lateral ``x0/y0`` offsets.
   * - CLSM / FLIM
     - a :class:`~tttrlib.SimScanner` raster + per-species decays; reconstruct with :class:`~tttrlib.CLSMImage`.


Cookbook
========

Each recipe is a runnable ``config`` dict (pass to ``SimEngine.from_dict``); the
matching example scripts live in ``examples/simulation/`` and the JSON files in
``examples/simulation/configs/``.

Single molecule & FCS
---------------------

One diffusing species, uniform brightness, open volume. Correlate the resulting
photon stream to recover the diffusion time
:math:`\tau_D = w_0^2 / (4D)` and the number of molecules in the focus.

.. code-block:: python

    config = {"settings": {"dt": 0.01, "n_ph_max": 500000, "n_channels": 2},
              "species": [{"D": 3.0, "q": [50.0, 50.0]}],
              "k_rad": [0.0], "k_nrad": [0.0], "background": [0.05, 0.05],
              "population": [1.5],
              "excitation": {"type": "gaussian3d", "w0": 0.3, "z0": 2.0,
                             "extent_xy": 2.0, "extent_z": 4.0, "spacing": 0.1}}

*Implication:* higher ``population`` (or a smaller ``box``) raises the count rate
but lowers the FCS amplitude ``G(0) ≈ 1/N``; a slower ``D`` shifts ``G(τ)`` to
longer lag times.

Proximity ratios (static FRET)
------------------------------

Two static FRET states, encoded purely through ``q``. With detection channels
``[green, red]``, a state of efficiency ``E`` has ``q = [(1−E)·b, E·b]`` for a
total brightness ``b``. A mixture yields a proximity-ratio histogram with one
peak per state.

.. code-block:: python

    b = 60.0
    def fret_state(E, D=3.0):
        return {"D": D, "q": [(1 - E) * b, E * b]}
    config = {"settings": {"dt": 0.01, "n_ph_max": 500000, "n_channels": 2},
              "species": [fret_state(0.25), fret_state(0.75)],
              "k_rad": [0, 0, 0, 0], "k_nrad": [0, 0, 0, 0],   # static: no exchange
              "background": [0.05, 0.05], "population": [0.5, 0.5],
              "excitation": {"type": "gaussian3d", "w0": 0.3, "z0": 2.0,
                             "extent_xy": 2.0, "extent_z": 4.0, "spacing": 0.1}}

*Implication:* the proximity ratio ``PR = n_red / (n_green + n_red)`` per burst
clusters at ``E`` (before γ/background correction); two well-separated ``E`` give
two peaks.

Dynamic FRET (exchange kinetics)
--------------------------------

The same two states, now interconverting via the **spontaneous** rate matrix
``k_nrad`` (row-major i→j, per macro-time unit). Fast exchange relative to the
diffusion time merges the two E-peaks into a single dynamically-averaged peak;
intermediate exchange produces the characteristic bridge between them.

.. code-block:: python

    k12, k21 = 0.5, 0.5          # per ms; interconversion of state 0 <-> 1
    config["k_nrad"] = [0.0, k12,
                        k21, 0.0]

*Implication:* increasing ``k12``/``k21`` (relative to ``1/τ_D``) collapses the
two proximity-ratio peaks toward the population-weighted mean and broadens the
distribution — the classic dynamic-FRET signature. Use ``k_rad`` instead for
excitation-driven transitions (e.g. photobleaching, photo-induced FRET changes).

Burst analysis of the simulated stream
--------------------------------------

To obtain a *burst-wise* proximity-ratio histogram (rather than the per-species
ground truth), export the stream with :meth:`SimEngine.to_tttr` and run tttrlib's
burst search on it, exactly as for a measurement. The **cumulative** (CUSUM/SPRT)
search decides photon-by-photon between "background" and "in a burst" from the
inter-photon times:

.. code-block:: python

    tttr = sim.to_tttr(dt=0.01, n_channels=2, laser_period=32.0)
    starts_stops = tttr.burst_search_cusum_sprt(
        min_photons=20,               # discard bursts smaller than this
        background_cps=sum(cfg["background"]) * 1000.0,  # sim's known background (counts/s)
        signal_to_background_ratio=4.0,   # how many times brighter a burst is than background
        alpha=0.01,                   # false-positive rate (background called a burst)
        beta=0.01,                    # false-negative rate (a real burst missed)
    )

The parameters mean:

* ``min_photons`` — the minimum burst size; raising it removes noise spikes and
  keeps only well-defined single-molecule events.
* ``background_cps`` — the baseline count rate the test compares against; taking it
  from the simulation (``sum(background) × 1000`` counts/s) avoids estimating it.
* ``signal_to_background_ratio`` — the brightness contrast the test is tuned to
  detect; the in-burst rate hypothesis is ``S/B × background_cps``.
* ``alpha`` / ``beta`` — the SPRT type-I / type-II error rates; ``0.01`` bounds
  each at ~1 %.

The sliding-window search ``burst_search_sliding_window(L, m, T)`` is also
available (``L`` = min photons, ``m`` photons within a time window ``T`` seconds
sets the rate threshold). The full pipeline — simulate → export → detect → per-burst
``E`` — is in ``examples/simulation/fret_smfret.py``; for well-separated static
states it yields two peaks and for fast exchange a single bridged peak.

.. note::

   Realistic bursts need enough photons per single-molecule transit, so the FRET
   configs use a slow, bright fluorophore (``D ≈ 0.05`` µm²/ms, ``b ≈ 300`` kcps)
   and a dilute population — see the config files for the exact values.

.. note:: **TAC reversal on export.**
   Becker & Hickl SPC hardware records the micro-time in *reverse start-stop* order
   (the raw ADC value is ``n_microtime_channels - 1 - micro_time``), and the SPC
   reader un-reverses it on read-back. :meth:`SimEngine.to_tttr` therefore encodes
   with ``reverse_tac=True`` by default, so the read-back ``micro_time`` matches the
   simulated one. The low-level :class:`SimMicrotimeEncoder.reverse_tac` field
   defaults to ``False`` (write the physical micro-time verbatim); pass
   ``reverse_tac=False`` to :meth:`~SimEngine.to_tttr` only when you want the raw
   physical TAC in the file (its read-back micro-time is then inverted).

Anisotropy
----------

Give the species a fundamental anisotropy ``r0`` and a rotational diffusion
``D_rot`` (rad²/ns); detect a parallel (channel 0) and perpendicular (channel 1)
pair. The steady-state anisotropy follows Perrin,
:math:`r = r_0 / (1 + \tau / \theta)`.

.. code-block:: python

    config = {"settings": {"dt": 0.01, "n_ph_max": 500000, "n_channels": 2,
                           "n_microtime_channels": 4096, "microtime_resolution": 0.008,
                           "laser_period": 32.0},
              "species": [{"D": 3.0, "q": [50.0, 50.0], "r0": 0.4, "D_rot": 0.05,
                           "l1": 0.03, "l2": 0.03,
                           "decay": {"lifetimes": [4.0], "amplitudes": [1.0], "dt": 0.008}}],
              "k_rad": [0.0], "k_nrad": [0.0], "background": [0.0, 0.0], "population": [1.5],
              "excitation": {"type": "gaussian3d", "w0": 0.3, "z0": 2.0,
                             "extent_xy": 2.0, "extent_z": 4.0, "spacing": 0.1}}

*Implication:* faster ``D_rot`` (shorter rotational correlation time ``θ``)
depolarises emission and lowers ``r``; ``r`` also decays across the micro-time
because rotation continues during the excited-state lifetime.

Point-spread functions (PSF gallery)
------------------------------------

The excitation/detection field is a :class:`~tttrlib.SimGrid`. Choose the focus
model per ``excitation["type"]``:

* ``"gaussian3d"`` — separable 3D Gaussian ``w0``/``z0`` (add ``"analytic": true``
  to evaluate it grid-free — zero construction/memory, exact for a Gaussian).
* ``"gaussian_lorentzian"`` — confocal MDF with a z-expanding waist
  ``w(z)² = w0²(1 + (z/zR)²)``.
* ``"radial"`` — a numeric/measured PSF supplied on the ``(r, z)`` half-plane
  (``rz`` array, ``nr``, ``nz``, ``r_step``, ``z_step``), evaluated by cylindrical
  symmetry. From Python, build one with the numpy helpers:

.. code-block:: python

    import numpy as np, tttrlib
    field = tttrlib.SimGrid.numeric_from_numpy(rz_2d, r_step=0.05, z_step=0.05)
    # or load a measured/vectorial PSF (e.g. a PyBroMo .mat):
    field = tttrlib.SimGrid.numeric_from_file("psf.npy", r_step=0.05, z_step=0.05)

*Implication:* the observation volume shape sets the FCS diffusion-time and the
molecular-brightness distribution; a realistic (aberrated) numeric PSF differs
measurably from an ideal Gaussian in the wings.

PIE (pulsed interleaved excitation)
-----------------------------------

PIE separates directly-excited-donor from directly-excited-acceptor photons by
the **excitation pulse that produced them**, which shows up as a different
micro-time window. Model the delayed (acceptor-excitation) emission as a species
whose decay is shifted into the second half of the ``laser_period`` via
``SimDecay.t0``, routed to the red channels.

.. code-block:: python

    P = 32.0     # laser_period (ns); prompt = [0, P/2), delay = [P/2, P)
    config = {"settings": {"n_channels": 2, "laser_period": P,
                           "n_microtime_channels": 4096, "microtime_resolution": 0.008, "dt": 0.01,
                           "n_ph_max": 300000},
              "species": [
                  {"D": 3.0, "q": [40.0, 20.0],                     # FRET pair, prompt window
                   "decay": {"lifetimes": [3.5], "amplitudes": [1.0], "dt": 0.008, "t0": 0.0}},
                  {"D": 3.0, "q": [0.0, 30.0],                      # direct acceptor, delayed window
                   "decay": {"lifetimes": [3.0], "amplitudes": [1.0], "dt": 0.008, "t0": P / 2.0}},
              ],
              "k_rad": [0, 0, 0, 0], "k_nrad": [0, 0, 0, 0],
              "background": [0.0, 0.0], "population": [0.7, 0.7],
              "excitation": {"type": "gaussian3d", "w0": 0.3, "z0": 2.0,
                             "extent_xy": 2.0, "extent_z": 4.0, "spacing": 0.1}}

*Implication:* gating the micro-time on the prompt vs delayed window recovers the
FRET signal vs the acceptor-directly-excited signal (the basis of ALEX/PIE stoichiometry).

ALEX (alternating laser excitation)
-----------------------------------

Where PIE separates the two excitation sources on the **micro-time** axis (both
lasers pulse every ``laser_period``), **micro-second ALEX** alternates the green
(donor) and red (acceptor) laser on the **macro-time / diffusion timescale** — so
the excitation source cannot be recovered from the micro-time; it is encoded in
the macro-time. The engine models this directly:

* pass **one excitation grid per laser** (``excitation`` becomes an array),
* set ``settings.alex_period`` (in macro-time units, ``= dt``); each macro-window
  is assigned to a laser by an equal-duty round-robin
  ``floor(fmod(T0·dt, alex_period)/(alex_period/n_lasers))``,
* give each species a **per-laser brightness matrix** ``q_alex`` (one row per
  laser). A doubly-labelled FRET pair uses ``q_alex = [[(1-E)·b, E·b], [0, b_A]]``
  so it emits DD (ch0) + DA (ch1) under the green laser and AA (ch1) under the red
  laser. An empty ``q_alex`` broadcasts the scalar ``q`` to every laser.

.. code-block:: python

    config = {"settings": {"n_channels": 2, "dt": 0.01, "alex_period": 0.1,   # 10 windows/cycle
                           "n_ph_max": 600000, "alex_markers": False},
              "species": [                                                     # two FRET populations
                  {"D": 0.05, "q_alex": [[240.0, 60.0], [0.0, 300.0]]},        # E = 0.2
                  {"D": 0.05, "q_alex": [[60.0, 240.0], [0.0, 300.0]]},        # E = 0.8
              ],
              "population": [0.15, 0.15],
              "excitation": [                                                  # one grid per laser
                  {"type": "gaussian3d", "w0": 0.30, "z0": 2.0, "extent_xy": 2.0, "extent_z": 4.0, "spacing": 0.1},
                  {"type": "gaussian3d", "w0": 0.32, "z0": 2.1, "extent_xy": 2.0, "extent_z": 4.0, "spacing": 0.1}]}

Set ``alex_markers: true`` to additionally write an explicit laser-switch marker
(``event_type`` = ``alex_marker_event_type``, routing channel = laser index) at
each switch — useful as ground truth in tests, but impractical over long dilute
runs (a marker every few windows), so the macro-time encoding is the format-faithful
mechanism.

*Implication:* the alternation lives in the macro-time. Fold it back with
:meth:`TTTR.alex_to_microtime` (period in macro-clock ticks) and gate on the
resulting ALEX phase — the donor detector marks the green window, the acceptor the
red — then histogram per-burst ``E`` and stoichiometry ``S`` into the 2D ALEX
E-S plot. See ``examples/simulation/alex_smfret.py`` and the notebooks
``alex_01_simulation_basics.ipynb`` / ``alex_02_smfret_es.ipynb``.

.. note::
   Export the simulated stream to a ``TTTR`` by building it from ``photons()``
   (macro-time from ``macro_window``/``arrival_time``, plus ``micro_time``,
   ``channel``) — the ``SM`` container keeps macro-time + routing (micro-time
   dropped, as for a real .sm file), while ``PTU`` / ``Photon-HDF5`` additionally
   preserve the micro-time. :meth:`SimEngine.to_tttr` also works (it emits SPC-130
   and now carries the simulated micro-time faithfully and skips markers).

MFD (multiparameter, dual-colour)
---------------------------------

MFD combines colour, polarization, lifetime and intensity. Build the channel
axis as ``spectral × polarization`` (e.g. 4 channels: green∥, green⊥, red∥,
red⊥), give each emitting state its own ``SimDecay`` (its lifetime) and route it
through ``q`` and the anisotropy parameters. Per-channel *lifetimes* (donor vs
acceptor) come from modelling donor-emission and acceptor-emission as separate
species with different decays.

*Implication:* every photon then carries the four MFD observables (channel →
colour+polarization, micro-time → lifetime, macro-time → burst/intensity), so a
2D lifetime-vs-``E`` or ``r``-vs-``E`` MFD plot is recovered directly from
``sim.photons()``.

CLSM / FLIM imaging
-------------------

Use discrete ``emitters`` for the sample and drive a raster scan with
:class:`~tttrlib.SimScanner`; the output is a marker-annotated stream that
:class:`~tttrlib.CLSMImage` reconstructs, with per-species decays giving FLIM
contrast. See :doc:`clsm-flim-guide` and ``examples/simulation/clsm_star_scan.py``.


.. _sim-performance:

Performance knobs
=================

For dilute single-molecule work the sim is *photon-starved* (most windows emit
nothing). Opt-in throughput knobs on ``settings`` — all statistically validated,
default off (exact):

* ``per_molecule_skip`` — coasting: skip molecules far from the focus, exact
  catch-up on wake.
* ``fast_grid_bbox`` — reject far-from-focus field lookups cheaply.
* ``independent_molecules`` — simulate each molecule's timeline independently and
  merge (parallel across molecules; needs ``max_windows`` > 0).
* ``active_margin`` — shrink the box to focus+margin (open volume). Exact for
  diffusion; for kinetics use a margin ``≳ √(2·D/k_min)`` so states equilibrate.

See ``performance_guide`` for the trade-offs and measured speedups.


Flow and directed transport
===========================

The simulator supports advective transport (flow) and static barriers (occlusion),
modelled on the Smoluchowski advection–diffusion equation:

.. math::

    dr = v(r)\,dt + \sqrt{2D}\,dW

A molecule in a velocity field :math:`v(r)` follows the Itô SDE above, integrated with
Euler–Maruyama for the noise. The **drift** is integrated exactly for a uniform field
(constant drift needs no scheme) and with an explicit **midpoint** step for any other
field. That is not a refinement but a correctness requirement: plain Euler applied to a
rigid rotation has map :math:`I + \omega\,\Delta t\,A` with determinant
:math:`1 + (\omega\Delta t)^2 > 1`, so it inflates phase-space volume on every step and
molecules spiral outward. The error is :math:`O(\Delta t^2)` per step but *systematic*, so
it accumulates linearly in time rather than averaging away — at
:math:`\omega\Delta t = 0.002` the radius grows by a factor 1.82 over 300 000 windows.
Midpoint reduces the per-step volume error to :math:`(\omega\Delta t)^4/4` and costs one
extra field lookup, which the uniform path never pays. Independently of the scheme, the
drift is sampled at the step start, so the field should vary slowly over one diffusion
length :math:`\sigma = \sqrt{2D\Delta t}`.

The three built-in fields are all divergence-free and therefore preserve a uniform
equilibrium concentration:

* **Uniform** — constant :math:`v = (v_x, v_y, v_z)`. Evaluated analytically; no grid.
* **Poiseuille** — Hagen–Poiseuille pipe flow along an axis with a parabolic cross-section
  :math:`v_a(\rho) = v_{\max}(1 - \rho^2/R^2)`, clamped to 0 outside radius :math:`R`.
* **Rotation** — rigid-body rotation about an axis, :math:`v = \omega \times r`.

Arbitrary fields can be constructed from three same-shaped component arrays via
``from_components``. A compressible field (one violating :math:`\nabla\cdot v = 0`)
will concentrate molecules — real physics, but it invalidates any homogeneous-sample
correlation analysis. The user is warned, not prevented.

The ``v_scale`` attribute on :class:`SimSpecies` controls coupling of each species to the
flow field (0 = not advected, e.g. a surface-bound dark state).

**Injection must be advection-aware.** The open-volume surface-flux model injects molecules
across the ellipsoid boundary. Without flow the per-area influx rate is
:math:`\sigma/\sqrt{2\pi}` (step size :math:`\sigma = \sqrt{2D\Delta t}`). With flow
the normal displacement has mean :math:`\mu = -v_\perp\Delta t`, giving the generalised
influx weight :math:`w = \mu\Phi(\mu/\sigma) + \sigma\phi(\mu/\sigma)` where
:math:`\Phi` is the standard-normal CDF and :math:`\phi` its PDF. Using the old
:math:`\sigma/\sqrt{2\pi}` formula under flow under-injects upstream-facing surfaces
and the population slowly drains — a silent, cumulative error that the simulator now
corrects.

**Barriers (occlusion).** An occlusion mask ``occ(r) ∈ [0,1]`` implements excluded-volume
rejection: a proposed step to :math:`r'` is accepted with probability
:math:`1 - \text{occ}(r')`. When :math:`v = 0` this satisfies detailed balance and the
**equilibrium** concentration inside a region of occlusion :math:`q` is :math:`1 - q` times
the outside value (the effective diffusion is reduced by the same factor). So the mask is an
excluded-volume / partial-accessibility medium — not a membrane with a permeability.

Three limitations:

* A wall thinner than about :math:`4\sigma` may be tunnelled through in a single step
  with no warning. Ensure ``thickness ≳ 4\sqrt{2D\Delta t}``.
* Partial occlusion (:math:`0 < \text{occ} < 1`) combined with flow is outside the
  validated regime. Hard walls (:math:`\text{occ} = 1`) with flow are fine.
* The :math:`1 - q` law is an **equilibrium** statement, and an open volume is not at
  equilibrium: molecules are injected across the surface and absorbed at it. When the
  turnover time is comparable to the time needed to diffuse across the occluded region the
  measured density ratio lands well above :math:`1 - q` — in a box of radius 3 µm with a
  1 µm slab at :math:`q = 0.5` and :math:`D = 3`, it is 0.66 rather than 0.50, because the
  residence time :math:`R^2/6D` is only about three times the crossing time
  :math:`L^2/2D`. Nothing is wrong in that run; the equilibrium law simply does not apply
  to it. Seal the region with :math:`\text{occ} = 1` walls (no injection, no absorption)
  and the law is recovered to about 1 %, which is how
  ``test_partial_occlusion_follows_one_minus_occ`` measures it.

Making a flow simulation fast
-----------------------------

Measured on a 200-molecule open volume, 8 cores, in order of what they are worth:

* **``independent_molecules``: 5-6x.** Each molecule's whole timeline is simulated on its
  own and the photon streams are merged, so there is no per-window barrier and the work is
  embarrassingly parallel. This is by far the largest single win for a stationary-focus
  run, and it is exact: it draws the injection count from a Poisson over the horizon and
  gives each molecule an independent birth time. Needs ``max_windows > 0``.
* **``per_molecule_skip`` (coasting) on top: a further ~1.3x**, so ~7x combined. Uniform
  fields only -- a non-uniform field has no closed-form catch-up and an occlusion mask
  would be tunnelled through, so both disable it.
* **``drift_midpoint: false``: 22-26 %** on a grid field, by dropping the second field
  lookup. Safe for shear-like fields such as ``poiseuille``; see the discussion above for
  when it is not.
* **A cylindrically symmetric PSF on a ``(rho, z)`` table: up to 2.9x**, and the cost
  stops depending on lateral resolution. Pass ``"radial": true`` on a ``gaussian3d``,
  ``gaussian_lorentzian`` or ``radial`` field. A confocal focus depends only on distance
  from the optical axis and on z, so an x-y-z lattice stores one number per azimuth that
  is the same number: 81x81x161 (8.5 MB) becomes 41x161 (53 kB), which is the difference
  between streaming from RAM and reading from cache on every lookup, and the interpolation
  drops from trilinear to bilinear. Measured at 400k windows: 4.4 -> 3.9 s at 0.10 um
  spacing, 5.4 -> 3.9 s at 0.05 um, and 11.7 -> 4.0 s at 0.025 um -- the radial run is
  flat while the lattice degrades.

  This is **less general, so it is opt-in**: a radial table cannot represent an
  astigmatic focus (different x and y waists), a tilted or comatic PSF, or anything else
  that varies with azimuth. The full lattice remains the default. Note the radial form is
  also the *more* accurate of the two where it applies, having no azimuthal interpolation
  error -- at 0.10 um spacing the two differ by 2.3 % in count rate, and it is the lattice
  that is wrong.

* **Threads in the default window mode: only above ~2000 molecules.** The per-window
  fork/join dominates below that -- at 200 molecules forcing it on is *three times slower*.
  ``parallel_threshold`` (default 2048) encodes this; the measured speedup is 1.1x at 2000
  molecules and 1.9-2.3x at 20000. Prefer ``independent_molecules``, which has no barrier.

Grid fields cost about 1.7x a uniform one, so if the physics only needs a constant drift,
use ``{"type": "uniform"}`` rather than a lattice holding a constant.

**Coasting** is disabled for any non-uniform field or for any occlusion mask. A uniform
field uses a quadratic bound that accounts for both diffusion and drift (the original
diffusion-only bound would under-estimate the coastable gap). Coasting for a non-uniform
or masked sample would silently lose molecules that tunnel through a wall or miss the
focus — the safe answer is to never skip windows.


API reference
=============

.. currentmodule:: tttrlib

.. autoclass:: SimEngine
   :members:
   :undoc-members:

.. autoclass:: SimSystem
   :members:

.. autoclass:: SimIntegrator
   :members:

.. autoclass:: SimSpecies
   :members:

.. autoclass:: SimGrid
   :members:

.. autoclass:: SimVectorGrid
   :members:

.. autoclass:: SimDecay
   :members:

.. autoclass:: SimScanner
   :members:

.. autoclass:: SimState
   :members:
