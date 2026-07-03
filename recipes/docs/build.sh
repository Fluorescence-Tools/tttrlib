#!/usr/bin/env bash
set -euxo pipefail

# tttrlib itself comes from the freshly built conda package (host dependency);
# the CI docs job passes its conda-bld channel to rattler-build. Nothing is
# compiled here - this recipe only runs Sphinx against the installed package.
"${PYTHON}" -c "import tttrlib; print('tttrlib', tttrlib.__version__ if hasattr(tttrlib, '__version__') else 'imported')"

# 1. Reference data for the example gallery (examples are NOT executed).
export TTTRLIB_DATA="${SRC_DIR}/tttr-data"
export TTTRLIB_DOCS_EXECUTE_EXAMPLES=0
"${PYTHON}" test/download_test_data.py --output-dir "${TTTRLIB_DATA}" --quiet || \
  echo "warning: reference data download failed; gallery thumbnails may be missing"

# 2. Build the HTML documentation.
export BUILD_TIER="${BUILD_TIER:-4}"
cd doc
mkdir -p _build/api
"${PYTHON}" -m sphinx -T -b html -d _build/doctrees . _build/html/stable
"${PYTHON}" ../tools/check_docs_pages.py _build/html/stable || true
rm -rf _build/html/stable/api
[ -d _build/api ] && cp -r _build/api _build/html/stable/ || true
cd ..

# 3. Install the built HTML into the package.
mkdir -p "${PREFIX}/share/doc/tttrlib"
cp -r doc/_build/html/stable "${PREFIX}/share/doc/tttrlib/html"
