# Shims to build the original FRET_burstML MEX likelihood natively

`junk/FRET_burstML/burstMLProject.zip` holds the MATLAB/MEX (Win32 threads +
GSL) source `modules/spectroscopy/burst/BurstML` was ported from. These four
headers stand in for `Windows.h`, `process.h`, `mex.h` and `matrix.h` -- threads
run synchronously, priorities are ignored, `mxArray` is a plain double matrix --
so `mlhDiffNTRbkg_MT.cpp` compiles unchanged on macOS/Linux against GSL, and
`ab_burstml_mex_driver.cpp` evaluates its negative log-likelihood from stdin.

`test/python/burstfilter/test_ab_burst_reference.py::TestBurstMLAgainstTheOriginalMex`
unzips the archive, builds this, and asserts `BurstML::neg_log_likelihood`
equals the original digit for digit (skips without GSL or a compiler).
