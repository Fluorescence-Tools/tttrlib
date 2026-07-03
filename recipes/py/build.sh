
if [[ "${target_platform}" == osx-* ]]; then
  # See https://conda-forge.org/docs/maintainer/knowledge_base.html#newer-c-features-with-old-sdk
  CXXFLAGS="${CXXFLAGS} -D_LIBCPP_DISABLE_AVAILABILITY"
fi

mkdir b2 && cd b2
cmake -S .. -B . \
  -DCMAKE_CXX_COMPILER="${CXX}" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DBUILD_PYTHON_INTERFACE=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_LIBRARY=OFF \
  -DWITH_AVX=OFF \
  -DPython_ROOT_DIR="${PREFIX}/bin" \
  -DBUILD_PYTHON_DOCS=OFF \
  -G Ninja \
  ${CONFIG_ARGS}

ninja install -j ${CPU_COUNT}

# Convert SWIG output to a package
# SWIG generates: tttrlib.py (module) + _tttrlib*.so (extension)
# We need both in tttrlib/ package directory
PYTHON_SITE_PACKAGES=$(${PREFIX}/bin/python -c "import site; print(site.getsitepackages()[0])")

# Find and move the compiled extension (_tttrlib*.so) to package dir
mkdir -p "${PYTHON_SITE_PACKAGES}/tttrlib"
for f in "${PYTHON_SITE_PACKAGES}"/_tttrlib*.so; do
    if [[ -f "$f" ]]; then
        mv "$f" "${PYTHON_SITE_PACKAGES}/tttrlib/"
    fi
done

# Move the Python wrapper to __init__.py
if [[ -f "${PYTHON_SITE_PACKAGES}/tttrlib.py" ]]; then
    mv "${PYTHON_SITE_PACKAGES}/tttrlib.py" "${PYTHON_SITE_PACKAGES}/tttrlib/__init__.py"
fi
