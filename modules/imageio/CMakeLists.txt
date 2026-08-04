# SPDX-License-Identifier: BSD-3-Clause
#
# imageio -- TIFF I/O for 2D and 3D numeric arrays.
#
# A genuine leaf: TiffArrayIO.h includes <string> and <cstdint> and nothing
# else, and no other tttrlib header includes it. That makes it the right first
# extraction -- if anything goes wrong here it is the module machinery, not a
# tangle in the sources.
#
# Its one external dependency is libtiff, which is wired at directory scope for
# the whole project today. Naming it here is what will eventually let a build
# without TIFF support simply not configure this module.

tttrlib_add_module(
    NAME     imageio
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/TiffArrayIO.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    SWIG_INTERFACES Tiff.i
    TEST_DIR test/python/tiff
)
