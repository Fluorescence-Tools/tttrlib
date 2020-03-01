CMAKE_MINIMUM_REQUIRED(VERSION 3.13)

SET(PROJECT_NAME tttrlib)
PROJECT(${PROJECT_NAME})

OPTION(BUILD_DOC "Build documentation" OFF)
OPTION(BUILD_PYTHON_INTERFACE "Build Python interface" ON)
OPTION(BUILD_R_INTERFACE "Build R interface" OFF)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}")

ENABLE_LANGUAGE(CXX)
SET(CMAKE_CXX_STANDARD 11)

# Windows
if(MSVC)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest")
    add_definitions(-DMS_WIN64)
endif(MSVC)

# MACOSX_DEPLOYEMENT_TARGET
# explicit link to libc++ for recent osx versions
FIND_PACKAGE(Threads)
if(APPLE)
    MESSAGE(STATUS "Explicitely link to libc++ on modern osx versions")
    ADD_DEFINITIONS(-stdlib=libc++)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread")
    # Don't set MACOSX_RPATH by default
    set(CMAKE_MACOSX_RPATH 0)
endif(APPLE)

## Boost
# use static Boost in Windows
if(WIN32)
    set(Boost_USE_STATIC_LIBS ON)
    set(Boost_USE_STATIC ON)
    set(Boost_USE_MULTITHREAD )
endif(WIN32)
FIND_PACKAGE(Boost 1.36 REQUIRED COMPONENTS date_time filesystem iostreams)
INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIRS})
LINK_LIBRARIES(${Boost_LIBRARIES})

FIND_PACKAGE(HDF5 1.8 REQUIRED COMPONENTS C)
INCLUDE_DIRECTORIES(${HDF5_INCLUDE_DIRS})
LINK_LIBRARIES(${HDF5_LIBRARIES})
ADD_DEFINITIONS(-DH5_BUILT_AS_DYNAMIC_LIB)

# FIND_PACKAGE(OpenMP REQUIRED)

FILE(GLOB SRC_files "../src/*.cpp")
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/src)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/include)

ADD_SUBDIRECTORY(ext)

# Build documentation with doxygen
IF(BUILD_DOC)
    find_package(Doxygen REQUIRED)
    IF (DOXYGEN_FOUND)
    ELSE (DOXYGEN_FOUND)
        message("Doxygen need to be installed to generate the doxygen documentation")
    ENDIF (DOXYGEN_FOUND)
ENDIF()

# Add install target
install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib/static
        PUBLIC_HEADER DESTINATION include)

