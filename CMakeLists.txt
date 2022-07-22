CMAKE_MINIMUM_REQUIRED(VERSION 3.13)
ENABLE_LANGUAGE(CXX)
SET(CMAKE_CXX_STANDARD 11)
SET(PROJECT_NAME tttrlib)
PROJECT(${PROJECT_NAME})

# CMAKE
###########
SET(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}")
MESSAGE(STATUS "CMAKE_MODULE_PATH='${CMAKE_MODULE_PATH}'")

################################################################################
# BUILD OPTIONS
################################################################################
OPTION(INSTALL "Install the library and the include files" ON)
OPTION(BUILD_PYTHON_INTERFACE "Build Python interface" ON)
OPTION(BUILD_PYTHON_DOCS "Build documentation of SWIG Python interface" ON)
OPTION(BUILD_R_INTERFACE "Build R interface" OFF)
OPTION(INSTALL "Install the library and the include files" ON)

## Compiler flags
########################
if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set(TARGET_ARCHITECTURE haswell)
    cmake_compiler_machine_option(ARCHITECTURE_FLAG ${TARGET_ARCHITECTURE})
    message(STATUS "Use compiler option: ${ARCHITECTURE_FLAG}")
    ADD_COMPILE_OPTIONS(${ARCHITECTURE_FLAG})
endif()

IF (CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(MSVC)
        ADD_COMPILE_OPTIONS(/Wall)
    else()
        ADD_COMPILE_OPTIONS(-Wall -Wextra -pedantic)
    endif()
ENDIF (CMAKE_BUILD_TYPE STREQUAL "Debug")
if (MSVC)
    ADD_DEFINITIONS(-DMS_WIN64)
    ADD_DEFINITIONS(-D_USE_MATH_DEFINES)
    # https://devblogs.microsoft.com/cppblog/simd-extension-to-c-openmp-in-visual-studio/
    # /Oi is for intrinsics
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -openmp:experimental /Oi")
endif (MSVC)
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
FIND_PACKAGE(Boost 1.36 REQUIRED COMPONENTS filesystem iostreams locale)
INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIRS})
LINK_LIBRARIES(${Boost_LIBRARIES})

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
###########################
IF (APPLE)
    # macOS install OMP via: brew install libomp
    SET(OMP_INCLUDE_DIR "/usr/local/opt/libomp/include")
    SET(OMP_LIB_DIR "/usr/local/opt/libomp/lib")
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



