.. _plugins:

Plugins: adding a format without rebuilding
============================================

Drop a compiled plugin into a directory and the capability it provides is there
the next time tttrlib runs. No rebuild of tttrlib, no source, no change to the
calling code:

.. code-block:: python

   import tttrlib

   data = tttrlib.TTTR("measurement.mylab", "MYLAB")   # by name
   data = tttrlib.TTTR("measurement.mylab")            # or by content

A plugin can add a **file format**, a **decay fit model** or a **burst search**:

.. code-block:: python

   fit = tttrlib.DecayFit2("mymodel")                    # a model from a plugin
   bursts = tttr.burst_search_by_name("mysearch", **p)   # a search from one
   tttrlib.registry("fit")["mymodel"]                    # with its schema

All three are reachable through the entry points that already take a name, so
nothing calling tttrlib has to know a plugin is involved.

Where tttrlib looks
-------------------

In order, with the first match by plugin *name* winning:

1. every directory in ``$TTTRLIB_PLUGIN_PATH``
2. ``<the tttrlib package>/plugins/``
3. the per-user directory: ``$XDG_DATA_HOME/tttrlib/plugins`` (Linux),
   ``~/Library/Application Support/tttrlib/plugins`` (macOS),
   ``%LOCALAPPDATA%\tttrlib\plugins`` (Windows)

Only files named ``tttrlib_<name>.{so,dylib,dll}`` are probed, so a plugin's own
dependency libraries can sit beside it without being mistaken for plugins. Load
order within a directory is case-folded filename order, so a bug is reproducible
rather than filesystem-dependent.

The current working directory and the directory of the file being opened are
**never** searched. That pair is the whole of the DLL-hijacking attack -- a
shared instrument drive holding a data file and a helpful-looking library next
to it -- and it is the one case where the convenience is not worth it.

Controlling it
--------------

.. code-block:: shell

   TTTRLIB_PLUGINS=0            # load nothing
   TTTRLIB_PLUGINS=only:mylab   # load only this one

Pinning matters more than it looks. A published result should not depend on what
happened to be sitting in a directory the day it was produced, and
``only:`` is how an analysis says which capabilities it meant to use.

Seeing what happened
--------------------

.. code-block:: python

   import tttrlib, json
   print(json.dumps(tttrlib.registry("plugin"), indent=2))

Every candidate is listed with its status -- ``loaded``, ``failed``,
``quarantined``, ``shadowed`` or ``disabled`` -- plus its version, absolute path,
sha256 and what it contributed. A plugin that fails to load is **reported there
rather than raised**: importing tttrlib cannot be broken by whatever is in a
plugin directory, and a diagnostic that scrolled past the terminal is not a
diagnostic.

The sha256 is there for provenance. "Which binary produced this figure" is a
question a published result has to be able to answer.

.. _plugin_abi:

Writing one
-----------

Include ``tttrlib_plugin.h`` and implement ``tttrlib_plugin_init_v1``:

.. code-block:: c

   #include "tttrlib_plugin.h"

   static tttrlib_container_v1 kContainer = {
       sizeof(tttrlib_container_v1),
       "MYLAB", "My Lab time tagger", NULL, "mylab", NULL, 1,
       my_sniff, my_open, NULL, NULL, my_read, my_close, NULL
   };

   TTTRLIB_PLUGIN_EXPORT int
   tttrlib_plugin_init_v1(const tttrlib_host_v1* host, tttrlib_plugin_info_v1* info) {
       info->name = "mylab";
       info->version = "1.0.0";
       return host->register_container(&kContainer);
   }

.. code-block:: shell

   cc -shared -fPIC -O2 -o tttrlib_mylab.so mylab.c -I<tttrlib>/modules/plugin/include

Note what is *not* on that command line: any tttrlib library. A plugin resolves
nothing from the host at link time -- everything it may call arrives as a
function pointer in the host table -- which is what lets it be built with a
different compiler, standard library and runtime than tttrlib itself.

