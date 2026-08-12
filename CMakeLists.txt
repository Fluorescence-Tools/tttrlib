CMAKE_MINIMUM_REQUIRED(VERSION 3.13)

SET(PROJECT_NAME tttrlib)

# One source of truth for the version: pyproject.toml. Parsed rather than
# duplicated so a release bump cannot leave CMake's idea of the version behind,
# which is what a SOVERSION on a per-module shared library would then inherit.
FILE(READ "${CMAKE_CURRENT_SOURCE_DIR}/pyproject.toml" _tttrlib_pyproject)
STRING(REGEX MATCH "\nversion[ \t]*=[ \t]*\"([0-9]+\.[0-9]+\.[0-9]+)\"" _m "${_tttrlib_pyproject}")
IF(CMAKE_MATCH_1)
  SET(TTTRLIB_VERSION "${CMAKE_MATCH_1}")
ELSE()
  SET(TTTRLIB_VERSION "0.0.0")
  MESSAGE(WARNING "Could not read the version from pyproject.toml; using ${TTTRLIB_VERSION}")
ENDIF()
UNSET(_tttrlib_pyproject)

PROJECT(${PROJECT_NAME} VERSION ${TTTRLIB_VERSION})
MESSAGE(STATUS "tttrlib version: ${PROJECT_VERSION}")

# C++ standard
ENABLE_LANGUAGE(CXX)

# Ninja names no toolchain, so CMake takes the first compiler on PATH, and on a
# Windows runner that is a MinGW gcc. For the Python wheel that is fatal and
# silent: the extension has to match the MSVC-built interpreter it loads into.
# The Visual Studio generator ruled it out by construction; Ninja is 2.3x faster
# and does not, so the guarantee moves here.
#
# Scoped to the wheel deliberately. MinGW is a supported toolchain elsewhere in
# this project -- LTO is disabled for it below, Mat.h works around its duplicate
# TLS init -- and the ImageJ JNI native is built with it today. `prebuild.mjs`
# handles the same question the other way, by using Ninja on Windows only when
# VCINSTALLDIR says a Visual Studio environment is active.
IF(WIN32 AND NOT MSVC AND DEFINED ENV{SKBUILD})
  MESSAGE(FATAL_ERROR
    "The Python wheel must be built with MSVC; CMake selected "
    "'${CMAKE_CXX_COMPILER_ID}' (${CMAKE_CXX_COMPILER}). With the Ninja "
    "generator the compiler comes from PATH, so configure from a Visual Studio "
    "developer prompt, or pass -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl.")
ELSEIF(WIN32 AND NOT MSVC)
  MESSAGE(STATUS
    "Windows build using '${CMAKE_CXX_COMPILER_ID}', not MSVC -- supported "
    "here, but the artefact will depend on that runtime.")
ENDIF()
SET(CMAKE_CXX_STANDARD 17)
ADD_DEFINITIONS(-D_LIBCPP_ENABLE_CXX17_REMOVED_FEATURES)

# CMAKE
###########
SET(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}")
MESSAGE(STATUS "CMAKE_MODULE_PATH='${CMAKE_MODULE_PATH}'")

################################################################################
# BUILD OPTIONS
################################################################################
# SIMD / AVX
# ----------
# tttrlib builds a SINGLE portable binary that contains both scalar and AVX
# kernels and dispatches between them at RUNTIME based on the host CPU's
# capabilities (see tttrlib::cpu_features in include/info.h). This runtime
# dispatch is enabled automatically on x86_64 - no flag needed - and stays safe
# on machines without AVX because the AVX kernels use per-function target
# attributes and are only entered after a CPUID check.
#
# Two optional switches change this default behaviour:
#   WITH_AVX (default OFF): additionally compile the ENTIRE library with native
#       AVX2/FMA arch flags (-mavx2 -mfma / /arch:AVX2). Produces a faster but
#       NON-portable binary that REQUIRES an AVX2 CPU. Use only for machine-tuned
#       local builds, never for conda/pip/wheel distribution.
#   TTTRLIB_NO_SIMD (default OFF): compile a scalar-only binary with no AVX
#       kernels at all.
OPTION(WITH_AVX "Also build the whole library with native AVX2/FMA arch flags (non-portable, requires AVX2 CPU)" OFF)
OPTION(TTTRLIB_NO_SIMD "Disable all SIMD/AVX code paths (scalar-only build)" OFF)
OPTION(INSTALL "Install the library and the include files" ON)
OPTION(BUILD_PHOTON_HDF "Build photon hdf5 reader" ON)
# TIFF I/O for 2D/3D arrays via a bundled, statically-linked libtiff. No new
# runtime dependency: only libtiff's built-in codecs (uncompressed, LZW,
# PackBits, CCITT, ...) are compiled in. WITH_TIFF_ZLIB additionally enables the
# Deflate/"ZIP" codec, which requires a zlib to link against.
OPTION(WITH_TIFF "Build bundled libtiff-based TIFF I/O (no external runtime dependency)" ON)
OPTION(WITH_TIFF_ZLIB "Also enable libtiff Deflate/ZIP codec (requires zlib to link against)" OFF)
# Linkage mode for TIFF: OFF (default) bundles thirdparty/libtiff as a STATIC lib
# (pip wheels carry no external tiff dependency); ON links the system/conda-forge
# shared libtiff dynamically (used for the conda package, like HDF5).
OPTION(WITH_TIFF_SYSTEM "Link the system/conda libtiff (dynamic) instead of the bundled static one" OFF)

