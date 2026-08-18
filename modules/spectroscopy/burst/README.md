# `spectroscopy/burst` — Burst Search and Feature Extraction

Algorithms for identifying photon bursts and computing burst metrics in single-molecule fluorescence experiments.

## Contents

- **`TTTRBurstSearch.cpp`**: the `TTTR::burst_search*` entry points — sliding
  window (m-out-of-n) and CUSUM/SPRT — defined here rather than in `core`, so
  core contains no burst-search code (they stay *members* because that is the
  public API in four languages; see the file header).
- **`BurstSearchDispatch.h` / `BurstSearchDispatch.cpp`**: the dispatch table a
  search is reached by name through (`burst_search(mode)`,
  `burst_search_by_name`), keyed on the registry key.
- **`BurstSearchKalman.h` / `BurstSearchKalman.cpp`**: Kalman-filtered count rate burst search with Mahalanobis detection.
- **`BurstSearchBOCPD.h` / `BurstSearchBOCPD.cpp`**: Bayesian Online Changepoint Detection (Adams & MacKay 2007) with Gamma-Poisson conjugate model.
- **`BurstSearchMaxTree.h` / `BurstSearchMaxTree.cpp`**: Max-tree attribute filtering burst search (threshold-free).
- **`BurstSearchBayesianBlocks.h` / `BurstSearchBayesianBlocks.cpp`**: Bayesian blocks burst search.
- **`BurstSearchRegistry.cpp`**: registers every built-in burst search (description + dispatch in one call) and the burst pipeline operations in the one registry (core, `Registry.h`) when the library loads.
- **`BurstSignificance.h`**: Exact Poisson and Li & Ma significance statistics,
  the sigma conversions and the trials correction.
- **`BurstConfidence.h` / `BurstConfidence.cpp`**: `TTTR::burst_confidence` —
  how strongly the photons support each burst, in sigma, measured against the
  background flanking it, so the number means the same for every search.
- **`BurstFeature.h` / `BurstFeature.cpp`**: the base a per-burst feature is
  built on — named photon streams, the per-photon KDE (`build_kde`) the 2CDE
  variants use, and the per-burst reduction.
- **`BurstFeatureExtractor.h` / `BurstFeatureExtractor.cpp`**: the burst table:
  counts, durations, mean macro/micro times, proximity ratio and corrected FRET
  efficiency per named detector.
- **`BurstFilter.h` / `BurstFilter.cpp`**: turning a raw burst list into an
  accepted one — channel and micro-time selection, size / duration /
  background filters, merging, and the JSON state a `.pto` records.
- **`RecurrenceAnalysis.h` / `RecurrenceAnalysis.cpp`**: recurrence analysis of
  single particles (RASP) — same-molecule probability from inter-burst gaps and
  the efficiencies of recurring bursts.
- **`BVA.h` / `BVA.cpp`**: Burst Variance Analysis (BVA).
- **`TwoCDE.h` / `TwoCDE.cpp`**: Two-Channel Kernel Density Estimator (2CDE) analysis.
- **`BurstML.h` / `BurstML.cpp`**: Maximum-likelihood burst analysis with a combined diffusion-kinetics-photon observation model (port of FRET_burstML, `mlhDiffNTRbkg_MT`). The combined evolution operator is eigendecomposed once per parameter set and the log-likelihood of all bursts is maximised via Nelder-Mead. Uses std-only QR eigendecomposition (`modules/math/QREigen.h`) and Nelder-Mead (`modules/math/NelderMead.h`) — no GSL dependency. Verified to recover FRET efficiency on simulated 2-state bursts. See the `BurstML.h` file header for the `5n`/`(4+n)n` parameter layout.

## Examples

- `examples/single_molecule/plot_burstml_two_state.py` (+ `.ipynb`): `BurstML` on simulated two-state diffusing FRET bursts -- likelihood profiles around the truth and the Nelder-Mead `fit`.
- `examples/single_molecule/plot_two_cde.py` (+ `.ipynb`): `TwoCDE` FRET-2CDE (Laplace/Gaussian) on static vs switching bursts, and ALEX-2CDE on acceptor blinking.
- `examples/single_molecule/plot_kalman_burst_detection.py` (+ `.ipynb`): `TTTR.burst_search_kalman` and the underlying `kalman_filter` on a simulated two-channel trace.
- `examples/single_molecule/plot_background_rate.py` (+ `.ipynb`): `estimate_background_rate` (corrections module) -- the tail MLE vs the naive 1/mean on a bursty stream, tail-fraction bias/variance.

## Dependencies

- Depends on `util`, `math` (BurstML uses `QREigen` and `NelderMead`), `core`,
  `plugin` (a search a plugin contributed), nlohmann/json.

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