``examples/plugin/tttrlib_example.c`` is a complete, commented, working example
providing a container, a fit model and a burst search from one library. It is the same file the test suite
loads to prove this page is true.

A fit model implements ``create``, ``n_parameters``, ``evaluate`` and
``destroy``, and that is all. It does **not** implement the optimiser: the host
wraps the objective in the library's own bounded, constraint-aware L-BFGS, so a
plugin fit and a built-in fit differ in the objective and in nothing else. That
is deliberate — writing the optimiser is most of the work of a fit model and
none of the photophysics, and every plugin doing it separately would mean every
plugin doing it slightly differently.

A model must publish a ``params_schema``. Without one a caller is back to
counting slots in a flat array, which is the thing the registry exists to
abolish, so registration is refused rather than accepted with a gap.

A burst search is the smallest table of the three -- arrival times in, index
ranges out, one call, no handle. It is dispatched by *name* rather than by
attribute, because a plugin has no method on ``TTTR`` to reach: its registry
entry carries ``provider: "plugin"`` and no ``method``, and that absence is the
signal. If it finds more bursts than the buffer it was given holds, it reports
the total it would have written and the host calls again with a bigger one --
returning the first N bursts of a measurement would be a wrong answer that
looks like a right one.

The five contracts
------------------

The boundary is plain C, and these are the rules that keep it stable. They are
stated in full in ``tttrlib_plugin.h``; in brief:

**Version negotiation is by symbol name.** The host looks up
``tttrlib_plugin_init_v1``. A future ABI exports a *different* symbol, so a v1
plugin keeps working forever and a v2-only plugin on an old host gets a clean
"needs a newer tttrlib" rather than a crash.

**Forward compatibility inside a version is by** ``struct_size``, the first field
of every struct. Fields may only be appended. This is how the host table grows a
``register_decay_fit`` without disturbing a plugin compiled today.

**Event buffers are always host-allocated.** The plugin fills what it was given
and says how much it wrote. It never frees host memory or vice versa, which
makes the classic cross-runtime free mismatch structurally impossible on the
path that runs once per photon.

**No exception crosses, in either direction.** Host callbacks are ``noexcept``;
plugin entry points return status codes. ``TTTRLIB_UNSUPPORTED`` is distinct from
an error -- a sniffer saying "not mine" is a normal answer.

**Strings** passed in are valid for the call; strings passed back are owned by
the plugin until ``close``.

A plugin may not take a name the library already uses -- for a format or for a
fit model. Registration is refused, not shadowed: a plugin that could silently
replace the built-in ``fit23`` or the PTU reader would be a supply-chain
problem rather than a feature.

What a plugin cannot do
-----------------------

Add a Python class. The language bindings are generated at build time, so there
is no runtime path to synthesise one. If you need genuinely new types, write an
ordinary Python package that imports tttrlib -- that is the supported answer,
and it always has been.

Containment, and its limits
---------------------------

A plugin that fails to load, fails to initialise, or registers something invalid
is caught, has every registration it made rolled back, and is marked failed.
Nothing else changes. It is never unloaded -- ``dlclose`` after static
constructors have run is a well-known way to crash at exit.

A plugin that *hard-crashes* takes the process with it, exactly as a bad numpy
extension does. There is no sandbox here and pretending otherwise would be
worse than saying so. The proportionate mitigations are: ``RTLD_NOW``, so a
missing symbol is a diagnosis at load rather than a crash mid-file;
``TTTRLIB_PLUGINS=0``; and a quarantine marker written before the library is
opened and removed once it survives -- so a plugin that killed the last process
is skipped on the next run instead of making Python unstartable.

Directories that are world-writable are refused outright. The temp directory is
refused too, but only when tttrlib went looking there itself: a path you named
in ``TTTRLIB_PLUGIN_PATH`` is a deliberate act, and trying a plugin out before
installing it is exactly what that variable is for.
