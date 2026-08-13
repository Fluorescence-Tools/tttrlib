.. _hmm_state_decoding:

HMM state decoding: distributions, not winner-takes-all
=======================================================

Fitting an HMM tells you *how many* states there are and what they look
like. Using it means going one step further and asking which state each photon
belongs to. That step is called **decoding**, and which decoder is right depends
entirely on the question being asked.

This page is about a bias that is easy to introduce and hard to notice: the
standard decoder, Viterbi, answers a different question from the one most burst
analysis actually asks, and the difference systematically inflates
well-separated states while erasing ambiguous and short-lived ones.

.. contents::
   :local:
   :depth: 1


Two different questions
-----------------------

"Most likely sequence"
   Of all possible state trajectories, which single one has the highest
   probability given the data? This is what :meth:`tttrlib.HMM.viterbi`
   returns, and it is the right answer when you want *the* trajectory — for
   plotting one burst, or as a point estimate of the path.

"Distribution over states"
   Across all the photons, what fraction belongs to each state? This is what
   per-state decays, per-state spectra, state populations and occupancy
   fractions are made of — and it is a *different* question.

The trap is that the first answer looks like it also answers the second. It does
not, and the way it fails is one-directional.


Why winner-takes-all is biased
------------------------------

Take a burst where the model is genuinely unsure: every photon has a posterior
of 0.7 for state 0 and 0.3 for state 1.

.. code-block:: text

   gamma = (0.7, 0.3) for every photon in the burst

   Viterbi ->  100 % state 0,   0 % state 1     <- the 30 % is erased
   gamma   ->   70 % state 0,  30 % state 1     <- faithful by construction

The argmax is not wrong about the most likely *sequence*. It is wrong as an
*occupancy estimator*, because it discards the 30 % rather than distributing it.
Every photon in the burst is resolved the same way, so the error does not
average out — it accumulates in one direction.

The consequences in practice:

* **Well-separated states are inflated.** They win the argmax even in the
  photons where they are only mildly favoured.
* **Ambiguous states are erased.** A state that is never the single most likely
  one anywhere can get zero photons while carrying real posterior mass.
* **Short-lived states vanish.** A dwell too short to dominate its neighbours in
  the maximum-likelihood path disappears entirely.

This is measurable. On simulated data with overlapping emission profiles
(200 000 photons, three states), the occupancy error against the posterior:

.. code-block:: text

   decoder                     max |occupancy - posterior|
   viterbi                                 0.0246
   marginal gamma draw (jitter)            0.0003
   FFBS path sampling                      0.0017

``benchmarks/bench_h2mm.py`` reports this number beside the timings, so a
regression in the decoders shows up as a quality change, not just a speed one.


gamma: the per-photon posterior
-------------------------------

The quantity that answers the distribution question is

.. math::

   \gamma_t(i) = P(s_t = i \mid \text{data}, \lambda)
               = \frac{\alpha_t(i)\,\beta_t(i)}{\sum_j \alpha_t(j)\,\beta_t(j)}

from the scaled forward-backward recursion — the same array the reference
``H2MM_C`` implementation calls ``gamma``, so the numbers are directly
comparable. tttrlib's E-step has always formed it internally and contracted it
away; :meth:`tttrlib.HMM.posterior` now returns it.

.. code-block:: python

   import tttrlib

   engine = tttrlib.HMM()
   engine.set_bursts_from_tttr(data, bursts, [green, red], 3, 1)
   model = engine.fit(n_states=3)

   gamma, n_underflow = engine.gamma(model)   # (n_photons, n_states) float32
   occupancy = gamma.mean(axis=0)             # unbiased state occupancy

Notes:

* ``gamma`` is ``float32``. At 10 M photons and 4 states the array is 160 MB,
  and a probability used for weighting and thresholding does not need more
  precision than that.
* ``n_underflow`` counts photons where the forward scale underflowed to zero.
  Those rows carry no information and are returned uniform. A non-zero count
  means the model assigns (near-)zero probability to part of the data — treat
  the whole decode with suspicion rather than just those photons.
