Two programs live here. Neither is part of the build or the suite, and both
exist for the same reason: a test written against our own code shares its
assumptions, so it cannot catch an assumption that is wrong. These check the
same things against somebody else's implementation, and the dependency that
needs is one tttrlib must not acquire.

| | checks | against |
|---|---|---|
| `compare_burst_table_to_chisurf.py` | that `tttr sm`'s burst table is ChiSurf's burst table | **ChiSurf**'s `generate_burst_dataframe` |

---

# Checking a burst table against ChiSurf

`compare_burst_table_to_chisurf.py` runs ChiSurf's own writer over the same
photons as `tttr sm --csv` and diffs the two tables cell for cell. The suite's
`test/python/misc/test_cli_sm_burst_table.py` checks the same thing against a
NumPy reimplementation of ChiSurf's documented rules — which is what makes it
runnable in CI, and also what leaves the gap this closes: a rule read wrongly is
read the same wrong way in the reimplementation. ChiSurf's `burst.py` is the
format; if the two disagree, tttrlib is wrong.

Usage is in the module docstring, including the two ways `import tttrlib` ends
up resolving to a different build than the binary under test (an env built for
another Python or architecture, and an editable install's meta path finder
outranking `PYTHONPATH`). The program prints `tttrlib.__file__` first so a run
can be trusted.

---

# Checking a `.pto` with somebody else's parser

`pto_ebml_check` moved to ptolib (https://github.com/tpeulen/ptolib,
`tools/pto_ebml_check.cpp`, CMake option `PTOLIB_BUILD_EBML_CHECK`) together
with the container it validates. Build it there against libebml and point
`TTTRLIB_PTO_EBML_CHECK` at the binary; `test/python/test_pto.py` runs it when
the variable is set and skips otherwise. ptolib's own `pto verify` is this
reader's walker; the libebml check is the one that does not trust it.
