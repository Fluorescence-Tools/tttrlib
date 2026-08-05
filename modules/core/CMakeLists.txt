# SPDX-License-Identifier: BSD-3-Clause
#
# core -- the photon-stream data model.
#
# TTTR and the types that travel with it: the header, ranges, selections, masks,
# channels, the record readers and the file-type detection that decides which
# one to use. Everything above this reads photon streams; nothing here knows
# what they will be used for.
#
# It had to come out before anything else in the tangled middle. `burst`, `nn`
# and the rest all sit on both ends of an edge with the residual `legacy` module
# while core is inside it, and CMake rejects a link cycle between shared
# libraries outright. See modules/MODULE-DEBT.md.
#
# Two things here are core's only by accident of ownership, and both are
# recorded debt:
#
#   * The burst SEARCHES. Five translation units define `TTTR::` methods
#     outside TTTR.cpp, because TTTR.h publishes the whole burst-search API as
#     its own methods. They are implementations of core's own class, so they
#     cannot live anywhere else -- and they drag BurstSignificance.h and the
#     BurstSearch*.h headers in with them. What is left over (BurstFilter,
#     BurstFeature, BVA, TwoCDE) is genuinely separable and becomes `burst`.
#     Exit: free functions taking `const TTTR&`, with the methods kept as
#     forwarders and SWIG %extend re-attaching them to the Python proxy, so the
#     API does not change.
#
#   * The FORMAT readers. PhotonscoreD7 and the HDF5 paths inside TTTR.cpp are
#     here because the reader interface does not exist yet. Splitting them into
#     io_* modules is what makes file formats plugin-extensible, and is the
#     reason this layering matters at all.
tttrlib_add_module(
    NAME     core
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/DataStore.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/Histogram.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/HistogramAxis.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/TTTR.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/TTTRHeader.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/TTTRMask.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/TTTRRange.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/TTTRSelection.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/Channel.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/FileCheck.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/MicrotimeLinearization.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstConfidence.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstSearchBayesianBlocks.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstSearchKalman.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstSearchMaxTree.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/BurstSearchRegistry.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  io io_ps io_sm io_cz io_bh io_pq io_be io_hdf5 util
    EXTERNAL_DEPS tttrlib::json
    SWIG_INTERFACES Histogram.i HistogramNd.i DataStore.i TTTR.i TTTRHeader.i TTTRRange.i TTTRSelection.i TTTRMask.i
                    Channel.i FileCheck.i
    TEST_DIR test/python/tttr
)
