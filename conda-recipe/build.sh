#!/usr/bin/env bash
git submodule update --recursive --init --remote
rm -r -f build
$PYTHON setup.py install --single-version-externally-managed --record=record.txt
cd build

# cmake \
#   -DCMAKE_BUILD_TYPE=Release \
#   -G Ninja \
#   -DBUILD_PYTHON_INTERFACE=OFF \
#   -DCMAKE_INSTALL_LIBDIR=lib \
#   -DCMAKE_PREFIX_PATH=$PREFIX \
#   -DCMAKE_INSTALL_PREFIX=$PREFIX \
#   ..
# ninja install -k 0

cmake \
 -DCMAKE_INSTALL_PREFIX=$PREFIX \
 -DCMAKE_PREFIX_PATH=$PREFIX \
 -DBUILD_PYTHON_INTERFACE=OFF \
 -DCMAKE_BUILD_TYPE=Release \
 ..
make
make install
