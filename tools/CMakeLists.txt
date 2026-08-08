# SPDX-License-Identifier: BSD-3-Clause

OPTION(TTTRLIB_PTO_TUI "Build PTO terminal UI viewer (ptoview)" OFF)
OPTION(TTTRLIB_APE_TOOLS "Build Actually Portable Executable (APE) binaries" OFF)
# The tttr CLI moved to modules/cli, where it is a module like every other
# subsystem and its executable target is guarded on BUILD_LIBRARY.

ADD_EXECUTABLE(pto pto.c pto_read.c)
SET_TARGET_PROPERTIES(pto PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)

IF(TTTRLIB_PTO_TUI)
    TARGET_COMPILE_DEFINITIONS(pto PRIVATE PTO_HAS_TUI PTO_NO_MAIN)
    TARGET_SOURCES(pto PRIVATE ptoview.cpp)
    SET_TARGET_PROPERTIES(pto PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

    ADD_EXECUTABLE(ptoview ptoview.cpp pto_read.c)
    SET_TARGET_PROPERTIES(ptoview PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
ENDIF()
