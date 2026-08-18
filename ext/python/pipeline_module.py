# SPDX-License-Identifier: BSD-3-Clause
"""``tttrlib.pipeline`` -- the module an mmfdb workflow step names.

An mmfdb workflow runs a Python step by importing ``module:callable``
(``run_kind: python``), and every step :meth:`tttrlib.Pipeline.to_mmfdb`
writes names ``tttrlib.pipeline:run_step``. This module is that import path:
it re-exports the pipeline API, which lives in the extension's namespace
because it is generated into it by SWIG.

    >>> from tttrlib.pipeline import Pipeline, run_step
"""

from tttrlib import (                     # noqa: F401  (re-export)
    Pipeline,
    run_step,
    PIPELINE_FORMAT,
    PIPELINE_FORMAT_VERSION,
    MMFDB_WORKFLOW_VERSION,
    describe,
    defaults,
    resolve,
    compose,
    registry,
)

__all__ = ["Pipeline", "run_step", "PIPELINE_FORMAT", "PIPELINE_FORMAT_VERSION",
           "MMFDB_WORKFLOW_VERSION", "describe", "defaults", "resolve", "compose",
           "registry"]
