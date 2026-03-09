
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

# No executables to copy (tttrlib is a library only)
