# SPDX-License-Identifier: BSD-3-Clause
#
# io_store -- the native store file (.dstore): a DataStore, saved and reloaded.
#
# No EXTERNAL_DEPS, and that is the point of it as much as the speed is. Every
# other way to put a DataStore on disk goes through HDF5, so a build configured
# with BUILD_PHOTON_HDF=OFF cannot persist one at all. This one has nothing to
# switch off.
#
# Depends on core for the same reason io_csv and io_hdf5_table do: it produces
# and consumes a DataStore, which lives there, and building the table into
# anything else first would mean holding two copies at the peak. The arrow runs
# io_store -> core, and core does not depend on io_store, so there is no cycle.
tttrlib_add_module(
    NAME     io_store
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/io_store.cpp"
    SWIG_INTERFACES StoreFile.i
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  io core
    TEST_DIR test/python
)
