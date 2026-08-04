# SPDX-License-Identifier: BSD-3-Clause
#
# burst -- burst selection, features and the analyses built on them.
#
# What is left of the burst code once the SEARCHES are accounted for. Five
# translation units define `TTTR::` methods outside TTTR.cpp, because TTTR.h
# publishes the whole burst-search API as its own methods; those are
# implementations of core's own class and stay in `core`. The rest -- filtering
# and selection, per-burst features, burst variance analysis and the
# two-channel kernel density estimator -- has no such tie and comes out whole.
#
# This module could not be extracted until `core` was, which is the correction
# to the planned order recorded in modules/MODULE-DEBT.md.
tttrlib_add_module(
    NAME     burst
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstFilter.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstFeature.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstFeatureExtractor.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BVA.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/TwoCDE.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util core
    EXTERNAL_DEPS tttrlib::json
    SWIG_INTERFACES BurstFilter.i BurstFeature.i BurstFeatureExtractor.i BVA.i TwoCDE.i
    TEST_DIR test/python/burstfilter
)
