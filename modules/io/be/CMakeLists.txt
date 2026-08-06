# SPDX-License-Identifier: BSD-3-Clause
#
# See include/io_be.h. Depends on `io` and nothing else.
#
# Unlike the other io_* modules this one does not plug into TTTRHeader: a `.ttr`
# has no header to read. What it needs instead -- sample clock, laser rate,
# channel count, TDC calibration -- arrives as container parameters, declared by
# the format's `params_schema` in the registry and parsed here by
# ttr_params_from_json(). That is why this module needs the JSON library: the
# parameters cross the language boundary as JSON so that no binding has to know
# this format exists.
tttrlib_add_module(
    NAME     io_be
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/io_be.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util io
    EXTERNAL_DEPS tttrlib::json
    TEST_DIR test/python/tttr
)
