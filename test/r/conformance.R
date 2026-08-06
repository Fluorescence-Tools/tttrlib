# SPDX-License-Identifier: BSD-3-Clause
#
# The R runner of the cross-language conformance suite.
#
# A port of test/conformance/py/interpreter.py: the same case list, the same
# vocabulary, the same expected values. A failure here means the R binding
# disagrees with the others -- never that the number needs updating. See
# test/conformance/README.md.
#
# Module discovery matches the other R tests:
#   TTTRLIB_R_SO / TTTRLIB_R_WRAPPER -> raw build artifacts
#   otherwise                        -> library(tttrlib)
# TTTRLIB_DATA points at the data root; TTTRLIB_CONFORMANCE_REPORT, when set,
# receives the JSON report tools/conformance_matrix.py reads.

suppressPackageStartupMessages(library(jsonlite))

so <- Sys.getenv("TTTRLIB_R_SO")
wrapper <- Sys.getenv("TTTRLIB_R_WRAPPER")
if (nzchar(so) && nzchar(wrapper)) {
  dyn.load(so); source(wrapper); cacheMetaData(1)
} else {
  library(tttrlib)
}

HERE <- tryCatch({
  a <- commandArgs(trailingOnly = FALSE)
  dirname(sub("^--file=", "", a[grep("^--file=", a)][1]))
}, error = function(e) ".")
CASES_DIR <- normalizePath(file.path(HERE, "..", "conformance", "cases"))
DATA_ROOT <- Sys.getenv("TTTRLIB_DATA", unset = "tttr-data")

# The ColumnType and Hdf5WriteMode enumerations are passed as plain integers in
# R -- see the note in ext/r/tttrlib.i about SWIG's R backend emitting two
# different names for the same scoped-enum constant. These are the C++
# declaration order, which is the enum's value.
CT <- c(float64 = 0L, float32 = 1L, int64 = 2L, int32 = 3L, int16 = 4L,
        int8 = 5L, uint64 = 6L, uint32 = 7L, uint16 = 8L, uint8 = 9L,
        bool = 10L, string = 11L)
CT_NAME <- setNames(names(CT), as.character(CT))
HDF5_UPDATE <- 0L

MAX_EXACT_INTEGER <- 2^53

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

# rarrays.i's ARGOUTVIEW argout appends to the result list, so a void getter
# with one output array comes back wrapped. unlist() is the whole conversion.
view_of <- function(x) as.numeric(unlist(x))

# A multi-dimensional result, flattened ROW-major.
#
# rarrays.i correctly hands back a column-major R array with a dim attribute, so
# m[i, j] is C's [i][j] -- but as.numeric()/unlist() then walk it column-major
# and produce the TRANSPOSE of the flat order the other three runners see. That
# is not hypothetical: it turned the PDA S1S2 peak from cell 189 into cell 99.
#
# aperm() with the reversed permutation makes the last dimension vary fastest,
# which is exactly C order; for a matrix it is t(). Order-independent reductions
# (sum, max, count_gt) never noticed, which is what made this worth fixing
# rather than working around at the one call site that caught it.
row_major <- function(x) {
  if (is.list(x)) {
    # Two different list shapes arrive here. A void getter with one ARGOUTVIEW
    # output appends to a result list -- list(NULL, array) -- so the array is the
    # LAST element. A std::vector<std::vector<double>> comes back as a list of
    # numeric ROWS, and list order is already row-major, so it just unlists.
    if (length(x) == 1L) return(row_major(x[[1]]))
    if (all(vapply(x, is.numeric, logical(1)))) return(as.numeric(unlist(x)))
    return(row_major(x[[length(x)]]))
  }
  if (is.null(dim(x))) as.numeric(x)
  else as.numeric(aperm(x, rev(seq_along(dim(x)))))
}

# A binding is either a numeric vector or a string list; the element ops have to
# take both without turning "keep" into NA. unlist() collapses the list form and
# keeps the type, which is exactly what is wanted.
flat <- function(on) if (is.list(on)) unlist(on) else on

new_store_with <- function() DataStore()

