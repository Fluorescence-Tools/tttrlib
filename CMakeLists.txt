CMAKE_MINIMUM_REQUIRED(VERSION 3.13)
SET(PROJECT_NAME tttrlib)
PROJECT(${PROJECT_NAME})
ENABLE_LANGUAGE(CXX)
SET(CMAKE_CXX_STANDARD 11)

################################################################################
###### OPTIONS
################################################################################
option(INSTALL "Install the library and the include files" ON)
option(BUILD_PYTHON_INTERFACE "Build the python interface" ON)
OPTION(BUILD_DOC "Build documentation" OFF)
OPTION(BUILD_R_INTERFACE "Build R interface" OFF)

################################################################################
###### Conda
################################################################################

## Check for Conda
###########################
IF(DEFINED ENV{CONDA_PREFIX})
    MESSAGE(STATUS "CONDA env seen: --[$ENV{CONDA_PREFIX}]--")
    SET(ENV{PATH} "$ENV{CONDA_PREFIX}/bin;$ENV{PATH}")
    # Define python_command that is used to call python and locate the python libraries
    if (MSVC)
        INCLUDE_DIRECTORIES($ENV{CONDA_PREFIX}/Library/include)
        LINK_DIRECTORIES($ENV{CONDA_PREFIX}/Library/lib)
        SET(python_command $ENV{CONDA_PREFIX}/python.exe)
    ELSE()
        INCLUDE_DIRECTORIES($ENV{CONDA_PREFIX}/include)
        LINK_DIRECTORIES($ENV{CONDA_PREFIX}/lib)
        SET(python_command $ENV{CONDA_PREFIX}/bin/python)
    ENDIF()
ELSE()
    MESSAGE(STATUS "No conda environment defined")
ENDIF()
SET(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}")

## Include and lib dir
###########################
if(UNIX)
    if(DEFINED ENV{CONDA_PREFIX})
        LINK_DIRECTORIES("$ENV{CONDA_PREFIX}/lib")
        INCLUDE_DIRECTORIES("$ENV{CONDA_PREFIX}/include")
    else()
        LINK_DIRECTORIES(/usr/local/lib)
        INCLUDE_DIRECTORIES(/usr/local/include)
    endif()
endif()

################################################################################
###### Compiler flags
################################################################################

# Debug build detection
###########################
IF (CMAKE_BUILD_TYPE STREQUAL "Release")
    MESSAGE("Release build")
    ADD_DEFINITIONS("-DVERBOSE_TTTRLIB=0")
ELSE (CMAKE_BUILD_TYPE STREQUAL "Debug")
    MESSAGE("Debug build")
    ADD_DEFINITIONS("-DVERBOSE_TTTRLIB=1")
ENDIF (CMAKE_BUILD_TYPE STREQUAL "Release")

# Windows
##############
if (MSVC)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest")
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

################################################################################
###### INCLUDES AND LIBRARIES
################################################################################

## FFTW
##############
FIND_PACKAGE(FFTW REQUIRED)
LINK_LIBRARIES(${FFTW_LIBRARIES})
INCLUDE_DIRECTORIES(${FFTW_INCLUDE_DIRS})

## Does not work on Windows and conda mkl
## maybe the pytorch people will resolve this issue in the future.
### Intel MKL
###############
#find_package(MKL REQUIRED)
#if (MKL_FOUND)
#    INCLUDE_DIRECTORIES(${MKL_INCLUDE_DIRS})
#    LINK_LIBRARIES(${MKL_CORE_LIBRARY})
#else ()
#    message(WARNING "MKL libs not found")
#endif ()


## Boost
###########################
if (WIN32)
    # use static Boost in Windows
    set(Boost_USE_STATIC_LIBS ON)
    set(Boost_USE_STATIC ON)
    set(Boost_USE_MULTITHREAD)
endif (WIN32)
FIND_PACKAGE(Boost 1.36 REQUIRED COMPONENTS date_time filesystem iostreams)
INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIRS})
LINK_LIBRARIES(${Boost_LIBRARIES})
MESSAGE(${Boost_INCLUDE_DIRS})

## HDF5
###########################
FIND_PACKAGE(HDF5 1.10 REQUIRED COMPONENTS C)
INCLUDE_DIRECTORIES(${HDF5_INCLUDE_DIRS})
LINK_LIBRARIES(${HDF5_LIBRARIES})
# needed for build on Windows
ADD_DEFINITIONS(-DH5_BUILT_AS_DYNAMIC_LIB)

## OpenMP
###########################
if (APPLE)
    # When using conda use CONDA_PREFIX
    if(DEFINED ENV{CONDA_PREFIX})
        SET(OMP_INCLUDE_DIR "$ENV{CONDA_PREFIX}/include")
        SET(OMP_LIB_DIR "$ENV{CONDA_PREFIX}/lib")
    else()
        SET(OMP_INCLUDE_DIR "/usr/local/opt/libomp/include")
        SET(OMP_LIB_DIR "/usr/local/opt/libomp/lib")
    endif()
    # dirty hack to make OpenMP work see:
    # https://gitlab.kitware.com/cmake/cmake/issues/18098
    set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp -I${OMP_INCLUDE_DIR}" CACHE INTERNAL "OpenMP flags for Xcode toolchain.")
    set(OpenMP_CXX_LIB_NAMES "omp" CACHE INTERNAL "OpenMP lib name for Xcode toolchain.")
    set(OpenMP_C_FLAGS "-Xpreprocessor -fopenmp -I${OMP_INCLUDE_DIR}" CACHE INTERNAL "OpenMP flags for Xcode toolchain.")
    set(OpenMP_C_LIB_NAMES "libomp" "libgomp" "libiomp5")
    set(OpenMP_omp_LIBRARY "${OMP_LIB_DIR}/libomp.dylib" CACHE INTERNAL "OpenMP lib name for Xcode toolchain.")
    set(OpenMP_libomp_LIBRARY ${OpenMP_C_LIB_NAMES})
    set(OpenMP_libgomp_LIBRARY ${OpenMP_C_LIB_NAMES})
    set(OpenMP_libiomp5_LIBRARY ${OpenMP_C_LIB_NAMES})
endif ()
FIND_PACKAGE(OpenMP REQUIRED)
LINK_LIBRARIES(OpenMP::OpenMP_CXX)

# Doxygen
###########################
IF (BUILD_DOC)
    FIND_PACKAGE(Doxygen REQUIRED)
    IF (DOXYGEN_FOUND)
    ELSE (DOXYGEN_FOUND)
        MESSAGE("Doxygen need to be installed to generate the doxygen documentation")
    ENDIF (DOXYGEN_FOUND)
ENDIF ()

################################################################################
###### Targets
################################################################################
FILE(GLOB SRC_files "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/correlation/*.cpp"
        )
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
FILE(GLOB HEADERS_FILES "${CMAKE_SOURCE_DIR}/include/*.h")
FILE(GLOB HEADERS_FILES "${CMAKE_SOURCE_DIR}/include/*.h")
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

## Compiler warnings
########################
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic -Werror)
endif()
