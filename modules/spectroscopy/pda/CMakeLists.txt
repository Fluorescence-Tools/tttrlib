# SPDX-License-Identifier: BSD-3-Clause
tttrlib_add_module(
    NAME     pda
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/Pda.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/PdaCallback.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/PdaBurstLikelihood.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/Pda3cCore.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util math core
    EXTERNAL_DEPS tttrlib::pocketfft
    SWIG_INTERFACES Pda.i Pda3cCore.i
    TEST_DIR test/python/pda
)
