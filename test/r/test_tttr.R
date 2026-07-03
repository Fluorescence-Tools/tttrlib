# Cross-language reference test for the R bindings.
# Asserts the SAME values as test/python/tttr/test_cross_language_reference.py.
#
# The SWIG-generated module can be provided in two ways:
#   * installed package  -> library(tttrlib)
#   * raw build artifacts -> dyn.load()/source() via env vars:
#       TTTRLIB_R_SO      : path to the built tttrlib.so / .dylib
#       TTTRLIB_R_WRAPPER : path to the generated tttrlib.R
# Data file location comes from TTTRLIB_DATA (default: ./tttr-data).

REF_SIZE          <- 183657
REF_N_MICRO_CHAN  <- 4096
REF_MACRO_FIRST   <- 56916
REF_SUM_MICRO     <- 242477881
REF_SUM_MACRO     <- 443406877425185
REF_SUM_ROUTING   <- 880650
REF_CORRCURVE_SIZE <- 16
REF_BURST_LEN     <- 586
REF_BURST_SUM     <- 59237329
REF_HIST_LEN       <- 4096
REF_HIST_PEAK_CHAN <- 814
REF_HIST_PEAK_VAL  <- 676
REF_MICRO_AT_0     <- 1440
REF_ROUTING_AT_0   <- 9
REF_MICRO_RES      <- 3.2958984375e-12
REF_BY_CHANNEL_0   <- 56499
REF_BY_CHANNEL_8   <- 79468
REF_USED_CHANNELS  <- c(0, 1, 8, 9)

so      <- Sys.getenv("TTTRLIB_R_SO")
wrapper <- Sys.getenv("TTTRLIB_R_WRAPPER")
if (nzchar(so) && nzchar(wrapper)) {
  dyn.load(so)
  source(wrapper)
  cacheMetaData(1)
} else {
  library(tttrlib)
}

data_root <- Sys.getenv("TTTRLIB_DATA", unset = "tttr-data")
fn <- file.path(data_root, "bh", "bh_spc132.spc")
stopifnot(file.exists(fn))

tttr <- TTTR(fn, "SPC-130")

# --- scalar getters -------------------------------------------------------
stopifnot(TTTR_size(tttr) == REF_SIZE)
stopifnot(TTTR_get_n_valid_events(tttr) == REF_SIZE)
stopifnot(TTTR_get_number_of_micro_time_channels(tttr) == REF_N_MICRO_CHAN)

# --- native R-vector marshalling (rarrays.i) ------------------------------
macro <- TTTR_get_macro_times(tttr)
stopifnot(length(macro) == REF_SIZE)
stopifnot(macro[1] == REF_MACRO_FIRST)
stopifnot(sum(as.numeric(macro)) == REF_SUM_MACRO)

micro <- TTTR_get_micro_times(tttr)
stopifnot(length(micro) == REF_SIZE)
stopifnot(sum(as.numeric(micro)) == REF_SUM_MICRO)

routing <- TTTR_get_routing_channel(tttr)
stopifnot(length(routing) == REF_SIZE)
stopifnot(sum(as.numeric(routing)) == REF_SUM_ROUTING)

# --- correlator curve (deterministic, no data file needed) ----------------
cc <- CorrelatorCurve()
CorrelatorCurve_n_bins_set(cc, 3)
CorrelatorCurve_n_casc_set(cc, 5)
stopifnot(CorrelatorCurve_size(cc) == REF_CORRCURVE_SIZE)

# --- burst search (std::vector<long long> -> native R vector) -------------
bursts <- TTTR_burst_search(tttr, 30L, 10L, 1e-3, "sliding_window")
stopifnot(length(bursts) == REF_BURST_LEN)
stopifnot(sum(as.numeric(bursts)) == REF_BURST_SUM)

# --- micro-time histogram (multi-output -> list(NULL, hist, time)) --------
hist <- TTTR_get_microtime_histogram(tttr)[[2]]
stopifnot(length(hist) == REF_HIST_LEN)
stopifnot(which.max(hist) - 1L == REF_HIST_PEAK_CHAN)   # R is 1-based -> 0-based channel
stopifnot(max(hist) == REF_HIST_PEAK_VAL)

# --- index getters (event 0) ----------------------------------------------
stopifnot(TTTR_get_macro_time_at(tttr, 0L) == REF_MACRO_FIRST)
stopifnot(TTTR_get_micro_time_at(tttr, 0L) == REF_MICRO_AT_0)
stopifnot(TTTR_get_routing_channel_at(tttr, 0L) == REF_ROUTING_AT_0)

# --- micro-time resolution (header) ---------------------------------------
stopifnot(abs(TTTRHeader_micro_time_resolution_get(TTTR_get_header(tttr)) - REF_MICRO_RES) < 1e-20)

# --- sub-selection by routing channel (native c() vector) -----------------
stopifnot(TTTR_size(TTTR_get_tttr_by_channel(tttr, c(0L))) == REF_BY_CHANNEL_0)
stopifnot(TTTR_size(TTTR_get_tttr_by_channel(tttr, c(8L))) == REF_BY_CHANNEL_8)

# --- used routing channels ------------------------------------------------
uc <- sort(as.integer(TTTR_get_used_routing_channels(tttr)))
stopifnot(all(uc == REF_USED_CHANNELS))

# --- header (std::string -> R character) ----------------------------------
hdr <- TTTRHeader_get_json(TTTR_get_header(tttr))
stopifnot(grepl("MeasDesc_ContainerType", hdr))

cat("R cross-language reference test: PASS\n")
