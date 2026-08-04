# SPDX-License-Identifier: BSD-3-Clause
#
# superres -- CLSM super-resolution reconstruction (SRRF/eSRRF, ISM, pixel
# reassignment).
#
# A leaf by its header -- CLSMSuperRes.h includes <vector> and <memory> and
# nothing of tttrlib's -- but not by its implementation, which reads images and
# photon streams. DEPENDS is legacy for now and becomes `core imaging` once
# those exist; recording it as a dependency rather than pretending it is
# free-standing is the point of declaring it at all.
tttrlib_add_module(
    NAME     superres
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/CLSMSuperRes.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util legacy
    EXTERNAL_DEPS tttrlib::pocketfft
    SWIG_INTERFACES CLSMSuperRes.i
    TEST_DIR test/python/clsm
)
