CMAKE_MINIMUM_REQUIRED(VERSION 3.13)

SET(PROJECT_NAME tttrlib)
PROJECT(${PROJECT_NAME})

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
    FIND_PACKAGE(HDF5 COMPONENTS C)
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
      /GL          # Whole program optimization
      /w           # Disable all warnings in Release builds
    )
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
    SET(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")  # Link-time code generation
    SET(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG")
  ELSE()
    # GCC/Clang optimizations
    ADD_COMPILE_OPTIONS(
      -O3                    # Maximum optimization
      -funroll-loops         # Unroll loops
      -finline-functions     # Inline functions
      -fomit-frame-pointer   # Omit frame pointer
      -flto                  # Link-time optimization
      -w                     # Disable all warnings in Release builds
    )
    SET(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto")
    SET(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -flto")
  ENDIF()
ENDIF ()

# Operation sytem specific flags
IF (MSVC)
    ADD_DEFINITIONS(-DMS_WIN64)
    ADD_DEFINITIONS(-D_USE_MATH_DEFINES)
    # Issue with MSVC exceptions (https://stackoverflow.com/questions/6832666/lnk2019-when-including-asio-headers-solution-generated-with-cmake)
    # https://github.com/microsoft/vcpkg/discussions/25414
    ADD_DEFINITIONS(/DWIN32 /D_WINDOWS /GR /EHsc)
ENDIF (MSVC)
if(APPLE)
    FIND_PACKAGE(Threads)

    # comatibility with older systems
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mmacosx-version-min=10.13")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mmacosx-version-min=10.13")
    set(CMAKE_LFLAGS "${CMAKE_LFLAGS} -mmacosx-version-min=10.13")

    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread")
    # explicit link to libc++ for recent osx versions
    MESSAGE(STATUS "Explicitly link to libc++ on modern osx versions")
    ADD_DEFINITIONS(-stdlib=libc++)
    SET(CMAKE_LFLAGS "${CMAKE_LFLAGS} -mmacosx-version-min=10.9")
    # Don't set MACOSX_RPATH by default
    # https://github.com/conda/conda/issues/3624
    if(DEFINED ENV{CONDA_PREFIX})
        set(MACOSX_RPATH OFF)
        set(CMAKE_SKIP_RPATH TRUE)
        set(CMAKE_SKIP_INSTALL_RPATH TRUE)
    endif()
endif()


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
    LINK_LIBRARIES(TIFF::tiff)
    ADD_COMPILE_DEFINITIONS(BUILD_TIFF)
    MESSAGE(STATUS "TIFF support: bundled libtiff 4.7.0 (static, built-in codecs; Deflate/ZIP=${WITH_TIFF_ZLIB})")
  ELSE()
    MESSAGE(WARNING "WITH_TIFF=ON but thirdparty/libtiff not found and WITH_TIFF_SYSTEM=OFF; disabling TIFF support")
  ENDIF()
ENDIF()


IF(BUILD_LIBRARY)
  ## Shared / static library
  ###########################
  # cmake needs unique target name, thus place shared in front of project
  ADD_LIBRARY(${PROJECT_NAME}Shared SHARED ${SRC_files})
  ADD_LIBRARY(${PROJECT_NAME}Static STATIC ${SRC_files})

  # Link libraries (Boost removed - using C++17 standard library)

  # Define which headers are going to be included in the install target
  FILE(GLOB_RECURSE HEADERS_FILES LIST_DIRECTORIES true "${CMAKE_SOURCE_DIR}/include/*.h*")
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

