# SPDX-License-Identifier: BSD-3-Clause
#
# io_ps -- the Photonscore LINCam ".photons" (D7) reader and writer.
#
# The first per-vendor format module, and the one that shows the split is real:
# it depends on `io` and on nothing else. Not on core, not on TTTR -- the reader
# decodes a file into arrays, and the LINCam's x/y positions become marker
# events, which is a decision about the format and not about the photon-stream
# data model.
#
# That is the shape every io_* module should have. A format module that needs
# TTTR.h has not been separated, it has just been moved, and it could not be
# replaced by a plugin -- which is the point of the exercise.
tttrlib_add_module(
    NAME     io_ps
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/PhotonscoreD7.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util io
    TEST_DIR test/python/tttr
)
