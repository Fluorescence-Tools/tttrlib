
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

# Copy Python helper files that are not included in the SWIG-generated wrapper
# These files are in the tttrlib/ source directory
PYTHON_SITE_PACKAGES=$(${PREFIX}/bin/python -c "import site; print(site.getsitepackages()[0])")
if [[ ! -d "${PYTHON_SITE_PACKAGES}/tttrlib" ]]; then
    mkdir -p "${PYTHON_SITE_PACKAGES}/tttrlib"
fi
cp ../tttrlib/*.py "${PYTHON_SITE_PACKAGES}/tttrlib/"
