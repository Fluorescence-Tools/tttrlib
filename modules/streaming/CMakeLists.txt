# SPDX-License-Identifier: BSD-3-Clause
#
# streaming -- online / real-time analysis for live photon streams.
#
# Streaming consumers that accept photons one at a time (or in small batches)
# and maintain incremental state.  Each class mirrors a batch consumer:
#
#   StreamingCLSMImage      ↔  CLSMImage (scanned-image reconstruction)
#   StreamingCorrelator     ↔  Correlator (Wahl multi-tau FCS)
#   StreamingBurstDetector  ↔  BurstFilter (FRETBursts-style burst search)
#   StreamingDecayHistogram ↔  DecayHistogram (TCSPC microtime histogram)
#   StreamingPhasor         ↔  DecayPhasor (FLIM phasor g, s)
#   StreamingIntensityTrace ↔  compute_intensity_trace (MCS / intensity trace)
#
# The consumers are header-only. Most are std-only; StreamingCLSMImage is the
# exception and deliberately so -- it delegates each completed frame to the
# ordinary CLSMImage rather than carrying a second copy of the marker semantics,
# so it depends on core and clsm.
tttrlib_add_module(
    NAME     streaming
    INTERFACE
    HEADERS  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    DEPENDS  core clsm
    SWIG_INTERFACES Streaming.i
    TEST_DIR test/python/streaming
)
