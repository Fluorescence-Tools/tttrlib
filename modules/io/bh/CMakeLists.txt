# SPDX-License-Identifier: BSD-3-Clause
#
# See include/io_bh.h. Depends on `io` and nothing else; TTTRHeader's
# statics forward here so the public API is unchanged.
tttrlib_add_module(
    NAME     io_bh
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/io_bh.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/io_bh_set.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util io
    EXTERNAL_DEPS tttrlib::json
    SWIG_INTERFACES BhSet.i
    TEST_DIR test/python/tttr
)