add_typed <- function(store, name, values, type_name) {
  idx <- DataStore_add_column(store, name, CT[[type_name]])
  col <- DataStore_column(store, idx)
  v <- as.numeric(unlist(values))
  setter <- switch(type_name,
    float64 = Column_set_f64, float32 = Column_set_f32,
    int64 = Column_set_i64, int32 = Column_set_i32,
    int16 = Column_set_i16, int8 = Column_set_i8,
    uint64 = Column_set_u64, uint32 = Column_set_u32,
    uint16 = Column_set_u16, uint8 = Column_set_u8,
    stop("no setter for ", type_name))
  setter(col, v)
  # set_n_rows is what makes the store agree with its columns; the C++ side
  # keeps them independent on purpose (a store can be resized before filling).
  if (DataStore_n_rows(store) < length(v)) DataStore_set_n_rows(store, length(v))
  invisible(NULL)
}

column_typed <- function(store, name, type_name) {
  col <- DataStore_column_by_name(store, name)
  got <- CT_NAME[[as.character(Column_type(col))]]
  if (!identical(got, type_name))
    stop(sprintf("column '%s' is %s, the case asked for %s", name, got, type_name))
  getter <- switch(type_name,
    float64 = Column_get_f64_view, float32 = Column_get_f32_view,
    int64 = Column_get_i64_view, int32 = Column_get_i32_view,
    int16 = Column_get_i16_view, int8 = Column_get_i8_view,
    uint64 = Column_get_u64_view, uint32 = Column_get_u32_view,
    uint16 = Column_get_u16_view, uint8 = Column_get_u8_view,
    stop("no view for ", type_name))
  view_of(getter(col))
}

# ---------------------------------------------------------------------------
# The dispatch table -- one entry per op in test/conformance/OPS.md
# ---------------------------------------------------------------------------

