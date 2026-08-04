# SPDX-License-Identifier: BSD-3-Clause
#
# nn -- a small feed-forward neural network.
#
# One source and one header. Used by the HMM surrogate; it needs the simulator's
# PCG generator and Eigen, and nothing else -- not imaging, despite what the
# module graph in the plan assumed.
#
# This is the second of the two Eigen consumers. With `localization` and `nn`
# both declaring it, Eigen no longer has to be REQUIRED for the whole project.
tttrlib_add_module(
    NAME     nn
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/NeuralNet.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util sim
    EXTERNAL_DEPS tttrlib::json tttrlib::eigen
    SWIG_INTERFACES NeuralNet.i
    TEST_DIR test/python/hmm
)
