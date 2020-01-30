
find_program(python_command python "python executable.")

IF(python_command)
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
    execute_process(COMMAND python -c "import sys; print(sys.executable)"
            RESULT_VARIABLE retval
            WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
            OUTPUT_VARIABLE python_full_path
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    INCLUDE_DIRECTORIES(BEFORE ${Python_NumPy_PATH} ${Python_INCLUDE_DIR})
    LINK_LIBRARIES(${PYTHON_LIBRARY})
    MESSAGE(STATUS "Python_NumPy_PATH='${Python_NumPy_PATH}'")
    MESSAGE(STATUS "Python_INCLUDE_DIR='${Python_INCLUDE_DIR}'")
    MESSAGE(STATUS "PYTHON_LIBRARIES='${PYTHON_LIBRARIES}'")
ELSE()
    message(SEND_ERROR "FindPython.cmake requires the following variables to be set: python_command")
ENDIF()