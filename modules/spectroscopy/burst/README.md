# `spectroscopy/burst` — Burst Search and Feature Extraction

Algorithms for identifying photon bursts and computing burst metrics in single-molecule fluorescence experiments.

## Contents

- **`TTTRBurstSearch.h` / `TTTRBurstSearch.cpp`**: Sliding window (m-out-of-n), sliding time window, Bayesian blocks, and CUSUM/SPRT burst search methods.
- **`BurstSearchKalman.h` / `BurstSearchKalman.cpp`**: Kalman-filtered count rate burst search with Mahalanobis detection.
- **`BurstSearchBOCPD.h` / `BurstSearchBOCPD.cpp`**: Bayesian Online Changepoint Detection (Adams & MacKay 2007) with Gamma-Poisson conjugate model.
- **`BurstSearchMaxTree.h` / `BurstSearchMaxTree.cpp`**: Max-tree attribute filtering burst search (threshold-free).
- **`BurstSearchBayesianBlocks.h` / `BurstSearchBayesianBlocks.cpp`**: Bayesian blocks burst search.
- **`BurstSearchRegistry.cpp`**: Machine-readable JSON registry of all burst searches.
- **`BurstSignificance.h`**: Exact Poisson and Li & Ma significance statistics.
- **`BurstFeature.h` / `BurstFeatureExtractor.h`**: Feature extraction algorithms for detected bursts (brightness, anisotropy, FRET efficiency, 2CDE).
- **`BVA.h` / `BVA.cpp`**: Burst Variance Analysis (BVA).
- **`TwoCDE.h` / `TwoCDE.cpp`**: Two-Channel Kernel Density Estimator (2CDE) analysis.
- **`BurstML.h` / `BurstML.cpp`**: Maximum-likelihood burst analysis with a combined diffusion-kinetics-photon observation model (port of FRET_burstML, `mlhDiffNTRbkg_MT`). The combined evolution operator is eigendecomposed once per parameter set and the log-likelihood of all bursts is maximised via Nelder-Mead. Uses std-only QR eigendecomposition (`modules/math/QREigen.h`) and Nelder-Mead (`modules/math/NelderMead.h`) — no GSL dependency. Verified to recover FRET efficiency on simulated 2-state bursts. See the `BurstML.h` file header for the `5n`/`(4+n)n` parameter layout.

## Dependencies

- Depends on `core`, `util`, `math` (BurstML uses `QREigen` and `NelderMead`).

## Adding a burst search

`TTTR::burst_search(L, m, T, mode, alpha, beta)` resolves `mode` through a
table (`BurstSearchDispatch.h`), so a new search is added where it is
implemented rather than by editing the dispatcher:

```cpp
register_burst_search("my_search",
    [](TTTR& d, int L, int m, double T, double alpha, double beta) {
        return d.my_search(L, m, T);
    });
```

Call it from `register_builtin_burst_searches()` for a search compiled in, or
through the plugin entry point for one that arrives in a shared library.

Two things about this seam are worth knowing before you use it:

**The narrow signature does not fit every algorithm.** `(L, m, T, alpha, beta)`
is what `TTTR::burst_search` publishes in three languages, and it cannot carry
an arbitrary parameter set. Three built-ins already reinterpret `T` — as the
Kalman bin width, the max-tree background window, and the Bayesian-blocks
`p0` — and that reinterpretation is a property of *this signature*, not of the
algorithms. A search whose parameters do not fit keeps its own full-fidelity
entry point and reinterprets what it can here; say so at the registration, not
in a comment inside a branch.

**An unrecognised mode runs the sliding window.** It does not raise. That has
always been the behaviour and callers rely on it, so the dispatcher does not
validate the name. If you need to know whether a name is real, ask
`burst_search_dispatch_names()` or the registry.

A duplicate registration is rejected rather than replacing the incumbent: which
of two searches answers to one name should not depend on which translation unit
initialised first.
