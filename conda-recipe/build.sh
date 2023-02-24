git submodule update --recursive --init --remote
mkdir b2 && cd b2


# There is an additional check if the build machine supports.
# On macOS the software is build on a machine that supports AVX.
# To support x86 on M1 machines the AVX flag must be turned off.
if [ "$(uname)" == "Darwin" ]; then
    # Do something under Mac OS X platform   
    export WITH_AVX=0 # no AVX under macOS -> issues with M1
elif [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
    # Do something under GNU/Linux platform
    export WITH_AVX=1
fi

cmake -DCMAKE_INSTALL_PREFIX="$PREFIX" \
 -DCMAKE_PREFIX_PATH="$PREFIX" \
 -DBUILD_PYTHON_INTERFACE=ON \
 -DCMAKE_BUILD_TYPE=Release \
 -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="$SP_DIR" \
 -DCMAKE_SWIG_OUTDIR="$SP_DIR" \
 -DWITH_AVX="$WITH_AVX" \
 -G Ninja ..

# On some platforms (notably aarch64 with Drone) builds can fail due to
# running out of memory. If this happens, try the build again; if it
# still fails, restrict to one core.
ninja install -k 0 || ninja install -k 0 || ninja install -j 1

# Copy programs to bin
cd $PREFIX/bin
cp $SRC_DIR/bin/* .