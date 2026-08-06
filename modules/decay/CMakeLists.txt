# SPDX-License-Identifier: BSD-3-Clause
#
# decay -- fluorescence decay fitting.
#
# The models (fit23/24/25/26, n-exponential, the donor-FRET-anisotropy kernel),
# the convolution and statistics they are built from, the priors, and the
# problem and setup types that describe a fit.
#
# It depends on `opt` for the L-BFGS optimizer and on `registry` for the
# named-parameter helpers. It does NOT depend on core: a decay fit takes arrays
# and a description, not a photon stream -- which is why it comes out cleanly
# even though it is the second-largest thing in the library.
#
# src/DecayFitModelRegistration.h stays under src/ deliberately: it is the
# internal hook that makes model registration explicit rather than a static
# initialiser nothing references, and it is not API.
tttrlib_add_module(
    NAME     decay
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/DecayConvolution.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/DecayFit.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/DecayFit23.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/DecayFit24.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/DecayFit25.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/DecayFit26.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/DecayFitDFA.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/DecayFitModel.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/DecayFitModelFit2x.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/DecayFitModelNExp.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/DecayFitPlugin.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/DecayFitNExp.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/DecayFitPrior.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/DecayFitProblem.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/DecayFitSetup.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/DecayStatistics.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util registry plugin
    EXTERNAL_DEPS tttrlib::json tttrlib::pocketfft
    SWIG_INTERFACES DecayFit.i DecayConvolution.i
    TEST_DIR test/python/decayfit
)
