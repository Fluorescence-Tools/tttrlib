#!/usr/bin/env bash
set -euxo pipefail

if [[ "${target_platform}" == osx-* ]]; then
  export CXXFLAGS="${CXXFLAGS} -D_LIBCPP_DISABLE_AVAILABILITY"
fi

# 1. Generate the SWIG R wrapper + interface and build the static C++ core to
#    link the R package against.
mkdir -p b2 && cd b2
cmake -S .. -B . \
  -DCMAKE_CXX_COMPILER="${CXX}" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DBUILD_PYTHON_INTERFACE=OFF \
  -DBUILD_R_INTERFACE=ON \
  -DBUILD_LIBRARY=ON \
  -DWITH_AVX=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -G Ninja
ninja tttrlibR tttrlibStatic -j "${CPU_COUNT}"
cd ..

# 2. Locate the generated wrapper (.cxx), the R interface (.R) and the static core.
WRAP=$(find "$(pwd)/b2" -name 'tttrlibR_wrap.cxx' | head -1)
R_IF=$(find "$(pwd)/b2" -name 'tttrlib.R' | head -1)
STATIC=$(find "$(pwd)/b2" -name 'libtttrlib_static.a' | head -1)
test -f "${WRAP}"; test -f "${R_IF}"; test -f "${STATIC}"

# 3. Stage into the R package and substitute the Makevars placeholders. R CMD
#    INSTALL then compiles tttrlib_wrap.cpp -> tttrlib.so, linking the static core.
mkdir -p ext/r/pkg/src ext/r/pkg/R
cp "${WRAP}" ext/r/pkg/src/tttrlib_wrap.cpp
cp "${R_IF}" ext/r/pkg/R/tttrlib.R
# Replicate CMake's include dirs so the wrapper compiles against the same headers
# as the static core: repo root (for "include/Foo.h"), src, and the vendored
# thirdparty (HighFive, nlohmann/json), plus the conda prefix (hdf5, pocketfft).
ROOT="$(pwd)"
INCDIRS="-I${ROOT}/include -I${ROOT}/src -I${ROOT}/thirdparty -I${ROOT}/thirdparty/nlohmann_json/include -I${ROOT}/thirdparty/HighFive/include -I${PREFIX}/include"
sed -e "s|@TTTRLIB_INCLUDE@|${ROOT}|g" \
    -e "s|@HDF5_CFLAGS@|${INCDIRS}|g" \
    -e "s|@TTTRLIB_LIBS@|${STATIC}|g" \
    -e "s|@HDF5_LIBS@|-L${PREFIX}/lib -lhdf5|g" \
    ext/r/pkg/src/Makevars.in > ext/r/pkg/src/Makevars
rm -f ext/r/pkg/src/Makevars.in
printf 'CXX_STD = CXX17\n' >> ext/r/pkg/src/Makevars

# 4. Install into the conda R library.
"${PREFIX}/bin/R" CMD INSTALL --no-multiarch --no-test-load ext/r/pkg
