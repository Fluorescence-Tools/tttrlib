# PRD-007 — BurstNet integration for the photon simulator

> **PRD #:** 007 · **Status:** In Progress · **Created:** 2026-07-03 · **Owner:** tpeulen
> **Related:** PRD-005 (photon simulator), PRD-006 (TTTR round-trip I/O)
> **Downstream repo:** `github.com/tpeulen/burstnet` (`/Users/tpeulen/dev/burstnet`)
>
> **Progress (tttrlib side, re-verified 2026-08-05):** ✅ G1 background micro-time · ✅ G3
> numpy/Pythonic ergonomics (`SimEngine.photons()` / `from_dict()`, list→`VectorDouble` coercion) ·
> ✅ G4 unit contract · ✅ G6 anisotropy · ✅ G7 (`SimEngine.to_tttr()`) · ✅ G2 ask #2 (`step()`
> resumable offline-cache recipe) · 🟡 G2 ask #1 — the throughput work landed far beyond
> `skip_empty_windows` (coasting + grid bbox + independent/async molecules + `active_margin` +
> focus-sized grids ⇒ ~1000× real-time) and, since 2026-08-05, the `n_ph_max` stopping contract
> BurstNet actually uses reaches it too (horizon search + truncation, below). **Open:** G5
> (per-channel decay, P3, not blocking).

## Summary

BurstNet (a Transformer that reads raw smFRET photon streams and predicts kinetic
parameters) has **removed its bundled Burbulator C++ DLL** and now generates all of its
synthetic training data through the tttrlib `Sim*` photon simulator (PRD-005). The swap is
implemented and functionally correct end-to-end. This PRD records the integration contract
BurstNet depends on and, more importantly, the **gaps in tttrlib that must close for BurstNet
to train at scale**. The headline blocker is throughput at realistic single-molecule
concentration; the headline correctness gap is that background photons carry no micro-time.

## Problem / motivation

BurstNet used to ship a Windows-era Burbulator DLL loaded via a hand-written ctypes shim, with
a two-step workflow: `simulate_ov3()` (photon macro-times, routing, per-photon species) then
`convert_to_spc132(pulsed_exc=1, F, lookup)` (SPC-132 bytes carrying TCSPC micro-times sampled
from species decay CDFs), followed by an SPC re-parse. tttrlib's `SimEngine` now does all of
this in one `run()`, returning the per-photon arrays directly — so BurstNet deleted `src/csrc/`,
`libburbulator.dylib`, `burbulator_dll_wrapper.py`, the `build_burbulator` setuptools machinery,
the `data2spc_tac` `F`/`lookup` construction path, and the SPC byte round-trip.

The integration is done and verified for correctness, but two classes of problem remain that are
tttrlib's to solve so BurstNet "runs smoothly": (1) it cannot yet generate data fast enough at
scientifically realistic parameters, and (2) a small number of feature/ergonomics/documentation
gaps force workarounds in BurstNet. Both are captured below.

## How BurstNet drives the simulator (the integration contract)

Per training stream, BurstNet builds one `SimSample` + `SimSettings` + excitation `SimGrid`,
calls `SimEngine.run()`, and reads six parallel per-photon arrays. Mapping from BurstNet's
`KineticScheme`/`SimulationConfig`:

- **One `SimSpecies` per state.** Conformational FRET states *and* photophysical states
  (donor-only / acceptor-only / dark / bleached) are each a species. Per species:
  `D` (diffusion), `q` (per-channel brightness — the FRET/PIE/MFD channel split is computed in
  BurstNet and pushed into `q`), `r0`/`l1`/`l2` (calibration), and `decay` = a `SimDecay`
  from the IRF-convolved per-species micro-time PDF (`SimDecay.from_pattern`).
- **Kinetics** via `set_rate_matrices(k_rad, k_nrad)` (row-major N×N): `k_rad` diagonal
  `1/τ_D`, `k_nrad` diagonal FRET rate, off-diagonal conformational + photophysical transitions.
- **Population** via `set_population(species, expected_count)` for seeded states; photophysical
  species with no seed are reached only through the transition matrix.
- **Background** via `set_background(q_bg)` per channel.
- **Box / focus** via `set_box` + `SimGrid.gaussian3d(w0, z0, …)`.
- **Read back:** `macro_window` (→ macro-time window index), `channel` (→ routing),
  `emitting_species` (→ per-photon state; **background = `n_species`**), `emitting_molecule`
  (→ per-photon molecule id), `micro_time` (→ native TAC channel), `event_type` (filter `==0`).
  BurstNet cuts these into fixed-size chunks; all per-photon labels (state, coarse class,
  molecule) and per-burst targets (fractions, E, mean-arrival, rate matrix, P(r/R0)) derive from
  these arrays plus the Python-side scheme.

