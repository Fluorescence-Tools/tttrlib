# SPDX-License-Identifier: BSD-3-Clause
#
# util -- what everything else needs and nothing else owns.
#
# Verbosity, byte order, bit twiddling, progress reporting, RNGs, an L-BFGS
# solver and a small feed-forward network. The unifying property is not subject
# matter: it is that none of them knows what a photon is, so nothing above has
# to be rebuilt when one changes.
#
# `opt` (i_lbfgs.h) and `nn` (NeuralNet) were separate modules and are folded in
# here. Neither touches the data model -- L-BFGS minimises a function and the
# network multiplies matrices -- and each was one file with a module around it.
#
# SimPcgRandom.h moved here from `sim` for the same reason: it is a PRNG, named
# for where it happened to land rather than what it does, and it was the only
# thing making the network depend on the simulator.
#
# The cost is that util now carries json and eigen on its PUBLIC interface, so
# every module inherits those include directories. Both are header-only
# INTERFACE targets -- no code, no link -- which is what makes the trade
# acceptable rather than merely convenient.
tttrlib_add_module(
    NAME     util
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/Verbose.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/ProgressMonitor.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/ProgressTicker.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/NeuralNet.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    EXTERNAL_DEPS tttrlib::json tttrlib::eigen
    SWIG_INTERFACES NeuralNet.i
    TEST_DIR test/python/misc
)
