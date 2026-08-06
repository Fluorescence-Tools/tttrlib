# SPDX-License-Identifier: BSD-3-Clause
#
# HDF5. One directory, because HDF5 is a file format like any other and the
# person looking for "where does tttrlib do HDF5" should find one place.
#
# It builds TWO targets, and that is the only thing here worth explaining. They
# are not two halves of a format; they are two different jobs that happen to use
# the same library, and they sit on opposite sides of `core`:
#
#   io_hdf5        Photon-HDF5 -- a photon stream in a file. Like every other
#                  vendor module it decodes into plain arrays and knows nothing
#                  about what a photon stream is for, so `core` depends on it:
#                  core -> io_hdf5 -> io.
#
#   io_hdf5_table  A columnar table -- one dataset per column -- read straight
#                  into a DataStore. A DataStore lives in `core`, so this is
#                  ABOVE core: io_hdf5_table -> core -> io_hdf5.
#
# Both arrows in one target would be a cycle, which is why they stay two. Merge
# them and CMake refuses; that is the check, and this comment is the reason.
# io_csv and io_store are the same shape (a DataStore backend, above core) and
# io_pq/io_bh the other (a format, below it) -- neither is special, and neither
# is this.
#
# tttrlib::highfive carries the BUILD_PHOTON_HDF definition, and both targets
# build either way: without it every entry point is present and fails cleanly,
# so nothing above has to be compiled twice.
#
# io_hdf5 is where HighFive and <hdf5.h> stop. Before it existed, TTTRHeader.h
# forward-declared HighFive::Group for one private method, so every consumer of
# the photon-stream data model -- imaging, correlation, fitting -- needed an
# HDF5 toolchain on its include path to compile a header that has nothing to do
# with HDF5.
tttrlib_add_module(
    NAME     io_hdf5
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/io_hdf5.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  io
    EXTERNAL_DEPS tttrlib::highfive
    TEST_DIR test/python/tttr
)

tttrlib_add_module(
    NAME     io_hdf5_table
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/io_hdf5_table.cpp"
    SWIG_INTERFACES Hdf5Table.i
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  io core
    EXTERNAL_DEPS tttrlib::highfive
    TEST_DIR test/python
)
