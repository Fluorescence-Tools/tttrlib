# SPDX-License-Identifier: BSD-3-Clause
#
# registry -- the curated capability description, by name.
#
# `tttrlib.registry("fit")`, `registry("burst_search")`, `registry("objective")`
# and friends: JSON-Schema descriptions of what the library can do, so a caller
# like chisurf can render a form for a capability without knowing anything
# tttrlib-specific about it.
#
# Its own module rather than a corner of `core` because this is the layer the
# plugin host will attach to. Today the tables are static literals paired with
# hardcoded dispatch; turning them into real runtime name->capability tables is
# what makes a dropped-in binary able to add a file format, a fit model or a
# burst search. Registry.h itself includes only <string> and <vector>, which is
# the property that lets everything above depend on it cheaply.
tttrlib_add_module(
    NAME     registry
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/Registry.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/FitRegistry.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/OperationRegistry.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  io plugin util core burst algorithm   # burst_search_algorithms_json lives in burst now
    EXTERNAL_DEPS tttrlib::json
    SWIG_INTERFACES Registry.i
    TEST_DIR test/python
)
