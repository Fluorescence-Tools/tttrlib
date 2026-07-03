# tttrlib for R

R bindings for the tttrlib C++ library, generated with SWIG.

## Building

The SWIG wrapper is generated from `ext/r/tttrlib.i` (which re-uses the shared
interface fragments in `ext/python/`). Two routes:

### Via CMake (developer build)

```sh
cmake -S . -B build-r -DBUILD_PYTHON_INTERFACE=OFF -DBUILD_R_INTERFACE=ON
cmake --build build-r --target tttrlibR
```

This produces `tttrlib.so` (the R loadable module) and stages `tttrlib.R`.

### Via R CMD INSTALL (package build)

Generate the wrapper into the package tree, then install:

```sh
swig -c++ -r -I include -I ext/python -I ext/r \
     -o ext/r/pkg/src/tttrlib_wrap.cpp -outdir ext/r/pkg/R ext/r/tttrlib.i
R CMD INSTALL ext/r/pkg
```

## Usage notes

* SWIG-R exposes C++ objects as **S4 classes** with an external-pointer `@ref`
  slot. Methods are called as generics, e.g. `TTTR_get_macro_times(obj)` rather
  than `obj$get_macro_times()`.
* Numeric arrays are passed and returned as native R vectors. 64-bit photon
  macro-times are carried through `double`, so exact integer values above 2^53
  lose precision (a known limitation of R's numeric type).
* `PdaCallback` subclassing (directors) is **not** available in R; only the
  built-in callbacks are exposed.
