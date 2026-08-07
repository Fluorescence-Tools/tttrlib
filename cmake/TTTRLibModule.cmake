# SPDX-License-Identifier: BSD-3-Clause
#
# The module system.
#
# tttrlib is built as a single blob: one recursive glob of src/*.cpp feeds five
# targets, so the same sources are compiled up to five times and nothing
# separates TTTR I/O, imaging, correlation, fitting, bursts, HMM and the
# simulator. This file provides the declaration a subsystem needs in order to be
# a unit with its own sources, its own dependencies and its own library.
#
#   tttrlib_add_module(
#       NAME          <name>              # -> target tttrlib_<name>, lib tttrlib_<name>.so
#       SOURCES       <files...>          # .cpp files this module claims
#       HEADERS       <dirs...>           # PUBLIC include dirs (defaults to none)
#       DEPENDS       <module names...>   # other tttrlib modules
#       EXTERNAL_DEPS <targets...>        # e.g. tttrlib::json, tttrlib::eigen
#       SWIG_INTERFACES <files...>        # .i fragments this module contributes
#       TEST_DIR      <dir>               # test/python/<dir>, linked not moved
#       INTERFACE                         # header-only: no SOURCES, no library
#       OPTIONAL                          # gives the module a WITH_<NAME> switch
#   )
#
#   tttrlib_finalize_modules()
#
# The finalize step is the part that matters for correctness: it fails the
# configure if a source under src/ is claimed by two modules, or by none. "Did I
# forget a file" stops being a question anyone has to answer by reading.
#
# Boundary enforcement is by include path, not by rewriting includes. The include
# layout stays FLAT -- sources say #include "CLSMImage.h" -- and a module only
# gets the include directories of itself and its declared DEPENDS. A .cpp in one
# module that includes another's header simply fails to compile. That costs zero
# edited lines and keeps working for C++ consumers who write <tttrlib/TTTR.h>.

include_guard(GLOBAL)

# SHARED is the point of the exercise: a module per subsystem, loadable and
# replaceable on its own. STATIC exists for the consumers that need one fat
# native object -- the R package links libtttrlib_static.a through R CMD INSTALL,
# and the ImageJ loader extracts the native to a RANDOMISED temporary filename,
# which breaks DT_NEEDED/install-name matching outright.
set(TTTRLIB_MODULE_TYPE "SHARED" CACHE STRING "Linkage of tttrlib modules: SHARED or STATIC")
set_property(CACHE TTTRLIB_MODULE_TYPE PROPERTY STRINGS SHARED STATIC)

# Accumulated across tttrlib_add_module() calls; read by tttrlib_finalize_modules().
set_property(GLOBAL PROPERTY TTTRLIB_MODULE_LIST "")
set_property(GLOBAL PROPERTY TTTRLIB_CLAIMED_SOURCES "")
set_property(GLOBAL PROPERTY TTTRLIB_MODULE_INCLUDE_DIRS "")

# Point a target at its own directory for sibling libraries.
function(tttrlib_set_sibling_rpath target)
    if(APPLE)
        set_target_properties(${target} PROPERTIES
                INSTALL_RPATH "@loader_path"
                MACOSX_RPATH ON)
    elseif(UNIX)
        set_target_properties(${target} PROPERTIES INSTALL_RPATH "$ORIGIN")
    endif()
    set_target_properties(${target} PROPERTIES
            SKIP_BUILD_RPATH OFF
            SKIP_INSTALL_RPATH OFF
            INSTALL_RPATH_USE_LINK_PATH OFF)
endfunction()