* If fractional weights are acceptable, **weighting by gamma is exact and has
  lower variance than any draw**. Reach for the samplers below only when the
  output has to be one integer per photon.


When the output must be a hard label
------------------------------------

Most of the pipeline downstream of a decode is typed for integers: a photon goes
into one histogram, one channel, one selection. Once one state per photon is
mandatory, gamma-weighting is off the table and the real comparison is
**drawing from gamma versus taking its argmax**.

Drawing wins, for one reason: it reproduces the marginal by construction. Over
the photons at :math:`\gamma = (0.7, 0.3)`, roughly 70 % land in state 0 and
30 % in state 1 — which is what gamma said. The argmax cannot reproduce it at
all.

tttrlib provides two ways to draw.

.. list-table::
   :header-rows: 1
   :widths: 18 26 26 30

   * - decoder
     - draws from
     - photon distribution
     - dwell / transition structure
   * - ``viterbi``
     - — (argmax)
     - biased (winner-takes-all)
     - consistent (it *is* the ML path)
   * - ``jitter``
     - marginal gamma, per photon
     - **faithful**
     - fragmented — do not use
   * - ``ffbs``
     - joint posterior, whole path
     - **faithful**
     - **valid**


Marginal draw (jitter)
~~~~~~~~~~~~~~~~~~~~~~

:meth:`tttrlib.HMM.sample_states` draws each photon's state independently from
its own gamma row:

.. code-block:: python

   path, n_underflow = engine.jitter_path(model, seed=0)

Cheap, exact in the marginal, and the right tool for per-photon questions:
occupancies, per-state decays, per-state micro-time or spectral histograms.

.. warning::

   The draws are **independent per photon**, so the sampled path has none of
   gamma's temporal correlation. A state that is solidly occupied at
   :math:`\gamma = (0.9, 0.1)` will still see about one photon in ten flipped,
   scattered at random — turning a single long dwell into a shower of spurious
   one-photon dwells. In the test suite a marginal draw produces more than twice
   as many dwells as the joint draw on identical data.

   **Never** compute dwell times, transition counts, or a transition-density
   plot from a marginal draw. Independent draws also give error bars that are
   far too tight, because they ignore the correlation between neighbouring
   photons.


Joint draw (FFBS)
~~~~~~~~~~~~~~~~~

:meth:`tttrlib.HMM.sample_paths` implements **forward filtering, backward
sampling**: the same scaled forward pass, then

.. math::

   s_N \sim \alpha_N, \qquad
   s_t \sim \alpha_t(i)\,A^{\Delta t}[i,\, s_{t+1}]

sweeping backwards. Each draw is an exact sample from
:math:`P(\text{path} \mid \text{data})`, so it keeps the temporal correlation
that the marginal draw throws away:

.. code-block:: python

   paths = engine.ffbs_paths(model, seed=0, n_samples=20)   # (20, n_photons)

Averaging a quantity over the draws is **multiple imputation**: the spread
across draws is the decoding uncertainty, which a single Viterbi path reports as
zero. Use FFBS whenever dwell times, transition counts or path-level error bars
matter.

The forward filter is computed once per burst and shared by every draw, so extra
draws are close to free — eight FFBS draws over 200 000 photons cost 3.3 ms
against 1.6 ms for gamma alone.

.. note::

   Coincident photons (zero macro-time gap) share a state by construction:
   the propagator over :math:`\Delta t = 0` is the identity, so no decoder can
   place a transition between them.


Reproducibility
~~~~~~~~~~~~~~~

Both samplers use a **counter-based** random number generator: each draw is a
pure function of ``(seed, draw index, photon index)`` rather than being pulled
from a shared stream. Bursts therefore decode in any order on any number of
threads and the result is bit-identical. A single shared generator would have
made the output depend on thread scheduling.


Persisting a decode
-------------------

