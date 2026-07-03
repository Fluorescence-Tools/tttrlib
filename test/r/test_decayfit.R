# Cross-language DecayFit23 reference test for the R bindings (PRD-001).
# DecayFit23.modelf takes plain numeric arrays, so it is callable identically
# from Python, R and Java. Asserts the SAME model-function values as
# test/python/decayfit/test_decayfit_cross_language.py and test/java/DecayFitTest.java.
#
# R note: because R is copy-on-modify, the SWIG "output" array (mfunction) is
# returned rather than mutated in place. rarrays.i appends each array argument to
# the returned list, so the model function is the LAST element.

REF_OUT_4  <- 0.092688
REF_OUT_20 <- 0.043233
REF_SUM    <- 0.99

FN            <- 32
REF_TWO_ISTAR <- 23.802337
REF_FIT_TAU   <- 0.74219
REF_FIT_RS    <- 0.25974
REF_FIT24_TI       <- 2.41049
REF_FIT25_TI       <- 4.738831
REF_FIT25_BEST_TAU <- 0.5
REF_FIT26_TI       <- 2.218772
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

N <- 16
# deterministic IRF: a Gaussian at channel 4 in each of the p/s halves
irf <- numeric(2 * N)
for (half in c(0, N)) for (i in 0:(N - 1)) irf[half + i + 1] <- exp(-((i - 4.0)^2) / (2 * 0.5 * 0.5))
bg <- numeric(2 * N)
param       <- c(2.0, 0.01, 0.38, 1.2)          # tau, gamma, r0, rho
corrections <- c(2.0 * N, 1.0, 0.1, 0.1, N - 1)  # period, g, l1, l2, conv_stop
m <- numeric(2 * N)

r  <- DecayFit23_modelf(param, irf, bg, 0.5, corrections, m)
mf <- r[[length(r)]]   # the model function (last returned array)

stopifnot(length(mf) == 2 * N)
stopifnot(abs(mf[5]  - REF_OUT_4)  < 1e-5)   # R is 1-based: index 5 == C index 4
stopifnot(abs(mf[21] - REF_OUT_20) < 1e-5)
stopifnot(abs(sum(mf) - REF_SUM)   < 1e-5)

# --- full fit23 loop via fit_v (by-value inputs -> clean vector return) ----
irf2 <- numeric(2 * FN)
for (half in c(0, FN)) for (i in 0:(FN - 1)) irf2[half + i + 1] <- exp(-((i - 8.0)^2) / (2 * 0.5 * 0.5))
bg2  <- numeric(2 * FN)
corr <- c(2.0 * FN, 1.0, 0.1, 0.1, FN / 2 - 1)
fitData <- DecayFitData(0.5, corr, irf2, bg2, DATA)   # (dt, corrections, irf, background, data)

x     <- c(2.1, 0.01, 0.38, 1.2, -1, 0, 0, 0)
fixed <- c(0L, 0L, 1L, 1L)
res   <- DecayFit23_fit_v(x, fixed, fitData)          # [twoIstar, x0..x7]

stopifnot(abs(res[1] - REF_TWO_ISTAR) < 1e-3)
stopifnot(abs(res[2] - REF_FIT_TAU)   < 1e-3)
stopifnot(abs(res[8] - REF_FIT_RS)    < 1e-3)

# --- fit24 / fit25 / fit26 via fit_v (bg = 0.2 keeps the MLE finite) --------
make_fit_data <- function(bg_level) {
  bg <- numeric(2 * FN) + bg_level
  DecayFitData(0.5, corr, irf2, bg, DATA)
}

r24 <- DecayFit24_fit_v(c(3.8, 0.02, 0.4, 0.8, 1.0, -1.0, 0, 0), c(0L, 0L, 0L, 0L, 0L), make_fit_data(0.2))
stopifnot(abs(r24[1] - REF_FIT24_TI) < 1e-3)

r25 <- DecayFit25_fit_v(c(0.5, 1.0, 2.0, 4.0, 0.02, 0.38, 0, 0, 0), c(0L, 0L, 0L, 0L, 1L, 1L), make_fit_data(0.2))
stopifnot(abs(r25[1] - REF_FIT25_TI) < 1e-3)
stopifnot(abs(r25[2] - REF_FIT25_BEST_TAU) < 1e-3)

r26 <- DecayFit26_fit_v(c(0.5, 0), c(0L), make_fit_data(0.2))
stopifnot(abs(r26[1] - REF_FIT26_TI) < 1e-3)

cat("R DecayFit cross-language reference test (fit23-26): PASS\n")