OPTION(BUILD_LIBRARY "Build the library and the include files" ON)
OPTION(BUILD_PYTHON_INTERFACE "Build Python interface" ON)
OPTION(BUILD_PYTHON_DOCS "Build documentation of SWIG Python interface" OFF)
OPTION(BUILD_R_INTERFACE "Build R interface" OFF)
OPTION(BUILD_JAVA_INTERFACE "Build Java interface" OFF)
OPTION(BUILD_JAVASCRIPT_INTERFACE "Build JavaScript (Node-API) interface" OFF)
OPTION(VERBOSE_TTTRLIB "Verbose tttrlib build" OFF)

# Build-turnaround switches. Neither changes what is produced for a release
# build; both exist so that iterating on one file does not cost a whole-program
# link. See CMakePresets.json for ready-made developer configurations.
OPTION(TTTRLIB_CCACHE "Use ccache as the compiler launcher when it is available" ON)
OPTION(TTTRLIB_LTO "Link-time optimization in Release builds" ON)

# ccache. `pip install -e .` reconfigures into a fresh build directory often
# enough that the cache is what makes a rebuild cheap. Restricted to GCC/Clang:
# ccache's MSVC support needs a different invocation and is not exercised in CI.
#
# Never inside conda-build (which includes the bioconda recipe): those builds run
# once in a throwaway prefix, so a cache buys nothing, and interposing a launcher
# in front of conda's compiler activation wrappers is a risk taken for no gain.
IF(DEFINED ENV{CONDA_BUILD} OR DEFINED ENV{CONDA_BUILD_STATE})
  SET(TTTRLIB_CCACHE OFF)
  MESSAGE(STATUS "ccache: skipped (conda-build)")
ENDIF()

