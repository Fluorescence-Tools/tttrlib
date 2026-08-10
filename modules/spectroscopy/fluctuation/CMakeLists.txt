# SPDX-License-Identifier: BSD-3-Clause
#
# fluctuation -- photon-counting histogram (PCH) and FIDA
#
tttrlib_add_module(
    NAME     fluctuation
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/PhotonCountingHistogram.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  core util
    SWIG_INTERFACES PhotonCountingHistogram.i
    TEST_DIR test/python/fluctuation
)
