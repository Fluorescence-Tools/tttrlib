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
OPTION(BUILD_LIBRARY "Build the library and the include files" ON)
OPTION(BUILD_PYTHON_INTERFACE "Build Python interface" ON)
OPTION(BUILD_PYTHON_DOCS "Build documentation of SWIG Python interface" OFF)
OPTION(BUILD_R_INTERFACE "Build R interface" OFF)



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

# Build type specific optimizations
IF (CMAKE_BUILD_TYPE STREQUAL "Debug")
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


## HDF5 - Handled by HighFive (header-only)
###########################
IF(BUILD_PHOTON_HDF)
  ADD_COMPILE_DEFINITIONS(BUILD_PHOTON_HDF)
ENDIF(BUILD_PHOTON_HDF)

# Targets
#############################
FILE(GLOB_RECURSE SRC_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
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
    # On Windows (MSVC + multi-config generators), CMake's FindHDF5 module can
    # fail when only release import libraries are available (common on conda-forge
    # and some local setups). HighFive is header-only, so we can disable its
    # internal HDF5 discovery and link HDF5 ourselves.
    if(WIN32)
      set(HIGHFIVE_FIND_HDF5 OFF CACHE BOOL "Disable HighFive's HDF5 find_package on Windows" FORCE)
    endif()
    ADD_SUBDIRECTORY(thirdparty/HighFive)
    LINK_LIBRARIES(HighFive::HighFive)

    if(WIN32)
      # Prefer a proper CMake package (vcpkg), otherwise fall back to a minimal
      # lookup (conda-forge often ships only release import libs, which can trip
      # up CMake's FindHDF5 in multi-config generators).
      set(TTTRLIB_HDF5_TARGET "")
      find_package(HDF5 CONFIG QUIET)
      if(TARGET hdf5::hdf5)
        set(TTTRLIB_HDF5_TARGET hdf5::hdf5)
      elseif(TARGET hdf5::hdf5-shared)
        set(TTTRLIB_HDF5_TARGET hdf5::hdf5-shared)
      endif()

      if(NOT TTTRLIB_HDF5_TARGET)
        find_path(TTTRLIB_HDF5_INCLUDE_DIR
          NAMES hdf5.h
          HINTS "${HDF5_ROOT}" "${CMAKE_PREFIX_PATH}"
          PATH_SUFFIXES include Library/include
        )
        find_library(TTTRLIB_HDF5_LIBRARY
          NAMES hdf5
          HINTS "${HDF5_ROOT}" "${CMAKE_PREFIX_PATH}"
          PATH_SUFFIXES lib Library/lib
        )
        if(NOT TTTRLIB_HDF5_INCLUDE_DIR OR NOT TTTRLIB_HDF5_LIBRARY)
          message(FATAL_ERROR "HDF5 not found on Windows. Set HDF5_ROOT or CMAKE_PREFIX_PATH to a prefix containing include/hdf5.h and lib/hdf5.lib")
        endif()
      endif()

      add_library(tttrlib_hdf5 INTERFACE)
      if(TTTRLIB_HDF5_TARGET)
        target_link_libraries(tttrlib_hdf5 INTERFACE ${TTTRLIB_HDF5_TARGET})
        message(STATUS "HDF5 (Windows): using CMake target '${TTTRLIB_HDF5_TARGET}'")
      else()
        target_include_directories(tttrlib_hdf5 INTERFACE "${TTTRLIB_HDF5_INCLUDE_DIR}")
        target_link_libraries(tttrlib_hdf5 INTERFACE "${TTTRLIB_HDF5_LIBRARY}")
        message(STATUS "HDF5 (Windows): include='${TTTRLIB_HDF5_INCLUDE_DIR}', lib='${TTTRLIB_HDF5_LIBRARY}'")
      endif()

      # When linking against the shared HDF5 build on Windows, the headers only
      # use __declspec(dllimport) if H5_BUILT_AS_DYNAMIC_LIB is defined.
      if(NOT HDF5_USE_STATIC_LIBRARIES)
        target_compile_definitions(tttrlib_hdf5 INTERFACE H5_BUILT_AS_DYNAMIC_LIB)
      endif()
    endif()
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

