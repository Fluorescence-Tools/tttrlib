# SPDX-License-Identifier: BSD-3-Clause
#
# See include/io_sm.h. A per-vendor format module: it depends on `io` and nothing
# else, because a reader for a file format has no business knowing about the
# photon-stream data model. TTTRHeader's statics forward here.
tttrlib_add_module(
    NAME     io_sm
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/io_sm.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util io
    EXTERNAL_DEPS tttrlib::json
    TEST_DIR test/python/tttr
)
