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
#   util                    verbosity, SIMD dispatch, RNGs, bit ops, L-BFGS,
#                           and the feed-forward network -- everything that does
#                           not know what a photon is
#   core                    the photon-stream data model -- TTTR and friends,
#                           plus histograms: binning a photon property is as basic
#                           as the properties themselves, and everything above
#                           core does it
#   registry                capability descriptions, by name
#   imaging                 CLSM + correlator + phasor, and the things that take an
#                           image and give back a better one: super-resolution
#                           and single-molecule localization
#   decay                   decay fitting (does NOT depend on core)
#   burst                   burst selection and features
#   sim                     the simulator (depends on nothing above util)
#   hmm                     photon-by-photon HMMs -- the top of the graph
#   io/{pq,bh,cz,sm,ps,be,hdf5,image,csv}, pda
#
# The order in which they were EXTRACTED is not this order and is not the one
# originally planned; see modules/MODULE-DEBT.md for why core had to come first.
add_subdirectory(util)
add_subdirectory(io)   # declares `io` and, beneath it, one module per format
add_subdirectory(plugin)  # the drop-in binary plugin ABI, host and loader; sits on `io`
add_subdirectory(sim)
add_subdirectory(core)
add_subdirectory(imaging)
add_subdirectory(decay)

add_subdirectory(burst)
add_subdirectory(registry)   # calls into burst
add_subdirectory(hmm)
add_subdirectory(pda)

tttrlib_finalize_modules()
