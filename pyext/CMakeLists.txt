cmake_policy(SET CMP0078 NEW)
cmake_policy(SET CMP0086 OLD)

FIND_PACKAGE(SWIG REQUIRED)
INCLUDE(${SWIG_USE_FILE})
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})

FIND_PACKAGE(PythonInterp)
FIND_PACKAGE(PythonLibs)
execute_process(
        COMMAND python -c
        "from __future__ import print_function; import numpy; print(numpy.get_include())"
        RESULT_VARIABLE Python_NumPy_PATH
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "Python_NumPy_PATH='${Python_NumPy_PATH}'")
message(STATUS "PYTHON_INCLUDE_DIR='${PYTHON_INCLUDE_DIR}'")
INCLUDE_DIRECTORIES(BEFORE ${Python_NumPy_PATH} ${PYTHON_INCLUDE_DIR})
LINK_LIBRARIES(${PYTHON_LIBRARY})

SET(MODULE_NAME tttrlib)
SET(CMAKE_SWIG_FLAGS "")
SET(SWIG_SRC tttrlib.i)
# The output path is controlled by the setup.py
#SET(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/pyext)

SET_PROPERTY(SOURCE tttrlib.i PROPERTY CPLUSPLUS ON)
file(GLOB SRC_files "../src/*.cpp")
SWIG_ADD_LIBRARY(
        ${MODULE_NAME}
        LANGUAGE python
        SOURCES
        tttrlib.i
        ${SRC_files}
)

INCLUDE_DIRECTORIES(
        ${CMAKE_CURRENT_SOURCE_DIR}
)

TARGET_LINK_LIBRARIES(
        ${MODULE_NAME}
        ${HDF5_LIBRARIES}
        ${PYTHON_LIBRARY}
)

get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
foreach(dir ${dirs})
    message(STATUS "INCLUDE_DIRECTORY='${dir}'")
endforeach()

get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY LINK_LIBRARIES)
foreach(dir ${dirs})
    message(STATUS "LINK_LIBRARIES='${dir}'")
endforeach()

get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY LINK_DIRECTORIES)
foreach(dir ${dirs})
    message(STATUS "LINK_DIRECTORIES='${dir}'")
endforeach()

