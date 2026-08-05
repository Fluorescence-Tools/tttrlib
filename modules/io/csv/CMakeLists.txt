# SPDX-License-Identifier: BSD-3-Clause
#
# io_csv -- a threaded CSV reader that fills a DataStore directly.
#
# The one module here that depends on `core`, and the exception is deliberate.
# The vendor format modules deliberately do NOT: they decode a file into arrays,
# and the dependency arrow runs core -> io_* -> io so a format can change without
# recompiling the photon-stream data model.
#
# This one produces a DataStore, which lives in core, and building it into
# anything else first would mean copying every value twice and holding both
# copies at the peak -- which for a table that is most of the memory in the
# process is what decides whether the file opens at all. So the arrow runs the
# other way for this module: io_csv -> core. There is no cycle, because core
# does not depend on io_csv.
tttrlib_add_module(
    NAME     io_csv
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/io_csv.cpp"
    SWIG_INTERFACES CsvReader.i
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  io core
    TEST_DIR test/python
)
