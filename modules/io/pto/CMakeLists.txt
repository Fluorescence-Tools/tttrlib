# SPDX-License-Identifier: BSD-3-Clause
#
# io_pto -- tttrlib's half of PTO, the PhoTon cOntainer (an EBML document with
# DocType "pto").
#
# The container itself is ptolib (thirdparty/ptolib/ptolib.h), the header
# shared with IMP.bff, and is compiled once in modules/core/src/DataStore.cpp.
# What this module adds needs a photon library behind it: recognising a photon
# file by its bytes, indexing a record stream with cues, reading a range of
# events out of an object into a TTTR, streaming an acquisition into a
# container, and the registration that makes TTTR("run.pto") work.
#
# The standalone `pto` inspector and the `ptoview` TUI moved to ptolib with the
# container; `tttr pto` and `tttr tui` in modules/cli remain.
tttrlib_add_module(
    NAME     io_pto
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/io_pto.cpp"
    SWIG_INTERFACES Pto.i
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  io core io_store
    TEST_DIR test/python
)
