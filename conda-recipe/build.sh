#!/usr/bin/env bash
rm -r -f build
$PYTHON setup.py install --single-version-externally-managed --record=record.txt

cd build
cmake .. \
  -DCMAKE_INSTALL_PREFIX=$PREFIX \
  -DCMAKE_PREFIX_PATH=$PREFIX \
  -DBUILD_PYTHON_INTERFACE=OFF \
  -DVERBOSE_TTTRLIB=0

make
make install
