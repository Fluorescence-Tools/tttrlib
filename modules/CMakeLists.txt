# SPDX-License-Identifier: BSD-3-Clause
#
# Module declarations.
#
# The split is complete: src/ and include/ are empty and there is no residual
# `legacy` module. Every source belongs to exactly one subsystem, and
# tttrlib_finalize_modules() fails the configure if that ever stops being true.
#
# Declaration order is dependency order, because a module's DEPENDS must already
# exist. The layering, bottom up:
#
#   util                    verbosity, SIMD dispatch, RNG facades, bit ops
#   opt (header-only)       L-BFGS
#   hist                    histograms
#   core                    the photon-stream data model -- TTTR and friends
#   registry                capability descriptions, by name
#   imaging                 CLSM + correlator + phasor (co-located; mutual friends)
#   decay                   decay fitting (does NOT depend on core)
#   burst                   burst selection and features
#   sim                     the simulator (depends on nothing above util)
#   nn                      feed-forward network, for the HMM surrogate
#   hmm                     photon-by-photon HMMs -- the top of the graph
#   imageio, pda, superres, localization
#
# The order in which they were EXTRACTED is not this order and is not the one
# originally planned; see modules/MODULE-DEBT.md for why core had to come first.
add_subdirectory(util)
add_subdirectory(io)
add_subdirectory(io_photonscore)
add_subdirectory(opt)
add_subdirectory(hist)
add_subdirectory(imageio)
add_subdirectory(sim)
add_subdirectory(localization)
add_subdirectory(core)
add_subdirectory(imaging)
add_subdirectory(registry)
add_subdirectory(decay)

add_subdirectory(burst)
add_subdirectory(nn)
add_subdirectory(hmm)
add_subdirectory(pda)
add_subdirectory(superres)

tttrlib_finalize_modules()
