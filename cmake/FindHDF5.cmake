# FindHDF5.cmake - HDF5 finder and configuration for tttrlib
# Handles dual-source approach: h5py DLLs + system/vcpkg headers or traditional finding.

include(FindPackageHandleStandardArgs)

if(BUILD_PHOTON_HDF)
    add_compile_definitions(BUILD_PHOTON_HDF)

    # Control HDF5 source selection (controlled by pyproject.toml cmake.args)
    if(TTTRLIB_HDF5_SOURCE)
        set(HDF5_SOURCE "${TTTRLIB_HDF5_SOURCE}")
        message(STATUS "HDF5 source set by cmake variable: ${HDF5_SOURCE}")
    elseif(DEFINED ENV{CIBUILDWHEEL})
        set(HDF5_SOURCE "H5PY_VCPKG_HEADERS")
        message(STATUS "CI build detected - using h5py with vcpkg headers")
    elseif(DEFINED ENV{CONDA_BUILD})
        set(HDF5_SOURCE "TRADITIONAL")
        message(STATUS "Conda build detected - using traditional HDF5")
    else()
        set(HDF5_SOURCE "H5PY")
        message(STATUS "Local build detected - using h5py HDF5 source")
    endif()

    ############################################################################
    # Option 1: HDF5 from h5py with system/vcpkg headers
    ############################################################################
    if(HDF5_SOURCE STREQUAL "H5PY_VCPKG_HEADERS" OR HDF5_SOURCE STREQUAL "H5PY")

        # Find Python interpreter
        find_package(Python3 3.9 REQUIRED QUIET)
        if(NOT Python3_FOUND)
            message(FATAL_ERROR "Python 3.9 or higher is required to find h5py")
        endif()

        # Get h5py package location and check availability
        execute_process(
            COMMAND ${Python3_EXECUTABLE} -c
                "import h5py, os
print(os.path.dirname(h5py.__file__))
print(1 if hasattr(h5py.version, 'hdf5_version') else 0)
print(h5py.version.hdf5_version if hasattr(h5py.version, 'hdf5_version') else 'N/A')"
            OUTPUT_VARIABLE H5PY_INFO
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE H5PY_FIND_RESULT
        )

        if(NOT H5PY_FIND_RESULT EQUAL 0)
            message(FATAL_ERROR "h5py package not found. Please install h5py: pip install h5py")
        endif()

        # Parse output
        string(REPLACE "\n" ";" H5PY_INFO_LIST "${H5PY_INFO}")
        list(LENGTH H5PY_INFO_LIST H5PY_INFO_LENGTH)

        if(H5PY_INFO_LENGTH LESS 3)
            message(FATAL_ERROR "Failed to parse h5py information. Expected 3 lines, got ${H5PY_INFO_LENGTH}. Output: ${H5PY_INFO}")
        endif()

        list(GET H5PY_INFO_LIST 0 H5PY_DIR)
        list(GET H5PY_INFO_LIST 1 H5PY_HAS_VERSION_ATTR)
        list(GET H5PY_INFO_LIST 2 H5PY_HDF5_VERSION)

        message(STATUS "Found h5py at: ${H5PY_DIR}")
        message(STATUS "h5py HDF5 version: ${H5PY_HDF5_VERSION}")

        # Version check: HDF5 >= 1.10.4 required
        if(H5PY_HDF5_VERSION STREQUAL "N/A")
            message(FATAL_ERROR "Cannot determine h5py HDF5 version. Please ensure h5py >= 3.10 is installed.")
        endif()

        string(REPLACE "." ";" H5PY_VERSION_PARTS "${H5PY_HDF5_VERSION}")
        list(GET H5PY_VERSION_PARTS 0 HDF5_MAJOR_VERSION)
        list(GET H5PY_VERSION_PARTS 1 HDF5_MINOR_VERSION)

        if(HDF5_MAJOR_VERSION LESS 1 OR (HDF5_MAJOR_VERSION EQUAL 1 AND HDF5_MINOR_VERSION LESS 10))
            message(FATAL_ERROR "h5py HDF5 version ${H5PY_HDF5_VERSION} is too old. Minimum required: 1.10.4")
        endif()

        message(STATUS "HDF5 version check passed: ${H5PY_HDF5_VERSION} >= 1.10.4")

        # Determine platform-specific library names
        if(WIN32)
            set(HDF5_LIB_NAME "hdf5.dll")
            set(HDF5_HL_LIB_NAME "hdf5_hl.dll")
            set(HDF5_ZLIB_NAME "zlib.dll")
        elseif(APPLE)
            set(HDF5_LIB_NAME "libhdf5.dylib")
            set(HDF5_HL_LIB_NAME "libhdf5_hl.dylib")
            set(HDF5_ZLIB_NAME "libzlib.dylib")
        else()
            set(HDF5_LIB_NAME "libhdf5.so")
            set(HDF5_HL_LIB_NAME "libhdf5_hl.so")
            set(HDF5_ZLIB_NAME "libzlib.so")
        endif()

        set(HDF5_LIB_PATH "${H5PY_DIR}/${HDF5_LIB_NAME}")
        set(HDF5_HL_LIB_PATH "${H5PY_DIR}/${HDF5_HL_LIB_NAME}")
        set(HDF5_ZLIB_PATH "${H5PY_DIR}/${HDF5_ZLIB_NAME}")

        if(NOT EXISTS "${HDF5_LIB_PATH}")
            message(FATAL_ERROR "HDF5 library not found at: ${HDF5_LIB_PATH}")
        endif()

        message(STATUS "Found HDF5 library from h5py: ${HDF5_LIB_PATH}")

        # Find HDF5 headers from system/vcpkg (without HL component requirement to avoid logic errors in hybrid modes)
        set(_hdf5_components C)
        if(NOT HDF5_SOURCE STREQUAL "H5PY_VCPKG_HEADERS")
            list(APPEND _hdf5_components HL)
        endif()
        
        # Manually help CMake find HDF5 if paths were provided
        if(TTTRLIB_HDF5_HEADERS_DIR)
            set(HDF5_ROOT "${TTTRLIB_HDF5_HEADERS_DIR}/..")
        endif()

        # Try to find HDF5 headers
        find_package(HDF5 COMPONENTS ${_hdf5_components} QUIET)

        if(NOT HDF5_FOUND)
            message(WARNING "HDF5 headers not found from system/vcpkg. Falling back to traditional search.")
            set(HDF5_IS_FROM_H5PY FALSE)
            find_package(HDF5 REQUIRED COMPONENTS C)
            set(HDF5_IS_FROM_H5PY TRUE)
        endif()

        message(STATUS "Using HDF5 headers: ${HDF5_INCLUDE_DIRS}")

        # Create HDF5::HDF5 imported target if not exists
        if(NOT TARGET HDF5::HDF5)
            add_library(HDF5::HDF5 SHARED IMPORTED)
            set_property(TARGET HDF5::HDF5 PROPERTY IMPORTED_LOCATION "${HDF5_LIB_PATH}")
            set_target_properties(HDF5::HDF5 PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${HDF5_INCLUDE_DIRS}"
            )
        endif()

        # Store paths for DLL bundling
        set(HDF5_DLL_PATHS "${HDF5_LIB_PATH}")
        if(EXISTS "${HDF5_HL_LIB_PATH}")
            list(APPEND HDF5_DLL_PATHS "${HDF5_HL_LIB_PATH}")
        endif()
        if(EXISTS "${HDF5_ZLIB_PATH}")
            list(APPEND HDF5_DLL_PATHS "${HDF5_ZLIB_PATH}")
        endif()

        set(HDF5_LIBRARIES "${HDF5_LIB_PATH}")
        set(HDF5_VERSION ${H5PY_HDF5_VERSION})
        set(HDF5_IS_FROM_H5PY TRUE)
        set(HDF5_C_LIBRARIES "${HDF5_LIB_PATH}")
        set(HDF5_HL_LIBRARIES "${HDF5_HL_LIB_PATH}")
        set(HDF5_ROOT "${H5PY_DIR}")
        set(ENV{HDF5_ROOT} "${H5PY_DIR}")

        # Add bundled headers to compiler flags for HighFive/others
        if(HDF5_INCLUDE_DIRS)
            file(TO_CMAKE_PATH "${HDF5_INCLUDE_DIRS}" HDF5_INCLUDE_DIRS_CMAKE)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I\"${HDF5_INCLUDE_DIRS_CMAKE}\"")
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -I\"${HDF5_INCLUDE_DIRS_CMAKE}\"")
        endif()

        find_package_handle_standard_args(HDF5
            REQUIRED_VARS HDF5_LIBRARIES HDF5_INCLUDE_DIRS
            VERSION_VAR HDF5_VERSION
        )

    ############################################################################
    # Option 2: Traditional HDF5 sources (conda/source builds)
    ############################################################################
    else()
        message(STATUS "Using traditional HDF5 sources (vcpkg/conda-forge/system)")
        set(HDF5_IS_FROM_H5PY FALSE)
        include(${CMAKE_ROOT}/Modules/FindHDF5.cmake)
        find_package(HDF5 REQUIRED COMPONENTS C HL)
        set(HDF5_DLL_PATHS "")
    endif()

    message(STATUS "HDF5 source is h5py: ${HDF5_IS_FROM_H5PY}")
    message(STATUS "HDF5 version: ${HDF5_VERSION}")
    message(STATUS "HDF5 libraries: ${HDF5_LIBRARIES}")

else()
    message(STATUS "HDF5 support disabled (BUILD_PHOTON_HDF=OFF)")
    set(HDF5_IS_FROM_H5PY FALSE)
    set(HDF5_DLL_PATHS "")
endif()
