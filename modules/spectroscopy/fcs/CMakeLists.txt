# SPDX-License-Identifier: BSD-3-Clause
#
# fcs -- fluorescence correlation spectroscopy & correlator
#
tttrlib_add_module(
    NAME     fcs
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/Correlator.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/CorrelatorCurve.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/CorrelatorPhotonStream.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util core
    SWIG_INTERFACES Correlator.i
    TEST_DIR test/python/clsm
)
