# SPDX-License-Identifier: BSD-3-Clause
#
# util -- plumbing shared across tttrlib.
#
# Verbosity, byte order, bit twiddling, progress reporting, CPU feature
# detection, parallel-for, bidirectional maps. The unifying property is not
# subject matter: it is that none of them knows what a photon is, and none of
# them is a numerical kernel — those live in the math module now.
#
# The numerical code (Mat.h, NeuralNet, i_lbfgs.h, Random.h, SimPcgRandom.h)
# was extracted into modules/math/ so that a module needing only Verbose no
# longer transitively pulls a matrix library or a neural net.
tttrlib_add_module(
    NAME     util
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/Verbose.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/ProgressMonitor.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/ProgressTicker.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