OPS <- list(
  # -- generic --------------------------------------------------------------
  # length() of a character vector counts its ELEMENTS, so a single string would
  # answer 1 where every other language answers its character count. A string
  # list is an R list here (see the ds.* ops), so the two cases do not collide.
  len = function(on, a) if (is.character(on) && !is.list(on)) nchar(on) else length(on),
  sum = function(on, a) sum(as.numeric(on)),
  mean = function(on, a) mean(as.numeric(on)),
  min = function(on, a) min(as.numeric(on)),
  max = function(on, a) max(as.numeric(on)),
  # which.max is 1-based and the vocabulary is 0-based; both resolve ties to
  # the lowest index, which is what OPS.md requires.
  argmax = function(on, a) which.max(as.numeric(on)) - 1L,
  argmin = function(on, a) which.min(as.numeric(on)) - 1L,
  first = function(on, a) flat(on)[[1]],
  last = function(on, a) { v <- flat(on); v[[length(v)]] },
  nth = function(on, a) flat(on)[[as.integer(a[[1]]) + 1L]],
  slice = function(on, a) {
    v <- flat(on)
    lo <- as.integer(a[[1]]) + 1L
    hi <- min(as.integer(a[[2]]), length(v))
    if (hi < lo) v[0] else v[lo:hi]
  },
  to_list = function(on, a) as.list(flat(on)),
  unique_sorted = function(on, a) as.list(sort(unique(as.numeric(on)))),
  shape = function(on, a) list(length(on)),
  contains = function(on, a) grepl(a[[1]], on, fixed = TRUE),
  round = function(on, a) round(as.numeric(on), as.integer(a[[1]])),
  identity = function(on, a) on,
  count_gt = function(on, a) sum(as.numeric(on) > as.numeric(a[[1]])),

  # -- tttr -----------------------------------------------------------------
  "tttr.open" = function(on, a) TTTR(a[[1]], a[[2]]),
  "tttr.size" = function(on, a) TTTR_size(on),
  "tttr.n_valid_events" = function(on, a) TTTR_get_n_valid_events(on),
  "tttr.n_micro_channels" = function(on, a) TTTR_get_number_of_micro_time_channels(on),
  "tttr.macro_times" = function(on, a) as.numeric(TTTR_get_macro_times(on)),
  "tttr.micro_times" = function(on, a) as.numeric(TTTR_get_micro_times(on)),
  "tttr.routing_channels" = function(on, a) as.numeric(TTTR_get_routing_channel(on)),
  "tttr.macro_time_at" = function(on, a) TTTR_get_macro_time_at(on, as.integer(a[[1]])),
  "tttr.micro_time_at" = function(on, a) TTTR_get_micro_time_at(on, as.integer(a[[1]])),
  "tttr.routing_channel_at" = function(on, a) TTTR_get_routing_channel_at(on, as.integer(a[[1]])),
  "tttr.used_routing_channels" = function(on, a) as.numeric(TTTR_get_used_routing_channels(on)),
  "tttr.by_channel" = function(on, a) TTTR_get_tttr_by_channel(on, as.integer(unlist(a[[1]]))),
  "tttr.burst_search" = function(on, a)
    as.numeric(TTTR_burst_search(on, as.integer(a[[1]]), as.integer(a[[2]]),
                                 as.numeric(a[[3]]), a[[4]])),
  # The multi-output getter comes back as list(NULL, histogram, time axis).
  #
  # Only the no-argument form is reachable from R. SWIG's R overload dispatcher
  # matches against the C++ parameter list, so it tests argument 2 against
  # '_p_p_double' -- the output-pointer parameter that rarrays.i removes from
  # the R signature. No two-argument call can ever satisfy that, so passing a
  # coarsening explicitly always raises "cannot find overloaded function". The
  # no-argument overload defaults to 1, which is what the cases use.
  "tttr.microtime_histogram" = function(on, a) {
    if (as.integer(a[[1]]) != 1L)
      stop("UNSUPPORTED_OP:tttr.microtime_histogram with coarsening != 1 ",
           "(SWIG's R overload dispatcher cannot match the explicit form)")
    as.numeric(TTTR_get_microtime_histogram(on)[[2]])
  },
  "tttr.header_json" = function(on, a) TTTRHeader_get_json(TTTR_get_header(on)),
  "tttr.micro_time_resolution" = function(on, a)
    TTTRHeader_micro_time_resolution_get(TTTR_get_header(on)),
  "tttr.macro_time_resolution" = function(on, a)
    TTTRHeader_macro_time_resolution_get(TTTR_get_header(on)),

  # -- correlator -----------------------------------------------------------
  "correlator.curve_size" = function(on, a) {
    cc <- CorrelatorCurve()
    CorrelatorCurve_n_bins_set(cc, as.integer(a[[1]]))
    CorrelatorCurve_n_casc_set(cc, as.integer(a[[2]]))
    CorrelatorCurve_size(cc)
  },

  "correlator.new" = function(on, a) {
    co <- Correlator()
    Correlator_n_bins_set(co, as.integer(a[[1]]))
    Correlator_n_casc_set(co, as.integer(a[[2]]))
    co
  },
  "correlator.set_tttr" = function(on, a) Correlator_set_tttr(on, a[[1]], a[[2]]),
  "correlator.x_axis" = function(on, a) as.numeric(unlist(Correlator_get_x_axis(on))),
  "correlator.correlation" = function(on, a)
    as.numeric(unlist(Correlator_get_corr_normalized(on))),

  # -- datastore ------------------------------------------------------------
  "ds.new" = function(on, a) new_store_with(),
  "ds.n_rows" = function(on, a) DataStore_n_rows(on),
  "ds.n_columns" = function(on, a) DataStore_n_columns(on),
  "ds.n_groups" = function(on, a) DataStore_n_groups(on),
  "ds.column_names" = function(on, a) as.list(as.character(DataStore_column_names(on))),
  "ds.add_group" = function(on, a) DataStore_add_group(on, a[[1]]),
  "ds.ensure_group" = function(on, a) DataStore_ensure_group(on, a[[1]]),
  "ds.group" = function(on, a) DataStore_group(on, a[[1]]),
  "ds.has_group" = function(on, a) DataStore_has_group(on, a[[1]]),
  "ds.remove_group" = function(on, a) DataStore_remove_group(on, a[[1]]),
  "ds.group_names" = function(on, a) as.list(as.character(DataStore_group_names(on))),
  "ds.group_paths" = function(on, a) as.list(as.character(DataStore_group_paths(on))),
  "ds.set_label" = function(on, a) DataStore_set_label(on, a[[1]]),
  "ds.label" = function(on, a) DataStore_label(on),
  "ds.nbytes" = function(on, a) DataStore_nbytes(on),
  "ds.add_string" = function(on, a) {
    idx <- DataStore_add_column(on, a[[1]], CT[["string"]])
    col <- DataStore_column(on, idx)
    for (s in unlist(a[[2]])) Column_push_string(col, s)
    if (DataStore_n_rows(on) < length(unlist(a[[2]])))
      DataStore_set_n_rows(on, length(unlist(a[[2]])))
    invisible(NULL)
  },
  "ds.column_strings" = function(on, a) {
    col <- DataStore_column_by_name(on, a[[1]])
    n <- Column_size(col)
    if (n == 0) return(list())
    lapply(seq_len(n) - 1L, function(i) Column_string_at(col, as.integer(i)))
  },
  "ds.column_dtype" = function(on, a)
    CT_NAME[[as.character(Column_type(DataStore_column_by_name(on, a[[1]])))]],
  "ds.select_range" = function(on, a) {
    idx <- DataStore_find(on, a[[1]])
    if (idx < 0) stop("no column ", a[[1]])
    DataStore_select_range(on, idx, as.numeric(a[[2]]), as.numeric(a[[3]]))
  },
  "ds.n_selected" = function(on, a) DataStore_n_selected(on),

  # -- tiff ---------------------------------------------------------------------
  # aperm() turns the C-order (frame, row, col) block into the R array whose
  # dim attribute rarrays.i reads, which is the inverse of what row_major does
  # on the way back.
  "tiff.write_f64" = function(on, a) {
    d <- as.integer(c(a[[2]], a[[3]], a[[4]]))
    arr <- aperm(array(as.numeric(unlist(a[[5]])), dim = rev(d)), c(3, 2, 1))
    tiff_write_f64(a[[1]], arr)
  },
  "tiff.read_f64" = function(on, a) row_major(tiff_read_f64(a[[1]])),

  # -- bursts -------------------------------------------------------------------
  "burst.new" = function(on, a) BurstFilter(a[[1]]),
  "burst.find" = function(on, a) row_major(BurstFilter_find_bursts(on)),
  "burst.properties" = function(on, a) row_major(BurstFilter_get_all_burst_properties(on)),

  # -- pda ----------------------------------------------------------------------
  # get_1dhistogram is a two-output getter, so rarrays.i returns
  # list(NULL, x, y); the y values are the third element.
  "pda.new" = function(on, a)
    Pda(as.integer(a[[1]]), as.integer(a[[2]]), as.numeric(a[[3]]),
        as.numeric(a[[4]]), as.numeric(unlist(a[[5]]))),
  "pda.append" = function(on, a) Pda_append(on, as.numeric(a[[1]]), as.numeric(a[[2]])),
  "pda.evaluate" = function(on, a) Pda_evaluate(on),
  "pda.s1s2" = function(on, a) row_major(Pda_get_S1S2_matrix(on)),
  "pda.histogram_y" = function(on, a) as.numeric(Pda_get_1dhistogram(on)[[3]]),

  # -- decay fitting ------------------------------------------------------------
  "fit.names" = function(on, a) as.list(as.character(decay_fit_names())),
  "fit.setup_names" = function(on, a) as.list(as.character(decay_fit_setup_names(a[[1]]))),
  "fit.result_names" = function(on, a)
    as.list(as.character(decay_fit_result_names(a[[1]], 0L))),
  "fit.setup_vector" = function(on, a)
    as.numeric(decay_fit_setup_vector(a[[1]], a[[2]])),
  "fit.problem" = function(on, a) {
    p <- DecayFitProblem(as.integer(a[[1]]), as.integer(a[[2]]), as.numeric(a[[3]]))
    DecayFitProblem_irf_set(p, as.numeric(unlist(a[[4]])))
    DecayFitProblem_background_set(p, as.numeric(unlist(a[[5]])))
    DecayFitProblem_data_set(p, as.numeric(unlist(a[[6]])))
    p
  },
  "fit.new" = function(on, a)
    DecayFit2(a[[1]], as.numeric(unlist(a[[2]])), as.numeric(unlist(a[[3]]))),
  "fit.run" = function(on, a)
    DecayFit2_fit(on, as.numeric(unlist(a[[1]])),
                  DecayFitConstraints(as.integer(unlist(a[[2]]))), a[[3]]),
  "fit.objective" = function(on, a) DecayFitOutcome_objective_get(on),
  "fit.parameters" = function(on, a) as.numeric(DecayFitOutcome_parameters_get(on)),
  "fit.results" = function(on, a) as.numeric(DecayFitOutcome_results_get(on)),

  # -- clsm ---------------------------------------------------------------------
  # R's SWIG overload dispatch only matches the all-defaults call reliably, which
  # is why get_mean_micro_time is called with just the TTTR -- the same reason
  # test_clsm.R gives.
  "clsm.open" = function(on, a)
    CLSMImage(a[[1]], CLSMSettings(), NULL, TRUE, as.integer(unlist(a[[2]]))),
  "clsm.n_frames" = function(on, a) CLSMImage_n_frames_get(on),
  "clsm.n_lines" = function(on, a) CLSMImage_n_lines_get(on),
  "clsm.n_pixel" = function(on, a) CLSMImage_n_pixel_get(on),
  "clsm.intensity" = function(on, a) row_major(CLSMImage_get_intensity(on)),
  "clsm.mean_micro_time" = function(on, a)
    row_major(CLSMImage_get_mean_micro_time(on, a[[1]])),
  # The mask is built here rather than in the case file: an R array with a dim
  # attribute is what rarrays.i reads as INPLACE_ARRAY3, and a literal mask of
  # 2.6 million values in JSON would be absurd. R is 1-based, so the rectangle's
  # bounds shift by one.
  "clsm.decay_of_pixels" = function(on, a) {
    nf <- CLSMImage_n_frames_get(on); nl <- CLSMImage_n_lines_get(on)
    np <- CLSMImage_n_pixel_get(on)
    b <- as.integer(unlist(a[[2]]))
    m <- array(0L, dim = c(nf, nl, np))
    m[(b[[1]] + 1L):nf, (b[[2]] + 1L):b[[3]], (b[[4]] + 1L):b[[5]]] <- 1L
    as.numeric(CLSMImage_get_decay_of_pixels_v(on, a[[1]], m,
                                               as.integer(a[[3]]),
                                               as.logical(a[[4]])))
  },

  # The _v accessor, added to CLSM.i for exactly this: a plain vector return has
  # no output pointers for SWIG's R overload dispatcher to trip over. Same
  # reason get_phasor_v exists.
  "clsm.fluorescence_decay" = function(on, a)
    as.numeric(CLSMImage_get_fluorescence_decay_v(on, a[[1]], as.integer(a[[2]]),
                                                  as.logical(a[[3]]))),

  # -- histogram ---------------------------------------------------------------
  "hist.new" = function(on, a) doubleHistogram(),
  "hist.set_axis" = function(on, a)
    doubleHistogram_set_axis(on, as.integer(a[[1]]), a[[2]], as.numeric(a[[3]]),
                             as.numeric(a[[4]]), as.integer(a[[5]]), a[[6]]),
  # matrix(ncol = 1) is the (n, 1) shape; rarrays.i reads an R matrix as
  # IN_ARRAY2 directly, dimensions and all.
  "hist.update" = function(on, a)
    doubleHistogram_update(on, matrix(as.numeric(unlist(a[[1]])), ncol = 1)),
  "hist.counts" = function(on, a) as.numeric(unlist(doubleHistogram_get_histogram(on))),

  # -- bitmask ----------------------------------------------------------------
  # R cannot mutate a caller's vector (copy-on-modify), so rarrays.i's INPLACE
  # typemap RETURNS the filled vector rather than writing through. That is the
  # documented R semantics, and it is why this op reads the result.
  "bitmask.new" = function(on, a) BitMask(as.integer(a[[1]])),
  "bitmask.set" = function(on, a) BitMask_set(on, as.integer(a[[1]]), as.logical(a[[2]])),
  "bitmask.size" = function(on, a) BitMask_size(on),
  "bitmask.count" = function(on, a) BitMask_count(on),
  "bitmask.to_bytes" = function(on, a)
    as.numeric(unlist(BitMask_to_bytes(on, integer(BitMask_size(on))))),

  # -- registry ---------------------------------------------------------------
  "registry.json" = function(on, a) registry_json(),
  "registry.category_json" = function(on, a) registry_category_json(a[[1]]),
  "registry.categories" = function(on, a) as.list(as.character(registry_categories())),

  # -- files ----------------------------------------------------------------
  "file.write_text" = function(on, a) writeLines(a[[2]], a[[1]]),

  # -- hdf5 -----------------------------------------------------------------
  "hdf5.write" = function(on, a)
    write_hdf5_table(a[[1]], a[[2]], a[[3]], 0L, HDF5_UPDATE),
  "hdf5.read" = function(on, a) {
    s <- DataStore()
    read_hdf5_table_into(s, a[[1]], a[[2]], TRUE)
    s
  },
  "hdf5.groups" = function(on, a) as.list(as.character(hdf5_table_groups(a[[1]]))),
  "hdf5.has" = function(on, a) hdf5_table_has(a[[1]], a[[2]])
)

