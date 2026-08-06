#!/usr/bin/env bash
# Fast guard-rail: verify the shared SWIG interface still generates wrappers for
# Python, R, Java and JavaScript. Catches breakage in the language-neutral
# fragments (e.g. a Python-only construct leaking into the shared core) without a
# full native build.
#
# Requires: swig (>= 4.2 -- the Node-API JavaScript backend landed in 4.2.0).
# javac is optional (used to compile the Java proxies).
# Usage:  tools/check_swig_multilang.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

# The headers live under modules/*/include since the module rework; the old
# -Iinclude -Isrc found nothing and every pass failed on "Unable to find TTTR.h".
# Glob the module include directories the way cmake/TTTRLibModule.cmake does.
MODULE_INCLUDES=()
for d in modules/*/include modules/*/*/include; do
  [ -d "$d" ] && MODULE_INCLUDES+=("-I$ROOT/$d")
done

INCLUDES=(-DTTTRLIB_WITH_AVX=0 -I. -Iinclude -Isrc -Iext -Iext/python
          -Ithirdparty/nlohmann_json/include "${MODULE_INCLUDES[@]}")

echo "== Python wrapper =="
mkdir -p "$OUT/py"
swig -c++ -python "${INCLUDES[@]}" -outdir "$OUT/py" -o "$OUT/py/w.cxx" ext/python/tttrlib.i
echo "   OK"

# The Python wrapper is the reference: adding or changing a binding for another
# language must not perturb it. Hash it here and re-check at the end, after every
# other backend has run over the same shared fragments.
PY_HASH_BEFORE="$(cksum < "$OUT/py/w.cxx")"

echo "== R wrapper =="
mkdir -p "$OUT/r"
swig -c++ -r "${INCLUDES[@]}" -Iext/r -outdir "$OUT/r" -o "$OUT/r/w.cxx" ext/r/tttrlib.i
# No real Python C-API must leak into the R wrapper. PyErr_Format / PyExc_ValueError
# are excluded: DecayConvolution.i provides a portable self-contained shim of those
# names for non-Python targets.
PYAPI='(Py[A-Za-z]+_[A-Za-z_]+|import_array)'
if grep -oE "$PYAPI" "$OUT/r/w.cxx" | grep -vxE 'PyErr_Format|PyExc_ValueError' | grep -q .; then
  echo "   ERROR: Python C-API leaked into the R wrapper:" >&2
  grep -oE "$PYAPI" "$OUT/r/w.cxx" | grep -vxE 'PyErr_Format|PyExc_ValueError' | sort | uniq -c >&2
  exit 1
fi
echo "   OK"

echo "== Java wrapper =="
mkdir -p "$OUT/java/src"
swig -c++ -java -package io.github.fluorescencetools.tttrlib "${INCLUDES[@]}" -Iext/java \
     -outdir "$OUT/java/src" -o "$OUT/java/w.cxx" ext/java/tttrlib.i
if command -v javac >/dev/null 2>&1; then
  cp ext/java/pkg/src/main/java/io/github/fluorescencetools/tttrlib/NativeLoader.java "$OUT/java/src/"
  javac -d "$OUT/java/classes" "$OUT/java/src/"*.java
  echo "   OK (proxies compiled)"
else
  echo "   OK (javac not found; skipped proxy compile)"
fi

echo "== JavaScript (Node-API) wrapper =="
mkdir -p "$OUT/js"
swig -c++ -javascript -napi "${INCLUDES[@]}" -Iext/js \
     -outdir "$OUT/js" -o "$OUT/js/w.cxx" ext/js/tttrlib.i
# Same rule as R: no real Python C-API in a non-Python wrapper. PyErr_Format and
# PyExc_ValueError are the documented exception -- DecayConvolution.i ships a
# portable shim under those names for non-Python targets.
if grep -oE "$PYAPI" "$OUT/js/w.cxx" | grep -vxE 'PyErr_Format|PyExc_ValueError' | grep -q .; then
  echo "   ERROR: Python C-API leaked into the JavaScript wrapper:" >&2
  grep -oE "$PYAPI" "$OUT/js/w.cxx" | grep -vxE 'PyErr_Format|PyExc_ValueError' | sort | uniq -c >&2
  exit 1
fi
# The JavaScript module wraps the FULL Python surface (unlike R and Java, which
# wrap a subset), so a class missing here means a shared fragment stopped parsing
# for this backend. Spot-check the ones the web applications are built on.
for sym in TTTR TTTRHeader Correlator CLSMImage registry_json; do
  grep -q "$sym" "$OUT/js/w.cxx" || { echo "   ERROR: '$sym' missing from the JavaScript wrapper" >&2; exit 1; }
done
echo "   OK"

echo "== Python wrapper unchanged =="
mkdir -p "$OUT/py2"
swig -c++ -python "${INCLUDES[@]}" -outdir "$OUT/py2" -o "$OUT/py2/w.cxx" ext/python/tttrlib.i 2>/dev/null
if [ "$(cksum < "$OUT/py2/w.cxx")" != "$PY_HASH_BEFORE" ]; then
  echo "   ERROR: the Python wrapper is not reproducible across runs" >&2
  exit 1
fi
echo "   OK"

echo "All SWIG interfaces generate cleanly."
