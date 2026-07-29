# Cross-language decay-fit reference test for the R bindings.
#
# Every fit is reached the same way in every language: build one by registry
# name, hand it a DecayFitProblem, read the parameters and the named result
# columns back. This asserts the SAME numbers as
# test/python/decayfit/test_decay_fit_interface.py and test/java/DecayFitTest.java
# — agreement across bindings is what makes the flat parameter/setup/result
# vectors trustworthy.
#
# The setup vector is built by `decay_fit_setup_vector`, never by hand: that
# builder lives in C++ precisely so no binding has to count slot positions.

REF_TWO_ISTAR      <- 23.802337
REF_FIT_TAU        <- 0.74219
REF_FIT_RS         <- 0.25974
REF_FIT24_TI       <- 2.41049
REF_FIT25_TI       <- 4.738831
REF_FIT25_BEST_TAU <- 0.5
REF_FIT26_TI       <- 2.218772

FN   <- 32
DT   <- 0.5
DATA <- c(0,0,0,1,9,7,5,5,5,2,2,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
          0,0,0,3,2,2,2,2,3,0,1,0,1,1,1,2,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0)

so      <- Sys.getenv("TTTRLIB_R_SO")
wrapper <- Sys.getenv("TTTRLIB_R_WRAPPER")
if (nzchar(so) && nzchar(wrapper)) {
  dyn.load(so)
  source(wrapper)
  cacheMetaData(1)
} else {
  library(tttrlib)
}

# SWIG-R marshals a plain R vector into std::vector, so nothing needs wrapping
# on the way in (the same idiom test_pda.R uses).
vd <- function(x) as.numeric(x)
vi <- function(x) as.integer(x)

# The deterministic IRF shared by every language's copy of this test.
irf <- numeric(2 * FN)
for (half in c(0, FN))
  for (i in 0:(FN - 1))
    irf[half + i + 1] <- exp(-((i - 8.0)^2) / (2 * 0.5 * 0.5))

# Setup by name; a JSON object is the one structured type every binding has.
setup_json <- sprintf(
  paste0('{"dt": %f, "period": %f, "g_factor": 1.0, "l1": 0.1, "l2": 0.1, ',
         '"convolution_stop": %d, "soft_bifl_scatter_flag": true}'),
  DT, 2.0 * FN, as.integer(FN / 2 - 1))

make_problem <- function(bg_level) {
  p <- DecayFitProblem(2L, as.integer(FN), DT)
  p$irf        <- as.numeric(irf)
  p$background <- as.numeric(rep(bg_level, 2 * FN))
  p$data       <- as.numeric(DATA)
  p
}

run_fit <- function(name, start, link, bg_level) {
  fit <- DecayFit2(name, decay_fit_setup_vector(name, setup_json), vd(irf))
  fit$fit(vd(start), DecayFitConstraints(vi(link)), make_problem(bg_level))
}

# Members come back as plain R vectors (the C++ side returns them by value), so
# ordinary 1-based indexing applies — no SWIG vector object to unwrap.
result_of <- function(model, outcome, column) {
  names <- decay_fit_result_names(model, 0L)
  outcome$results[which(names == column)]
}

# --- fit23: tau and gamma free, r0/rho held (link -1 = fixed) -----------------
# Start at 1.0: this 58-photon reference is sparse enough that the objective
# falls monotonically with tau, so the historical answer is a local minimum whose
# basin is roughly 0.5 to 1.2. Outside it the fit "succeeds" with tau in the tens
# of thousands. See the Python reference test for the full note.
r23 <- run_fit("fit23", c(1.0, 0.01, 0.38, 1.2), c(0, 0, -1, -1), 0.0)
stopifnot(abs(r23$objective - REF_TWO_ISTAR) < 1e-3)
stopifnot(abs(r23$parameters[1] - REF_FIT_TAU) < 1e-3)
stopifnot(abs(result_of("fit23", r23, "r_experimental") - REF_FIT_RS) < 1e-3)

# --- fit24 / fit25 / fit26 (bg = 0.2 keeps the MLE finite) --------------------
r24 <- run_fit("fit24", c(3.8, 0.02, 0.4, 0.8, 1.0), c(0, 0, 0, 0, 0), 0.2)
stopifnot(abs(r24$objective - REF_FIT24_TI) < 1e-3)

r25 <- run_fit("fit25", c(0.5, 1.0, 2.0, 4.0, 0.02, 0.38), c(0, 0, 0, 0, -1, -1), 0.2)
stopifnot(abs(r25$objective - REF_FIT25_TI) < 1e-3)
stopifnot(abs(r25$parameters[1] - REF_FIT25_BEST_TAU) < 1e-3)
# fit25 classifies rather than measures, so which candidate won *is* the answer.
stopifnot(result_of("fit25", r25, "selected_index") == 0)

r26 <- run_fit("fit26", c(0.5), c(0), 0.2)
stopifnot(abs(r26$objective - REF_FIT26_TI) < 1e-3)

# --- the registry describes what the code does -------------------------------
stopifnot(length(decay_fit_setup_names("fit23")) ==
          length(decay_fit_setup_vector("fit23", "{}")))
stopifnot("fit23" %in% decay_fit_names())

cat("R decay-fit cross-language reference test (fit23-26): PASS\n")
