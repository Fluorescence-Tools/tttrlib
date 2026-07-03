# Package-level tests (run by `R CMD check` when the package is installed).
# Data-driven assertions require the reference file; they are skipped when it is
# not present (e.g. CRAN-style checks). TTTRLIB_DATA points at the tttr-data root.
library(tttrlib)

REF_SIZE         <- 183657
REF_N_MICRO_CHAN <- 4096
REF_MACRO_FIRST  <- 56916
REF_SUM_MICRO    <- 242477881

data_root <- Sys.getenv("TTTRLIB_DATA", unset = "tttr-data")
ref_file  <- file.path(data_root, "bh", "bh_spc132.spc")

test_that("core S4 classes and constructors are available", {
  expect_true(exists("new_TTTR"))
  expect_true(exists("TTTR_size"))
  expect_true(exists("TTTR_get_macro_times"))
})

test_that("TTTR reads the reference file with correct values", {
  skip_if_not(file.exists(ref_file), "reference data not available")
  tttr <- new_TTTR(ref_file, "SPC-130")
  expect_equal(TTTR_size(tttr), REF_SIZE)
  expect_equal(TTTR_get_number_of_micro_time_channels(tttr), REF_N_MICRO_CHAN)
  macro <- TTTR_get_macro_times(tttr)
  expect_equal(length(macro), REF_SIZE)
  expect_equal(macro[1], REF_MACRO_FIRST)
  expect_equal(sum(as.numeric(TTTR_get_micro_times(tttr))), REF_SUM_MICRO)
})
