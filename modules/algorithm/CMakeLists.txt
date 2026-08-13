# SPDX-License-Identifier: BSD-3-Clause
#
# algorithm -- the algorithm descriptor and the one registration path.
#
# Deliberately at the bottom of the dependency graph, depending
# on nothing but a JSON writer: `registry` already depends on `burst` (it calls
# burst_search_algorithms_json), so an algorithm module that wanted to register
# itself could not depend on `registry` without closing a cycle. Putting the
# mechanism in its own module is what lets every algorithm module -- burst,
# decay, fcs, hmm, pda -- declare itself, which is the whole point of having one
# registration path rather than a literal per family.
tttrlib_add_module(
    NAME     algorithm
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/AlgorithmRegistry.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BuiltinAlgorithms.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    EXTERNAL_DEPS tttrlib::json
)