**Contract items BurstNet relies on — please lock these in the PRD-005 "locked API":**

1. `emitting_species == n_species` marks a **background** photon (verified). Stable index.
2. `micro_time` is a **forward-time** TAC channel index sampled from `SimDecay` — no legacy
   `tac = N_tac − tac − 1` inversion. (Simpler than the DLL path; keep it that way.)
3. `event_type == 0` = photon, `== 1` = marker; `run()` without a scanner emits only photons.
4. The engine is unit-agnostic: `dt`, `D`, and the rate matrices share one abstract time unit;
   BurstNet treats it as **milliseconds** and reads `macro_window`/`arrival_time` back in ms.

## Gaps (highlighted — this is the actionable core)

> Priority: **P1** = blocks training at scale · **P2** = forces a workaround · **P3** = nice-to-have.

### G1 · Background photons carry no micro-time — **P1 (correctness)** — ✅ DONE

> **Resolved (tttrlib):** `SimSample::set_background_decay(SimDecay)` /
> `set_background_decays([SimDecay per channel])` (+ JSON `"background_decay": {"pattern": …}`).
> Background photons now sample their micro-time from that pattern; `emitting_species == n_species`
> label unchanged. Verified: recovered background micro-time histogram corr 1.000 with the input
> pattern; test `test/python/simulation/test_imaging.py::test_background_micro_time_follows_pattern`.


`set_background(q_bg)` injects per-channel Poisson background, but those photons come back with
`micro_time == 0` (verified: with two decaying species present, all `emitting_species == n`
photons had `micro_time` identically 0, one distinct value). BurstNet's per-photon class head
must distinguish scatter/background from signal by its **micro-time shape** (an IRF-scatter,
uniform-dark, or "dirt" decay), and a constant 0 both destroys that realism and lets the model
cheat (`mt==0 ⇒ background`).

- **Workaround in place (BurstNet side):** after `run()`, BurstNet resamples the TAC channel of
  every `emitting_species >= n` photon from its own `build_background_pdf` CDF. Works, but it
  duplicates simulator responsibility and can't see per-channel differences.
- **Ask:** let background carry a micro-time distribution in the engine — e.g. an optional
  per-channel `SimDecay` argument to `set_background`, or model background as a first-class
  non-diffusing species with a `decay` and a spatially-uniform emission. Keep the
  `emitting_species == n_species` label.

### G2 · Few-molecule / single-molecule throughput — **P1 (the central blocker)** — 🟡 PARTIAL

> **Progress (tttrlib):** `SimSettings.skip_empty_windows` (opt-in) — when no molecule is inside the
> excitation grid box, advance one **safe coarse time-step** (bounded by the closest molecule's
> distance so a ~`skip_safety`·σ step cannot reach the focus), batching background over the interval.
> **Count-rate-preserving** (ratio 0.988 vs fixed-dt over 15k photons; test
> `test_engine.py::test_skip_empty_windows_preserves_count_rate`), ~2.4× on a 2 µm box and ~3.5× on
> the 10 µm box. The coarse step is bounded by the *closest* molecule, so this is a solid win, not the
> order-of-magnitude one — a bigger gain needs per-molecule **first-passage / Green's-function (GFRD)**
> stepping (asynchronous, decouples molecule time from the global window clock), which is a larger
> future item. G2 ask #2 (resumable `step()` offline cache) remains the practical path for now.

Scientifically realistic confocal single-molecule settings (box 10 µm, `w0≈0.3 µm`, ~5 molecules,
kHz count rate, `dt = 1 µs`) are photon-starved, so reaching a usable pool is dominated by
diffusion stepping through mostly-empty windows. Measured on this machine (CPU):

- 2 µm box, ~6k photons: **~66 s**.
- 10 µm box (the realistic default): **did not finish in 120 s**.

At `pool_target_chunks` scale this makes on-the-fly training impractical — the same wall BurstNet
hit with the legacy DLL. The `Sim*` thread pool does not help here: the per-window molecule loop
is threshold-gated to **serial** for few molecules, and `set_num_threads` can't parallelize a
5-molecule FCS run. The bottleneck is per-window work × an enormous window count, not encoding.

