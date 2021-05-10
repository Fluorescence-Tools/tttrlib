cmake_policy(SET CMP0078 NEW)
cmake_policy(SET CMP0086 OLD)

FIND_PACKAGE(SWIG REQUIRED)
INCLUDE(${SWIG_USE_FILE})
INCLUDE_DIRECTORIES(.)
SET(MODULE_NAME ${PROJECT_NAME})

IF(BUILD_PYTHON_INTERFACE)
    MESSAGE("Building Python interface")
    FIND_PACKAGE(Python)
    SET_SOURCE_FILES_PROPERTIES(python/tttrlib.i PROPERTIES CPLUSPLUS ON)
    INCLUDE_DIRECTORIES(${Python_INCLUDE_DIR})
    INCLUDE_DIRECTORIES(${Python_NumPy_PATH})
    SWIG_ADD_LIBRARY(
        ${MODULE_NAME}
        LANGUAGE python
        SOURCES python/tttrlib.i ${SRC_files}
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
            ${Boost_LIBRARIES}
    )
    IF (NOT PYTHON_LINK_LIBRARIES)
        IF (APPLE)
            SET_TARGET_PROPERTIES(${MODULE_NAME} PROPERTIES LINK_FLAGS "-Wl,-flat_namespace,-undefined,dynamic_lookup")
        ENDIF()
    ENDIF(NOT PYTHON_LINK_LIBRARIES)
ENDIF(BUILD_PYTHON_INTERFACE)


IF(BUILD_R_INTERFACE)
    MESSAGE("Building R interface")
    FIND_PACKAGE(R REQUIRED)
    INCLUDE_DIRECTORIES(${R_INCLUDE_DIR})
    INCLUDE(${SWIG_USE_FILE})
    SWIG_ADD_LIBRARY(
            ${MODULE_NAME}
            LANGUAGE r
            SOURCES python/tttrlib.i ${SRC_files}
    )
    TARGET_LINK_LIBRARIES(
            ${MODULE_NAME}
            ${HDF5_LIBRARIES}
            ${R_LIBRARIES}
            ${CMAKE_THREAD_LIBS_INIT}
            ${Boost_LIBRARIES}
    )
ENDIF()



