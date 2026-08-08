#!/usr/bin/env bash
set -euxo pipefail

if [[ "${target_platform}" == osx-* ]]; then
  # See https://conda-forge.org/docs/maintainer/knowledge_base.html#newer-c-features-with-old-sdk
  CXXFLAGS="${CXXFLAGS} -D_LIBCPP_DISABLE_AVAILABILITY"
fi

# Build the C++ shared library and the tttr CLI. No Python, no R: this package
# ships only libtttrlib + the binary. OpenMP links through the conda-forge
# compiler activation (llvm-openmp on macOS, libgomp on Linux), which sets up
# the right -L and -l flags for the host -- unlike a manual cmake call into an
# active env, where system clang does not know where conda put libomp.
mkdir -p b2 && cd b2
cmake -S .. -B . \
  -DCMAKE_CXX_COMPILER="${CXX}" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DBUILD_LIBRARY=ON \
  -DBUILD_PYTHON_INTERFACE=OFF \
  -DBUILD_R_INTERFACE=OFF \
  -DBUILD_PYTHON_DOCS=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -DWITH_AVX=OFF \
  -DWITH_TIFF=ON \
  -DWITH_TIFF_SYSTEM=ON \
  -G Ninja

ninja tttrlibShared tttrlibStatic tttr -j "${CPU_COUNT}"
ninja install

# The tttr binary's RPATH (@loader_path/../lib on macOS, $ORIGIN/../lib on
# Linux) is set as a target property that overrides CMAKE_SKIP_INSTALL_RPATH,
# so it survives the install unchanged. Verify the binary runs in-place.
"$PREFIX/bin/tttr" --help >/dev/null
