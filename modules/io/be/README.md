# `io/be` — BrightEyes-TTM Format I/O

Reader for **BrightEyes-TTM** raw time-tagging data (`.ttr`) — the open-hardware
time-tagging module from the Vicidomini lab (IIT): a Xilinx Kintex-7 TDC of
~30 ps resolution built for SPAD arrays of 25 or 49 elements, whose element
number is the routing channel and whose scan markers are the rising edges of the
step-byte enable bits.

Becker & Hickl `.spc` is [`io/bh`](../bh); the two are unrelated.

## Contents

- **`include/io_be.h`** — the format: word layout, the 16-bit macro-time step
  unwrapping, the micro time as `code - laser_code (mod 256)`, and the marker
  decoding.
- **`src/io_be.cpp`** — header inspection, sniffing and the record decoder.

## Validation

A/B-tested against the vendor parser **libttp 0.1.43**
(`ttpCython.timeProcessNewProtocol`) on the first 4 M words of the Zenodo
sample: 326 835 photons' channels, macro times and micro times identical, and
the pixel/line/frame markers reproduced
(`test/python/tttr/test_ab_core_reference.py`; register:
`okf/testing/algorithm-validation.md`).

## Dependencies

- Depends on `io/base`, `util`, nlohmann/json.
