#!/usr/bin/env bash
set -euxo pipefail

# 1. Build & install tttrlib into the host python (needed for autodoc/gallery).
export CMAKE_ARGS="-DWITH_AVX=OFF"
"${PYTHON}" -m pip install . --no-deps --no-build-isolation -vv

# 2. Reference data for the example gallery (examples are NOT executed).
export TTTRLIB_DATA="${SRC_DIR}/tttr-data"
export TTTRLIB_DOCS_EXECUTE_EXAMPLES=0
"${PYTHON}" -m pip install --no-build-isolation pooch tqdm || true
"${PYTHON}" test/download_test_data.py --output-dir "${TTTRLIB_DATA}" --quiet || \
  echo "warning: reference data download failed; gallery thumbnails may be missing"

# 3. Build the HTML documentation.
export BUILD_TIER="${BUILD_TIER:-4}"
cd doc
mkdir -p _build/api
"${PYTHON}" -m sphinx -T -b html -d _build/doctrees . _build/html/stable
"${PYTHON}" ../tools/check_docs_pages.py _build/html/stable || true
rm -rf _build/html/stable/api
[ -d _build/api ] && cp -r _build/api _build/html/stable/ || true
cd ..

# 4. Install the built HTML into the package.
mkdir -p "${PREFIX}/share/doc/tttrlib"
cp -r doc/_build/html/stable "${PREFIX}/share/doc/tttrlib/html"