# The eleven typed ds.add_*/ds.column_* ops differ only in the type name, so
# they are stamped rather than written out. force() is what makes each closure
# remember its own type instead of all of them sharing the loop's last value.
make_add <- function(t) { force(t); function(on, a) add_typed(on, a[[1]], a[[2]], t) }
make_column <- function(t) { force(t); function(on, a) column_typed(on, a[[1]], t) }
# The op suffix is the short C spelling (ds.add_f64); the dtype name is the long
# one (float64). They are not the same string and the mapping has to be written
# down once.
SUFFIX <- c(f64 = "float64", f32 = "float32", i64 = "int64", i32 = "int32",
            i16 = "int16", i8 = "int8", u64 = "uint64", u32 = "uint32",
            u16 = "uint16", u8 = "uint8")
for (sfx in names(SUFFIX)) {
  OPS[[paste0("ds.add_", sfx)]] <- make_add(SUFFIX[[sfx]])
  OPS[[paste0("ds.column_", sfx)]] <- make_column(SUFFIX[[sfx]])
}

# ---------------------------------------------------------------------------
# Comparison
# ---------------------------------------------------------------------------

compare <- function(expected, actual, tol) {
  if (is.list(expected)) {
    act <- if (is.list(actual)) actual else as.list(actual)
    if (length(expected) != length(act))
      return(sprintf("expected %d elements, got %d", length(expected), length(act)))
    for (i in seq_along(expected)) {
      why <- compare(expected[[i]], act[[i]], tol)
      if (!is.null(why)) return(sprintf("[%d]: %s", i - 1L, why))
    }
    return(NULL)
  }
  if (is.logical(expected)) {
    if (!is.logical(actual) || !identical(as.logical(expected), as.logical(actual)))
      return(sprintf("expected %s, got %s", expected, paste(actual, collapse = ",")))
    return(NULL)
  }
  if (is.character(expected)) {
    if (!identical(as.character(expected), as.character(actual)))
      return(sprintf("expected '%s', got '%s'", expected, paste(actual, collapse = ",")))
    return(NULL)
  }
  if (is.logical(actual) || is.character(actual))
    return(sprintf("expected a number, got %s", paste(actual, collapse = ",")))
  e <- as.numeric(expected); v <- as.numeric(actual)
  if (length(v) != 1L) return(sprintf("expected one number, got %d", length(v)))
  if (is.null(tol) || tol == 0) {
    if (!isTRUE(e == v)) return(sprintf("expected %.17g, got %.17g", e, v))
    return(NULL)
  }
  scale <- max(abs(e), abs(v), 1e-300)
  if (abs(e - v) <= tol * scale) return(NULL)
  sprintf("expected %.17g, got %.17g (relative error %.3g > %g)",
          e, v, abs(e - v) / scale, tol)
}

