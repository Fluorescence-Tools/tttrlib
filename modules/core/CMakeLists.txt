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
# The burst searches USED to be here, and are not any more: see
# modules/burst/src/TTTRBurstSearch.cpp. They stay declared as TTTR methods,
# because that is the public interface in three languages, but a member function
# may be defined in any translation unit that sees the class -- so the
# implementations live in `burst` and core contains no burst-search code at all.
#
#   * The FORMAT readers. PhotonscoreD7 and the HDF5 paths inside TTTR.cpp are
#     here because the reader interface does not exist yet. Splitting them into
#     io_* modules is what makes file formats plugin-extensible, and is the
#     reason this layering matters at all.
tttrlib_add_module(
    NAME     core
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/DataStore.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/RecordStreamWriter.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/Histogram.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/HistogramAxis.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/TTTR.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/TTTRHeader.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/TTTRMask.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/TTTRRange.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/TTTRSelection.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/TTTRStream.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/Channel.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/FileCheck.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/MicrotimeLinearization.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  io plugin io_ps io_sm io_cz io_bh io_pq io_be io_fl io_hdf5 util
    EXTERNAL_DEPS tttrlib::json
    SWIG_INTERFACES Histogram.i HistogramNd.i DataStore.i TTTR.i TTTRHeader.i TTTRRange.i TTTRSelection.i TTTRMask.i
                    Channel.i FileCheck.i RecordStream.i
    TEST_DIR test/python/tttr
)
