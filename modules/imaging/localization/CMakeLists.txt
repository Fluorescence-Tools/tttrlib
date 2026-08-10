# SPDX-License-Identifier: BSD-3-Clause
tttrlib_add_module(
    NAME     localization
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/ImageLocalization.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  math core clsm
    EXTERNAL_DEPS tttrlib::autodiff
    SWIG_INTERFACES Localization.i
    TEST_DIR test/python/clsm
)
