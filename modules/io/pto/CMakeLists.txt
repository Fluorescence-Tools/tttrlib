# SPDX-License-Identifier: BSD-3-Clause
#
# io_pto -- PTO, the PhoTon cOntainer: an EBML document with DocType "pto".
#
# The container that holds a whole measurement -- the photon stream, the bursts
# found in it, a spectrum, the original instrument file -- each with a UID that
# survives the file being rewritten, and typed tags that can reference those
# UIDs so an application can record how they relate.
#
# Depends on io_store because the natural payload for anything tabular is a
# .dstore, and embedding one costs nothing: io_store can write straight into an
# already-open file, so a photon stream goes into the container without a copy
# and without a temporary. Nothing else is needed -- like io_store, this module
# has no external dependency, so a build with BUILD_PHOTON_HDF=OFF has a
# container and a table format and needs no HDF5 at all.
tttrlib_add_module(
    NAME     io_pto
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/io_pto.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/pto_read.c"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/pto.c"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/ptoview.cpp"
    SWIG_INTERFACES Pto.i
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  io core io_store
    TEST_DIR test/python
)

OPTION(TTTRLIB_PTO_TUI "Build PTO terminal UI viewer (ptoview)" OFF)
OPTION(TTTRLIB_APE_TOOLS "Build Actually Portable Executable (APE) binaries" OFF)

ADD_EXECUTABLE(pto_tool "${CMAKE_CURRENT_SOURCE_DIR}/src/pto.c" "${CMAKE_CURRENT_SOURCE_DIR}/src/pto_read.c")
SET_TARGET_PROPERTIES(pto_tool PROPERTIES OUTPUT_NAME "pto" C_STANDARD 99 C_STANDARD_REQUIRED ON)
TARGET_INCLUDE_DIRECTORIES(pto_tool PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
TARGET_COMPILE_DEFINITIONS(pto_tool PRIVATE PTO_STANDALONE_BUILD)

IF(TTTRLIB_PTO_TUI)
    ADD_EXECUTABLE(ptoview "${CMAKE_CURRENT_SOURCE_DIR}/src/ptoview.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/pto_read.c")
    SET_TARGET_PROPERTIES(ptoview PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    TARGET_INCLUDE_DIRECTORIES(ptoview PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    TARGET_COMPILE_DEFINITIONS(ptoview PRIVATE PTOVIEW_STANDALONE_EXE)
ENDIF()