**Benchmark (2026-07-03, this machine, CPU) — wall time is bound by integration-window count, and
a uniform `dt` increase does NOT reduce it:**

| config | wall | N_ph | windows | count rate | windows/s |
|---|---|---|---|---|---|
| box 2 µm, dt 1 µs | 19.9 s | 2000 | 127 457 | 15.7 kHz | ~6400 |
| box 2 µm, **dt 16 µs** | 19.9 s | 2001 | 8 234 | 15.2 kHz | — |
| box 1 µm, dt 1 µs | 4.0 s | 2001 | 18 219 | 110 kHz | ~4600 |

Two findings: (a) at fixed `dt`, wall ≈ windows / ~5–6k·s⁻¹, and windows-to-target =
`N_ph / (count_rate · dt)`; (b) raising the API `dt` **16×** cut the reported window count 15× but
left **wall time and the count rate unchanged** — the engine holds its internal integration step
fixed (sub-steps a large `dt`), so a uniform `dt` bump is a **no-op for speed** and only coarsens the
macro-time grid. The only lever that helped was **concentration** (box 2 µm→1 µm ⇒ 7× count rate ⇒
5× faster), i.e. ask #3. This sharpens ask #1: the speedup must come from *spatially-adaptive*
stepping (coarse far from the focus, fine only near it), not from a bigger global `dt`.

- **Asks (any one materially helps; ranked):**
  1. A **fast few-molecule stepping path** — **spatially-adaptive** stepping (big steps for molecules
     far from the focus, fine steps only near it) or an analytic in-focus residence-time + emission
     sampler that skips empty windows. NB (per the benchmark) a *uniform* larger `dt` does not help —
     the engine already sub-steps it; the win must come from not stepping far-field molecules finely.
  2. A documented, **deterministic, resumable streaming recipe** (`step()` with per-molecule keyed
     RNG and `get/setState`) so BurstNet can **pre-materialize a labeled cache offline in
     parallel** once (~1–2 h) and then train off the cache. This is BurstNet's chosen fallback;
     tttrlib support = confirm `step()` preserves molecule identity/labels across calls and
     document the seeding contract for reproducible shards. **✅ VERIFIED:** chunked `step()`
     (5×1000 windows) is bit-identical to one-shot `step(5000)` — reproducible, resumable, identity-
     preserving; seeding contract `(base, id, window)` documented in PRD-005 "Unit contract & locked
     API". (The fast few-molecule stepping — ask #1 — remains open, the central P1 throughput item.)
  3. A **concentration/box preset + guidance** for trading realism for speed (smaller box / higher
     concentration keeps on-the-fly training viable) with the count-rate implications spelled out.
- **Acceptance:** generate a realistic-parameter labeled stream at a rate that makes either
  on-the-fly training (target: ≥ a few stream-calls/s with worker parallelism) or a one-time cache
  build (target: ~50–100k labeled chunks in ~1–2 h across CPU workers) practical.

#### G2 resolution (2026-08-05) — the photon budget reaches the fast path

The throughput work all landed on `run_independent()`, which is **fixed-duration by
construction**: each molecule's birth count is `Poisson(rate_in·W)` and its birth time is
`U[0,W)`, so the horizon `W` is baked into the realization and a short run cannot be extended
without changing it. `run()` therefore gated the fast path on `max_windows > 0`
(`modules/sim/src/SimEngine.cpp`), and `run_independent_impl` never looked at `n_ph_max`.

That was worse than a missed optimization. With this repo's own idiom — `max_windows = 10**8`
as an "effectively infinite" sentinel plus a real `n_ph_max`, used throughout
`test_engine.py`, and the shape of BurstNet's contract — enabling `independent_molecules` did
not fall back to the slow path. It dispatched `run_independent(10**8)` and tried to simulate
10⁸ windows, drawing a Poisson birth count scaled to that horizon. A silent runaway.

`SimEngine::run_independent_budgeted()` searches the horizon instead: simulate a cheap pilot,
extrapolate the horizon the observed count rate needs for `n_ph_max` (×1.3 margin, always at
least doubling), re-simulate, and truncate the merged, time-ordered stream at exactly the
budget. It stops at whichever of `n_ph_max` / `max_windows` binds first, matching the fixed-`dt`
engine, so both sentinel idioms now work without having to tell them apart.

