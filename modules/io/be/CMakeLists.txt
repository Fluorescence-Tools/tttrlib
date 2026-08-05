# SPDX-License-Identifier: BSD-3-Clause
#
# See include/io_be.h. Depends on `io` and nothing else.
#
# Unlike the other io_* modules this one does not plug into TTTRHeader: a `.ttr`
# has no header to read, and the parameters it needs -- sample clock, laser rate,
# channel count, TDC calibration -- have nowhere to enter the container API yet.
# Until that is settled the decoder is reached directly, which keeps the format
# usable without pretending it fits an interface it does not.
tttrlib_add_module(
    NAME     io_be
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/io_be.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util io
    TEST_DIR test/python/tttr
)
