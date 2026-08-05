# SPDX-License-Identifier: BSD-3-Clause
#
# pda -- photon distribution analysis.
#
# Nothing else in the tree includes Pda.h, so it comes out whole. It reaches
# down into the photon-stream data model (TTTR.h) and uses pocketfft for its
# convolutions, so it is a leaf of the include graph without being independent
# of core -- which is what DEPENDS records. Pda.cpp is also the worst
# amplification in the project: 460 source lines that used to preprocess to
# 138k, most of it json.hpp arriving through TTTR.h.
tttrlib_add_module(
    NAME     pda
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/Pda.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/PdaCallback.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util core hist
    EXTERNAL_DEPS tttrlib::pocketfft
    SWIG_INTERFACES Pda.i
    TEST_DIR test/python
)
