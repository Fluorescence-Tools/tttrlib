# SPDX-License-Identifier: BSD-3-Clause
#
# io -- what tttrlib knows about file formats, as data.
#
# Below `core`, not above it: a format description is data, and describing a
# format must not require the photon-stream data model. TTTRFormat.h includes
# <string> and <vector>; TTTRHeaderTypes.h is 53 #defines. That is the whole
# dependency surface, which is what lets a format module stay cheap and what
# lets the same shape be mirrored across a C plugin ABI later.
#
# The point of the module is the table. The same knowledge -- which container is
# called what, which extension it uses, which record types are valid in it --
# was written out six times in six shapes across five files, with nothing
# keeping them in step. Adding the Photonscore reader took eleven edits across
# five files, none of which the compiler would have caught.
tttrlib_add_module(
    NAME     io
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/IORegistry.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/FileIO.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/TTTRTags.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/TTTRStreamWriter.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util
    EXTERNAL_DEPS tttrlib::json
    TEST_DIR test/python/tttr
)
