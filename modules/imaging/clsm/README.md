# `imaging/clsm` — Confocal Laser Scanning Microscopy

Implements spatial reconstruction of 2D/3D images from TTTR marker streams (line sync, frame sync, pixel sync).

## Contents

- **`CLSMImage.h` / `CLSMImage.cpp`**: Top-level CLSM image container (frames, lines, pixels).
- **`CLSMFrame.h` / `CLSMFrame.cpp`**: Frame container for multi-frame CLSM acquisition.
- **`CLSMLine.h` / `CLSMLine.cpp`**: Line container for line scanning markers.
- **`CLSMPixel.h` / `CLSMPixel.cpp`**: Pixel container storing photon indices per pixel.
- **`DecayPhasor.h` / `DecayPhasor.cpp`**: FLIM Phasor transformation per pixel.

## Dependencies

- Depends on `core`, `util`, `fcs`.
- nlohmann/json, pocketfft. Not Eigen: the module declared `tttrlib::eigen`
  without including it, and that declaration is gone along with the dependency.
