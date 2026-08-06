# SPDX-License-Identifier: BSD-3-Clause
#
# io_pto -- PTO, the PhoTon cOntainer: an EBML document with DocType "pto".
#
# The container that holds a whole measurement -- the photon stream, the bursts
# found in it, a spectrum, the original instrument file -- each with a UID that
# survives the file being rewritten, and typed tags that can reference those
# UIDs so an application can record how they relate.
#
# Depends on io_store because the natural payload for anything tabular is a
# .dstore, and embedding one costs nothing: io_store can write straight into an
# already-open file, so a photon stream goes into the container without a copy
# and without a temporary. Nothing else is needed -- like io_store, this module
# has no external dependency, so a build with BUILD_PHOTON_HDF=OFF has a
# container and a table format and needs no HDF5 at all.
tttrlib_add_module(
    NAME     io_pto
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/io_pto.cpp"
    SWIG_INTERFACES Pto.i
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  io core io_store
    TEST_DIR test/python
)
