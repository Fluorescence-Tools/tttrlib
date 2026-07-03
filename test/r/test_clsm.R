# Cross-language CLSM reference test for the R bindings (PRD-001).
# Asserts the SAME reconstruction as Python (test/python/clsm) and Java
# (test/java/CLSMTest.java): dimensions and total intensity of a CLSM image.
#
# Module provided via TTTRLIB_R_SO + TTTRLIB_R_WRAPPER (raw build artifacts) or
# an installed package. Data file location via TTTRLIB_DATA (default ./tttr-data).

REF_N_FRAMES      <- 40
REF_N_LINES       <- 256
REF_N_PIXEL       <- 256
REF_INTENSITY_SUM <- 3364714
REF_MMT_NONZERO   <- 610553
REF_PHASOR_VALID  <- 412275

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
fn <- file.path(data_root, "imaging", "pq", "ht3", "pq_ht3_clsm.ht3")
stopifnot(file.exists(fn))

tttr <- TTTR(fn)

# Native R integer vectors are marshalled to std::vector<int> by rarrays.i, so
# the routing channels are passed directly as c(0L) (no VectorInt32 needed).
img <- CLSMImage(tttr, CLSMSettings(), NULL, TRUE, c(0L))

stopifnot(CLSMImage_n_frames_get(img) == REF_N_FRAMES)
stopifnot(CLSMImage_n_lines_get(img) == REF_N_LINES)
stopifnot(CLSMImage_n_pixel_get(img) == REF_N_PIXEL)

intensity <- CLSMImage_get_intensity(img)
stopifnot(length(intensity) == REF_N_FRAMES * REF_N_LINES * REF_N_PIXEL)
stopifnot(sum(as.numeric(intensity)) == REF_INTENSITY_SUM)

# FastLifetime (mean micro time) — count pixels with a defined value (> 0).
# R's SWIG overload dispatch only matches the all-defaults call reliably.
mmt <- CLSMImage_get_mean_micro_time(img, tttr)
stopifnot(sum(mmt > 0) == REF_MMT_NONZERO)

# Phasor via the get_phasor_v accessor (the native get_phasor has a pointer-typed
# default arg that R cannot dispatch). Flattened [frame,line,pixel,2]; g is at the
# odd 1-based positions (even 0-based). Count pixels with a defined g (> -1).
ph <- CLSMImage_get_phasor_v(img, tttr)
g  <- ph[seq(1, length(ph), 2)]
stopifnot(sum(g > -1) == REF_PHASOR_VALID)

cat("R CLSM cross-language reference test: PASS\n")
