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
# Default WITH_AVX to OFF for conda/pip builds (safer, wider compatibility)
# Users can enable with: cmake -DWITH_AVX=ON
OPTION(WITH_AVX "Compile tttrlib with AVX intrinsics (default OFF for conda/pip builds)" OFF)
OPTION(INSTALL "Install the library and the include files" ON)
OPTION(BUILD_PHOTON_HDF "Build photon hdf5 reader" ON)
OPTION(BUILD_LOGIC_ANALYZER "Build logic analyzer file support" ON)
OPTION(BUILD_LOGIC_ANALYZER_SALEAE "Build Saleae Logic 2 binary format support" ON)
OPTION(BUILD_LOGIC_ANALYZER_VCD "Build VCD (Value Change Dump) format support" ON)
OPTION(BUILD_LOGIC_ANALYZER_FST "Build FST (Fast Signal Trace) format support" ON)
OPTION(BUILD_LIBRARY "Build the library and the include files" ON)
OPTION(BUILD_PYTHON_INTERFACE "Build Python interface" ON)
OPTION(BUILD_PYTHON_DOCS "Build documentation of SWIG Python interface" OFF)
OPTION(BUILD_R_INTERFACE "Build R interface" OFF)
OPTION(VERBOSE_TTTRLIB "Verbose tttrlib build" OFF)

## System-specific adjustments
################################
# FST is not supported on Windows (libfst requires MinGW, not MSVC)
IF(BUILD_LOGIC_ANALYZER_FST AND WIN32)
  MESSAGE(WARNING "FST support disabled on Windows (libfst requires MinGW, not MSVC)")
  SET(BUILD_LOGIC_ANALYZER_FST OFF CACHE BOOL "Build FST (Fast Signal Trace) format support" FORCE)
ENDIF()