function(tttrlib_add_module)
    set(options OPTIONAL INTERFACE)
    set(one_value NAME TEST_DIR)
    set(multi_value SOURCES HEADERS DEPENDS EXTERNAL_DEPS SWIG_INTERFACES)
    cmake_parse_arguments(M "${options}" "${one_value}" "${multi_value}" ${ARGN})

    if(NOT M_NAME)
        message(FATAL_ERROR "tttrlib_add_module: NAME is required")
    endif()
    if(M_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "tttrlib_add_module(${M_NAME}): unrecognised arguments: ${M_UNPARSED_ARGUMENTS}")
    endif()

    string(TOUPPER "${M_NAME}" M_UPPER)
    set(target "tttrlib_${M_NAME}")

    # An optional module gets a switch. Default ON so the shipped package is
    # API-identical no matter how it was configured.
    if(M_OPTIONAL)
        option(WITH_${M_UPPER} "Build the ${M_NAME} module" ON)
        if(NOT WITH_${M_UPPER})
            message(STATUS "module ${M_NAME}: DISABLED (WITH_${M_UPPER}=OFF)")
            set_property(GLOBAL APPEND PROPERTY TTTRLIB_CLAIMED_SOURCES ${M_SOURCES})
            return()
        endif()
    endif()

    # A header-only module is a real module: it declares a dependency edge and a
    # boundary, it just has nothing to compile. Reaching for one is what keeps a
    # shared header from being filed under whichever module happened to use it
    # first -- i_lbfgs.h sat in the HMM cluster and gave `decay` and
    # `localization` phantom dependencies on `hmm` for no reason at all.
    if(M_INTERFACE)
        if(M_SOURCES)
            message(FATAL_ERROR "tttrlib_add_module(${M_NAME}): INTERFACE modules have no SOURCES")
        endif()
        add_library(${target} INTERFACE)
        add_library(tttrlib::${M_NAME} ALIAS ${target})
        foreach(dir IN LISTS M_HEADERS)
            target_include_directories(${target} INTERFACE "${dir}")
            set_property(GLOBAL APPEND PROPERTY TTTRLIB_MODULE_INCLUDE_DIRS "${dir}")
        endforeach()
        foreach(dep IN LISTS M_DEPENDS)
            target_link_libraries(${target} INTERFACE tttrlib::${dep})
        endforeach()
        foreach(dep IN LISTS M_EXTERNAL_DEPS)
            target_link_libraries(${target} INTERFACE ${dep})
        endforeach()
        set_property(GLOBAL APPEND PROPERTY TTTRLIB_MODULE_LIST ${M_NAME})
        set_property(GLOBAL PROPERTY TTTRLIB_MODULE_${M_NAME}_SWIG "${M_SWIG_INTERFACES}")
        set_property(GLOBAL PROPERTY TTTRLIB_MODULE_${M_NAME}_TESTS "${M_TEST_DIR}")
        message(STATUS "module ${M_NAME}: header-only (INTERFACE)")
        return()
    endif()

    if(NOT M_SOURCES)
        message(FATAL_ERROR "tttrlib_add_module(${M_NAME}): no SOURCES")
    endif()

    add_library(${target} ${TTTRLIB_MODULE_TYPE} ${M_SOURCES})
    add_library(tttrlib::${M_NAME} ALIAS ${target})

    # Every module gets the project's own include dirs and defines, plus exactly
    # the third-party dependencies it declared.
    target_link_libraries(${target} PUBLIC tttrlib::build_config)
    foreach(dep IN LISTS M_DEPENDS)
        target_link_libraries(${target} PUBLIC tttrlib::${dep})
    endforeach()
    foreach(dep IN LISTS M_EXTERNAL_DEPS)
        target_link_libraries(${target} PUBLIC ${dep})
    endforeach()

    foreach(dir IN LISTS M_HEADERS)
        target_include_directories(${target} PUBLIC "${dir}")
        set_property(GLOBAL APPEND PROPERTY TTTRLIB_MODULE_INCLUDE_DIRS "${dir}")
    endforeach()

    # No VERSION/SOVERSION. These libraries are shipped inside the Python
    # package next to the extension, not installed as system libraries, and a
    # versioned soname means a real file plus two symlinks -- which a wheel
    # stores as three full copies. The versioned C++ library for system
    # consumers is a separate target and keeps its soname.
    set_target_properties(${target} PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            OUTPUT_NAME "${target}")

    if(TTTRLIB_MODULE_TYPE STREQUAL "SHARED")
        # The wheel build sets CMAKE_CXX_VISIBILITY_PRESET=hidden globally, which
        # is right for the extension -- it should export only PyInit__tttrlib --
        # and fatal for a module library, which exists to be linked against. A
        # module with hidden visibility links to nothing and the first split
        # wheel would fail at import with undefined symbols.
        set_target_properties(${target} PROPERTIES
                C_VISIBILITY_PRESET default
                CXX_VISIBILITY_PRESET default
                VISIBILITY_INLINES_HIDDEN OFF
                # MSVC exports nothing without __declspec(dllexport), and there is
                # no such annotation anywhere in include/. Until the export macros
                # land (they are ~97 sites), let CMake generate the .def file.
                WINDOWS_EXPORT_ALL_SYMBOLS ON)
        # Find siblings next to itself: modules and the extension are installed
        # into the same directory.
        #
        # SKIP_INSTALL_RPATH OFF explicitly, because the top-level CMakeLists
        # sets CMAKE_SKIP_INSTALL_RPATH globally under conda-build on macOS. That
        # was harmless while the extension had no sibling to find; now it would
        # strip the only thing that lets it find one.
        tttrlib_set_sibling_rpath(${target})
    else()
        # A static archive consumed outside this build (R CMD INSTALL, the ImageJ
        # native) must hold real machine code: an ar/linker without the LTO plugin
        # drops vague-linkage symbols out of slim LTO members -- typeinfo and
        # vtables, e.g. _ZTI9TTTRRange -- and the consumer fails at load.
        if(NOT MSVC)
            target_compile_options(${target} PRIVATE -fno-lto)
        endif()
    endif()

    set_property(GLOBAL APPEND PROPERTY TTTRLIB_MODULE_LIST ${M_NAME})
    set_property(GLOBAL APPEND PROPERTY TTTRLIB_CLAIMED_SOURCES ${M_SOURCES})
    set_property(GLOBAL PROPERTY TTTRLIB_MODULE_${M_NAME}_SWIG "${M_SWIG_INTERFACES}")
    set_property(GLOBAL PROPERTY TTTRLIB_MODULE_${M_NAME}_TESTS "${M_TEST_DIR}")

    list(LENGTH M_SOURCES _n)
    message(STATUS "module ${M_NAME}: ${_n} sources, ${TTTRLIB_MODULE_TYPE}")
