# SPDX-License-Identifier: BSD-3-Clause
tttrlib_add_module(
    NAME     clsm
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/CLSMImage.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/CLSMFrame.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/CLSMLine.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/CLSMPixel.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/DecayPhasor.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util core fcs
    EXTERNAL_DEPS tttrlib::json tttrlib::pocketfft
    SWIG_INTERFACES CLSM.i DecayPhasor.i
    TEST_DIR test/python/clsm
)