## System specific compiler flags
################################
# AVX - Only enable on Intel x86_64 machines (all platforms)
# Disable on ARM (aarch64, arm64) and other non-x86_64 architectures
IF(NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  set(WITH_AVX OFF CACHE STRING "Disable AVX on non-x86_64 architectures (ARM, etc.)." FORCE)
  MESSAGE(STATUS "SIMD/AVX disabled: Not an x86_64 machine (detected: ${CMAKE_HOST_SYSTEM_PROCESSOR})")
ELSE()
  IF(WITH_AVX)
    MESSAGE(STATUS "SIMD/AVX enabled: (${CMAKE_SYSTEM_NAME})")
  ELSE()
    MESSAGE(STATUS "SIMD/AVX disabled via WITH_AVX=OFF (${CMAKE_SYSTEM_NAME})")
  ENDIF()
ENDIF()
IF(WITH_AVX)
    INCLUDE(simd)
ENDIF()

IF(WITH_AVX)
    ADD_COMPILE_DEFINITIONS(TTTRLIB_WITH_AVX=1)
ELSE()
    ADD_COMPILE_DEFINITIONS(TTTRLIB_WITH_AVX=0)
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
# HDF5 Support (Mandatory) - Dual Source Approach
################################################################################

IF(BUILD_PHOTON_HDF)
    ADD_COMPILE_DEFINITIONS(BUILD_PHOTON_HDF)

    # Control HDF5 source selection (controlled by pyproject.toml cmake.args)
    if(TTTRLIB_HDF5_SOURCE)
        set(HDF5_SOURCE "${TTTRLIB_HDF5_SOURCE}")
        message(STATUS "HDF5 source set by cmake variable: ${HDF5_SOURCE}")
    elseif(DEFINED ENV{CIBUILDWHEEL} OR DEFINED ENV{CONDA_BUILD})
        set(HDF5_SOURCE "H5PY")
        message(STATUS "CI/conda build detected - using h5py HDF5 source")
    else()
        set(HDF5_SOURCE "TRADITIONAL")
        message(STATUS "Local build detected - using traditional HDF5 source")
    endif()

    ############################################################################
    # Option 1: HDF5 from h5py with system/vcpkg headers
    ############################################################################
    if(HDF5_SOURCE STREQUAL "H5PY_VCPKG_HEADERS" OR HDF5_SOURCE STREQUAL "H5PY")

        # Find Python interpreter
        find_package(Python3 3.9 REQUIRED QUIET)
        if(NOT Python3_FOUND)
            message(FATAL_ERROR "Python 3.9 or higher is required to find h5py")
        endif()

        # Get h5py package location and check availability
        execute_process(
            COMMAND ${Python3_EXECUTABLE} -c
                "import h5py, os
print(os.path.dirname(h5py.__file__))
print(1 if hasattr(h5py.version, 'hdf5_version') else 0)
print(h5py.version.hdf5_version if hasattr(h5py.version, 'hdf5_version') else 'N/A')"
            OUTPUT_VARIABLE H5PY_INFO
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE H5PY_FIND_RESULT
        )

        if(NOT H5PY_FIND_RESULT EQUAL 0)
            message(FATAL_ERROR "h5py package not found. Please install h5py: pip install h5py")
        endif()

        # Parse output (each line is a separate variable)
        string(REPLACE "\n" ";" H5PY_INFO_LIST "${H5PY_INFO}")
        list(LENGTH H5PY_INFO_LIST H5PY_INFO_LENGTH)

        # Debug output
        message(STATUS "H5PY_INFO: ${H5PY_INFO}")
        message(STATUS "H5PY_INFO_LIST length: ${H5PY_INFO_LENGTH}")

        if(H5PY_INFO_LENGTH LESS 3)
            message(FATAL_ERROR "Failed to parse h5py information. Expected 3 lines, got ${H5PY_INFO_LENGTH}. Output: ${H5PY_INFO}")
        endif()

        list(GET H5PY_INFO_LIST 0 H5PY_DIR)
        list(GET H5PY_INFO_LIST 1 H5PY_HAS_VERSION_ATTR)
        list(GET H5PY_INFO_LIST 2 H5PY_HDF5_VERSION)

        message(STATUS "Found h5py at: ${H5PY_DIR}")
        message(STATUS "h5py HDF5 version: ${H5PY_HDF5_VERSION}")

        # Version check: HDF5 >= 1.10.4 required
        if(H5PY_HDF5_VERSION STREQUAL "N/A")
            message(FATAL_ERROR "Cannot determine h5py HDF5 version. Please ensure h5py >= 3.10 is installed.")
        endif()

        string(REPLACE "." ";" H5PY_VERSION_PARTS "${H5PY_HDF5_VERSION}")
        list(GET H5PY_VERSION_PARTS 0 HDF5_MAJOR_VERSION)
        list(GET H5PY_VERSION_PARTS 1 HDF5_MINOR_VERSION)

        if(HDF5_MAJOR_VERSION LESS 1)
            message(FATAL_ERROR "h5py HDF5 version ${H5PY_HDF5_VERSION} is too old. Minimum required: 1.10.4")
        elseif(HDF5_MAJOR_VERSION EQUAL 1 AND HDF5_MINOR_VERSION LESS 10)
            message(FATAL_ERROR "h5py HDF5 version ${H5PY_HDF5_VERSION} is too old. Minimum required: 1.10.4")
        endif()

        message(STATUS "HDF5 version check passed: ${H5PY_HDF5_VERSION} >= 1.10.4")

        # Determine platform-specific library names
        if(WIN32)
            set(HDF5_LIB_NAME "hdf5.dll")
            set(HDF5_HL_LIB_NAME "hdf5_hl.dll")
            set(HDF5_ZLIB_NAME "zlib.dll")
        elseif(APPLE)
            set(HDF5_LIB_NAME "libhdf5.dylib")
            set(HDF5_HL_LIB_NAME "libhdf5_hl.dylib")
            set(HDF5_ZLIB_NAME "libzlib.dylib")
        else()
            set(HDF5_LIB_NAME "libhdf5.so")
            set(HDF5_HL_LIB_NAME "libhdf5_hl.so")
            set(HDF5_ZLIB_NAME "libzlib.so")
        endif()

        # Check if HDF5 libraries exist in h5py directory
        set(HDF5_LIB_PATH "${H5PY_DIR}/${HDF5_LIB_NAME}")
        set(HDF5_HL_LIB_PATH "${H5PY_DIR}/${HDF5_HL_LIB_NAME}")
        set(HDF5_ZLIB_PATH "${H5PY_DIR}/${HDF5_ZLIB_NAME}")

        if(NOT EXISTS "${HDF5_LIB_PATH}")
            message(FATAL_ERROR "HDF5 library not found at: ${HDF5_LIB_PATH}")
        endif()

        message(STATUS "Found HDF5 library: ${HDF5_LIB_PATH}")

        # Find HDF5 headers from system/vcpkg (required for both H5PY and H5PY_VCPKG_HEADERS)
        find_package(HDF5 COMPONENTS C HL)

        if(NOT HDF5_FOUND)
            message(WARNING "HDF5 headers not found from system/vcpkg. For testing, falling back to traditional HDF5 search.")
            message(WARNING "For proper builds, please install HDF5 development headers:")
            message(WARNING "  - Windows: vcpkg install hdf5:x64-windows")
            message(WARNING "  - Linux (Ubuntu/Debian): sudo apt-get install libhdf5-dev")
            message(WARNING "  - macOS: brew install hdf5")

            # Fall back to traditional HDF5 search for testing
            set(HDF5_IS_FROM_H5PY FALSE)
            find_package(HDF5 REQUIRED COMPONENTS C HL)
            set(HDF5_IS_FROM_H5PY TRUE)  # Reset for our logic
        endif()

        message(STATUS "Using HDF5 headers from system/vcpkg: ${HDF5_INCLUDE_DIRS}")

        # Create HDF5::HDF5 imported target (link against h5py DLLs but use system headers)
        add_library(HDF5::HDF5 SHARED IMPORTED)
        set_property(TARGET HDF5::HDF5 PROPERTY IMPORTED_LOCATION "${HDF5_LIB_PATH}")
        set_target_properties(HDF5::HDF5 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${HDF5_INCLUDE_DIRS}"
        )

        # Store paths for DLL bundling during install
        set(HDF5_DLL_PATHS "${HDF5_LIB_PATH}")
        if(EXISTS "${HDF5_HL_LIB_PATH}")
            list(APPEND HDF5_DLL_PATHS "${HDF5_HL_LIB_PATH}")
        endif()
        if(EXISTS "${HDF5_ZLIB_PATH}")
            list(APPEND HDF5_DLL_PATHS "${HDF5_ZLIB_PATH}")
        endif()

        # Set HDF5_FOUND and required variables for compatibility with FindHDF5
        set(HDF5_LIBRARIES "${HDF5_LIB_PATH}")
        set(HDF5_VERSION ${H5PY_HDF5_VERSION})
        set(HDF5_IS_FROM_H5PY TRUE)

        # Also set variables that FindHDF5.cmake expects
        set(HDF5_C_LIBRARIES "${HDF5_LIB_PATH}")
        set(HDF5_HL_LIBRARIES "${HDF5_HL_LIB_PATH}")

        # Set HDF5_ROOT so FindHDF5.cmake can find it
        set(HDF5_ROOT "${H5PY_DIR}")
        set(ENV{HDF5_ROOT} "${H5PY_DIR}")

        message(STATUS "HDF5 library found from h5py: ${H5PY_HDF5_VERSION}")
        message(STATUS "HDF5 headers found from system/vcpkg: ${HDF5_INCLUDE_DIRS}")

    ############################################################################
    # Option 2: Traditional HDF5 sources (conda/source builds)
    ############################################################################
    else()
        message(STATUS "Using traditional HDF5 sources (vcpkg/conda-forge/system)")
        set(HDF5_IS_FROM_H5PY FALSE)

        # Use standard FindHDF5 module
        find_package(HDF5 REQUIRED COMPONENTS C HL)

        if(NOT HDF5_FOUND)
            message(FATAL_ERROR "HDF5 not found from traditional sources. Please install:")
            message(FATAL_ERROR "  - Windows: vcpkg install hdf5:x64-windows")
            message(FATAL_ERROR "  - Linux (Ubuntu/Debian): sudo apt-get install libhdf5-dev")
            message(FATAL_ERROR "  - macOS: brew install hdf5")
        endif()

        # Store empty DLL paths for traditional builds (not needed)
        set(HDF5_DLL_PATHS "")

    endif()

    message(STATUS "HDF5 source: ${HDF5_IS_FROM_H5PY}")
    message(STATUS "HDF5 version: ${HDF5_VERSION}")
    message(STATUS "HDF5 libraries: ${HDF5_LIBRARIES}")

ELSE()
    message(STATUS "HDF5 support disabled (BUILD_PHOTON_HDF=OFF)")
    set(HDF5_IS_FROM_H5PY FALSE)
    set(HDF5_DLL_PATHS "")

ENDIF()

# Build type specific optimizations
IF (CMAKE_BUILD_TYPE STREQUAL "Debug")
  SET(VERBOSE_TTTRLIB ON)
  MESSAGE(STATUS "Debug build: Enabling all warnings")
  if(MSVC)
    ADD_COMPILE_OPTIONS(/Wall)
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
    # Add AVX2 flag only if explicitly enabled by user
    IF(WITH_AVX)
      ADD_COMPILE_OPTIONS(/arch:AVX2)
      MESSAGE(STATUS "MSVC: AVX2 flag enabled via /arch:AVX2")
    ELSE()
      MESSAGE(STATUS "MSVC: AVX2 flag disabled (use -DWITH_AVX=ON to enable)")
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

## Logic Analyzer
###########################
IF(BUILD_LOGIC_ANALYZER)
  ADD_COMPILE_DEFINITIONS(BUILD_LOGIC_ANALYZER)
  IF(BUILD_LOGIC_ANALYZER_SALEAE)
    ADD_COMPILE_DEFINITIONS(BUILD_LOGIC_ANALYZER_SALEAE)
  ENDIF()
  IF(BUILD_LOGIC_ANALYZER_VCD)
    ADD_COMPILE_DEFINITIONS(BUILD_LOGIC_ANALYZER_VCD)
  ENDIF()
  IF(BUILD_LOGIC_ANALYZER_FST)
    ADD_COMPILE_DEFINITIONS(BUILD_LOGIC_ANALYZER_FST)
  ENDIF()
ENDIF(BUILD_LOGIC_ANALYZER)

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
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmann_json/include)

