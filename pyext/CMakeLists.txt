cmake_policy(SET CMP0078 NEW)
cmake_policy(SET CMP0086 OLD)

FIND_PACKAGE(SWIG REQUIRED)
INCLUDE(${SWIG_USE_FILE})
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})

FIND_PACKAGE(PythonInterp)
FIND_PACKAGE(PythonLibs)
execute_process(
        COMMAND python -c "from __future__ import print_function; import numpy; print(numpy.get_include())"
        OUTPUT_VARIABLE Python_NumPy_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
        COMMAND python -c "from sysconfig import get_paths as gp; print(gp()['include'])"
        OUTPUT_VARIABLE Python_INCLUDE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
INCLUDE_DIRECTORIES(BEFORE ${Python_NumPy_PATH} ${Python_INCLUDE_DIR})
INCLUDE_DIRECTORIES(BEFORE ${Python_NumPy_PATH} ${PYTHON_INCLUDE_DIR})
LINK_LIBRARIES(${PYTHON_LIBRARY})
message(STATUS "Python_NumPy_PATH='${Python_NumPy_PATH}'")
message(STATUS "Python_INCLUDE_DIR='${Python_INCLUDE_DIR}'")
message(STATUS "PYTHON_LIBRARY='${PYTHON_LIBRARY}'")

SET(MODULE_NAME tttrlib)
SET(CMAKE_SWIG_FLAGS "")

SET_SOURCE_FILES_PROPERTIES(tttrlib.i PROPERTIES CPLUSPLUS ON)
file(GLOB SRC_files "../src/*.cpp")
SWIG_ADD_LIBRARY(
        ${MODULE_NAME}
        TYPE MODULE
        LANGUAGE python
        SOURCES tttrlib.i ${SRC_files}
)
TARGET_LINK_LIBRARIES(
        ${MODULE_NAME}
        ${HDF5_LIBRARIES}
        ${PYTHON_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT}
        Boost::date_time Boost::filesystem Boost::iostreams
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

