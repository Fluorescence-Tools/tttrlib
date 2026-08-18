# SPDX-License-Identifier: BSD-3-Clause
#
# cli -- the tttr command-line runner.
#
# One executable, many jobs: format conversion, FCS correlation, CLSM image
# export, burst search, and the PTO container (explorer subcommands plus a
# terminal UI). It is a module like every other subsystem so the sources are
# claimed and dependency-checked; the executable target below is only built
# when there is a shared library to link it against (BUILD_LIBRARY=ON), because
# the whole point of the binary is to sit next to libtttrlib and find it via a
# relative rpath.
#
# main.cpp is claimed here too, so it is covered by the finalize check and the
# library contains a main symbol; the executable's own copy of main() wins at
# link time, which is the standard behaviour of ELF/Mach-O interposition.

tttrlib_add_module(
    NAME     cli
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_common.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cli_progress.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/detector_setup.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_detectors.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_convert.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_correlate.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_image.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_formats.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_pto.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_tui.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_sm.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_sim.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cli_main.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    # io_csv: cmd_sm.cpp includes io_csv_writer.h to write the burst tables. The
    # module system grants a module only its own and its declared dependencies'
    # include directories, so an undeclared edge is a "file not found", not a
    # link error later.
    # decay: cmd_sm.cpp fits one lifetime per burst through DecayFitModel.h.
    DEPENDS  util core simulation fcs clsm superres localization burst decay io io_store io_pto io_image io_csv
    EXTERNAL_DEPS tttrlib::json tttrlib::cxxopts
)

# No cli module, no tttr binary: main.cpp is the module's front door.
IF(BUILD_LIBRARY AND WITH_CLI)
    ADD_EXECUTABLE(tttr "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
    SET_TARGET_PROPERTIES(tttr PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

    # Land the binary in <build>/bin (not <build>/modules/cli). The install
    # RPATH is "@loader_path/../lib", which only resolves when bin/ and lib/ are
    # siblings the way an install puts them; mirroring that here means the build
    # tree binary runs without DYLD_LIBRARY_PATH, and a <env>/bin/tttr symlink
    # pointing at it picks up every rebuild with no reinstall.
    SET_TARGET_PROPERTIES(tttr PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    # Flat includes: headers say #include "TTTR.h", so every module include dir
    # plus the vendored third-party headers must be on the path. Being the last
    # module added, the global property is complete at this point.
    GET_PROPERTY(TTTR_TOOL_MODULE_DIRS GLOBAL PROPERTY TTTRLIB_MODULE_INCLUDE_DIRS)
    TARGET_INCLUDE_DIRECTORIES(tttr PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${TTTR_TOOL_MODULE_DIRS}
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/thirdparty
        ${CMAKE_SOURCE_DIR}/thirdparty/nlohmann_json/include
        ${CMAKE_SOURCE_DIR}/thirdparty/cxxopts
    )

    TARGET_LINK_LIBRARIES(tttr PRIVATE ${PROJECT_NAME}Shared)

    # The tool installs to bin/, the shared lib to lib/: look one dir up so a
    # conda/native install finds libttttrlib without DYLD_LIBRARY_PATH, which
    # macOS SIP strips from the environment of anything but the shell.
    #
    # SKIP_INSTALL_RPATH OFF + BUILD_WITH_INSTALL_RPATH ON force this onto the
    # target even inside conda-build, where conda sets CMAKE_SKIP_INSTALL_RPATH
    # globally. The CLI binary is not one of the library targets conda
    # post-processes itself.
    #
    # `BUILD_WITH_INSTALL_RPATH` is OFF, and that is load-bearing. With it ON the
    # build-tree binary carries the *install* rpath and nothing else — and
    # `@loader_path/../lib` does not resolve in a build tree, where the library
    # lands at `<build>/libtttrlib.dylib` rather than `<build>/lib/`. That was
    # survivable while it was the only entry (the binary failed loudly and a
    # DYLD_LIBRARY_PATH fixed it); it stopped being survivable the moment a
    # second entry was added for libomp, because a conda prefix commonly holds a
    # `libtttrlib.dylib` symlink into some *other* build tree, and the tool then
    # started silently — running a stale library, with the wrong subcommands and
    # no error anywhere. A loud failure is a bug; a quiet wrong answer is worse.
    #
    # OFF gives the build tree the rpath CMake computes from what it actually
    # linked (its own library, and libomp's directory), and the install keeps the
    # relocatable one below. libomp needs naming there because conda's and
    # Homebrew's both carry an `@rpath/libomp.dylib` install name and neither
    # ships beside libtttrlib.
    IF(APPLE)
        SET(TTTR_RPATH "@loader_path/../lib")
        IF(TTTRLIB_OMP_DIR)
            LIST(APPEND TTTR_RPATH "${TTTRLIB_OMP_DIR}")
        ENDIF()
        SET_TARGET_PROPERTIES(tttr PROPERTIES
                SKIP_INSTALL_RPATH OFF
                BUILD_WITH_INSTALL_RPATH OFF
                INSTALL_RPATH "${TTTR_RPATH}")
    ELSEIF(UNIX)
        SET(TTTR_RPATH "$ORIGIN/../lib")
        IF(TTTRLIB_OMP_DIR)
            LIST(APPEND TTTR_RPATH "${TTTRLIB_OMP_DIR}")
        ENDIF()
        SET_TARGET_PROPERTIES(tttr PROPERTIES
                SKIP_INSTALL_RPATH OFF
                BUILD_WITH_INSTALL_RPATH OFF
                INSTALL_RPATH "${TTTR_RPATH}")
    ENDIF()

    IF(INSTALL)
        INSTALL(TARGETS tttr RUNTIME DESTINATION bin)
    ENDIF()
ENDIF()