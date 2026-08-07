# Bugs found while building the sim-driven examples (PRD-008 / PRD-009)

> Running real analysis pipelines on simulated data (the sim as a ground-truth
> source) surfaced several latent bugs in tttrlib. Logged here with root cause,
> fix, and verification. Related: PRD-008 (simulator API), PRD-009 (sim-driven docs).

## 1. `SimEngine.to_tttr` — wrong macro-time scale  *(FIXED)*

**Symptom.** The exported `TTTR`'s absolute time did not match the simulation:
for `laser_period = 13.596 ns` the TTTR spanned 84.4 s where the sim ran 114.8 s
(ratio 0.735), so every time-based analysis on the exported TTTR (FCS lag times,
burst rates, intensity traces) was off by a constant factor.

**Root cause.** The SPC encoder ticks macro-time in units of `SYNC_DT = laser_period`
(ns), but `to_tttr` left the SPC header's `macro_time_clock` hardcoded at `100`
(⇒ reader `macro_time_resolution = 100 × 0.1 ns = 10 ns`), independent of the laser
period. The written ticks and the declared resolution disagreed by `laser_period/10`.

**Fix** (`ext/python/Sim.i`, `SimEngine.to_tttr`): set
`enc.macro_time_clock = round(laser_period * 10)` so the header resolution equals
the encoder's tick (`macro_time_resolution = macro_time_clock × 0.1 ns = laser_period`).

**Verified.** `laser_period = 32 ns` ⇒ `macro_time_resolution = 3.2e-8 s`, TTTR
span / sim time = 0.9999 (was 0.735).

## 2. `SimEngine.to_tttr` — channel remapping / inversion  *(FIXED)*

**Symptom.** Read-back `routing_channels` were `{0, 8}` (inverted: sim channel 0 →
hardware 8, sim channel 1 → hardware 0) instead of `{0, 1}`, so any
`channel == donor/acceptor` selection on the exported TTTR was wrong.

**Root cause.** The default `ch_conversion` was `[8, 0, 9, 1, 10, 2, …]`; since
`ch_conversion[i]` is the hardware channel for sim channel `i`, this scattered the
channels instead of preserving identity.

**Fix.** Default `ch_conversion = list(range(n_channels))` (identity), so the
exported TTTR's channels equal the simulation channels.

**Verified.** Read-back channels are `[0, 1]` matching the sim.

## 3. `TTTR::burst_search_cusum_sprt` — undetectable signal hypothesis  *(FIXED)*

**Symptom.** The cumulative (CUSUM/SPRT) burst search found ~0 bursts at sensible
signal-to-background ratios, and its S/B dependence was *inverted* (higher S/B →
more bursts), while the sliding-window search found 414 bursts on the same data.

**Root cause** (`src/TTTR.cpp`). In the provided-S/B branch the in-burst rate
hypothesis was `I1 = I0/exp(2) + IB` with `I0 = (S/B − 1)·IB`. That leaves
`I1 ≈ IB` (signal indistinguishable from background), so the SPRT log-likelihood
`log(I1/IB) − dt·(I1−IB)` has almost no discriminating power; larger S/B merely
made `I1` less degenerate, hence the inversion.

**Fix.** `I1 = I0 + IB` (= `(S/B)·IB`), i.e. the total in-burst count rate — the
physically correct signal hypothesis for the exponential-interarrival SPRT.

**Verified.** On a two-state FRET sim (E = 0.25 / 0.75), the cusum search now
recovers a clean bimodal proximity-ratio histogram (peaks at 0.25 and 0.75); the
existing `test/python/burstfilter/test_burst_search_cusum.py` still passes.

## 4. `test_burst_search_cusum.py` — tuple/int comparison  *(FIXED)*

**Symptom.** `test_cusum_sprt_mode_basic` raised
`TypeError: '>=' not supported between instances of 'tuple' and 'int'`.

**Root cause.** `burst_search(...)` returns a plain sequence (SWIG tuple); the test
did `np.all(bursts >= 0)`. This was *masked* until bug #3 was fixed — cusum used to
return an empty result, so the guarded `if len(bursts) > 0:` branch never ran.

**Fix.** `bursts = np.asarray(bursts)` before the comparison.

## 5. `BurstFilter` — NULL `ARGOUTVIEWM` output crash on empty results  *(FIXED)*

**Symptom.** `tttrlib.BurstFilter(tttr.Get()).find_bursts()` raised
`ValueError: Cannot set the NumPy array 'base' dependency to NULL after
initialization` whenever the search returned **no** bursts.

**Root cause** (`src/BurstFilter.cpp`). Every `ARGOUTVIEWM_ARRAY1` output method
(`find_bursts`, `filter_by_size`, `get_burst_sizes`, `get_burst_durations`,
`get_background`, `merge_*`, …) set `*output = nullptr` for the empty case. The
managed-memory numpy typemap cannot take ownership of a NULL pointer, so building
the (0-length) array threw. Nine sites shared the bug.

**Fix.** Return a valid non-NULL 1-element buffer with length 0
(`malloc(sizeof(long long))`) so the typemap builds and owns a proper empty array.

**Verified.** `find_bursts()` returns an empty `ndarray` when nothing is found and
the correct bursts otherwise.
