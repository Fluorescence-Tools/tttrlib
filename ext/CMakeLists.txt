cmake_policy(SET CMP0078 NEW)
cmake_policy(SET CMP0086 OLD)

FIND_PACKAGE(SWIG REQUIRED)
INCLUDE(${SWIG_USE_FILE})
INCLUDE_DIRECTORIES(.)
SET(MODULE_NAME ${PROJECT_NAME})

IF(BUILD_PYTHON_INTERFACE)
    MESSAGE(STATUS "Building Python interface")
    FIND_PACKAGE(Python)
    SET_SOURCE_FILES_PROPERTIES(python/tttrlib.i PROPERTIES CPLUSPLUS ON)
    SWIG_ADD_LIBRARY(
        ${MODULE_NAME}
        LANGUAGE python
        SOURCES python/tttrlib.i ${SRC_files}
    )

    TARGET_LINK_LIBRARIES(
            ${MODULE_NAME}
            ${PYTHON_LIBRARY}
            ${CMAKE_THREAD_LIBS_INIT}
    )
    IF (NOT PYTHON_LINK_LIBRARIES)
        IF (APPLE)
            SET_TARGET_PROPERTIES(${MODULE_NAME} PROPERTIES LINK_FLAGS "-Wl,-flat_namespace,-undefined,dynamic_lookup")
        ENDIF()
    ENDIF(NOT PYTHON_LINK_LIBRARIES)
ENDIF(BUILD_PYTHON_INTERFACE)


IF(BUILD_R_INTERFACE)
    MESSAGE(STATUS "Building R interface")
    MESSAGE("!!!WARNING THE R INTERFACE IS NOT READY!!!")
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
            OpenMP::OpenMP_CXX
    )
ENDIF()