# R has one numeric type, so an exact integer expectation past 2^53 would be
# compared as a double and silently pass. Refuse it instead -- this is the
# runner OPS.md is talking about.
check_comparable <- function(case) {
  tol <- case$tolerance
  for (key in names(case$expect)) {
    if (!is.null(tol[[key]]) && tol[[key]] != 0) next
    for (v in unlist(case$expect[[key]])) {
      if (is.numeric(v) && abs(v) >= MAX_EXACT_INTEGER)
        stop(sprintf("%s: expected integer %s=%.17g is at or above 2^53, which R cannot compare exactly",
                     case$id, key, v))
    }
  }
}

# ---------------------------------------------------------------------------
# The case loop
# ---------------------------------------------------------------------------

results <- list()
record <- function(id, area, status, reason = "") {
  results[[length(results) + 1L]] <<-
    list(id = id, area = area, status = status, reason = reason)
}

run_case <- function(case, area) {
  bindings <- new.env(parent = emptyenv())
  produced <- character(0)
  tmp_paths <- character(0)
  n_tmp <- if (is.null(case$tmp)) 0L else as.integer(case$tmp)
  if (n_tmp > 0) tmp_paths <- replicate(n_tmp, tempfile("tttrlib-conf-"))
  on.exit(unlink(tmp_paths), add = TRUE)

  resolve <- function(x) {
    if (is.list(x)) return(lapply(x, resolve))
    if (is.character(x) && length(x) == 1L && startsWith(x, "$")) {
      nm <- substring(x, 2)
      if (grepl("^data[0-9]+$", nm))
        return(file.path(DATA_ROOT, case$data[[as.integer(substring(nm, 5)) + 1L]]))
      if (grepl("^tmp[0-9]+$", nm))
        return(tmp_paths[as.integer(substring(nm, 4)) + 1L])
      if (!exists(nm, envir = bindings, inherits = FALSE))
        stop("step reads unbound '", nm, "'")
      return(get(nm, envir = bindings, inherits = FALSE))
    }
    x
  }

  for (step in case$steps) {
    fn <- OPS[[step$op]]
    if (is.null(fn)) stop("UNSUPPORTED_OP:", step$op)
    args <- if (is.null(step$args)) list() else lapply(step$args, resolve)
    on <- if (is.null(step$on)) NULL else get(step$on, envir = bindings, inherits = FALSE)

    if (isTRUE(step$throws)) {
      threw <- inherits(try(fn(on, args), silent = TRUE), "try-error")
      if (!is.null(step$as)) assign(step$as, threw, envir = bindings)
      next
    }
    value <- fn(on, args)
    if (!is.null(step$as)) assign(step$as, value, envir = bindings)
  }

  failures <- character(0)
  for (key in names(case$expect)) {
    if (!exists(key, envir = bindings, inherits = FALSE)) {
      failures <- c(failures, sprintf("%s: the steps never bound it", key)); next
    }
    tol <- if (is.null(case$tolerance)) NULL else case$tolerance[[key]]
    why <- compare(case$expect[[key]], get(key, envir = bindings, inherits = FALSE), tol)
    if (!is.null(why)) failures <- c(failures, sprintf("%s: %s", key, why))
  }
  failures
}

