import numpy as np
import sys
import typing
import _tttrlib
import tttrlib
from importlib.metadata import version

try:
    __version__ = version(__package__ or __name__)
except:
    __version__ = "0.0.0"