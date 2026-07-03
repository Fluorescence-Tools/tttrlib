.. _docs_warning_burndown:

Documentation Warning Burn-Down
===============================

The documentation build is allowed to pass only with warnings that are known,
categorized, and non-blocking. The CI page scanner and coverage checker still
fail on structural page errors, tracebacks, missing pages, and undocumented API
symbols.

Current Baseline
----------------

Last checked with:

.. code-block:: bash

   TTTRLIB_DATA=/Users/tpeulen/dev/tttr-data \
   BUILD_TIER=4 \
   TTTRLIB_DOCS_EXECUTE_EXAMPLES=0 \
   python -m sphinx -b html -T \
     -d doc/_build/doctrees-ci-check \
     doc doc/_build/html-ci-check

The current non-blocking warning categories are:

.. list-table::
   :widths: 26 12 36 26
   :header-rows: 1

   * - Category
     - Count
     - Owner
     - Burn-down target
   * - Missing bibliography keys
     - 0
     - Documentation maintainers
     - Keep cited keys in ``doc/references.bib`` and render them from
       ``doc/zreferences.rst``.
   * - ``ref.any`` unresolved references
     - 0
     - Documentation maintainers
     - Keep filenames, environment variables, options, and code fragments in
       literal markup.
   * - Orphan source pages
     - 0
     - Documentation maintainers
     - Include active pages in a toctree or mark intentionally orphaned pages.
   * - Notebook/docutils inline-markup warnings
     - 0
     - Documentation maintainers
     - Keep notebook Markdown math and inline literals valid for Sphinx.
   * - Undefined labels and glossary terms
     - 0
     - Documentation maintainers
     - Add labels/glossary terms or update references.
   * - Unknown lexer names
     - 0
     - Documentation maintainers
     - Use supported lexer names such as ``bat`` or ``console``.
   * - Generated gallery metadata references
     - 0
     - Documentation maintainers
     - Clean gallery README references after example naming stabilizes.

Non-Blocking Rationale
----------------------

The current tracked baseline is zero warnings. Any new Sphinx warning should be
treated as a regression unless it is explicitly categorized here with a temporary
burn-down owner and target.

Required Checks
---------------

Every documentation change must still pass:

.. code-block:: bash

   python tools/check_docs_pages.py doc/_build/html-ci-check
   python tools/check_docs_coverage.py doc/api-coverage.yml doc doc/_build/html-ci-check
