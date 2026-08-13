# tttrlib Sim* — integration notes & gotchas (from the BurstNet integration)

Findings from driving the OpenMM-style `Sim*` subsystem (commit 75dbfe7d) from an external
project. Candidates for folding into `doc/simulator-guide.rst` or a troubleshooting page.

## Works as documented (verified)
- **FRET by routing `q`.** A species with `q = [(1-E)·b, E·b]` on `[green, red]` gives a
  per-burst proximity ratio equal to E: E=0.25 → PR 0.25, E=0.75 → PR 0.75 (imperative
  `SimSpecies.q` and `from_dict` both). No dedicated FRET feature needed — exactly as the guide says.
- **Lifetime.** `SimSpecies.decay = SimDecay.from_pattern(pattern, dt_ns, t0)`; the recovered
  micro-time histogram matches the input pattern. FRET-quenched donor decay τ_D·(1−E) shows up
  in the micro-time as expected.
- **Per-photon output.** `SimEngine.photons()` → dict of numpy arrays
  `{macro_window, arrival_time, channel, micro_time, species, molecule, event_type}` — filter
  `event_type == 0` for photons. `micro_time` is a TAC channel index.

## ⚠ Gotcha 1 (cost me hours): anisotropy params silently break channel routing
Setting `SimSpecies.r0 > 0` and/or `l1`/`l2 ≠ 0` on an instrument that is **not** polarization
resolved (i.e. the channels are NOT parallel/perpendicular pairs) **destroys the intensity
routing**: the per-channel `q` is no longer honoured, the proximity ratio goes flat (~0.28
regardless of E), and channels on a second laser get 0 photons.

Reproduce: one species, 3 channels `[DexDem, DexAem, AexAem]`, `q=[80,22,100]`.
- `r0=l1=l2=0` → channel counts track `q` (PR 0.21 for E=0.2, 0.80 for E=0.8). ✅
- `r0=0.38, l1=0.0308, l2=0.0368` → PR 0.28 for both E, AexAem = 0. ❌

**Only set `r0`/`l1`/`l2` when the channels form par/perp pairs.** For a plain spectral (green/red)
or PIE instrument leave them at 0. A validation error or warning in `SimEngine` when
`r0>0 || l1||l2` but the channel layout has no par/perp structure would have caught this
immediately. (Ideally: document the required channel ordering for anisotropy, and make the
non-polarization default a no-op.)

## ⚠ Gotcha 2: TAC window must span the laser period
`microtime_resolution × n_microtime_channels` must be ≳ `laser_period`, else sub-ns decays
collapse into a few edge bins and lifetime information is lost. A warning when the micro-time
window is much smaller than `laser_period` would help. (BurstNet now auto-derives
`tac_dt = laser_period / n_tac`.)

## ⚠ Gotcha 3: macOS OpenMP build — unresolved `___kmpc_barrier`
`pip install -e .` (default) produced a `_tttrlib.so` that failed to import with
`symbol not found in flat namespace '___kmpc_barrier'`. Preloading libomp works
(`DYLD_INSERT_LIBRARIES=…/libomp.dylib`) but is fragile; `CMAKE_ARGS="-DWITH_OPENMP=OFF"
pip install -e .` produces a cleanly-importable module. Also: CMake needs `HDF5_ROOT` /
`CMAKE_PREFIX_PATH` pointed at the conda prefix or it fails with "Could NOT find HDF5".

## API rename reference (old → new), for downstream code
`SimSample → SimSystem`, `SimSettings → SimIntegrator`; `SimSpecies/SimDecay/SimEngine/SimGrid`
unchanged. New conveniences: `SimEngine.from_dict/from_json`, `.photons()`, `.to_tttr()`,
`.get_state()/state_numpy()`; `TTTR.burst_search_cusum_sprt(min_photons, background_cps,
signal_to_background_ratio, alpha, beta)` for burst detection with a known background.
