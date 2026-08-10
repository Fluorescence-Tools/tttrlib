# SPDX-License-Identifier: BSD-3-Clause
tttrlib_add_module(
    NAME     hmm
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/HMM.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/HMMBayes.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/HMMConstraints.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/HMMRestraints.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/HMMState.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/HMMSurrogate.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/HMMVB.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  math core burst decay simulation
    EXTERNAL_DEPS tttrlib::json
    SWIG_INTERFACES HMM.i HMMConstraints.i HMMRestraints.i HmmSurrogate.i
    TEST_DIR test/python/hmm
)
