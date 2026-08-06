# SPDX-License-Identifier: BSD-3-Clause
#
# io_csv -- a threaded CSV reader that fills a DataStore directly, and the
# writer that takes one back out.
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
             "${CMAKE_CURRENT_SOURCE_DIR}/src/io_csv_writer.cpp"
    SWIG_INTERFACES CsvReader.i CsvWriter.i
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  io core
    TEST_DIR test/python
)

# Does std::to_chars take a double on this toolchain?
#
# Asked rather than deduced, because neither feature-test macros nor compiler
# versions answer it: libc++ has shipped the floating-point overloads for years
# without ever defining __cpp_lib_to_chars, and libstdc++ before GCC 11 has the
# header but not those overloads. The writer needs the shortest text that reads
# back as the same double; with this it gets it in one pass, and without it
# falls back to a snprintf ladder that is correct and several times slower.
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "${CMAKE_CXX17_STANDARD_COMPILE_OPTION}")
check_cxx_source_compiles("
#include <charconv>
int main() { char b[64]; return std::to_chars(b, b + 64, 1.5).ptr == b; }
" TTTRLIB_HAVE_FP_TO_CHARS)
unset(CMAKE_REQUIRED_FLAGS)
if(TTTRLIB_HAVE_FP_TO_CHARS)
    target_compile_definitions(tttrlib_io_csv PRIVATE TTTRLIB_HAVE_FP_TO_CHARS)
endif()
