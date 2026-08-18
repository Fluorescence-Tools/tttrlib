.. _plugins:

Drop-in plugins
===============

tttrlib loads **binary plugins** at run time. A plugin is one shared library
(``tttrlib_<name>.so`` / ``.dylib`` / ``.dll``) that exports a single C
function, ``tttrlib_plugin_init_v1``; dropped into a directory tttrlib
searches, it adds capabilities without a rebuild, a Python package or any
change to tttrlib itself. The interface is a versioned C ABI
(``modules/plugin/include/tttrlib_plugin.h``), so a plugin can be built with
any compiler and any language that can export a C symbol.

Nothing loads at ``import tttrlib``. Plugins are loaded once per process, the
first time a lookup could be answered by one (a file is opened by name, a
registry category is listed, a correlation method is set, ...).

Where tttrlib looks
-------------------

In this order; the first library of a given name wins:

1. every directory in ``TTTRLIB_PLUGIN_PATH`` (``:``-separated, ``;`` on
   Windows) -- for trying a plugin out before installing it;
2. the ``plugins/`` directory beside the tttrlib package itself
   (``site-packages/tttrlib/plugins/``);
3. the per-user directory: ``~/.local/share/tttrlib/plugins`` (Linux,
   ``$XDG_DATA_HOME`` honoured), ``~/Library/Application Support/tttrlib/plugins``
   (macOS), ``%LOCALAPPDATA%\tttrlib\plugins`` (Windows).

World-writable directories are never searched, and neither is the current
working directory. ``TTTRLIB_PLUGINS=0`` switches loading off;
``TTTRLIB_PLUGINS=only:example,other`` pins the set that may load.

What loaded, and why something did not, is in the registry::

    >>> import tttrlib
    >>> tttrlib.registry("plugin")
    {'example': {'status': 'loaded', 'version': '1.0.0', 'path': '...', 'sha256': '...'}}

A plugin whose ``init`` fails is rolled back completely: nothing it registered
before failing stays visible, and its record says what went wrong.

What a plugin can contribute
----------------------------

Six capability tables, each a C struct with a ``struct_size`` field so an old
plugin keeps working against a newer host and vice versa:

.. list-table::
   :header-rows: 1
   :widths: 30 35 35

   * - Table
     - Adds
     - Reached from Python as
   * - ``tttrlib_container_v1``
     - a file format: sniffer, header, record decoder
     - ``tttrlib.TTTR(path, "NAME")``, auto-detection,
       ``registry("file_container")``
   * - ``tttrlib_decay_fit_v1``
     - a decay-fit model (create / evaluate; the host runs its own optimiser)
     - ``tttrlib.DecayFit2("model_name")``, ``registry("fit")``
   * - ``tttrlib_burst_search_v1``
     - a burst search (arrival times in, index pairs out)
     - ``tttr.burst_search_by_name("name", **params)``,
       ``tttr.burst_search(..., mode="name")``, ``registry("burst_search")``
   * - ``tttrlib_operation_v1``
     - a self-describing pipeline operation (schema, inputs, outputs, execute)
     - ``registry("operation")``, the burst pipeline
   * - ``tttrlib_correlation_method_v1``
     - a correlation kernel on the host's lag axis
     - ``Correlator.method = "name"``,
       ``Correlator.correlation_method_names()``
   * - ``tttrlib_decay_prior_v1``
     - a prior kind for the decay fits (``lnpdf``, mode, support)
     - ``DecayFitPrior.from_json_string('{"kind": "name", ...}')``,
       ``DecayFitPrior.kinds()``, ``DecayFitConstraints.set_prior_json``

The example plugin
------------------

``examples/plugin/tttrlib_example.c`` registers one of each from a single
library and is what ``test/python/plugin/test_plugins.py`` exercises. Build it
with nothing but a C compiler and the header::

    cc -shared -fPIC -O2 -o tttrlib_example.so examples/plugin/tttrlib_example.c \
       -I modules/plugin/include          # .dylib on macOS; cl /LD on Windows

or with ``-DTTTRLIB_BUILD_EXAMPLE_PLUGIN=ON`` in a CMake build. Then::

    $ TTTRLIB_PLUGIN_PATH=$PWD python
    >>> import tttrlib, json, numpy as np
    >>> tttrlib.registry("plugin")["example"]["status"]
    'loaded'
    >>> "direct_plugin" in tttrlib.Correlator.correlation_method_names()
    True
    >>> p = tttrlib.DecayFitPrior.from_json_string(json.dumps({"kind": "laplace", "mu": 1.5, "b": 0.5}))
    >>> p.kind(), p.mode()
    ('laplace', 1.5)
    >>> t = tttrlib.TTTR("some.spc", "SPC-130")
    >>> t.burst_search_by_name("interphoton_plugin", max_gap=500.0, min_photons=5)

Writing one
-----------

Everything a plugin may call arrives as a function pointer in the host table
(``tttrlib_host_v1``: ``log``, ``set_error``, and one ``register_*`` per
capability); a plugin links against no tttrlib library. The rules that matter:

* Check ``host->struct_size`` before touching a ``register_*`` that was
  appended later than the one you were built against; the header says, per
  field, which came after which.
* Register only inside ``init``. Call ``set_error`` before returning a non-OK
  status -- a bare failure code becomes a message nobody can act on.
* Buffers the host hands in are valid for the call only; a search that finds
  more than the buffer holds reports the total and is called again with a
  larger one, so results are never truncated silently.
* Names are refused if a built-in or another plugin owns them.

The header is the specification; ``modules/plugin/README.md`` maps each table
to the layer that consults it.
