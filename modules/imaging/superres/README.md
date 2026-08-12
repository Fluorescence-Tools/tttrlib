# `imaging/superres` — Super-Resolution Microscopy

Structures and algorithms for super-resolution microscopy data handling (STED, RESOLFT).

## Contents

- **`CLSMSuperRes.h` / `CLSMSuperRes.cpp`**: Spatial grid pixel binning for super-resolution image reconstruction.

## Dependencies

- Depends on `imaging/clsm`, `core`, `math`, `util`.
- No third-party dependencies. The module's `CMakeLists.txt` previously declared
  `tttrlib::eigen` and `tttrlib::autodiff` while including neither; both targets
  are gone, and so are the packages behind them. Its dense arithmetic is `simd_scale`/`simd_add` from `Mat.h` and its
  randomness is `Random.h`, both in `math`.
