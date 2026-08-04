tttrlib plugin directory
========================

Drop a compiled tttrlib plugin into this directory and tttrlib picks up the
capability it provides the next time it is used -- no rebuild, no source, no
change to your code.

A plugin is a shared library named

    tttrlib_<name>.so      (Linux)
    tttrlib_<name>.dylib   (macOS)
    tttrlib_<name>.dll     (Windows)

Only files matching that pattern are probed, so a plugin's own dependency
libraries can sit beside it in this directory without being mistaken for
plugins themselves.

What a plugin can add
---------------------
A plugin registers into tttrlib's runtime tables, so what it adds is reachable
through the entry points that already take a name:

    a file format      tttrlib.TTTR(filename, "MYLAB")
                       tttr.write(filename, "MYLAB")
                       and it appears in get_supported_container_names()
    a fit model        tttrlib.DecayFit2("mymodel", setup, irf)
                       and it appears in tttrlib.fit_names()
    a burst search     tttr.burst_search_by_name("mysearch", **params)

A plugin cannot add a new Python class: the language bindings are generated at
build time, so there is no runtime path to synthesise one. If you need genuinely
new types, write an ordinary Python package that imports tttrlib -- that is the
supported answer for that case, and it always has been.

Where tttrlib looks
-------------------
In order, first match by plugin name wins:

    1. every directory in $TTTRLIB_PLUGIN_PATH
    2. this directory
    3. the per-user directory
         $XDG_DATA_HOME/tttrlib/plugins            (Linux)
         ~/Library/Application Support/tttrlib/plugins  (macOS)
         %LOCALAPPDATA%\tttrlib\plugins            (Windows)

The current working directory and the directory of the file being opened are
never searched.

Controlling it
--------------
    TTTRLIB_PLUGINS=0            load no plugins at all
    TTTRLIB_PLUGINS=only:mylab   load only the plugin named "mylab"

Pinning matters more than it looks: a published result should not depend on
what happened to be sitting in this directory on the day it was produced.

Checking what loaded
--------------------
    import tttrlib, json
    print(json.dumps(tttrlib.registry("plugin"), indent=2))

Every plugin is listed with its status (loaded / failed / quarantined /
conflict / shadowed), version, absolute path and sha256. A plugin that fails to
load is reported there rather than raising -- importing tttrlib cannot be broken
by whatever is in this directory.

Writing one
-----------
Include tttrlib_plugin.h (or the header-only C++ helper tttrlib_plugin.hpp) and
implement tttrlib_plugin_init_v1. The boundary is plain C by design, so a plugin
built with a different compiler, standard library or runtime than tttrlib itself
still loads. See examples/plugin/ in the tttrlib source tree for a complete,
working example.
