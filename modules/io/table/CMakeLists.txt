# SPDX-License-Identifier: BSD-3-Clause
#
# io_table -- one vocabulary for a table in a file, whatever the file is.
#
# The only module that depends on several format modules at once, and that is
# what it is for: it chooses between them. Everything below it stays unaware of
# the others, so the dependency arrows all point one way and adding a fourth
# table format touches this file and this module's dispatch, not the three that
# already exist.
#
# Declared after all of them in modules/io/CMakeLists.txt for that reason.
tttrlib_add_module(
    NAME     io_table
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/io_table.cpp"
    SWIG_INTERFACES Table.i
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  io_store io_hdf5_table io_csv io_pto io core
    TEST_DIR test/python
)
