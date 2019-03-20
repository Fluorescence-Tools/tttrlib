cmake_policy(SET CMP0078 NEW)

FIND_PACKAGE(SWIG REQUIRED)
INCLUDE(${SWIG_USE_FILE})
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})

FIND_PACKAGE(PythonInterp)
FIND_PACKAGE(PythonLibs)
INCLUDE_DIRECTORIES(${PYTHON_INCLUDE_DIR})
LINK_LIBRARIES(${PYTHON_LIBRARY})

execute_process(
        COMMAND python -c
        "from __future__ import print_function\ntry: import numpy; print(numpy.get_include(), end='')\nexcept:pass\n"
        RESULT_VARIABLE PYTHON_PREFIX_RESULT
        OUTPUT_VARIABLE Python_NumPy_PATH
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
INCLUDE_DIRECTORIES(${Python_NumPy_PATH})

# This is the "new" way but it does not work yet
# There may be fixes
#INCLUDE_DIRECTORIES(${Python_INCLUDE_DIRS} ${Python_NumPy_INCLUDE_DIRS})
#FIND_PACKAGE(Python COMPONENTS Interpreter)
#LINK_DIRECTORIES(${Python_LIBRARY_DIRS})
#INCLUDE_DIRECTORIES(${Python_INCLUDE_DIRS})


SET(MODULE_NAME tttrlib)
SET(CMAKE_SWIG_FLAGS "")
SET(SWIG_SRC tttrlib.i)
# The output path is controlled by the setup.py
#SET(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/pyext)

SET_PROPERTY(SOURCE tttrlib.i PROPERTY CPLUSPLUS ON)
SWIG_ADD_LIBRARY(
        ${MODULE_NAME}
        LANGUAGE python
        SOURCES
        tttrlib.i
        ../src/TTTR.cpp
        ../src/RecordReader.cpp
        ../src/Header.cpp
        ../src/Histogram.cpp
        ../src/Correlate.cpp
        ../src/Image.cpp
)

INCLUDE_DIRECTORIES(
        ${CMAKE_CURRENT_SOURCE_DIR}
)

TARGET_LINK_LIBRARIES(
        ${MODULE_NAME}
        ${HDF5_LIBRARIES}
        ${PYTHON_LIBRARY}
)
