# SPDX-License-Identifier: BSD-3-Clause
#
# The example plugin, built as part of the tree so the test suite can prove the
# drop-in path end to end rather than describing it.
#
# Note what this target does NOT do: link tttrlib. A plugin resolves nothing
# from the host at link time — every host function arrives as a pointer in the
# host table — and building it that way here is what keeps that property honest.
# It only needs the one public header.
#
# Off by default so an ordinary build does not pay for it; the test fixture and
# CI turn it on with -DTTTRLIB_BUILD_EXAMPLE_PLUGIN=ON.
option(TTTRLIB_BUILD_EXAMPLE_PLUGIN "Build the example drop-in plugin" OFF)

if(NOT TTTRLIB_BUILD_EXAMPLE_PLUGIN)
    return()
endif()

add_library(tttrlib_example MODULE tttrlib_example.c)

# The filename is the discovery contract: tttrlib probes exactly
# `tttrlib_<name>.<ext>` so that a plugin's own dependency libraries can sit
# beside it without being mistaken for plugins.
set_target_properties(tttrlib_example PROPERTIES
    PREFIX ""
    OUTPUT_NAME "tttrlib_example"
    C_VISIBILITY_PRESET hidden)

if(APPLE)
    set_target_properties(tttrlib_example PROPERTIES SUFFIX ".dylib")
endif()

target_include_directories(tttrlib_example
    PRIVATE "${CMAKE_SOURCE_DIR}/modules/plugin/include")
