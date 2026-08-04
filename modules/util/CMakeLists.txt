# SPDX-License-Identifier: BSD-3-Clause
#
# util -- the bottom layer: verbosity, CPU feature detection and SIMD dispatch,
# the parallel-for shim, the random-number facades, a bidirectional map, bit
# operations, string encoding and a progress monitor.
#
# Everything may depend on this and it depends on nothing, which is what makes
# it the right thing to pull out before the tangled middle of the library. Only
# two of these have an implementation file; the rest are headers, and naming
# them as a unit is mostly about making "who is allowed to use this" explicit
# rather than about compilation.
tttrlib_add_module(
    NAME     util
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/Verbose.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/ProgressMonitor.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    TEST_DIR test/python/misc
)
