# SPDX-License-Identifier: BSD-3-Clause
#
# math -- numerical kernels shared across tttrlib.
#
# Dense linear algebra (Mat.h), optimisers (i_lbfgs.h), RNGs (Random.h,
# SimPcgRandom.h), and the feed-forward network (NeuralNet) live here rather
# than in util, because they share a subject — numerical mathematics — and
# none of them knows what a photon is.
#
# The split from util separates "stuff that does math" from "stuff that does
# plumbing" (Verbose, Progress, ByteOrder, bimap).  A module that needs only
# Verbose no longer transitively pulls the neural net; a module that needs
# L-BFGS no longer transitively pulls progress reporting.
#
# Mat.h is the reason this module exists: it is a standalone, dependency-free
# dense matrix library with an Armadillo-flavoured API and a SIMD GEMM kernel
# (NEON / SSE2 / AVX) that holds its own against Eigen on the matrix sizes the
# rest of tttrlib uses.  NeuralNet was its first consumer; the HMM and burst
# modules are being migrated to use it for their matrix operations.
tttrlib_add_module(
    NAME     math
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/NeuralNet.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/Cluster.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/Deconvolution.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/Jitter.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util
    EXTERNAL_DEPS tttrlib::json tttrlib::pocketfft
    SWIG_INTERFACES NeuralNet.i Cluster.i Deconvolution.i Jitter.i
    TEST_DIR test/python/misc
)

# Cluster.cpp must not contract a*b+c into an FMA.
#
# The minimum spanning tree it builds is consumed by callers that keep their own
# pure-Python implementation of the same algorithm for environments without this
# library, and the two must agree exactly -- a mutual-reachability graph is full
# of tied edge weights, so a difference of one unit in the last place changes
# which edge is picked, which changes the dendrogram, which changes the cluster
# count. Contraction is the one optimisation that changes the bits, and it is
# worth nothing here: the distance loop is memory bound.
if(NOT MSVC)
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Cluster.cpp"
        PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()
