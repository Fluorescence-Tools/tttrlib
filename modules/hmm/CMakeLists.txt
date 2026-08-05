# SPDX-License-Identifier: BSD-3-Clause
#
# hmm -- photon-by-photon hidden Markov models.
#
# The top of the graph, and the module that depends on the most: core for the
# photon stream, burst for selection, decay for the priors HMMRestraints reuses,
# sim for the emission model, nn for the surrogate.
#
# Three of those are reach-ins rather than design -- HMMEmission.h -> SimDecay.h,
# HMMRestraints.h -> DecayFitPrior.h, HMMSurrogate.h -> NeuralNet.h. They are
# recorded in modules/MODULE-DEBT.md rather than resolved here: now that the
# edges are declared they are at least visible, and the build fails if a new one
# appears undeclared.
tttrlib_add_module(
    NAME     hmm
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/HMM.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/HMMBayes.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/HMMConstraints.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/HMMRestraints.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/HMMState.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/HMMSurrogate.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util core burst decay sim
    EXTERNAL_DEPS tttrlib::json
    SWIG_INTERFACES HMM.i HMMConstraints.i HMMRestraints.i HmmSurrogate.i
    TEST_DIR test/python/hmm
)