The subtlety worth recording: each attempt rewinds the global RNG streams to a **snapshot taken
when the search started**, not to the settings seed — building the sample (initial population,
dipole orientations, injection clocks) has already consumed draws, so reseeding would silently
shift every realization. With the snapshot, the surviving realization is a pure function of
(entry state, horizon), which makes a window-bound search **bit-identical** to calling
`run_independent(max_windows)` directly — pinned by
`test_window_horizon_still_binds_when_it_is_the_tighter_limit`.

Measured on a BurstNet-shaped config (box 10 µm, `dt` 1 µs, D 3 µm²/ms, 5 molecules, 20 000
photons): window path **0.57 s / 35 k ph/s** → independent path **0.08 s / 263 k ph/s**, a
**7.5×** gain on the contract that previously could not reach the fast path at all, with count
rates agreeing to <1%. Also fixed in passing: independent-mode ALEX laser-switch markers
bypassed `push_marker`, so `n_photons() - n_markers_` counted them as photons.

The one statistical risk is that accepting the first attempt which *reaches* the budget
conditions on that realization having produced ≥ `n_ph_max` photons, which could bias the count
rate upward. Measured against the fixed-`dt` window engine over 8 seeds: mean paired ratio
**0.9998**, bias **−0.02% ± 0.33%** (sem) — indistinguishable from unbiased.

Tests: `test/python/simulation/test_independent_budget.py` (7 cases — exact budget,
no-window-limit, count-rate parity with the window engine, determinism, the bit-identity
equivalence above, markers-don't-consume-budget, and the multi-seed bias check marked `slow`).
Full `simulation/` suite: 112 passed, no regressions.

### G3 · No numpy typemaps / Pythonic layer for `Sim*` — **P2 (ergonomics)** — ✅ DONE

> **Resolved:** `SimEngine.photons()` returns the whole stream as a dict of numpy arrays,
> `SimEngine.from_dict()` builds an engine from a config dict, and the vector setters coerce
> Python sequences (`species.q = [50.0]` no longer raises). Shipped as `photons()` rather than
> the `as_arrays()` name this PRD proposed.

Every input must be wrapped in `tttrlib.VectorDouble` (`SimSpecies.q = [.. ]` raises
`TypeError`), every output needs `np.asarray(engine.getter())`, there are no kwarg constructors,
and there is no `SimEngine → TTTR` convenience. BurstNet wraps all of this by hand.

- **Ask:** numpy in/out typemaps for the vector setters/getters; kwarg ctors
  (`SimSpecies(D=…, q=[…], decay=…)`, `SimSettings(dt=…, …)`); a `SimEngine.as_arrays()` /
  `to_tttr()` helper that returns a writable `TTTR` (see G7). Mirrors the `%pythoncode` ergonomics
  the CLSM/TTTR subsystems already ship.

### G4 · Unit contract is undocumented — **P2 (correctness hazard)** — ✅ DONE

> **Resolved:** documented in PRD-005 "Unit contract & locked API" — the macro-time axis
> (`dt`/`D`/`k_*`/`q`/`background`/`macro_window`/`arrival_time`) is one abstract unit; the micro-time
> axis (`microtime_resolution`/`laser_period`/`SimDecay.dt`/`micro_time`) is ns; they never mix.


The engine is unit-agnostic, but nothing documents which quantities share a unit. The examples
imply `dt`/`arrival_time` in seconds and `D` in µm²/s, while `microtime_resolution`/`laser_period`
are ns — a legitimate macro/micro split, but a silent trap: Burbulator used ms and µm²/ms, and
BurstNet only avoids a regression by **treating tttrlib's time unit as milliseconds everywhere**.
This is exactly the class of bug BurstNet just spent a milestone fixing.

- **Ask:** document the unit contract for `dt`, `D`, `k_rad`/`k_nrad`, `q`, `background`,
  `arrival_time`, `macro_window`, `microtime_resolution`, `laser_period`, and state explicitly
  that the macro axis and the micro-time axis are independent unit systems.

### G5 · Per-(species, channel) decay — **P3 (future MFD)**

`SimSpecies.decay` is one micro-time pattern per species, shared across all detection channels.
BurstNet currently also uses one decay per species (replicated across channels), so this is **not
blocking today**. Proper MFD/PIE with a distinct acceptor-channel lifetime (donor decay in the
donor channel vs. acceptor decay in the acceptor channel for the same FRET state) needs per-channel
decays.

- **Ask (future):** optional per-(species, channel) `SimDecay`.

### G6 · Anisotropy / polarization incomplete — **P3 (future)** — ✅ DONE

> **Resolved (tttrlib):** the anisotropy engine is implemented — per-molecule dipole, x-polarised
> photoselection (∝3·oₓ²), emission dipole = intrinsic `r0` cone tilt **plus rotational diffusion over
> the excited-state (micro-time) lifetime**, and a parallel(ch0)/perpendicular(ch1) split via `l1`/`l2`.
> `D_rot` is in rad²/ns. Verified against the Perrin equation `r = r0/(1+τ/θ)` (immobile r≈r0, τ=θ →
> r0/2, fast rotation → 0); test `test/python/simulation/test_anisotropy.py`, example
> `examples/simulation/anisotropy.py`. So `--include-polarization` can use true time-resolved anisotropy
> emitting into the two polarization channels. (Remaining: this shares one decay per species — proper
> MFD acceptor-channel lifetime is G5.)

### G7 · Direct `SimEngine → writable TTTR` for example/recipe export — **P3** — ✅ DONE

> **Resolved:** `SimEngine.to_tttr(dt, n_channels, ch_conversion=…, n_microtime_channels=…)`
> composes the engine arrays into a writable `TTTR` (absolute macro-time from
> `macro_window·dt + arrival_time`), so example export and recipe replay can write PTU/HT3/SPC
> again. `SimMicrotimeEncoder` carries the simulated micro-time faithfully and skips markers.

BurstNet's optional example-SPC export and `materialize-dataset` recipe replay wrote real SPC-130
files through the DLL; that path is now stubbed (`NotImplementedError`). The modern replacement is
`tttrlib.TTTR(macro, micro, routing, event).write(path)` from the engine arrays — but constructing
the absolute macrotime needs `macro_window·dt + arrival_time` composed correctly, and PTU/HT3/SPC
write semantics for a marker-free stream should be confirmed.

- **Ask:** a helper that composes `SimEngine`'s arrays into a writable `TTTR` (absolute macrotime,
  correct overflow handling), so BurstNet can re-enable example export and recipe replay against
  PTU/HT3/SPC.

