# SPDX-License-Identifier: BSD-3-Clause
#
# kinetics -- continuous-time Markov chain analysis (Gopich-Szabo, CTMC utilities)
#
tttrlib_add_module(
    NAME     kinetics
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/GopichSzabo.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  core util math
    SWIG_INTERFACES GopichSzabo.i
    TEST_DIR test/python/kinetics
)
