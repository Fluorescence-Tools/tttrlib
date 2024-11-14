mkdir b2 && cd b2

if [[ "${target_platform}" == osx-* ]]; then
  # See https://conda-forge.org/docs/maintainer/knowledge_base.html#newer-c-features-with-old-sdk
  CXXFLAGS="${CXXFLAGS} -D_LIBCPP_DISABLE_AVAILABILITY"
else
  export CONFIG_ARGS=""
fi

cmake -S .. -B . \
  -DCMAKE_CXX_COMPILER="${CXX}" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DBUILD_PYTHON_INTERFACE=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_LIBRARY=OFF \
  -DWITH_AVX=OFF \
  -DBoost_USE_STATIC_LIBS=OFF \
  -DPYTHON_VERSION=$(python -c 'import platform; print(platform.python_version())')\
  -G Ninja \
  "${CONFIG_ARGS}"

ninja install -j ${CPU_COUNT}

# Copy programs to bin
chmod 0755 $SRC_DIR/bin/*
cp -f $SRC_DIR/bin/* $PREFIX/bin
