import numpy as np
import sys
import typing

if sys.version_info[0] < 3:
    from importlib_metadata import version
else:   
    from importlib.metadata import version

try:
    __version__ = version(__package__ or __name__)
except:
    __version__ = "0.0.0"