endfunction()


# Every .cpp under src/ must be claimed by exactly one module.
#
# This is the check that makes the incremental extraction safe: a source moved
# into a new module but not removed from the residual glob is compiled twice and
# produces duplicate symbols at link time, somewhere far from the cause; a source
# forgotten entirely just silently stops being built. Both become a configure
# error naming the file.
function(tttrlib_finalize_modules)
    get_property(claimed GLOBAL PROPERTY TTTRLIB_CLAIMED_SOURCES)
    get_property(modules GLOBAL PROPERTY TTTRLIB_MODULE_LIST)

    # Everything that must be built: what is still in src/, plus what the
    # modules have taken into modules/<name>/src/ -- at any depth, because the
    # format modules are nested one level further under modules/io/.
    #
    # The depth matters more than it looks. A fixed modules/*/src/*.cpp pattern
    # stops seeing a module the moment it is moved a level down, and the failure
    # is silent in the worst way: the sources vanish from `all_sources`, so they
    # are not "unclaimed" either and the check still passes while covering less.
    # Moving the io_* modules under io/ dropped the count from 73 to 67 without
    # a word.
    #
    # PROJECT_SOURCE_DIR, not CMAKE_SOURCE_DIR: when a consumer embeds tttrlib
    # via add_subdirectory, CMAKE_SOURCE_DIR is the consumer's root, so this
    # would glob the consumer's src/ and flag every one of its files as an
    # unclaimed tttrlib source. PROJECT_SOURCE_DIR is tttrlib's own root because
    # project() is called here, and stays so for any subdirectory consumer.
    file(GLOB_RECURSE all_sources "${PROJECT_SOURCE_DIR}/src/*.cpp")
    file(GLOB_RECURSE module_sources "${PROJECT_SOURCE_DIR}/modules/*.cpp")
    list(APPEND all_sources ${module_sources})
    list(REMOVE_DUPLICATES all_sources)

    set(duplicates "")
    set(seen "")
    foreach(src IN LISTS claimed)
        if(src IN_LIST seen)
            list(APPEND duplicates "${src}")
        endif()
        list(APPEND seen "${src}")
    endforeach()
    if(duplicates)
        list(JOIN duplicates "\n  " _d)
        message(FATAL_ERROR
                "tttrlib_finalize_modules: source claimed by more than one module:\n  ${_d}")
    endif()

    set(unclaimed "")
    foreach(src IN LISTS all_sources)
        if(NOT src IN_LIST claimed)
            list(APPEND unclaimed "${src}")
        endif()
    endforeach()
    if(unclaimed)
        list(JOIN unclaimed "\n  " _u)
        message(FATAL_ERROR
                "tttrlib_finalize_modules: source under src/ claimed by no module:\n  ${_u}\n"
                "Add it to a module's SOURCES, or to the residual list in modules/legacy.")
    endif()

    list(LENGTH all_sources _total)
    list(JOIN modules ", " _mods)
    message(STATUS "tttrlib modules (${_total} sources, all claimed): ${_mods}")