# LBFGS
FILE(GLOB_RECURSE LBFGS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lbfgs/*.cpp")
LIST(APPEND SRC_files ${LBFGS})

# ALGLib
FILE(GLOB_RECURSE ALGLIB "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/alglib/*.cpp")
LIST(APPEND SRC_files ${ALGLIB})

# HighFive HDF5 reader
IF(BUILD_PHOTON_HDF)
  IF(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/HighFive/CMakeLists.txt")

    # If using h5py HDF5, add bundled headers globally BEFORE any subdirectories
    IF(HDF5_IS_FROM_H5PY)
      IF(TTTRLIB_HDF5_HEADERS_DIR)
        SET(HDF5_HEADERS_DIR "${TTTRLIB_HDF5_HEADERS_DIR}")
        # Convert relative path to absolute
        IF(NOT IS_ABSOLUTE "${HDF5_HEADERS_DIR}")
          SET(HDF5_HEADERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${HDF5_HEADERS_DIR}")
        ENDIF()
        INCLUDE_DIRECTORIES("${HDF5_HEADERS_DIR}")
        MESSAGE(STATUS "Added HDF5 headers from toml config: ${HDF5_HEADERS_DIR}")
      ELSE()
        MESSAGE(FATAL_ERROR "TTTRLIB_HDF5_HEADERS_DIR not set in cmake args. Please define in pyproject.toml")
      ENDIF()

      # Disable HighFive's HDF5 finding since we provide our own HDF5::HDF5 target
      SET(HIGHFIVE_FIND_HDF5 OFF CACHE BOOL "Disable HighFive HDF5 finding" FORCE)
    ENDIF()

    ADD_SUBDIRECTORY(thirdparty/HighFive)

    # If using h5py HDF5, link HighFive to our custom HDF5 target
    IF(HDF5_IS_FROM_H5PY)
      TARGET_LINK_LIBRARIES(HighFive INTERFACE HDF5::HDF5)
    ENDIF()

    LINK_LIBRARIES(HighFive::HighFive)

  ELSE()
    MESSAGE(WARNING "HighFive third-party sources are missing; disabling photon HDF support")
    SET(BUILD_PHOTON_HDF OFF CACHE BOOL "Build photon hdf5 reader" FORCE)
  ENDIF()
ENDIF()


IF(BUILD_LIBRARY)
  ## Shared / static library
  ###########################
  # cmake needs unique target name, thus place shared in front of project
  ADD_LIBRARY(${PROJECT_NAME}Shared SHARED ${SRC_files})
  ADD_LIBRARY(${PROJECT_NAME}Static STATIC ${SRC_files})

  # Add HDF5 include directories to tttrlib targets when using h5py
  IF(BUILD_PHOTON_HDF AND HDF5_IS_FROM_H5PY)
    TARGET_INCLUDE_DIRECTORIES(${PROJECT_NAME}Shared PRIVATE "${HDF5_BUNDLED_HEADERS_DIR}")
    TARGET_INCLUDE_DIRECTORIES(${PROJECT_NAME}Static PRIVATE "${HDF5_BUNDLED_HEADERS_DIR}")
  ENDIF()

  # Link libraries (Boost removed - using C++17 standard library)

  # Define which headers are going to be included in the install target
  FILE(GLOB_RECURSE HEADERS_FILES LIST_DIRECTORIES true "${CMAKE_SOURCE_DIR}/include/*.h*")
  SET_TARGET_PROPERTIES(${PROJECT_NAME}Shared PROPERTIES PUBLIC_HEADER "${HEADERS_FILES}")
  SET_TARGET_PROPERTIES(${PROJECT_NAME}Static PROPERTIES PUBLIC_HEADER "${HEADERS_FILES}")

  # By default the output name is the target. To not have different names, i.e.,
  # ProjectNameShard.dll and ProjectNameStatic.lib the output names are set manually.
  SET_TARGET_PROPERTIES(${PROJECT_NAME}Shared PROPERTIES LIBRARY_OUTPUT_NAME "${PROJECT_NAME}")
  SET_TARGET_PROPERTIES(${PROJECT_NAME}Shared PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")
  SET_TARGET_PROPERTIES(${PROJECT_NAME}Static PROPERTIES LIBRARY_OUTPUT_NAME "${PROJECT_NAME}")
  SET_TARGET_PROPERTIES(${PROJECT_NAME}Static PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")

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

