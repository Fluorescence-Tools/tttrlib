# SPDX-License-Identifier: BSD-3-Clause
#
# plugin -- the drop-in binary plugin ABI, host and loader.
#
# Sits directly on `io` and on nothing else, which is what makes the whole idea
# work: the capability a plugin contributes is a row in the format table, and
# that table was deliberately built as data with no dependency on the photon
# stream model. A plugin host that needed `core` could not be reached from
# beneath `core`, and the format table is beneath it.
#
# `${CMAKE_DL_LIBS}` is libdl where the platform has one and empty where the
# loader is in libc. Nothing else is linked: the boundary is plain C by design,
# so that a plugin built with a different compiler, standard library or runtime
# than tttrlib still loads.
tttrlib_add_module(
    NAME     plugin
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/PluginHost.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util io
    TEST_DIR test/python/plugin
)

target_link_libraries(tttrlib_plugin PRIVATE ${CMAKE_DL_LIBS})

# The host reports its own version to every plugin, so a plugin can refuse a
# host it was not built against.
target_compile_definitions(tttrlib_plugin
    PRIVATE TTTRLIB_VERSION_STRING="${PROJECT_VERSION}")
