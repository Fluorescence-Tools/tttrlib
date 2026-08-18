# SPDX-License-Identifier: BSD-3-Clause
#
# burst -- burst selection, features and the analyses built on them.
#
tttrlib_add_module(
    NAME     burst
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/TTTRBurstSearch.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstSearchDispatch.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstConfidence.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstSearchBayesianBlocks.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstSearchKalman.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstSearchBOCPD.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/RecurrenceAnalysis.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstSearchMaxTree.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstSearchRegistry.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstFilter.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstFeature.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstFeatureExtractor.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BVA.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/TwoCDE.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstML.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  util math core plugin
    EXTERNAL_DEPS tttrlib::json
    SWIG_INTERFACES BurstSignificance.i BurstFilter.i BurstFeature.i BurstFeatureExtractor.i BVA.i TwoCDE.i BurstML.i RecurrenceAnalysis.i
    TEST_DIR test/python/burstfilter
)