endfunction()


# Link a consumer (a SWIG target, a test, ...) against every built module.
#
# Plain signature on purpose: the SWIG targets in ext/ already use
# target_link_libraries() without keywords, and CMake refuses to let one target
# be linked both ways.
function(tttrlib_link_all_modules target)
    get_property(modules GLOBAL PROPERTY TTTRLIB_MODULE_LIST)
    foreach(m IN LISTS modules)
        target_link_libraries(${target} tttrlib::${m})
    endforeach()

endfunction()


# Put every module's public headers on the current DIRECTORY's include path.
#
# For the C++ compile step, linking tttrlib::<module> is enough -- the usage
# requirement carries the include directory. swig is different: UseSWIG builds
# the swig command line from directory-scope include directories, and neither a
# linked library's INTERFACE_INCLUDE_DIRECTORIES nor a target_include_directories
# call reaches it. Without this, swig fails to find a header that the compiler
# would have found perfectly well:
#   Tiff.i:32: Error: Unable to find 'TiffArrayIO.h'
#
# A macro, not a function, so include_directories() applies to the calling
# directory rather than to a scope that evaporates on return.
macro(tttrlib_module_include_directories)
    get_property(_tttrlib_mod_dirs GLOBAL PROPERTY TTTRLIB_MODULE_INCLUDE_DIRS)
    if(_tttrlib_mod_dirs)
        include_directories(${_tttrlib_mod_dirs})
    endif()
    unset(_tttrlib_mod_dirs)
endmacro()


# Install every module. COMPONENT matters: pyproject.toml installs only the
# "bindings" component, so a module installed without it yields a wheel that
# builds, repairs and uploads cleanly and then ImportErrors on the user's machine.
function(tttrlib_install_modules)
    set(one_value DESTINATION COMPONENT)
    cmake_parse_arguments(A "" "${one_value}" "" ${ARGN})
    if(NOT A_COMPONENT)
        message(FATAL_ERROR "tttrlib_install_modules: COMPONENT is required")
    endif()
    get_property(modules GLOBAL PROPERTY TTTRLIB_MODULE_LIST)
    foreach(m IN LISTS modules)
        install(TARGETS tttrlib_${m}
                COMPONENT ${A_COMPONENT}
                LIBRARY DESTINATION "${A_DESTINATION}"
                ARCHIVE DESTINATION "${A_DESTINATION}"
                RUNTIME DESTINATION "${A_DESTINATION}")
    endforeach()
endfunction()
