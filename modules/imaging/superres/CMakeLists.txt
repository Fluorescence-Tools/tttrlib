# SPDX-License-Identifier: BSD-3-Clause
tttrlib_add_module(
    NAME     superres
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/CLSMSuperRes.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  math core clsm
    SWIG_INTERFACES CLSMSuperRes.i
    TEST_DIR test/python/clsm
)
