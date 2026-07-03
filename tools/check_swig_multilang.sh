#!/usr/bin/env bash
# Fast guard-rail: verify the shared SWIG interface still generates wrappers for
# Python, R and Java. Catches breakage in the language-neutral fragments (e.g. a
# Python-only construct leaking into the shared core) without a full native build.
#
# Requires: swig (>= 4.1). javac is optional (used to compile the Java proxies).
# Usage:  tools/check_swig_multilang.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

INCLUDES=(-DTTTRLIB_WITH_AVX=0 -I. -Iinclude -Isrc -Iext -Iext/python
          -Ithirdparty/nlohmann_json/include)

echo "== Python wrapper =="
mkdir -p "$OUT/py"
swig -c++ -python "${INCLUDES[@]}" -outdir "$OUT/py" -o "$OUT/py/w.cxx" ext/python/tttrlib.i
echo "   OK"

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

echo "All SWIG interfaces generate cleanly."
