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

BASE_INCLUDES=(-DTTTRLIB_WITH_AVX=0 -I. -Iinclude -Isrc -Iext -Iext/python
          -Ithirdparty/nlohmann_json/include "${MODULE_INCLUDES[@]}")

# ext/CMakeLists.txt passes -DSWIGWORDSIZE64 on 64-bit Linux, so generate what is
# actually built rather than a variant of it. Without the flag SWIG resolves
# int64_t to 'long long' where the compiler says 'long', and a
# std::map<std::string, std::vector<int64_t>> return -- Photonscore's
# read_photons -- produces a Java wrapper that does not compile:
#   no match for 'operator=' (SwigValueWrapper<map<string, vector<long long>>>
#   ... map<string, vector<long>>)
INCLUDES=("${BASE_INCLUDES[@]}")
R_INCLUDES=("${BASE_INCLUDES[@]}")
if [ "$(uname -s)" = "Linux" ] && [ "$(getconf LONG_BIT)" = "64" ]; then
  INCLUDES+=(-DSWIGWORDSIZE64)
  # R is the same exception ext/CMakeLists.txt makes: SWIG's R backend
  # mishandles 'long long' returns under the flag before 4.4.
  SWIG_VERSION="$(swig -version | sed -n 's/^SWIG Version \([0-9.]*\).*/\1/p')"
  if [ "$(printf '4.4\n%s\n' "$SWIG_VERSION" | sort -V | head -1)" = "4.4" ]; then
    R_INCLUDES+=(-DSWIGWORDSIZE64)
  fi
fi

echo "== Python wrapper =="
mkdir -p "$OUT/py"
swig -c++ -python "${INCLUDES[@]}" -outdir "$OUT/py" -o "$OUT/py/w.cxx" ext/python/tttrlib.i
echo "   OK"

# The split Python extensions (TTTRLIB_PYTHON_SPLIT; ext/python/split/README.md)
# wrap the same fragments as four %modules that %import each other. They break
# in ways the monolith cannot -- a fragment missing from a module, a template
# only imported, a helper naming the wrong C module -- so generate all four too.
echo "== split Python wrappers =="
for m in core formats kernels spectroscopy imaging sim; do
  mkdir -p "$OUT/py_$m"
  swig -c++ -python "${INCLUDES[@]}" -Iext/python -outdir "$OUT/py_$m" \
       -o "$OUT/py_$m/w.cxx" "ext/python/split/mod_$m.i"
done
echo "   OK"

# The Python wrapper is the reference: adding or changing a binding for another
# language must not perturb it. Hash it here and re-check at the end, after every
# other backend has run over the same shared fragments.
PY_HASH_BEFORE="$(cksum < "$OUT/py/w.cxx")"

echo "== R wrapper =="
mkdir -p "$OUT/r"
swig -c++ -r "${R_INCLUDES[@]}" -Iext/r -outdir "$OUT/r" -o "$OUT/r/w.cxx" ext/r/tttrlib.i
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

# The proxy compile above checks the generated *Java*; it says nothing about the
# generated C++. That matters because ext/java/helpers.i is the one place the
# Java binding carries hand-written C++ (the `_into` accessors), and a mistake
# there generates and javac-compiles happily, then fails only in a full native
# build. Syntax-only, so it costs a parse rather than a compile.
JAVA_HOME_DIR="$(/usr/libexec/java_home 2>/dev/null || echo "${JAVA_HOME:-}")"
if [ -n "$JAVA_HOME_DIR" ] && [ -d "$JAVA_HOME_DIR/include" ] && command -v c++ >/dev/null 2>&1; then
  JNI_INC=("-I$JAVA_HOME_DIR/include")
  for d in "$JAVA_HOME_DIR/include"/*/; do [ -d "$d" ] && JNI_INC+=("-I$d"); done
  if c++ -fsyntax-only -std=c++17 "${INCLUDES[@]}" -Iext/java "${JNI_INC[@]}" \
         "$OUT/java/w.cxx" 2>"$OUT/java/cxx.log"; then
    echo "   OK (generated C++ compiles)"
  else
    echo "   FAILED: the generated Java wrapper C++ does not compile" >&2
    head -30 "$OUT/java/cxx.log" >&2
    exit 1
  fi
else
  echo "   (no JDK headers or c++; skipped generated-C++ compile)"
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

echo "== Python wrapper is reproducible =="
mkdir -p "$OUT/py2"
swig -c++ -python "${INCLUDES[@]}" -outdir "$OUT/py2" -o "$OUT/py2/w.cxx" ext/python/tttrlib.i 2>/dev/null
# NB: this compares two runs of THIS script, so it catches non-determinism in
# SWIG's output -- not a change against the previous commit. It does not tell
# you whether an edit altered the Python surface; `git diff` on the generated
# wrapper does. The old heading said "unchanged", which read as the latter.
if [ "$(cksum < "$OUT/py2/w.cxx")" != "$PY_HASH_BEFORE" ]; then
  echo "   ERROR: the Python wrapper is not reproducible across runs" >&2
  exit 1
fi
echo "   OK"

echo "== Exception handlers =="
# %exception is global state, not a scope: a bare `%exception;` after the global
# handler is installed removes it for every interface that follows, and a C++
# throw with no handler terminates the interpreter instead of raising.
python3 "$ROOT/tools/check_exception_handlers.py" || exit 1

echo "== Binding parity =="
# Generating is not the same as exposing. The four %include lists are separate
# files and have drifted before -- 16-18 interfaces present in Python and absent
# elsewhere, while every non-Python master claimed the lists were identical.
# Every gap must be declared with a reason; see tools/binding_parity_exceptions.txt.
python3 "$ROOT/tools/check_binding_parity.py" || exit 1

echo "All SWIG interfaces generate cleanly."
