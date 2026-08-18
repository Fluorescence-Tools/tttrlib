# SPDX-License-Identifier: BSD-3-Clause
#
# sim -- the diffusion / photon-emission simulator.
#
# The largest leaf: 18 headers and 6 sources whose only reach into the rest of
# the project is Random.h. It does not depend on core at all -- it produces
# photon streams rather than reading them.
#
# The edge runs the other way, and it is recorded debt: HMMEmission.h includes
# SimDecay.h and NeuralNet.cpp includes SimPcgRandom.h, so `hmm` and `nn` -- both
# still inside `legacy` -- depend on the simulator. That is why `legacy` names
# `sim` in DEPENDS rather than the reverse.
tttrlib_add_module(
    NAME     simulation
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/SimEngine.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/SimGrid.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/SimMicrotimeEncoder.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/SimRandom.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/SimSystem.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/SimVectorGrid.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  math core          # Random.h, SimPcgRandom.h — via SimCounterRandom.h
    EXTERNAL_DEPS tttrlib::highfive
    SWIG_INTERFACES Sim.i
    TEST_DIR test/python/simulation
)
