.. _pipelines:

Pipelines: reproducible data processing
=======================================

A tttrlib analysis can be written as a **document**: a list of steps, each
naming an entry of :ref:`the registry <plugins>` plus its parameters. Because
the steps are data rather than code, the document can be saved next to the
result, re-read on another machine, executed by another tool, and compared
between two runs — which is what makes a processing chain reproducible.

The document is written in the **mmfdb workflow schema** (version 1), so it is
simultaneously a tttrlib pipeline and an mmfdb workflow.

Compose
-------

Steps are named, not imported. ``describe`` refuses an unknown name where you
write it, not where it runs::

    >>> import tttrlib
    >>> p = (tttrlib.Pipeline("burst-analysis",
    ...                       description="sliding-window bursts, Li & Ma significance")
    ...      .then("burst_selection", L=30, m=5, T=5e-6)
    ...      .then("burst_significance", background_window=0.05, significance_mode=2))
    >>> print(p.describe())
    burst_selection: Burst search and selection (L=30, m=5, T=5e-06)
    burst_significance: Burst significance (Li & Ma, Poisson tails, trials correction) (...)

``|`` composes as well — ``p | "iso_contours"``, ``p | ("iso_contours",
{"level": 1.5})``, or two pipelines end to end. A ``Pipeline`` is a value:
``then`` returns a new one, so a base pipeline can be branched safely.

Export and import
-----------------

Three carriers, one document:

.. list-table::
   :header-rows: 1
   :widths: 22 38 40

   * - Carrier
     - Write
     - Read
   * - JSON
     - ``p.to_json()``, ``p.save("run.json")``
     - ``Pipeline.from_json(text)``, ``Pipeline.load("run.json")``
   * - ``.pto`` container
     - ``p.to_pto("run.pto")``
     - ``Pipeline.from_pto("run.pto")``, ``Pipeline.load("run.pto")``
   * - mmfdb workflow
     - ``p.to_mmfdb(sources={...})``
     - ``Pipeline.from_mmfdb(doc)``

In a ``.pto`` the document goes under mmfdb's own item name,
``_mmfdb_workflow.definition`` (with ``_mmfdb_workflow.name`` and
``.version`` beside it), so the container carrying the results also carries
the recipe that produced them, and a reader that knows mmfdb finds it without
knowing tttrlib. Writing into an existing container extends it.

What the document states
------------------------

.. code-block:: json

    {
      "format": "tttrlib.pipeline",
      "format_version": 1,
      "version": 1,
      "name": "burst-analysis",
      "software": {"package": "tttrlib", "version": "0.27.0"},
      "sources": {"raw": {"path": "measurement.spc", "kind": "raw_measurement"}},
      "steps": [
        {"id": "burst_selection",
         "operation": "burst_selection",
         "operation_type": "burst_selection",
         "params": {"L": 30, "m": 5, "T": 5e-06},
         "inputs": {}, "outputs": {},
         "python": "tttrlib.pipeline:run_step",
         "software": {"package": "tttrlib", "version": "0.27.0"}}
      ]
    }

* ``format`` / ``format_version`` — the tttrlib document format. A document
  from a newer format version is **refused** by default (``strict=True``) and
  warned about with ``strict=False``, which is what a reader wants when it
  only means to display the pipeline.
* ``version`` — the mmfdb workflow schema version.
* ``software`` — the tttrlib version that wrote it, on the document and on
  every step, so a result can be traced to the build that produced it.
* ``operation`` — the tttrlib registry key; ``operation_type`` — the mmfdb
  controlled-vocabulary term for what the step *does*. They differ where they
  must: seven burst searches are all ``burst_selection``, and ``mle_green`` and
  ``mle_red`` are both ``burst_lifetime_fitting``. Every registry entry's
  ``operation_type`` is a defined mmfdb term (enforced by
  ``test/python/test_registry_matches_mmfdb.py``).
* ``python`` — ``tttrlib.pipeline:run_step``, the callable an mmfdb ``python``
  step imports to run a tttrlib step.

Working with mmfdb
------------------

``to_mmfdb`` writes the workflow in mmfdb's key names (``sources`` + ``steps``
with ``id`` / ``operation_type`` / ``software`` / ``inputs`` / ``params`` /
``python`` / ``outputs``) so ``mmfdb workflow run`` can execute it and a
deposit CIF can embed it verbatim. The tttrlib operation name travels as the
``tttrlib_operation`` parameter, because mmfdb's step key is the operation
*type*.

``from_mmfdb`` reads such a document back and keeps the steps tttrlib can run:
a workflow that stitches tttrlib with FRETBursts or a command-line tool loads
here as its tttrlib steps, since only their owner can run the others.

From the command line
---------------------

``tttr sm`` writes the document into the ``.pto`` it produces, so the artifact
carries the recipe::

    tttr sm data.spc --setup setups.json -o run.pto
    python -c "import tttrlib; print(tttrlib.Pipeline.from_pto('run.pto').describe())"

and it can run one instead of options::

    tttr sm data.spc --write-pipeline recipe.json   # emit, do not read the data
    tttr sm data.spc --pipeline recipe.json -o run.pto
    tttr sm data.spc --pipeline previous.pto -o rerun.pto   # repeat an earlier run

The document records the search *and* the companions that actually ran (BVA,
FRET-2CDE, per-detector MLE), each with the burst table as its input.

Run and replay
--------------

``p.run(value, adapters=...)`` executes the steps in order, threading the value
through. An *adapter* — ``lambda previous: (args, kwargs)`` — wires a step
whose input is not simply the previous output (a burst table plus the photon
stream it indexes, say)::

    bursts = tttrlib.Pipeline.load("run.pto").run(
        data, adapters={"burst_selection": lambda t: ((t, "sliding_window"), {})})

The reloaded document computes the same numbers as the pipeline that was
written — that identity is what
``test/python/test_pipeline_document.py`` pins, together with the JSON,
``.pto`` and mmfdb round trips.

A worked example, from discovery through export to replay, is in
:doc:`auto_examples/miscellaneous/plot_registry_composition`.
