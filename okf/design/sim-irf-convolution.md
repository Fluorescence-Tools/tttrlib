# Simulating IRF-convolved micro-times for fit23

## The problem

`SimEngine` draws micro-times from the **species' decay pattern**, not the
detector's. Without an IRF, the pattern is a raw multi-exponential, so
Fit2x — which expects `data = IRF ⊗ exp(−t/tau)` — recovers garbage.

## The fix: `"irf"` key in `decay`

`SimDecay` supports IRF convolution at construction. Put an `"irf"` array
in the species' `decay` block:

```json
{
  "species": [{
    "D": 0.05, "q": [240.0, 60.0],
    "decay": {
      "lifetimes": [3.8], "amplitudes": [1.0],
      "dt": 0.0078125,
      "irf": [0.01, 0.05, 0.2, 0.8, 1.0, 0.6, 0.3, 0.1, ...]
    }
  }]
}
```

`SimDecay::multi_exponential_with_irf()` computes the pattern as
`IRF ⊗ Σᵢ ampᵢ · exp(−t/tauᵢ)`, then builds a Vose alias table for O(1)
per-photon sampling.

## Critical: dt must match laser_period / n_microtime_channels

The decay `dt` **must** equal `laser_period / n_microtime_channels`. If the
sim config has `laser_period=32.0` and `n_microtime_channels=4096`, then
`dt` must be `32.0/4096 = 0.0078125`, NOT `0.008`.

If these don't match, the decay pattern is built at the wrong bin width and
the TTTR encoding stretches/compresses it, causing a systematic bias in
lifetime recovery.

`to_tttr()` now defaults `microtime_resolution` and `laser_period` to the
engine's own `settings()`, so the round-trip preserves the simulation's
resolution exactly. The old hardcoded defaults (B&H SPC-130: 0.004069 ns,
13.596 ns) are only used when explicitly passed.

## Background decay

Without `background_decay`, background photons get `micro_time = 0`, creating
a delta spike at bin 0 that contaminates the IRF extraction. Set
`background_decay` to the same IRF-convolved pattern as the species:

```json
{
  "background_decay": {"lifetimes": [0.001], "irf": [...same IRF...], "dt": 0.0078125}
}
```

## Per-detector IRFs

Micro-time is **species-indexed**, not detector-indexed. To simulate
different IRFs per detector, model each detector's physical state as a
separate species with its own `decay` + `q` routing.

## Fit2x VV/VH contract

Fit2x's `Fit2xSettings` requires even-length IRF and background arrays:
`irf[:n]` = parallel (VV), `irf[n:]` = perpendicular (VH). For non-
polarization-resolved data, duplicate the single-channel arrays into both
halves: `irf_vvvh = np.concatenate([irf, irf])`. Background must be
area-normalised (chiSurf does this in `Fit2xSettings.__post_init__`).

## IRF extraction options

The pipeline's `compute_irf` supports three models (matching chiSurf):

| `irf_model` | What it does |
|---|---|
| `"gaussian"` | Fit symmetric Gaussian to rising edge of non-burst histogram |
| `"skewed"` | Fit skew-normal (α=1.5 default) — use when the IRF has a tail |
| `"experimental"` | Use baseline-subtracted raw histogram directly |

The Gaussian fit uses the **rising edge only** (the falling edge is
fluorescence decay, not instrument response), matching chiSurf's
`gaussian_prompt()` in `chisurf/core/fluorescence/burst/irf_bg.py`.

## Reference

- `SimDecay::multi_exponential_with_irf()` — `modules/simulation/include/SimDecay.h:109`
- `SimDecay::convolve()` — `SimDecay.h:80`
- `decay_from()` JSON parser — `modules/simulation/src/SimEngine.cpp:968`
- `to_tttr()` — `ext/python/Sim.i:326` (now defaults to engine settings)
- chiSurf `gaussian_prompt()` — `chisurf/core/fluorescence/burst/irf_bg.py:54`
- chiSurf `Fit2xSettings.__post_init__` — `chisurf/core/fluorescence/mle/fit2x.py:203`