A decoded assignment has to outlive the process. tttrlib offers two ways, built
on the same per-photon state array. They are equivalent photon for photon — a
test asserts exactly that — and differ only in what the file looks like
afterwards.

Path A — state-encoded routing channels
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Each ``(stream, state)`` pair gets its own routing-channel id, and one file is
written holding every photon:

.. code-block:: python

   path, _ = engine.viterbi_path(model)
   cmap = engine.build_channel_map(data, n_states=3)
   split = engine.split_routing_channels(data, path, cmap)
   split.write("decoded.ptu", "PTU")

The result is **self-describing**: per-state decays, FCS, burst analyses and
lifetime fits all become ordinary :class:`tttrlib.Channel` selections, with no
new plumbing anywhere downstream and no need for the consuming tool to know
an HMM was involved.

**Channel allocation — the whole id space is compacted.** A source file's
channels are usually sparse: 1, 12 and 30 for three detectors is perfectly
ordinary, and those gaps are dead weight in a record field only a few bits wide.
So the split does not merely append new ids, it renumbers everything. The used
source ids compress to ``0..k-1`` in ascending order, and the ``(stream, state)``
pairs are allocated immediately after, **densely, one step apart**, in
stream-major / state-minor order:

.. code-block:: text

   source 1, 12, 30    ->  0, 1, 2       (compressed, ascending)

   stream 0, state 0   ->  3       stream 1, state 0  ->  5
   stream 0, state 1   ->  4       stream 1, state 1  ->  6

Every id the split writes then lies in one run from 0 with no holes, and that is
what decides whether the result still fits a narrow container. Left
uncompressed, the example above would keep background photons on id 30 and need
**5 bits** to store a file with all of 7 distinct channels; compressed it needs
**3**, so the same split that would have required PTU now round-trips through an
SPC-600/256 record.

Both directions are recorded, so nothing is lost: ``cmap.used_channels[i]`` is
the original id and ``cmap.compressed_channels[i]`` the id it became, with
``cmap.source_map`` giving the same thing as a dict in Python and
``cmap.highest_channel()`` the largest id that will be written. The map is a
published lookup table, stored in the state sidecar, not an arithmetic stride a
reader is expected to reverse-engineer.

