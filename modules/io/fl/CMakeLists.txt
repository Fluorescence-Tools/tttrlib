# SPDX-License-Identifier: BSD-3-Clause
#
# io_fl -- the FLIM LABS time-tagger readers, STT1 and ITT1.
#
# Depends on `io` and nothing else, like every other vendor module: it turns a
# file into arrays and makes the one decision the format forces -- what integer
# tick a floating-point nanosecond becomes -- which is a fact about the format,
# not about the photon-stream data model.
#
# The JSON dependency is the header: all five FLIM LABS `.bin` formats carry
# their acquisition settings as a JSON blob behind the magic, and the laser
# period in it is what makes the times quantisable at all.
tttrlib_add_module(
    NAME     io_fl
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/io_fl.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util io
    EXTERNAL_DEPS tttrlib::json
    TEST_DIR test/python/tttr
)
