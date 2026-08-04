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
    IF(OpenMP_CXX_FOUND)
        MESSAGE(STATUS "OpenMP enabled: ${OpenMP_CXX_VERSION} (${CMAKE_HOST_SYSTEM_PROCESSOR})")
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
        SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
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
    ENDIF()
    FIND_PACKAGE(HDF5 COMPONENTS C)
    IF(HDF5_FOUND)
        MESSAGE(STATUS "HDF5 in use: ${HDF5_LIBRARIES}")
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

# Eigen — header-only dense linear algebra (no link step). Used by NeuralNet for
# the batched GEMMs of training/inference, and later by the forward-mode AD
# gradient provider. Vendored copy wins; otherwise fall back to the system one.
IF(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/eigen/Eigen/Core")
  INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/eigen)
ELSE()
  # No version floor: Eigen's config treats a major bump as incompatible, so
  # asking for "3.3" rejects Eigen 5.x. Everything used here is ancient API.
  FIND_PACKAGE(Eigen3 REQUIRED NO_MODULE)
  MESSAGE(STATUS "Using system Eigen3 ${Eigen3_VERSION}")
  # Eigen3::Eigen is an INTERFACE target (headers only, no link step); newer
  # Eigen no longer sets the legacy EIGEN3_INCLUDE_DIR variable.
  LINK_LIBRARIES(Eigen3::Eigen)
ENDIF()

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


IF(BUILD_LIBRARY)
  ## Shared / static library
  ###########################
  # cmake needs unique target name, thus place shared in front of project
  ADD_LIBRARY(${PROJECT_NAME}Shared SHARED ${SRC_files})
  ADD_LIBRARY(${PROJECT_NAME}Static STATIC ${SRC_files})
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
  FILE(GLOB_RECURSE HEADERS_FILES LIST_DIRECTORIES true
          "${CMAKE_SOURCE_DIR}/include/*.h*"
          "${CMAKE_SOURCE_DIR}/modules/*/include/*.h*")
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