## Non-goals

- Reintroducing any bundled simulator in BurstNet, or a "legacy mode" in tttrlib.
- Changing the `Sim*` scientific model — these are additive features, ergonomics, and docs.
- Byte-identity with the old SPC path (BurstNet no longer round-trips through SPC in training).

## Acceptance criteria (BurstNet "runs smoothly")

1. **G1:** background/scatter photons carry a non-degenerate, configurable micro-time distribution
   from the engine (no Python-side resampling needed).
2. **G2:** a realistic-parameter labeled stream is generated fast enough for either on-the-fly
   training or a practical one-time offline cache build (targets in G2).
3. **G3/G4:** numpy-native Python ergonomics and a documented unit contract, so no manual
   `VectorDouble` wrapping and no unit guesswork.
4. Contract items (background label, forward micro-time, `event_type`, unit convention) are locked
   in the PRD-005 API.

## Verification already done (BurstNet side)

- Single stream → chunks: correct per-photon `emitting_species` labels incl. background = `n`;
  micro-time physics correct (lower FRET ⇒ longer donor lifetime ⇒ later mean TAC: E=0.60→763,
  0.78→575, 0.87→436 TAC channels); background micro-times non-degenerate via the G1 workaround.
- Full dataset sample assembles all tensors: `photon_states`, `photon_classes`, and per-burst
  targets (`fractions`, `fret`, `mean_arrival`, `rates`, `distance_distribution`, `n_states`,
  `molecule_count_bucket`, `multi_molecule`) + `ref`.
- Only remaining limitation observed is throughput (G2).

## References

- tttrlib: `include/SimEngine.h`, `SimSample.h`, `SimSpecies.h`, `SimSettings.h`, `SimDecay.h`,
  `SimGrid.h`, `ext/python/Sim.i`; `SimEngine::default_json` (`src/SimEngine.cpp`);
  examples `examples/simulation/{flim_decay_pattern,rmf_trajectory,clsm_star_scan}.py`.
- BurstNet: `src/burstnet/training/simulate.py` (`_simulate_photons_tttrlib`,
  `BurbulatorTrainingEngine.simulate_stream`, `_scheme_to_simulate_kwargs`,
  `build_background_pdf`, `chunk_photon_stream`), `src/burstnet/training/dataset.py`.
- PRD-005 (photon simulator) — the parent subsystem whose "locked API" should absorb the contract
  items and gap fixes above.
