# SPDX-License-Identifier: BSD-3-Clause
#
# io_hdf5 -- Photon-HDF5 (v0.5) reading and writing.
#
# The last piece of core's I/O to be separated, and the one that was worth the
# most: HighFive and <hdf5.h> now stop here. Before this, TTTRHeader.h
# forward-declared HighFive::Group for one private method, so every consumer of
# the photon-stream data model -- imaging, correlation, fitting -- needed an
# HDF5 toolchain on its include path to compile a header that has nothing to do
# with HDF5.
#
# tttrlib::highfive carries the BUILD_PHOTON_HDF definition, and this module
# builds either way: without it every entry point is present and fails cleanly,
# so nothing above has to be compiled twice.
tttrlib_add_module(
    NAME     io_hdf5
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/io_hdf5.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  io
    EXTERNAL_DEPS tttrlib::highfive
    TEST_DIR test/python/tttr
)