.. warning::

   **The budget is the target container's record field, not the in-memory
   type.** tttrlib holds routing channels in a ``signed char``, but what a file
   can store is the record bitfield:

   .. list-table::
      :header-rows: 1
      :widths: 45 20 35

      * - container
        - channel bits
        - usable ids
      * - PTU / HydraHarp / TimeHarp260 T2, T3
        - 6
        - 0..63
      * - PicoHarp T2, T3
        - 4
        - 0..15
      * - Becker & Hickl SPC-130
        - 4
        - 0..15
      * - Becker & Hickl SPC-600/256
        - 3
        - 0..7

   A narrower container does **not** fail on an out-of-range id — it drops the
   high bits silently, so an id of 40 written to an SPC-130 file reads back as
   8 and two states quietly merge. This is why ``build_channel_map`` takes an
   explicit ``max_channel`` (defaulting to PTU's 63) rather than inheriting one
   from the source, and why **PTU is the assumed target** for a split. The
   budget needed is ``k + n_streams * n_states`` where ``k`` is the number of
   source channels; if that does not fit, the call throws, naming the numbers
   and pointing at Path B, which has no budget at all.

.. warning::

   **Every photon is renumbered, and channel ids do not survive a split.**
   Photons no decoder assigned — outside every burst, or matching no stream —
   move to the *compressed* form of the channel they were on, so they stay
   distinguishable from each other while the id space stays dense. Two
   consequences:

   * The compressed source ids afterwards hold **only** unassigned photons.
     Summing the channel that used to be the donor gives you background, not the
     donor total. That is a useful semantic — background is separated for free —
     and a sharp edge if you were not expecting it.
   * A downstream tool that hard-codes "channel 1 is the donor" will be wrong
     about a split file. Read ``cmap.source_map`` (or the ``channel_map`` in the
     sidecar) rather than assuming; that is what it is there for.

Path B — the msgpack state sidecar
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The source file is left untouched and the assignment travels beside it:

.. code-block:: python

   sidecar = engine.state_sidecar(path, model, "viterbi", 0, cmap)
   sidecar.write("decoded_hmm_states.msgpack")

   back = tttrlib.HmmStateSidecar.read("decoded_hmm_states.msgpack")
   idx = back.indices_for_state(1)          # source photon indices
   mask = back.mask_for_state(1)            # a TTTRMask, for the selection API

Use this when the raw data must not be altered, or when the state count exceeds
what the container's channel field can express. The cost is that only consumers
that accept a mask or an index array see the states; a tool reading the raw file
sees nothing.

**What is stored is the per-photon state array, not N masks.** A state
assignment is a *partition* — every photon has exactly one state — so N bitmasks
would store the same information at ``N/8`` bytes per photon that one ``uint8``
array stores in 1. Masks are the *view*: ``mask_for_state(k)`` materialises a
:class:`tttrlib.TTTRMask` on demand to feed the existing selection machinery.

The sidecar also carries the model, the decoder name, the seed, and the channel
map when Path A also ran — so a decode is reproducible from the file alone.


Formats: msgpack for large outputs
----------------------------------

**msgpack is the convention for tttrlib outputs that scale with the photon
count.** It costs no new dependency: nlohmann/json is already vendored and
provides ``to_msgpack`` / ``from_msgpack`` plus a ``json::binary`` value type,
which msgpack encodes as a native ``bin`` field — packed arrays travel as bytes
rather than as base64 or per-element decimal text. JSON stays the right choice
for small, human-readable payloads.

The same change fixed a serialisation defect found while building this feature.
:meth:`tttrlib.TTTRMask.to_json` emits one JSON integer **per event**, so a
10 M-photon mask is roughly 20 MB of text. :class:`tttrlib.TTTRMask` now also
has:

.. code-block:: python

   payload = mask.to_bytes()          # msgpack; the bit-packed words verbatim
   mask.from_bytes(payload)
   mask.write_msgpack("mask.msgpack")
   mask.read_msgpack("mask.msgpack")

which is ``size/8`` bytes plus a small header — over an order of magnitude
smaller than the JSON form. ``to_json`` remains for small masks and for
compatibility with existing payloads.


Choosing a decoder
------------------

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - what you want
     - what to use
   * - one trajectory, e.g. to plot a burst
     - ``viterbi``
   * - state occupancies / populations
     - ``gamma.mean(axis=0)`` — no draw needed
   * - a fractional weight per photon
     - ``gamma`` directly (exact, lowest variance)
   * - per-state decays, spectra, micro-time histograms
     - ``jitter`` (or ``gamma`` weights if the tool accepts them)
   * - dwell times, transition counts, TDP
     - ``ffbs``
   * - error bars on any of the above
     - ``ffbs`` with several draws (multiple imputation)


A note on the signed channel type
---------------------------------

Routing channels are stored in memory as ``signed char``. Negative ids are legal
there, but no reader in tttrlib produces one: markers are flagged through
``event_types == RECORD_MARKER`` rather than by a negative channel
(``MARKER_POSITION_X`` and ``MARKER_POSITION_Y`` are 0 and 1), and every reader
narrows an ``int16_t`` into the ``signed char``. Half the range is therefore
dead, and — as the table above shows — the usable range is set by the container,
not by the C++ type, so widening the type to ``uint8_t`` would buy range that no
supported format can store. The state split allocates from the non-negative
range only and leaves the type alone.


See also
--------

* :ref:`hmm_bva_guide` — fitting the model in the first place.
* ``examples/single_molecule/plot_hmm_state_channels.py`` — a complete
  worked example: fit, decode three ways, write both a PTU and a sidecar, read
  them back and show the per-state decays agree.
* ``benchmarks/bench_h2mm.py`` — decode cost and occupancy error side by side.
