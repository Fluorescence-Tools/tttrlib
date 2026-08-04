# SPDX-License-Identifier: BSD-3-Clause
#
# imaging -- confocal images and correlation.
#
# CLSMImage, its frame/line/pixel hierarchy, the correlator and the phasor
# transform, in one module because they cannot be separated: CLSMImage,
# Correlator and DecayPhasor are mutual friends and include each other's
# headers, and CMake will not accept a link cycle between shared libraries.
#
# Co-locating them is a decision, not an oversight, and it costs nothing in the
# bindings -- `tttrlib.Correlator` is named by its SWIG fragment, not by which
# library it happens to live in. The exit is to replace the three `friend`
# declarations with a narrow accessor; see modules/MODULE-DEBT.md.
#
# Closed against everything above core: nothing here includes a header owned by
# decay, burst, hmm or nn. That is what made it extractable in one step.
tttrlib_add_module(
    NAME     imaging
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/CLSMImage.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/CLSMFrame.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/CLSMLine.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/CLSMPixel.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/Correlator.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/CorrelatorCurve.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/CorrelatorPhotonStream.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/DecayPhasor.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util core
    EXTERNAL_DEPS tttrlib::json tttrlib::pocketfft
    SWIG_INTERFACES CLSM.i Correlator.i DecayPhasor.i
    TEST_DIR test/python/clsm
)
