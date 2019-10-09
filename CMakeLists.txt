CMAKE_MINIMUM_REQUIRED(VERSION 3.13)

SET(PROJECT_NAME tttrlib)
PROJECT(${PROJECT_NAME})

##############################################################
# 32/64 bit detection
###############################################################
if(${CMAKE_SIZEOF_VOID_P} MATCHES "8")
    # If in 64bit mode, we have to define the _OCC64 variable
    add_definitions(-D_OCC64)
    set(CMAKE_SWIG_FLAGS ${CMAKE_SWIG_FLAGS} -D_OCC64)
    set(BIT 64)
else(${CMAKE_SIZEOF_VOID_P} MATCHES "8")
    set(BIT 32)
endif(${CMAKE_SIZEOF_VOID_P} MATCHES "8")

ENABLE_LANGUAGE(CXX)
set(CMAKE_CXX_STANDARD 11)

# Windows
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest")
    if(BIT MATCHES "64")
        add_definitions(-DMS_WIN64)
    endif(BIT MATCHES "64")
endif(MSVC)

# MACOSX_DEPLOYEMENT_TARGET
# explicit link to libc++ for recent osx versions
if(APPLE)
    message(STATUS "Explicitely link to libc++ on modern osx versions")
    add_definitions(-stdlib=libc++)
endif(APPLE)

FIND_PACKAGE(HDF5 1.8 REQUIRED COMPONENTS C CXX HL)
INCLUDE_DIRECTORIES(${HDF5_INCLUDE_DIRS})
LINK_LIBRARIES(
        ${HDF5_LIBRARIES}
        ${HDF5_HL_LIBRARIES}
)
ADD_DEFINITIONS(-DH5_BUILT_AS_DYNAMIC_LIB)

OPTION (USE_OpenMP "Use OpenMP" ON)
FIND_PACKAGE(OpenMP)


INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/src)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/include)

ADD_SUBDIRECTORY(pyext)

INSTALL(
        TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        PUBLIC_HEADER DESTINATION include
)
