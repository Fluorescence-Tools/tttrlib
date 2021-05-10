CMAKE_MINIMUM_REQUIRED(VERSION 3.13)
ENABLE_LANGUAGE(CXX)
SET(CMAKE_CXX_STANDARD 11)
SET(PROJECT_NAME tttrlib)
PROJECT(${PROJECT_NAME})

################################################################################
# BUILD OPTIONS
################################################################################
option(INSTALL "Install the library and the include files" ON)
OPTION(BUILD_PYTHON_INTERFACE "Build Python interface" ON)
OPTION(INSTALL "Install the library and the include files" ON)
OPTION(BUILD_R_INTERFACE "Build R interface" OFF)

################################################################################
###### Conda
################################################################################

## Check for Conda
###########################
IF(DEFINED ENV{CONDA_PREFIX})
    MESSAGE(STATUS "CONDA env seen: $ENV{CONDA_PREFIX}")
    FILE(TO_CMAKE_PATH "$ENV{CONDA_PREFIX}" CONDA_PATH)
    SET(CMAKE_PREFIX_PATH "${CONDA_PATH}")
    SET(ENV{PATH} "${CONDA_PATH}/bin;$ENV{PATH}")
    SET(CMAKE_MODULE_PATH "${CONDA_PATH}/include/cmake")
    # Define python_command that is used to call python and locate the python libraries
    FILE(TO_CMAKE_PATH $ENV{PYTHON} python_command)
    MESSAGE(STATUS "CONDA PYTHON:" ${python_command})
    IF(MSVC)
        INCLUDE_DIRECTORIES(${CONDA_PATH}/Library/include)
        LINK_DIRECTORIES(${CONDA_PATH}/Library/lib)
    ELSE()
        INCLUDE_DIRECTORIES(${CONDA_PATH}/include)
        LINK_DIRECTORIES(${CONDA_PATH}}/lib)
    ENDIF()
ELSE()
    MESSAGE(STATUS "No conda environment defined")
ENDIF()
SET(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}")
MESSAGE(STATUS "CMAKE_MODULE_PATH='${CMAKE_MODULE_PATH}'")


#######################################
# Compiler flags
#######################################

# Debug build detection
###########################
IF (CMAKE_BUILD_TYPE STREQUAL "Release")
    MESSAGE("Release build")
    IF(MSVC)
        ADD_COMPILE_OPTIONS(/w)
    ELSE()
        ADD_COMPILE_OPTIONS(-w)
    ENDIF()
ELSE (CMAKE_BUILD_TYPE STREQUAL "Debug")
    MESSAGE("Debug build")
    ## Enable Compiler warnings
    #############################
    IF(MSVC)
        ADD_COMPILE_OPTIONS(/W4)
    ELSE()
        ADD_COMPILE_OPTIONS(-Wall -Wextra -pedantic)
    ENDIF()
    ADD_DEFINITIONS("-DVERBOSE_TTTRLIB=1")
ENDIF (CMAKE_BUILD_TYPE STREQUAL "Release")

# Windows
##############
if (MSVC)
    ADD_DEFINITIONS(-DMS_WIN64)
    ADD_DEFINITIONS(-D_USE_MATH_DEFINES)
endif (MSVC)

# MACOSX
##############
FIND_PACKAGE(Threads)
if (APPLE)
    # explicit link to libc++ for recent osx versions
    MESSAGE(STATUS "Explicitly link to libc++ on modern osx versions")
    ADD_DEFINITIONS(-stdlib=libc++)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread")
    # Don't set MACOSX_RPATH by default
    # https://github.com/conda/conda/issues/3624
    if(DEFINED ENV{CONDA_PREFIX})
        set(MACOSX_RPATH OFF)
        set(CMAKE_SKIP_RPATH TRUE)
        set(CMAKE_SKIP_INSTALL_RPATH TRUE)
    endif()
endif (APPLE)

#######################################
# INCLUDES AND LIBRARIES
#######################################

