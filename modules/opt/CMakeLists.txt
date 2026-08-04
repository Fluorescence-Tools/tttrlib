# SPDX-License-Identifier: BSD-3-Clause
#
# opt -- the L-BFGS optimizer, header-only.
#
# A module with nothing to compile is still worth declaring. i_lbfgs.h lived
# among the HMM headers, so every consumer of it -- `decay` through DecayFit.h
# and DecayFit23.h, `localization` through ImageLocalization.h -- appeared to
# depend on `hmm`. Those edges were an artefact of where the file sat, and
# naming the optimizer as its own unit is what removes them.
tttrlib_add_module(
    NAME      opt
    INTERFACE
    HEADERS   "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