n_pass <- 0L; n_fail <- 0L; n_skip <- 0L
for (path in sort(list.files(CASES_DIR, pattern = "\\.json$", full.names = TRUE))) {
  doc <- jsonlite::fromJSON(path, simplifyVector = FALSE)
  for (case in doc$cases) {
    why <- case$unsupported$r
    if (!is.null(why)) {
      record(case$id, doc$area, "unsupported", why); n_skip <- n_skip + 1L
      cat(sprintf("UNSUPPORTED %-56s %s\n", case$id, why)); next
    }
    missing <- Filter(function(d) !file.exists(file.path(DATA_ROOT, d)),
                     if (is.null(case$data)) list() else case$data)
    if (length(missing) > 0) {
      reason <- paste("missing", paste(unlist(missing), collapse = ", "))
      record(case$id, doc$area, "skip", reason); n_skip <- n_skip + 1L
      cat(sprintf("SKIP        %-56s %s\n", case$id, reason)); next
    }
    check_comparable(case)

    outcome <- tryCatch(run_case(case, doc$area), error = function(e) e)
    if (inherits(outcome, "error")) {
      msg <- conditionMessage(outcome)
      if (startsWith(msg, "UNSUPPORTED_OP:")) {
        reason <- paste("op", sub("^UNSUPPORTED_OP:", "", msg), "not implemented")
        record(case$id, doc$area, "unsupported", reason); n_skip <- n_skip + 1L
        cat(sprintf("UNSUPPORTED %-56s %s\n", case$id, reason)); next
      }
      record(case$id, doc$area, "fail", msg); n_fail <- n_fail + 1L
      cat(sprintf("ERROR       %-56s %s\n", case$id, msg)); next
    }
    if (length(outcome) > 0) {
      record(case$id, doc$area, "fail", paste(outcome, collapse = "; "))
      n_fail <- n_fail + 1L
      cat(sprintf("FAIL        %s\n", case$id))
      for (f in outcome) cat("              ", f, "\n")
    } else {
      record(case$id, doc$area, "pass"); n_pass <- n_pass + 1L
    }
  }
}

report <- Sys.getenv("TTTRLIB_CONFORMANCE_REPORT")
if (nzchar(report)) {
  writeLines(jsonlite::toJSON(list(language = "r", results = results),
                              auto_unbox = TRUE, pretty = TRUE), report)
}

cat(sprintf("R conformance: %d passed, %d failed, %d skipped\n",
            n_pass, n_fail, n_skip))
if (n_fail > 0) quit(status = 1)
