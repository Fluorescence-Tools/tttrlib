# SPDX-License-Identifier: BSD-3-Clause
#
# hist -- histograms and histogram axes.
#
# A leaf: both headers include only standard library headers. It is not a leaf
# of the dependency graph though -- TTTR.h and TTTRHeader.h include Histogram.h,
# so `core` depends on this rather than the other way round. That is the first
# declared edge between modules, and the reason `legacy` names it in DEPENDS.
tttrlib_add_module(
    NAME     hist
    SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/src/Histogram.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/HistogramAxis.cpp"
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    SWIG_INTERFACES Histogram.i
    TEST_DIR test/python
)
