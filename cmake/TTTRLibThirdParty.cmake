# SPDX-License-Identifier: BSD-3-Clause
#
# Third-party and build-configuration dependencies, as INTERFACE targets.
#
# Today every dependency is wired with directory-scope INCLUDE_DIRECTORIES() and
# LINK_LIBRARIES() in the top-level CMakeLists.txt, which means every target in
# the project gets every dependency: Eigen is a hard requirement of the whole
# build for the sake of two source files, and nothing records which subsystem
# actually needs what.
#
# The targets defined here name each dependency once so that a target can ask
# for exactly what it uses. They are created ALONGSIDE the existing wiring and
# nothing consumes them yet -- switching a target over is what removes it from
# the directory scope, and that happens per module. Defining them first keeps
# that later change to one line per module instead of an archaeology exercise.
#
# All of these are header-only (or resolve to an upstream imported target), so
# "linking" them adds include directories and compile definitions, not a link
# step.
#
#   tttrlib::json          nlohmann/json
#   tttrlib::pocketfft     pocketfft (vendored, header-only FFT)
#   tttrlib::autodiff      autodiff (vendored, forward-mode AD)
#   tttrlib::eigen         Eigen3 (dense linear algebra)
#   tttrlib::highfive      HighFive + HDF5   (only when BUILD_PHOTON_HDF)
#   tttrlib::build_config  the project's own include dirs and compile definitions

function(_tttrlib_define_interface name)
    if(NOT TARGET ${name})
        add_library(${name} INTERFACE)
    endif()
endfunction()

# --- nlohmann/json ------------------------------------------------------------
_tttrlib_define_interface(tttrlib_json)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmann_json/include")
    target_include_directories(tttrlib_json INTERFACE
            "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmann_json/include")
elseif(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(tttrlib_json INTERFACE nlohmann_json::nlohmann_json)
endif()
add_library(tttrlib::json ALIAS tttrlib_json)

# --- cxxopts ------------------------------------------------------------------
# Vendored single-header command-line parser (MIT), used by the tttr CLI.
# Reached as "cxxopts.hpp", so the include directory is thirdparty/cxxopts/.
_tttrlib_define_interface(tttrlib_cxxopts)
target_include_directories(tttrlib_cxxopts INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/cxxopts")
add_library(tttrlib::cxxopts ALIAS tttrlib_cxxopts)

# --- pocketfft ----------------------------------------------------------------
# Vendored and header-only. Reached as "pocketfft/pocketfft_hdronly.h", so the
# include directory is thirdparty/ rather than thirdparty/pocketfft/.
_tttrlib_define_interface(tttrlib_pocketfft)
target_include_directories(tttrlib_pocketfft INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
add_library(tttrlib::pocketfft ALIAS tttrlib_pocketfft)

# --- autodiff -----------------------------------------------------------------
# Vendored and header-only; used only by the localization fit.
_tttrlib_define_interface(tttrlib_autodiff)
target_include_directories(tttrlib_autodiff INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
add_library(tttrlib::autodiff ALIAS tttrlib_autodiff)

# --- Eigen --------------------------------------------------------------------
# Header-only, no link step. Used by the neural net and the localization fit --
# two subsystems, not the whole project, which is why this is worth naming.
_tttrlib_define_interface(tttrlib_eigen)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/eigen/Eigen/Core")
    target_include_directories(tttrlib_eigen INTERFACE
            "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/eigen")
elseif(TARGET Eigen3::Eigen)
    target_link_libraries(tttrlib_eigen INTERFACE Eigen3::Eigen)
endif()
add_library(tttrlib::eigen ALIAS tttrlib_eigen)

# --- HighFive / HDF5 ----------------------------------------------------------
# The only consumer is io_hdf5, and it is now the only module that pays for HDF5
# on its compile line.
#
# BUILD_PHOTON_HDF is deliberately NOT defined here. It answers "was this build
# configured with Photon-HDF5 support", which is a project-wide fact that core
# needs in order to offer the container at all -- and it must not drag the HDF5
# headers along with the answer. It lives on build_config below; the include
# path and the link libraries stay here.
_tttrlib_define_interface(tttrlib_highfive)
if(BUILD_PHOTON_HDF)
    if(TARGET HighFive::HighFive)
        target_link_libraries(tttrlib_highfive INTERFACE HighFive::HighFive)
    elseif(HIGHFIVE_INCLUDE_DIR)
        target_include_directories(tttrlib_highfive INTERFACE "${HIGHFIVE_INCLUDE_DIR}")
        if(TARGET HDF5::HDF5)
            target_link_libraries(tttrlib_highfive INTERFACE HDF5::HDF5)
        endif()
    endif()
endif()
add_library(tttrlib::highfive ALIAS tttrlib_highfive)

# --- the project's own build configuration ------------------------------------
# The include layout stays FLAT -- sources write #include "CLSMImage.h", not
# #include <tttrlib/clsm/CLSMImage.h>. Boundaries between modules are enforced by
# which include directories are on a target's compile line, not by rewriting
# every include in the tree. That costs zero edited lines and does not break C++
# consumers who include <tttrlib/TTTR.h> today.
_tttrlib_define_interface(tttrlib_build_config)
target_include_directories(tttrlib_build_config INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(tttrlib_build_config INTERFACE
        _LIBCPP_ENABLE_CXX17_REMOVED_FEATURES)
if(VERBOSE_TTTRLIB)
    target_compile_definitions(tttrlib_build_config INTERFACE VERBOSE_TTTRLIB)
endif()
# Whether the build has Photon-HDF5 support. A capability flag, not an include
# path: core reads it to decide whether to offer the container, and must be able
# to do so without an HDF5 toolchain in reach. Only io_hdf5 links
# tttrlib::highfive and includes the headers.
if(BUILD_PHOTON_HDF)
    target_compile_definitions(tttrlib_build_config INTERFACE BUILD_PHOTON_HDF)
endif()
# OpenMP belongs here because it is genuinely project-wide: the top-level
# CMakeLists puts ${OpenMP_CXX_FLAGS} into CMAKE_CXX_FLAGS, so *every*
# translation unit compiles with it, whichever module it ends up in. The link
# side did not follow -- ${OpenMP_EXE_LINKER_FLAGS} goes to
# CMAKE_EXE_LINKER_FLAGS, which reaches executables only. That was invisible
# while the sources were one static archive consumed by the extension (which
# links OpenMP::OpenMP_CXX itself), and became a hard link error the moment the
# module split made them SHARED libraries: compiled with -fopenmp, linked
# without it, so libtttrlib_legacy.dylib failed on every ___kmpc_* symbol.
#
# Carrying it on build_config rather than in tttrlib_add_module() keeps one
# statement of the rule: a module compiles with the project's flags, so it links
# the project's runtimes. Every module extracted from here on gets it for free.
if(WITH_OPENMP AND OpenMP_CXX_FOUND)
    target_link_libraries(tttrlib_build_config INTERFACE OpenMP::OpenMP_CXX)
endif()
add_library(tttrlib::build_config ALIAS tttrlib_build_config)

message(STATUS "Third-party INTERFACE targets defined (tttrlib::json, ::pocketfft, "
               "::autodiff, ::eigen, ::highfive, ::build_config)")