IF(TTTRLIB_CCACHE AND NOT MSVC AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
  FIND_PROGRAM(CCACHE_PROGRAM ccache)
  IF(CCACHE_PROGRAM)
    SET(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    SET(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    MESSAGE(STATUS "ccache: ${CCACHE_PROGRAM}")
  ELSE()
    MESSAGE(STATUS "ccache: not found (set TTTRLIB_CCACHE=OFF to stop looking)")
  ENDIF()
ENDIF()

## System-specific adjustments
################################


## System specific compiler flags
################################
# AVX - runtime dispatch on x86_64, force-scalar elsewhere / on request.
IF(NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  # Non-x86 (ARM, etc.): no AVX kernels compiled in.
  set(WITH_AVX OFF CACHE STRING "AVX unavailable on non-x86_64 architectures." FORCE)
  ADD_COMPILE_DEFINITIONS(TTTRLIB_WITH_AVX=0)
  MESSAGE(STATUS "SIMD/AVX: non-x86_64 host (${CMAKE_HOST_SYSTEM_PROCESSOR}) - scalar only")
ELSEIF(TTTRLIB_NO_SIMD)
  set(WITH_AVX OFF CACHE STRING "SIMD disabled via TTTRLIB_NO_SIMD." FORCE)
  ADD_COMPILE_DEFINITIONS(TTTRLIB_WITH_AVX=0)
  MESSAGE(STATUS "SIMD/AVX: disabled via TTTRLIB_NO_SIMD (scalar-only build)")
ELSE()
  # Default: AVX kernels compiled in, selected at runtime via CPUID.
  ADD_COMPILE_DEFINITIONS(TTTRLIB_WITH_AVX=1)
  MESSAGE(STATUS "SIMD/AVX: runtime dispatch enabled (portable binary, ${CMAKE_SYSTEM_NAME})")
  IF(WITH_AVX)
    # Opt-in: build the WHOLE library with native AVX2/FMA (non-portable).
    INCLUDE(simd)
    IF(NOT MSVC)
      ADD_COMPILE_OPTIONS(-mavx2 -mfma)
    ENDIF()
    MESSAGE(STATUS "SIMD/AVX: WITH_AVX=ON - whole library built with native AVX2/FMA (non-portable, requires AVX2 CPU)")
  ENDIF()
ENDIF()

# OpenMP - Works on all architectures (x86_64, ARM, etc.)
OPTION(WITH_OPENMP "Enable OpenMP parallelization for correlation" ON)
IF(WITH_OPENMP)
    FIND_PACKAGE(OpenMP)
    # AppleClang ships no OpenMP runtime and no -fopenmp, so FIND_PACKAGE fails
    # on macOS and the build then quietly turns OpenMP OFF for the *whole*
    # library -- every #pragma omp in tttrlib becomes a comment and the parallel
    # kernels run serial, at roughly an eighth of their speed, with nothing in
    # the output but one WARNING. A libomp is nevertheless present in every
    # environment tttrlib is built in (conda ships it as a dependency of the
    # compilers, Homebrew as the `libomp` keg); it only has to be pointed at.
    # Setting the cache variables FindOpenMP documents and asking again is the
    # supported way to do that.
    IF(APPLE AND NOT OpenMP_CXX_FOUND)
        FIND_PATH(TTTRLIB_OMP_INCLUDE_DIR omp.h
            HINTS "$ENV{CONDA_PREFIX}/include" "$ENV{PREFIX}/include"
                  /opt/homebrew/opt/libomp/include /usr/local/opt/libomp/include)
        FIND_LIBRARY(TTTRLIB_OMP_LIBRARY NAMES omp
            HINTS "$ENV{CONDA_PREFIX}/lib" "$ENV{PREFIX}/lib"
                  /opt/homebrew/opt/libomp/lib /usr/local/opt/libomp/lib)
        IF(TTTRLIB_OMP_INCLUDE_DIR AND TTTRLIB_OMP_LIBRARY)
            # Written into the CACHE, with FORCE, not as ordinary variables.
            # The FIND_PACKAGE above has already cached OpenMP_CXX_FLAGS as
            # NOTFOUND, and on the *second* configure of an existing build tree
            # that cached NOTFOUND is what FindOpenMP reads back -- so the
            # fallback worked exactly once, on the fresh cache, and any later
            # cmake re-run (touching a CMakeLists is enough) silently turned
            # OpenMP off again. The compile flags survive in CMAKE_CXX_FLAGS, so
            # the failure is not a warning: the sources still emit omp calls and
            # the link fails on _omp_set_num_threads.
            SET(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp -I${TTTRLIB_OMP_INCLUDE_DIR}"
                CACHE STRING "CXX compiler flags for OpenMP parallelization" FORCE)
            SET(OpenMP_CXX_LIB_NAMES "omp"
                CACHE STRING "CXX compiler libraries for OpenMP parallelization" FORCE)
            SET(OpenMP_omp_LIBRARY "${TTTRLIB_OMP_LIBRARY}"
                CACHE FILEPATH "Path to the OpenMP runtime" FORCE)
            FIND_PACKAGE(OpenMP)
            IF(OpenMP_CXX_FOUND)
                MESSAGE(STATUS "OpenMP: AppleClang served by ${TTTRLIB_OMP_LIBRARY}")
            ENDIF()
        ELSE()
            MESSAGE(WARNING "OpenMP: no libomp found for AppleClang; the parallel "
                            "kernels will run serial. Install it (conda: llvm-openmp, "
                            "Homebrew: libomp) or set CMAKE_PREFIX_PATH.")
        ENDIF()
    ENDIF()
    IF(OpenMP_CXX_FOUND)
        MESSAGE(STATUS "OpenMP enabled: ${OpenMP_CXX_VERSION} (${CMAKE_HOST_SYSTEM_PROCESSOR})")
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
        SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
        # The flags above only get the *compiler* to emit OpenMP calls; the
        # runtime still has to be linked, and `OpenMP_EXE_LINKER_FLAGS` is empty
        # for the hand-configured AppleClang path above (and would not reach
        # library targets even when it is not). Every target declared from here
        # down — the modules, the two whole-library targets, the SWIG modules
        # and the tests — gets it, which is what the imported target is for.
        LINK_LIBRARIES(OpenMP::OpenMP_CXX)
        # ...and something has to be able to FIND that runtime at load time.
        # conda's and Homebrew's libomp both carry an `@rpath/libomp.dylib`
        # install name, so a binary linking it needs an rpath entry pointing at
        # the directory it came from -- otherwise every executable and every
        # extension module dies at startup with "Library not loaded:
        # @rpath/libomp.dylib" and the only workaround is a DYLD_LIBRARY_PATH,
        # which macOS SIP strips from anything but a shell. The tool's own
        # `@loader_path/../lib` does not cover it: libomp is not installed
        # beside libtttrlib.
        # INSTALL_RPATH only. `CMAKE_BUILD_RPATH` entries are placed *first*, and
        # the OpenMP prefix is usually a conda prefix — which commonly holds a
        # `libtttrlib.dylib` symlink into some other build tree. Putting it ahead
        # of the build's own directory made the tool load a stale library and
        # start anyway, which is a worse failure than not starting at all. In the
        # build tree CMake already adds what it linked; only the installed binary
        # has to be told.
        IF(APPLE AND TTTRLIB_OMP_LIBRARY)
            GET_FILENAME_COMPONENT(TTTRLIB_OMP_DIR "${TTTRLIB_OMP_LIBRARY}" DIRECTORY)
            LIST(APPEND CMAKE_INSTALL_RPATH "${TTTRLIB_OMP_DIR}")
        ENDIF()
    ELSE()
        MESSAGE(WARNING "OpenMP requested but not found")
        SET(WITH_OPENMP OFF)
    ENDIF()
ELSE()
    MESSAGE(STATUS "OpenMP disabled")
ENDIF()

################################################################################
# HDF5 Support (Mandatory for Photon HDF)
################################################################################
IF(BUILD_PHOTON_HDF)
    # A caller that names an HDF5 (HDF5_ROOT, or a prefix on CMAKE_PREFIX_PATH
    # that ships HDF5) means it: honour that root instead of letting
    # find_package fall through to whatever HDF5 *CMake config package* happens
    # to be installed. On macOS that config is usually Homebrew's, and it wins
    # even when HDF5_ROOT points elsewhere — the extension then loads a second
    # HDF5 into a process that already has the environment's (via h5py or
    # PyTables), and whichever initialises first wins: writing a Photon-HDF5
    # file aborts the interpreter outright.
    IF(DEFINED HDF5_ROOT OR DEFINED ENV{HDF5_ROOT})
        SET(HDF5_NO_FIND_PACKAGE_CONFIG_FILE TRUE)
    ELSEIF(DEFINED ENV{CONDA_PREFIX} AND EXISTS "$ENV{CONDA_PREFIX}/include/H5public.h")
        # No explicit root, but the active conda env ships an HDF5. Its include
        # directory ends up on the compile line either way (Python, libtiff and
        # OpenMP hints all point into the env), so the HEADERS compiled against
        # are the env's whatever find_package picks -- and on macOS the config
        # package it picks is Homebrew's, a different ABI. That mismatch is
        # silent until the first Photon-HDF5 write, which aborts the process.
        # Same prefix for both halves; an explicit HDF5_ROOT still wins above.
        SET(HDF5_ROOT "$ENV{CONDA_PREFIX}")
        SET(HDF5_NO_FIND_PACKAGE_CONFIG_FILE TRUE)
        MESSAGE(STATUS "HDF5: pinned to active conda env $ENV{CONDA_PREFIX}")
    ENDIF()
    FIND_PACKAGE(HDF5 COMPONENTS C)
    IF(HDF5_FOUND)
        MESSAGE(STATUS "HDF5 in use: ${HDF5_LIBRARIES}")
    ENDIF()
    # Refuse a cross-prefix HDF5 inside a conda env. An extension built in env
    # A against env B's HDF5 carries an @rpath install name env A cannot
    # resolve (ImportError at first use), and if it does load, h5py brings the
    # env's own HDF5 into the same process — two ABIs, and the first write
    # aborts. Both failures are silent at build time, which is why this is an
    # error and not a warning. Skipped under conda-build, whose host prefix is
    # the intended cross-prefix.
    OPTION(TTTRLIB_ALLOW_HDF5_PREFIX_MISMATCH
           "Permit an HDF5 from outside the active conda env" OFF)
    IF(HDF5_FOUND AND DEFINED ENV{CONDA_PREFIX}
       AND EXISTS "$ENV{CONDA_PREFIX}/include/H5public.h"
       AND NOT DEFINED ENV{CONDA_BUILD} AND NOT DEFINED ENV{CONDA_BUILD_STATE}
       AND NOT TTTRLIB_ALLOW_HDF5_PREFIX_MISMATCH)
        LIST(GET HDF5_LIBRARIES 0 _tttrlib_hdf5_lib)
        STRING(FIND "${_tttrlib_hdf5_lib}" "$ENV{CONDA_PREFIX}/" _tttrlib_hdf5_in_env)
        IF(_tttrlib_hdf5_in_env EQUAL -1)
            MESSAGE(FATAL_ERROR
                "HDF5 resolved to ${_tttrlib_hdf5_lib}, but the active conda env "
                "$ENV{CONDA_PREFIX} ships its own HDF5. A cross-prefix HDF5 either "
                "fails to load (its @rpath is not on the env's search path) or "
                "loads beside the env's copy and aborts on the first write. Unset "
                "HDF5_ROOT (the configure then uses the env's HDF5), point it at "
                "the env, or set TTTRLIB_ALLOW_HDF5_PREFIX_MISMATCH=ON if the "
                "mismatch is intended.")
        ENDIF()
    ENDIF()
ENDIF()

# Build type specific optimizations
IF (CMAKE_BUILD_TYPE STREQUAL "Debug")
  SET(VERBOSE_TTTRLIB ON)
  MESSAGE(STATUS "Debug build: Enabling all warnings")
  if(MSVC)
    ADD_COMPILE_OPTIONS(/Wall /bigobj)
  ELSE(MSVC)
      ADD_COMPILE_OPTIONS(-Wall -Wextra -pedantic)
  ENDIF(MSVC)
ELSEIF (CMAKE_BUILD_TYPE STREQUAL "Release")
  # Aggressive optimizations for Release builds
  MESSAGE(STATUS "Release build: Enabling aggressive optimizations and disabling warnings")
  if(MSVC)
    # Remove default warning levels to avoid D9025 warning
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    
    # MSVC optimizations
    ADD_COMPILE_OPTIONS(
      /O2          # Maximum optimization (speed)
      /Ob2         # Inline function expansion
      /Oi          # Enable intrinsic functions
      /Ot          # Favor fast code
      /w           # Disable all warnings in Release builds
    )
    # /GL and /LTCG are a pair -- MSVC errors if one is set without the other.
    IF(TTTRLIB_LTO)
      ADD_COMPILE_OPTIONS(/GL)   # Whole program optimization
    ENDIF()
    # Native AVX2 for the whole library only when explicitly opted in via
    # WITH_AVX (non-portable). Runtime-dispatched AVX kernels do NOT need this:
    # MSVC accepts AVX/FMA intrinsics without /arch and the scalar paths stay
    # portable to non-AVX CPUs.
    IF(WITH_AVX)
      ADD_COMPILE_OPTIONS(/arch:AVX2)
      MESSAGE(STATUS "MSVC: whole library built with /arch:AVX2 (non-portable)")
    ELSE()
      MESSAGE(STATUS "MSVC: runtime AVX dispatch (no global /arch:AVX2)")
    ENDIF()
    # Suppress informational pragma messages from external libraries (HighFive)
    ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4996")  # Disable deprecation warnings
    IF(TTTRLIB_LTO)
      SET(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")  # Link-time code generation
      SET(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG")
    ENDIF()
  ELSE()
    # GCC/Clang optimizations
    ADD_COMPILE_OPTIONS(
      -O3                    # Maximum optimization
      -funroll-loops         # Unroll loops
      -finline-functions     # Inline functions
      -fomit-frame-pointer   # Omit frame pointer
      -w                     # Disable all warnings in Release builds
    )
    # LTO everywhere except MinGW: MinGW's ld fails to resolve symbols from
    # LTO bitcode inside static archives (e.g. TIFFSetErrorHandler from the
    # bundled libtiff.a), producing undefined references at link time.
    #
    # TTTRLIB_LTO=OFF is for iteration only: a whole-program link dominates the
    # turnaround of a one-file change, and `pip install -e .` configures Release
    # by default, so without this switch every developer build pays for it.
    # Release artefacts (wheels, conda) keep it ON.
    IF(NOT MINGW AND TTTRLIB_LTO)
      ADD_COMPILE_OPTIONS(-flto)
      SET(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto")
      SET(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -flto")
    ELSEIF(NOT MINGW)
      MESSAGE(STATUS "LTO disabled (TTTRLIB_LTO=OFF) -- developer build")
    ENDIF()
  ENDIF()
ENDIF ()

# Operation sytem specific flags
IF (MSVC)
    ADD_DEFINITIONS(-DMS_WIN64)
    ADD_DEFINITIONS(-D_USE_MATH_DEFINES)
    # Issue with MSVC exceptions (https://stackoverflow.com/questions/6832666/lnk2019-when-including-asio-headers-solution-generated-with-cmake)
    # https://github.com/microsoft/vcpkg/discussions/25414
    ADD_DEFINITIONS(/DWIN32 /D_WINDOWS)
    # windows.h min/max macros break std::min/std::max and
    # std::numeric_limits<>::max() in any TU that pulls it in transitively
    # (HDF5 does); NOMINMAX ends that class of break for good.
    ADD_DEFINITIONS(-DNOMINMAX)
    # /GR /EHsc are cl-only flags: keep them out of ADD_DEFINITIONS, or the
    # Ninja generator (cmcldeps) forwards them to rc.exe, which then fails
    # with RC1203 on the bundled libtiff version resource.
    ADD_COMPILE_OPTIONS("$<$<COMPILE_LANGUAGE:CXX>:/GR;/EHsc>")
ENDIF (MSVC)
if(APPLE)
    FIND_PACKAGE(Threads)

    # Compatibility with older systems. 10.15 is the floor: the sources use
    # std::filesystem and std::filesystem::path::u8string, which Apple's SDK
    # marks unavailable before macOS 10.15.
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mmacosx-version-min=10.15")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mmacosx-version-min=10.15")
    set(CMAKE_LFLAGS "${CMAKE_LFLAGS} -mmacosx-version-min=10.15")

    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread")
    # explicit link to libc++ for recent osx versions
    MESSAGE(STATUS "Explicitly link to libc++ on modern osx versions")
    ADD_DEFINITIONS(-stdlib=libc++)
    # Don't set MACOSX_RPATH in a conda BUILD -- conda-build rewrites install
    # names itself in its post-processing step.
    # https://github.com/conda/conda/issues/3624
    #
    # The condition used to be CONDA_PREFIX, which is set whenever *any* conda
    # environment is merely active. That silently disabled the wheel's RPATH for
    # every developer and CI machine that builds a wheel from inside a conda
    # env -- which is most of them -- so the installed extension had no LC_RPATH
    # at all and could not find a sibling library. CONDA_BUILD is set only by
    # conda-build, which is the case this is actually about.
    if(DEFINED ENV{CONDA_BUILD} OR DEFINED ENV{CONDA_BUILD_STATE})
        set(MACOSX_RPATH OFF)
        set(CMAKE_SKIP_RPATH TRUE)
        set(CMAKE_SKIP_INSTALL_RPATH TRUE)
    endif()
endif()

#######################################
# WHEEL RPATH
#######################################
# The extension and anything shipped beside it live in the same directory
# (tttrlib/), so the extension has to look next to itself at load time.
#
# This was configured in pyproject.toml as CMAKE_INSTALL_RPATH under
# [tool.cibuildwheel.*.environment] -- but CMake does not read
# CMAKE_INSTALL_RPATH from the environment, only as a variable, so it never took
# effect: the installed .so carried no LC_RPATH/RUNPATH at all. It has to be set
# here, where it is also platform-aware in one place and covers a plain
# `pip install .` rather than only cibuildwheel.
IF((SKBUILD OR DEFINED ENV{SKBUILD}) AND NOT CMAKE_SKIP_INSTALL_RPATH)
    IF(APPLE)
        SET(CMAKE_INSTALL_RPATH "@loader_path")
        SET(CMAKE_INSTALL_NAME_DIR "@rpath")
    ELSEIF(UNIX)
        SET(CMAKE_INSTALL_RPATH "$ORIGIN")
    ENDIF()
    # Link against the install RPATH straight away: a wheel is the only thing
    # this configuration ever produces, so there is no build-tree layout worth
    # relinking for, and it keeps the repaired wheel and the built one identical.
    SET(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
    MESSAGE(STATUS "Wheel build: CMAKE_INSTALL_RPATH='${CMAKE_INSTALL_RPATH}'")
ENDIF()


#######################################
# INCLUDES AND LIBRARIES
#######################################


# HDF5 is now handled above with dual source approach



IF(VERBOSE_TTTRLIB)
    ADD_COMPILE_DEFINITIONS(VERBOSE_TTTRLIB)
ENDIF(VERBOSE_TTTRLIB)

# Targets
#############################
FILE(GLOB_RECURSE SRC_files "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/src)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Third party
#############################
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
IF(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmann_json/include")
  INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmann_json/include)
ELSE()
  FIND_PACKAGE(nlohmann_json REQUIRED)
  LINK_LIBRARIES(nlohmann_json::nlohmann_json)
ENDIF()

# cxxopts — vendored single-header command-line parser for the tttr CLI
# (modules/cli). The whole-library targets compile every module source, so the
# header must be on the project-wide include path, exactly like nlohmann/json.
IF(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/cxxopts/cxxopts.hpp")
  INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/cxxopts)
ENDIF()

# Eigen is gone. It was a `REQUIRED` of the whole project for two things: the
# batched GEMMs in NeuralNet, now Mat.h (modules/math), and the derivative slot
# of the vectorized forward-mode gradient in ImageLocalization, now GradVec<N>
# (modules/math). Both replacements are measured against Eigen in
# benchmarks/bench_mat.cpp and benchmarks/bench_gradvec.cpp. Nothing else in the
# library ever used it -- `QREigen.h` is tttrlib's own eigensolver, not Eigen.

# HighFive HDF5 reader
IF(BUILD_PHOTON_HDF)
  IF(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/HighFive/CMakeLists.txt")
    ADD_SUBDIRECTORY(thirdparty/HighFive)
    LINK_LIBRARIES(HighFive::HighFive)
    ADD_COMPILE_DEFINITIONS(BUILD_PHOTON_HDF)
  ELSE()
    FIND_PATH(HIGHFIVE_INCLUDE_DIR "highfive/H5File.hpp")
    IF(HIGHFIVE_INCLUDE_DIR)
      MESSAGE(STATUS "Using system HighFive headers at: ${HIGHFIVE_INCLUDE_DIR}")
      INCLUDE_DIRECTORIES(${HIGHFIVE_INCLUDE_DIR})
      LINK_LIBRARIES(HDF5::HDF5)
      ADD_COMPILE_DEFINITIONS(BUILD_PHOTON_HDF)
    ELSE()
      MESSAGE(WARNING "HighFive not found; disabling photon HDF support")
    ENDIF()
  ENDIF()
ENDIF()


# TIFF I/O for 2D/3D arrays - bundled static (pip) or system shared (conda)
#############################
IF(WITH_TIFF)
  IF(WITH_TIFF_SYSTEM)
    # conda / distro build: dynamically link the system libtiff, exactly like
    # HDF5. Adds a shared-library runtime dependency resolved by the package
    # manager - nothing from thirdparty/libtiff is compiled.
    FIND_PACKAGE(TIFF REQUIRED)
    LINK_LIBRARIES(TIFF::TIFF)
    ADD_COMPILE_DEFINITIONS(BUILD_TIFF)
    MESSAGE(STATUS "TIFF support: system libtiff (dynamic link), version ${TIFF_VERSION}")
  ELSEIF(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libtiff/CMakeLists.txt")
    # Build libtiff as a STATIC, position-independent library with only its
    # built-in codecs so it can be bundled directly into the tttrlib module
    # without adding a runtime dependency. External codec libraries (jpeg, zstd,
    # lzma, webp, ...) and the tools/tests/docs/C++ wrapper are all disabled.
    SET(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    SET(CMAKE_POSITION_INDEPENDENT_CODE ON)
    SET(tiff-tools    OFF CACHE BOOL "" FORCE)
    SET(tiff-tests    OFF CACHE BOOL "" FORCE)
    SET(tiff-contrib  OFF CACHE BOOL "" FORCE)
    SET(tiff-docs     OFF CACHE BOOL "" FORCE)
    SET(tiff-install  OFF CACHE BOOL "" FORCE)
    SET(cxx           OFF CACHE BOOL "" FORCE)
    IF(WITH_TIFF_ZLIB)
      SET(zlib        ON  CACHE BOOL "" FORCE)
    ELSE()
      SET(zlib        OFF CACHE BOOL "" FORCE)
    ENDIF()
    SET(libdeflate    OFF CACHE BOOL "" FORCE)
    SET(jpeg          OFF CACHE BOOL "" FORCE)
    SET(old-jpeg      OFF CACHE BOOL "" FORCE)
    SET(jbig          OFF CACHE BOOL "" FORCE)
    SET(lerc          OFF CACHE BOOL "" FORCE)
    SET(lzma          OFF CACHE BOOL "" FORCE)
    SET(zstd          OFF CACHE BOOL "" FORCE)
    SET(webp          OFF CACHE BOOL "" FORCE)
    SET(pixarlog      OFF CACHE BOOL "" FORCE)
    ADD_SUBDIRECTORY(thirdparty/libtiff EXCLUDE_FROM_ALL)
    IF(NOT MSVC)
      # libtiff.a may be archived by an 'ar' without the LTO plugin (conda's
      # binutils, MinGW). Slim LTO members then have invisible symbols and the
      # final link silently leaves TIFF* symbols undefined (import error at
      # runtime). Always put real machine code into the bundled archive.
      TARGET_COMPILE_OPTIONS(tiff PRIVATE -fno-lto)
    ENDIF()
    LINK_LIBRARIES(TIFF::tiff)
    ADD_COMPILE_DEFINITIONS(BUILD_TIFF)
    MESSAGE(STATUS "TIFF support: bundled libtiff 4.7.0 (static, built-in codecs; Deflate/ZIP=${WITH_TIFF_ZLIB})")
  ELSE()
    MESSAGE(WARNING "WITH_TIFF=ON but thirdparty/libtiff not found and WITH_TIFF_SYSTEM=OFF; disabling TIFF support")
  ENDIF()
ENDIF()


# Name each third-party dependency as an INTERFACE target, so a target can later
# ask for exactly the ones it uses instead of inheriting all of them from the
# directory scope above. Defined here, after the upstream imported targets exist;
# nothing consumes them yet. See cmake/TTTRLibThirdParty.cmake.
INCLUDE(TTTRLibThirdParty)


#######################################
# MODULES
#######################################
# One declared unit per subsystem, each with its own sources, dependencies and
# library. Declared here, after the third-party targets exist, so a module can
# ask for exactly the dependencies it uses. See cmake/TTTRLibModule.cmake and
# modules/CMakeLists.txt -- which today declares exactly one module holding all
# of src/, on purpose.
INCLUDE(TTTRLibModule)
ADD_SUBDIRECTORY(modules)

# The example drop-in plugin. Opt-in (-DTTTRLIB_BUILD_EXAMPLE_PLUGIN=ON) and
# deliberately not linked against tttrlib: it exists so the plugin path can be
# tested end to end rather than only described.
ADD_SUBDIRECTORY(examples/plugin)

# Header-only C++ checks for the shared linear algebra. Opt-in
# (-DTTTRLIB_BUILD_CPP_TESTS=ON) because they need nothing but an include path
# and a compiler — no library, no Python, no test data — so they are also
# runnable by hand; see test/cpp/README.md.
#
# test_ad_gradient additionally needs thirdparty/ for the autodiff headers: it
# guards the vectorized forward-mode gradient, which is the one place a
# third-party template and a tttrlib type have to agree on a contract that is
# not documented upstream.
OPTION(TTTRLIB_BUILD_CPP_TESTS "Build the header-only C++ math tests" OFF)
IF(TTTRLIB_BUILD_CPP_TESTS)
    ENABLE_TESTING()
    FOREACH(_cpp_test test_mat_linalg test_qreigen test_ad_gradient)
        ADD_EXECUTABLE(${_cpp_test} "${PROJECT_SOURCE_DIR}/test/cpp/${_cpp_test}.cpp")
        TARGET_INCLUDE_DIRECTORIES(${_cpp_test}
            PRIVATE "${PROJECT_SOURCE_DIR}/modules/math/include"
                    "${PROJECT_SOURCE_DIR}/thirdparty")
        ADD_TEST(NAME ${_cpp_test} COMMAND ${_cpp_test})
    ENDFOREACH()

    # test_decay_likelihood is the odd one out: the Poisson likelihoods are not
    # header-only, so it compiles the two translation units it needs rather than
    # linking the decay module. That keeps it runnable by hand from a bare
    # checkout like the others, which is the property that makes these useful.
    ADD_EXECUTABLE(test_decay_likelihood
        "${PROJECT_SOURCE_DIR}/test/cpp/test_decay_likelihood.cpp"
        "${PROJECT_SOURCE_DIR}/modules/spectroscopy/decay/src/DecayStatistics.cpp"
        "${PROJECT_SOURCE_DIR}/modules/util/src/Verbose.cpp")
    TARGET_INCLUDE_DIRECTORIES(test_decay_likelihood
        PRIVATE "${PROJECT_SOURCE_DIR}/modules/spectroscopy/decay/include"
                "${PROJECT_SOURCE_DIR}/modules/util/include")
    ADD_TEST(NAME test_decay_likelihood COMMAND test_decay_likelihood)
ENDIF()

# Module headers, for the targets declared BELOW this line only.
#
# Placement is the whole point: modules/ has already been processed, so it does
# not inherit this, and a module still sees only its own headers plus those of
# the modules it declared as DEPENDS. What does need them is the pair of
# whole-library targets further down, which compile every source themselves.
tttrlib_module_include_directories()

# Every source in the project: what is still in src/, plus what the extracted
# modules own. tttrlib_finalize_modules() has already checked that this is each
# source exactly once.
GET_PROPERTY(TTTRLIB_ALL_SOURCES GLOBAL PROPERTY TTTRLIB_CLAIMED_SOURCES)


IF(BUILD_LIBRARY)
  ## Shared / static library
  ###########################
  # cmake needs unique target name, thus place shared in front of project
  # These build EVERY source, including the ones extracted modules now own.
  # libtttrlib.so is what a C++ consumer links and libtttrlib_static.a is what
  # the conda R package links, and both must stay complete: globbing src/ alone
  # would silently drop a capability with each extraction (the R package wraps
  # both Histogram and Tiff). See modules/MODULE-DEBT.md -- collapsing these onto
  # the module objects is the exit, and it must not change either artefact's name.
  ADD_LIBRARY(${PROJECT_NAME}Shared SHARED ${TTTRLIB_ALL_SOURCES})
  ADD_LIBRARY(${PROJECT_NAME}Static STATIC ${TTTRLIB_ALL_SOURCES})
  IF(NOT MSVC)
    # The static archive is consumed outside this build (e.g. R CMD INSTALL
    # links it into the R package). If it holds slim LTO bitcode, an ar/linker
    # without the LTO plugin drops vague-linkage symbols (typeinfo/vtables,
    # e.g. _ZTI9TTTRRange) and the consumer fails at load. Archive real code.
    TARGET_COMPILE_OPTIONS(${PROJECT_NAME}Static PRIVATE -fno-lto)
  ENDIF()

  # Link libraries (Boost removed - using C++17 standard library)

  # Define which headers are going to be included in the install target
  # A module's headers are public headers as well: they install flat into
  # include/tttrlib/ exactly as the ones still in include/ do, so a C++ consumer
  # sees one directory and #include <tttrlib/TiffArrayIO.h> keeps working no
  # matter which module owns it.
  # LIST_DIRECTORIES must stay FALSE here: GLOB_RECURSE with it ON returns every
  # directory it walked through, matching pattern or not, and INSTALL(PUBLIC_HEADER)
  # rejects a directory outright.
  FILE(GLOB_RECURSE HEADERS_FILES LIST_DIRECTORIES false
          "${CMAKE_SOURCE_DIR}/include/*.h*"
          "${CMAKE_SOURCE_DIR}/modules/*.h*")
  SET_TARGET_PROPERTIES(${PROJECT_NAME}Shared PROPERTIES PUBLIC_HEADER "${HEADERS_FILES}")
  SET_TARGET_PROPERTIES(${PROJECT_NAME}Static PROPERTIES PUBLIC_HEADER "${HEADERS_FILES}")

  # By default the output name is the target. To not have different names, i.e.,
  # ProjectNameShared.dll and ProjectNameStatic.lib the output names are set manually.
  # Use different names to avoid "multiple rules generate" ninja error
  SET_TARGET_PROPERTIES(${PROJECT_NAME}Shared PROPERTIES LIBRARY_OUTPUT_NAME "${PROJECT_NAME}")
  SET_TARGET_PROPERTIES(${PROJECT_NAME}Shared PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")
  SET_TARGET_PROPERTIES(${PROJECT_NAME}Static PROPERTIES LIBRARY_OUTPUT_NAME "${PROJECT_NAME}_static")
  SET_TARGET_PROPERTIES(${PROJECT_NAME}Static PROPERTIES OUTPUT_NAME "${PROJECT_NAME}_static")

  # Add install target
  #######################
    IF(INSTALL)
      INSTALL(TARGETS ${PROJECT_NAME}Shared
              RUNTIME DESTINATION bin
              LIBRARY DESTINATION lib
              ARCHIVE DESTINATION lib
              PUBLIC_HEADER DESTINATION include/${PROJECT_NAME}
      )
      INSTALL(TARGETS ${PROJECT_NAME}Static
              RUNTIME DESTINATION bin
              LIBRARY DESTINATION lib
              ARCHIVE DESTINATION lib
              PUBLIC_HEADER DESTINATION include/${PROJECT_NAME}
      )
  ENDIF(INSTALL)
ENDIF(BUILD_LIBRARY)

# SWIG Extensions
#####################################
ADD_SUBDIRECTORY(ext)


# Print additional (Debug) info
#####################################
IF(CMAKE_BUILD_TYPE STREQUAL "Debug")
  get_property(dirs DIRECTORY . PROPERTY INCLUDE_DIRECTORIES)
  foreach(dir ${dirs})
    message(STATUS "INCLUDE_DIRECTORY='${dir}'")
  endforeach()
  get_property(dirs DIRECTORY . PROPERTY LINK_LIBRARIES)
  foreach(dir ${dirs})
    message(STATUS "LINK_LIBRARIES='${dir}'")
  endforeach() 
  get_property(dirs DIRECTORY . PROPERTY LINK_DIRECTORIES)
  foreach(dir ${dirs})
    message(STATUS "LINK_DIRECTORIES='${dir}'")
  endforeach()
ENDIF(CMAKE_BUILD_TYPE STREQUAL "Debug")

