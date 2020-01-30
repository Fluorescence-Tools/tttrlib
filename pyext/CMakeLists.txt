cmake_policy(SET CMP0078 NEW)
cmake_policy(SET CMP0086 OLD)

FIND_PACKAGE(SWIG REQUIRED)
INCLUDE(${SWIG_USE_FILE})
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})

SET(MODULE_NAME tttrlib)

SET_SOURCE_FILES_PROPERTIES(tttrlib.i PROPERTIES CPLUSPLUS ON)
FILE(GLOB SRC_files "../src/*.cpp")

IF(BUILD_PYTHON_INTERFACE)
    FIND_PACKAGE(Python)
    MESSAGE("Building Python interface")
    INCLUDE_DIRECTORIES(${Python_INCLUDE_DIR})
    INCLUDE_DIRECTORIES(${Python_NumPy_PATH})
    SWIG_ADD_LIBRARY(
            ${MODULE_NAME}
            LANGUAGE python
            SOURCES tttrlib.i ${SRC_files}
    )

    EXECUTE_PROCESS(COMMAND ${PYTHON_EXECUTABLE} -c "import sysconfig; print(sysconfig.get_config_var('LDSHARED'))"
            RESULT_VARIABLE PYTHON_CVPY_PROCESS
            OUTPUT_VARIABLE PYTHON_LDSHARED
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    IF ("${PYTHON_LDSHARED}" MATCHES "dynamic_lookup")
        MESSAGE("Using static linking for Python")
        SET(PYTHON_LINK_LIBRARIES "NO")
    ELSE()
        SET(PYTHON_LINK_LIBRARIES "${PYTHON_LIBRARIES}")
    ENDIF()

    TARGET_LINK_LIBRARIES(
            ${MODULE_NAME}
            ${HDF5_LIBRARIES}
            ${PYTHON_LIBRARY}
            ${CMAKE_THREAD_LIBS_INIT}
            Boost::date_time
            Boost::filesystem
            Boost::iostreams
    )

    IF (NOT PYTHON_LINK_LIBRARIES)
        IF (APPLE)
            SET_TARGET_PROPERTIES(${MODULE_NAME} PROPERTIES LINK_FLAGS "-Wl,-flat_namespace,-undefined,dynamic_lookup")
        ENDIF()
    ENDIF()
ENDIF()


IF(BUILD_R_INTERFACE)
    MESSAGE("Building R interface")
    FIND_PACKAGE(R REQUIRED)
    INCLUDE_DIRECTORIES(${R_INCLUDE_DIR})
    INCLUDE(${SWIG_USE_FILE})
    SWIG_ADD_LIBRARY(
            ${MODULE_NAME}
            LANGUAGE r
            SOURCES tttrlib.i ${SRC_files}
    )
    TARGET_LINK_LIBRARIES(
            ${MODULE_NAME}
            ${HDF5_LIBRARIES}
            ${R_LIBRARIES}
            ${CMAKE_THREAD_LIBS_INIT}
            Boost::date_time
            Boost::filesystem
            Boost::iostreams
    )
ENDIF()


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

