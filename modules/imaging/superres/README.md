# `imaging/superres` — Super-Resolution Microscopy

Structures and algorithms for super-resolution microscopy data handling (STED, RESOLFT).

## Contents

- **`CLSMSuperRes.h` / `CLSMSuperRes.cpp`**: Spatial grid pixel binning for super-resolution image reconstruction (eSRRF, SOFI, ISM photon reassignment), plus the array-detector (ISM) kernels ported from VicidominiLab's BrightEyes-ISM and s2ISM: `shift_vectors` / `apr_reconstruction` (adaptive pixel reassignment), `focus_reconstruction` (focus-ISM in/out-of-focus split), `s2ism_reconstruction` (joint super-resolution + sectioning), `sofism_reconstruction`, FRC, detector lattices and PSF models.

## Validation and speed against the upstream code

The ISM kernels are A/B-tested against the VicidominiLab packages themselves
(`test/python/clsm/test_clsm_superres_ism_arrays.py`,
`test_clsm_superres_s2ism.py`, run live from the `../chisurf/junk` checkouts)
and benchmarked against them in the `vicidomini` venv
(`benchmarks/bench_vicidomini.py`, `competitors/bench_vicidomini.py`,
`check_vicidomini.py`; numbers in `PERF.md`):

| Kernel | Reference | Output | Speed (2026-08-17) |
|---|---|---|--:|
| `shift_vectors` | `APR_lib.ShiftVectors` | bit-identical | — |
| `apr_reconstruction` | `APR_lib.APR(mode='fourier')` | identical (3e-16) | 3.8× (5.0× vs the default `interp`) |
| `focus_reconstruction` | `FocusISM_lib.focusISM` | same background-fraction map to within noise (0.084 vs 0.083 error against the truth) | 291× |
| `s2ism_reconstruction` | `s2ISM.max_likelihood_reconstruction` | identical (2e-9; the reference is float32) | 3.6× |

Two reference conventions are pinned in the tests rather than copied: s2ISM's
`max_iter=n` performs n+1 updates (tttrlib's `max_iter` is the number of
updates), and it crops even-sized inputs. `apr_reconstruction` registers with
the reference's circular Fourier shift (until 2026-08-17 it used a canvas
zero-padded to twice the frame — 4× the FFT work, and a different answer where
content reached an edge); focus-ISM keeps a zero-padded margin of a few times
the shift (`focusISM` reassigns with the zero-filled `interp` mode), and the
half-frame padded `subpixel_shift` remains for the eSRRF/SOFISM paths, where
wrapped tails would be read as structure. Per-element
work runs in OpenMP.

## Dependencies

- Depends on `imaging/clsm`, `core`, `math`, `util`.
- No third-party dependencies. The module's `CMakeLists.txt` previously declared
  `tttrlib::eigen` and `tttrlib::autodiff` while including neither; both targets
  are gone, and so are the packages behind them. Its dense arithmetic is `simd_scale`/`simd_add` from `Mat.h` and its
  randomness is `Random.h`, both in `math`.
