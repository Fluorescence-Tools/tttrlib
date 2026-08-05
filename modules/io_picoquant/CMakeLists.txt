# SPDX-License-Identifier: BSD-3-Clause
#
# See include/io_picoquant.h. Depends on `io` and nothing else; TTTRHeader's
# statics forward here so the public API is unchanged.
tttrlib_add_module(
    NAME     io_picoquant
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/io_picoquant.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util io
    EXTERNAL_DEPS tttrlib::json
    TEST_DIR test/python/tttr
)
