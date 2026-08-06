# JavaScript tests

```
cmake -S . -B build -DBUILD_JAVASCRIPT_INTERFACE=ON && cmake --build build -j
node --test test/js/
```

Needs the reference data: `python test/download_test_data.py`, or point
`$TTTRLIB_DATA` at an existing copy. Tests whose data is missing skip rather than
fail, the same way the Python suite behaves.

`$TTTRLIB_JS_PKG` selects a staged package directory; by default the tests load
`ext/js/pkg/index.js`, whose loader already searches the build tree.

## The files

| | |
|---|---|
| `cross_language_reference.test.mjs` | The canonical values every binding must agree on. **The same assertions run in Python, R and Java.** A failure here means the JavaScript binding is wrong — never that the number needs updating. |
| `arrays.test.mjs` | The marshalling contract of `ext/js/jsarrays.i`: element types, byte offsets on sliced views, wrong-type rejection, shapes. |
| `tttr.test.mjs` | Readers, header, selections, micro-time histograms. |
| `registry.test.mjs` | The registry, and burst search driven entirely by it. |
| `analysis.test.mjs` | Correlation, CLSM imaging, burst filtering, phasors, the simulator, histograms. |

## What "on par with Python" means here

The Python suite is ~200 files and much of it is regression tests for C++
behaviour — which is language-independent and already covered. Duplicating it in
JavaScript would test the same C++ twice and the binding not at all.

So the split is deliberate:

- **Shared numbers** live in `cross_language_reference.test.mjs`, byte for byte
  the constants from `test/python/tttr/test_cross_language_reference.py`. This is
  what actually proves the two bindings read the same data.
- **Binding-specific risk** gets its own file, `arrays.test.mjs`. Python inherits
  its marshalling from numpy.i, which is upstream and mature; `jsarrays.i` is new,
  everything else depends on it, and its failure modes are silent — a
  reinterpreted element type, a sliced view read from the wrong offset. That is
  where JavaScript needs *more* testing than Python, not less.
- **Surface coverage** mirrors the Python directories subsystem by subsystem, and
  asserts invariants (shapes, conservation, monotonicity, two routes agreeing)
  rather than re-copying constants that would then have two homes and drift.
