CMAKE_MINIMUM_REQUIRED(VERSION 3.13)
SET(PROJECT_NAME tttrlib)
PROJECT(${PROJECT_NAME})

# C++ standard
ENABLE_LANGUAGE(CXX)
SET(CMAKE_CXX_STANDARD 11)

# CMAKE
###########
SET(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}")
MESSAGE(STATUS "CMAKE_MODULE_PATH='${CMAKE_MODULE_PATH}'")

INCLUDE(simd)
################################################################################
# BUILD OPTIONS
################################################################################
OPTION(WITH_AVX "Compile tttrlib with AVX intrinsics" ON)
OPTION(INSTALL "Install the library and the include files" ON)
OPTION(BUILD_PHOTON_HDF "Build photon hdf5 reader" ON)
OPTION(BUILD_ICS "Build with image correlation spectroscopy (requires fftw)" OFF)
OPTION(BUILD_LIBRARY "Build the library and the include files" ON)
OPTION(BUILD_PYTHON_INTERFACE "Build Python interface" ON)
OPTION(BUILD_PYTHON_DOCS "Build documentation of SWIG Python interface" ON)
OPTION(BUILD_R_INTERFACE "Build R interface" OFF)
OPTION(VERBOSE_TTTRLIB "Verbose tttrlib build" OFF)
OPTION(Boost_USE_STATIC "Static boost build" ON)

## Sytem specific compiler flags
################################
# AVX
IF((NOT CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64") OR APPLE)
  set(WITH_AVX OFF CACHE STRING "Disable AVX when compiling WITH_AVX=OFF." FORCE)
ENDIF()
IF(WITH_AVX)
  IF (MSVC)
      # https://devblogs.microsoft.com/cppblog/simd-extension-to-c-openmp-in-visual-studio/
      # /Oi is for intrinsics
      SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:AVX -openmp:experimental /Oi")
  ELSE(MSVC)
      STRING(APPEND CMAKE_CXX_FLAGS " -O3 -mavx -mfma")
  ENDIF ()
ENDIF()

# Debug build
IF (CMAKE_BUILD_TYPE STREQUAL "Debug")
  SET(VERBOSE_TTTRLIB ON)
  if(MSVC)
    ADD_COMPILE_OPTIONS(/Wall)
  ELSE(MSVC)
      ADD_COMPILE_OPTIONS(-Wall -Wextra -pedantic)
  ENDIF(MSVC)
ENDIF (CMAKE_BUILD_TYPE STREQUAL "Debug")

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
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread")
    # explicit link to libc++ for recent osx versions
    MESSAGE(STATUS "Explicitly link to libc++ on modern osx versions")
    ADD_DEFINITIONS(-stdlib=libc++)
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

# Conda
#######################################
FIND_PACKAGE(Conda)

## Boost
###########################
FIND_PACKAGE(CondaBoost)
IF(Boost_USE_STATIC)
  SET(Boost_USE_STATIC_LIBS ON)
ENDIF(Boost_USE_STATIC)
FIND_PACKAGE(Boost 1.36 REQUIRED COMPONENTS)
INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIRS})
LINK_LIBRARIES(${Boost_LIBRARIES})

## FFTW
##############
FIND_PACKAGE(FFTW)
IF(FFTW_FOUND AND BUILD_ICS)
  LINK_LIBRARIES(${FFTW_LIBRARIES})
  INCLUDE_DIRECTORIES(${FFTW_INCLUDE_DIRS})
  add_compile_definitions(BUILD_ICS)
ENDIF(FFTW_FOUND AND BUILD_ICS)

## HDF5
###########################cd
FIND_PACKAGE(HDF5 1.10 COMPONENTS C)
IF(HDF5_FOUND AND BUILD_PHOTON_HDF)
  INCLUDE_DIRECTORIES(${HDF5_INCLUDE_DIRS})
  LINK_LIBRARIES(${HDF5_LIBRARIES})
  add_compile_definitions(BUILD_PHOTON_HDF)
ENDIF()


IF(VERBOSE_TTTRLIB)
  add_compile_definitions(VERBOSE_TTTRLIB)
ENDIF(VERBOSE_TTTRLIB)

# Targets
#############################
FILE(GLOB_RECURSE SRC_files "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/src)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/include)
ADD_SUBDIRECTORY(ext)

IF(BUILD_LIBRARY)
  ## Shared / static library
  ###########################
  # cmake needs unique target name, thus place shared in front of project
  ADD_LIBRARY(${PROJECT_NAME}Shared SHARED ${SRC_files})
  ADD_LIBRARY(${PROJECT_NAME}Static STATIC ${SRC_files})

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


