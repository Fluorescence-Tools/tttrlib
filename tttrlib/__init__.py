from __future__ import annotations

# Add DLL directories so _tttrlib.pyd can find its dependencies.
# Supports both: pip-wheel layout (tttrlib.libs/) and conda layout (Library/bin/)
import os as _os
import sys as _sys

# DLL directories are only needed on Windows
if _sys.platform.startswith('win'):
    # 1. pip/delvewheel layout: tttrlib.libs/ sibling of tttrlib/
    _libs_dir = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), _os.pardir, 'tttrlib.libs'))
    if _os.path.isdir(_libs_dir):
        _os.add_dll_directory(_libs_dir)

    # 2. conda layout: Library/bin relative to the Python prefix
    _conda_bin = _os.path.join(_sys.prefix, 'Library', 'bin')
    if _os.path.isdir(_conda_bin):
        _os.add_dll_directory(_conda_bin)

del _os, _sys

# Import the SWIG-generated module and re-export all public symbols (ignoring __all__)
import tttrlib.tttrlib as _tttrlib_module
import sys as _sys

# Expose all non-private symbols from the SWIG module
for _name in dir(_tttrlib_module):
    if not _name.startswith('_'):
        globals()[_name] = getattr(_tttrlib_module, _name)
del _tttrlib_module, _name, _sys

from .ImageLocalizer import *

from ._experimental import ExperimentalWarning, experimental, mark_experimental

# Apply experimental marking to SWIG classes that are not yet stable API.
# Done here (post-import) so it works without a C++ rebuild.
if 'CLSMISM' in globals():
    mark_experimental(
        globals()['CLSMISM'],
        "CLSMISM is experimental and may change or be removed in a future "
        "release. Use with caution.",
    )

try:
    from ._version import version as __version__
except ImportError:
    __version__ = "0.0.0"