## Boost
###########################
IF(DEFINED ENV{CONDA_PREFIX})
    MESSAGE("Using Conda Boost")
    SET($ENV{BOOST_ROOT} ${CONDA_PATH})
    SET(Boost_NO_SYSTEM_PATHS ON)
    SET(Boost_NO_BOOST_CMAKE ON)
    SET($ENV{BOOST_INCLUDEDIR} ${CONDA_PATH}/lib)
    IF (CMAKE_BUILD_TYPE STREQUAL "Debug")
      SET(Boost_DEBUG ON)
      SET(Boost_DETAILED_FAILURE_MESSAGE ON)
    ENDIF (CMAKE_BUILD_TYPE STREQUAL "Debug")
    IF (WIN32)
      # Other root dir location
      SET(BOOST_ROOT ${CONDA_PATH}/Library)
      SET(Boost_INCLUDE_DIR ${CONDA_PATH}/Library/include)
      SET(Boost_LIBRARY_DIR ${CONDA_PATH}/Library/lib)
      # Win Boost Conda uses static linking
      # SET(Boost_USE_STATIC_LIBS ON)
      # SET(Boost_USE_STATIC ON)
    ELSE()
        SET(Boost_USE_STATIC_LIBS OFF)
        SET(Boost_USE_STATIC OFF)
        SET(Boost_USE_MULTITHREAD)
    ENDIF (WIN32)
ENDIF()

FIND_PACKAGE(Boost 1.36 REQUIRED COMPONENTS filesystem iostreams locale)
INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIRS})
LINK_LIBRARIES(${Boost_LIBRARIES})
MESSAGE(${Boost_INCLUDE_DIRS})

## FFTW
##############
FIND_PACKAGE(FFTW REQUIRED)
LINK_LIBRARIES(${FFTW_LIBRARIES})
INCLUDE_DIRECTORIES(${FFTW_INCLUDE_DIRS})

## HDF5
###########################
FIND_PACKAGE(HDF5 1.10 REQUIRED COMPONENTS C)
INCLUDE_DIRECTORIES(${HDF5_INCLUDE_DIRS})
LINK_LIBRARIES(${HDF5_LIBRARIES})
# needed for build on Windows
ADD_DEFINITIONS(-DH5_BUILT_AS_DYNAMIC_LIB)

## OpenMP
## macOS install OMP via: brew install libomp
###########################
IF (APPLE)
    # When using conda use CONDA_PREFIX
    if(DEFINED ENV{CONDA_PREFIX})
      SET(OMP_INCLUDE_DIR ${CONDA_PATH}/include)
      SET(OMP_LIB_DIR ${CONDA_PATH}/lib)
    else()
      SET(OMP_INCLUDE_DIR "/usr/local/opt/libomp/include")
      SET(OMP_LIB_DIR "/usr/local/opt/libomp/lib")
    endif()
    # see: https://gitlab.kitware.com/cmake/cmake/issues/18098
    MESSAGE(STATUS "OMP_INCLUDE_DIR: ${OMP_INCLUDE_DIR}")
    MESSAGE(STATUS "OMP_LIB_DIR: ${OMP_LIB_DIR}")
    set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp -I${OMP_INCLUDE_DIR}" CACHE INTERNAL "OpenMP flags for Xcode toolchain.")
    set(OpenMP_CXX_LIB_NAMES "omp" CACHE INTERNAL "OpenMP lib name for Xcode toolchain.")
    set(OpenMP_C_FLAGS "-Xpreprocessor -fopenmp -I${OMP_INCLUDE_DIR}" CACHE INTERNAL "OpenMP flags for Xcode toolchain.")
    set(OpenMP_C_LIB_NAMES "libomp" "libgomp" "libiomp5")
    set(OpenMP_omp_LIBRARY "${OMP_LIB_DIR}/libomp.dylib" CACHE INTERNAL "OpenMP lib name for Xcode toolchain.")
    set(OpenMP_libomp_LIBRARY ${OpenMP_C_LIB_NAMES})
    set(OpenMP_libgomp_LIBRARY ${OpenMP_C_LIB_NAMES})
    set(OpenMP_libiomp5_LIBRARY ${OpenMP_C_LIB_NAMES})
ENDIF (APPLE)
FIND_PACKAGE(OpenMP REQUIRED)
LINK_LIBRARIES(OpenMP::OpenMP_CXX)

# Targets
#############################
FILE(GLOB_RECURSE SRC_files "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/src)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/include)
ADD_SUBDIRECTORY(ext)

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



