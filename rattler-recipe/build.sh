
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

# Create tttrlib package directory and add __init__.py
# The SWIG generates tttrlib.py as a module file, but we also have helper files
# that need to be in a package directory for proper imports
PYTHON_SITE_PACKAGES=$(${PREFIX}/bin/python -c "import site; print(site.getsitepackages()[0])")

# Move the generated tttrlib.py to tttrlib/ package
if [[ -f "${PYTHON_SITE_PACKAGES}/tttrlib.py" ]]; then
    mkdir -p "${PYTHON_SITE_PACKAGES}/tttrlib"
    mv "${PYTHON_SITE_PACKAGES}/tttrlib.py" "${PYTHON_SITE_PACKAGES}/tttrlib/__init__.py"
fi

# Copy helper Python files from ext/python/
cp ../ext/python/*.py "${PYTHON_SITE_PACKAGES}/tttrlib/" 2>/dev/null || true
