# SPDX-License-Identifier: BSD-3-Clause
#
# graph -- a generic node/layer graph.
#
# Kept as its own module because that is the honest description of it: nothing
# else in tttrlib includes GraphNode.h or LayerNode.h, no binding wraps it, and
# no test exercises it. Isolating it is what makes that visible -- inside the
# monolith it was indistinguishable from code that is used.
#
# It is a candidate for deletion. Declaring it costs one small library and
# answers the question "is anything using this?" with a dependency graph instead
# of a guess.
tttrlib_add_module(
    NAME     graph
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/GraphNode.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/LayerNode.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    # LayerNode.cpp reaches into the CLSM hierarchy, which the headers do not
    # show -- another reason to have the edge declared rather than inherited.
    DEPENDS  util imaging
    OPTIONAL
)
