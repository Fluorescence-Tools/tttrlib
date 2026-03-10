from __future__ import annotations

# Import the SWIG-generated module and re-export all public symbols (ignoring __all__)
import tttrlib.tttrlib as _tttrlib_module

# Expose all non-private symbols from the SWIG module
for _name in dir(_tttrlib_module):
    if not _name.startswith('_'):
        globals()[_name] = getattr(_tttrlib_module, _name)
del _tttrlib_module, _name

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
