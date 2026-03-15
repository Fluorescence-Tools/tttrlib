# FindHDF5.cmake - HDF5 finder and configuration for tttrlib
# Uses traditional finding via find_package(HDF5).

include(FindPackageHandleStandardArgs)

if(BUILD_PHOTON_HDF)
    add_compile_definitions(BUILD_PHOTON_HDF)

    message(STATUS "Using traditional HDF5 sources (vcpkg/conda-forge/system)")
    
    # Prevent infinite recursion by removing local cmake dir from CMAKE_MODULE_PATH
    set(_ORIGINAL_CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH})
    list(REMOVE_ITEM CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
    
    find_package(HDF5 REQUIRED COMPONENTS C)
    
    # Restore CMAKE_MODULE_PATH
    set(CMAKE_MODULE_PATH ${_ORIGINAL_CMAKE_MODULE_PATH})
    
    set(HDF5_IS_FROM_H5PY FALSE)
    set(HDF5_DLL_PATHS "")

    message(STATUS "HDF5 version: ${HDF5_VERSION}")
    message(STATUS "HDF5 libraries: ${HDF5_LIBRARIES}")
    message(STATUS "HDF5 headers: ${HDF5_INCLUDE_DIRS}")

else()
    message(STATUS "HDF5 support disabled (BUILD_PHOTON_HDF=OFF)")
    set(HDF5_IS_FROM_H5PY FALSE)
    set(HDF5_DLL_PATHS "")
endif()
