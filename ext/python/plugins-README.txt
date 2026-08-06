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
through the entry points that already take a name.

A **file format**:

    tttrlib.TTTR(filename, "MYLAB")     read it by name
    tttrlib.TTTR(filename)              or by content, if it has a sniffer
    tttrlib.registry("file_container")["MYLAB"]
    tttrlib.TTTR.get_supported_container_names()

A **decay fit model**:

    tttrlib.DecayFit2("mymodel")        construct it by name
    tttrlib.registry("fit")["mymodel"]  with the parameter schema it published
    tttrlib.decay_fit_names()

A fit model supplies the objective and nothing else; tttrlib wraps it in its own
bounded, constraint-aware optimiser, so a plugin fit behaves exactly like a
built-in one.

Burst searches are the remaining capability on the same ABI; the host table
grows a register_* entry without disturbing an existing plugin (see
"struct_size" in tttrlib_plugin.h), so a plugin built against today's header
keeps working when they arrive.

Writing a format is read-only for now. A plugin container reports can_write
false and tttr.write(filename, "MYLAB") is refused rather than silently writing
something else.

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
Include tttrlib_plugin.h and implement tttrlib_plugin_init_v1. The boundary is
plain C by design, so a plugin built with a different compiler, standard library
or runtime than tttrlib itself still loads -- and a plugin links nothing from
tttrlib at all, because everything it may call arrives as a function pointer in
the host table:

    cc -shared -fPIC -O2 -o tttrlib_mylab.so mylab.c \
       -I<tttrlib>/modules/plugin/include

See examples/plugin/tttrlib_example.c in the tttrlib source tree for a
complete, working, commented example -- it is the same file the test suite
loads to prove that this directory works.
