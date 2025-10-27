"""CLSM memory and EGFP unit tests"""
from __future__ import division

import os
import unittest
import json
from pathlib import Path
import gc

import tttrlib

# Centralized test settings
from test_settings import settings, DATA_AVAILABLE  # type: ignore

