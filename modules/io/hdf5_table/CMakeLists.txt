# SPDX-License-Identifier: BSD-3-Clause
#
# io_hdf5_table -- a table in HDF5, one dataset per column, straight into a
# DataStore.
#
# Separate from io_hdf5 because `core` reads Photon-HDF5, so io_hdf5 cannot
# depend on `core` without a cycle -- and this needs `core`, since a DataStore
# lives there. The arrow runs core -> io_hdf5 and io_hdf5_table -> core, which
# is a line rather than a loop. Same reasoning as io_csv: a reader that produces
# a DataStore has to be above it, not below.
tttrlib_add_module(
    NAME     io_hdf5_table
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/io_hdf5_table.cpp"
    SWIG_INTERFACES Hdf5Table.i
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  io core
    EXTERNAL_DEPS tttrlib::highfive
    TEST_DIR test/python
)
