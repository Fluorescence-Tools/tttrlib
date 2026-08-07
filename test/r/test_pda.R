# Cross-language PDA (photon distribution analysis) reference test for the R
# bindings. Asserts the same S1S2 probability matrix as
# test/python/pda/test_pda_cross_language.py and test/java/PdaTest.java.
#
# R note: the 5-argument Pda(...) constructor is used (the 6-arg form with an
# explicit PdaImplementation enum does not match R's overload dispatch), and the
# native get_S1S2_matrix output-pointer method returns a plain R vector.

REF_S1S2_SUM   <- 1.0
REF_S1S2_MAX   <- 0.01800533
REF_HIST1D_SUM <- 0.92940452

so      <- Sys.getenv("TTTRLIB_R_SO")
wrapper <- Sys.getenv("TTTRLIB_R_WRAPPER")
if (nzchar(so) && nzchar(wrapper)) {
  dyn.load(so)
  source(wrapper)
  cacheMetaData(1)
} else {
  library(tttrlib)
}

Nmax <- 30L
# deterministic Poisson pF (lambda = 10)
pF <- numeric(Nmax + 1)
pF[1] <- exp(-10.0)
for (i in 1:Nmax) pF[i + 1] <- pF[i] * 10.0 / i
pF <- pF / sum(pF)

pda <- Pda(Nmax, 5L, 0.0, 0.0, pF)   # background 0; PDA_DEFAULT
Pda_append(pda, 0.5, 0.3)
Pda_append(pda, 0.5, 0.7)
Pda_evaluate(pda)

s <- Pda_get_S1S2_matrix(pda)
stopifnot(length(s) == (Nmax + 1) * (Nmax + 1))
stopifnot(abs(sum(s) - REF_S1S2_SUM) < 1e-5)
stopifnot(abs(max(s) - REF_S1S2_MAX) < 1e-6)

# 1-D histogram: the all-defaults call matches R's overload dispatch (x_max=1000,
# x_min=0.01, n_bins=81, log_x=TRUE); returns list(NULL, x-axis, y-values).
h  <- Pda_get_1dhistogram(pda)
hy <- h[[3]]
stopifnot(length(hy) == 81)
stopifnot(abs(sum(hy) - REF_HIST1D_SUM) < 1e-4)

cat("R PDA cross-language reference test: PASS\n")
