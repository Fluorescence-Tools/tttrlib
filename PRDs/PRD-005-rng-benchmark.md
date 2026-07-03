# PRD-005 — RNG backend & scope benchmark

> Companion to [PRD-005-photon-simulator.md](PRD-005-photon-simulator.md). Justifies the RNG
> defaults for the simulation engine. Measured 2026-07-03, Apple Silicon (arm64, clang, `-O3`).

## Two design axes

1. **Backend** (`SimRngKind`): xoshiro256++ / PCG32 / Philox4x32-10 / MT19937.
2. **Scope** (`SimRngScope`): **PerMolecule** — each molecule keyed by `(id, window)`, so results are
   **independent of thread count** (reproducible); **PerThread** — one stream per worker seeded once
   per window, faster but results depend on thread count and molecule partition.

For thread-count-independent reproducibility the engine (PerMolecule) reseeds a substream **per
molecule per window**, so *seeding cost* matters as much as raw throughput.

## Results (1M photons, 20,000 static emitters)

### Backend, in-engine (PerMolecule)

| Backend | serial | 8 threads | vs xoshiro | serial==parallel |
|---|--:|--:|--:|:--:|
| PCG32 | 0.65 s | 0.32 s | 0.95× | yes |
| **xoshiro256++** (default) | 0.69 s | 0.27 s | 1.0× | yes |
| Philox4x32-10 | 0.92 s | 0.32 s | 1.33× slower | yes |
| MT19937 | 28.2 s | 5.9 s | **≈41× slower** | yes |

### Raw throughput (seed once, stream 300M `random0e1e`)

| Backend | ns/draw | vs xoshiro |
|---|--:|--:|
| MT19937 | 1.14 | 1.11× faster |
| xoshiro256++ | 1.26 | 1.0× |
| PCG32 | 2.05 | 0.61× |
| Philox4x32-10 | 2.56 | 0.49× |

### Scope — reseed cost (PerMolecule vs PerThread, serial)

| Backend | PerMolecule | PerThread | reseed overhead |
|---|--:|--:|--:|
| **PCG32** | 0.65 s | 0.64 s | **+1.9%** |
| xoshiro256++ | 0.69 s | 0.63 s | +9.9% |
| MT19937 | 28.2 s | 0.68 s | +4060% |

## Interpretation

- **Raw speed ≠ in-engine speed.** MT19937 has the best raw throughput but is ≈41× slower in-engine:
  its 624-word `seed` init is paid per molecule per window. SHISHUA (previously evaluated) had the
  same problem — high raw throughput, expensive `prng_init` — and was dropped (also to avoid vendoring
  cost). What matters here is *cheap seeding*.
- **The per-molecule reseed is nearly free for cheap-seed generators.** PCG32's built-in
  stream-selection seeding costs only **+1.9%**, xoshiro's splitmix64 seeding +9.9%. So keeping
  PerMolecule (reproducible across thread counts) is essentially free — no need to drop to a single
  global stream (which would forbid parallelism) or to PerThread (which sacrifices reproducibility).
- **PerThread** exists for maximum speed / to make MT19937 usable (one seed per thread → 0.68 s), at
  the cost of thread-count-dependent output. Statistics are unchanged either way.
- **Quality:** xoshiro256++ and PCG32 both pass TestU01 BigCrush — high-quality for decay/microtime
  sampling *and* diffusion, so there is no need to split RNGs by purpose.
- **All PerMolecule runs are thread-count-independent** (serial output bit-identical to 8-thread).
  The ≈2–3× thread scaling is Amdahl-limited by the serial per-window merge/sort, not the RNG.

## Decision

Default **`SimRngKind::Xoshiro`** + **`SimRngScope::PerMolecule`**. **PCG32** is an equally-good (and
in-engine slightly faster, lowest-reseed-cost) alternative. **Philox** is the stateless counter-based
reference. **MT19937** and **PerThread** are available for study/max-speed but are not the default
(MT is seeding-dominated in PerMolecule; PerThread is thread-count-dependent). All are selectable via
`SimSettings` or the JSON config (`"rng_kind"`, `"rng_scope"`).

## Reproduce

Select via `SimSettings.rng_kind` / `.rng_scope` or JSON (`SimEngine.from_json`). Backend names in
Python: `tttrlib.SimRngKind_Xoshiro | _Pcg | _Philox | _Mt19937`; scopes
`tttrlib.SimRngScope_PerMolecule | _PerThread`; threads via `SimEngine.set_num_threads(n)`.
