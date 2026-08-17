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
             "${CMAKE_CURRENT_SOURCE_DIR}/src/HmmLattice.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/MaxEntQp.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/Nnls.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/KMeans.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/Kalman.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/Watershed.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util
    EXTERNAL_DEPS tttrlib::json tttrlib::pocketfft
    SWIG_INTERFACES NeuralNet.i Cluster.i Deconvolution.i Jitter.i Sampling.i
                    HmmLattice.i Kalman.i Watershed.i
    TEST_DIR test/python/misc
)

# Cluster.cpp, KMeans.cpp and Watershed.cpp carry their fp-contraction
# contract in source: all three open with `#pragma STDC FP_CONTRACT OFF`,
# because each is a bit-exact port of a Python implementation whose correctness
# depends on one unit in the last place (a tied mutual-reachability edge for
# Cluster.cpp, a restart-ranked inertia for KMeans.cpp, an interpolated
# contour endpoint for Watershed.cpp). A compile flag was tried first and
# rejected: `if(NOT MSVC)` silently skipped Windows, so the contract lived in
# the build instead of the code. The pragma is portable across clang, gcc and
# MSVC and needs no build-system support, so no per-file COMPILE_OPTIONS here.
# no per-file COMPILE_OPTIONS here.

# HmmLattice.cpp must not be compiled with fast math.
#
# -inf is a value in a log-domain lattice, not an error: a structurally
# constrained model has whole -inf columns, and a frame no state can explain is
# -inf across. `-ffast-math` implies `nnan`/`ninf`, which license the compiler
# to assume no operand is NaN or infinite -- and that folds away exactly the
# guards the recursion depends on (`vmax == -inf`, `maximum == -inf`,
# `total > 0`). The nan that appears instead spreads through the M-step into
# every later EM iteration *and* destroys the -inf log-likelihood that would
# have reported it, so the failure is both silent and permanent.
#
# The flag is pinned rather than merely not-set, because it is the kind of
# thing a global release-flags change adds without anyone thinking about this
# file. `test_hmm_lattice.py` has the all--inf test that catches it if this
# line is ever lost.
if(MSVC)
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/HmmLattice.cpp"
        PROPERTIES COMPILE_OPTIONS "/fp:precise")
else()
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/HmmLattice.cpp"
        PROPERTIES COMPILE_OPTIONS "-fno-fast-math")
endif()
