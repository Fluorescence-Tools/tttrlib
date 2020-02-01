#!/bin/bash
rm -r -f build
$PYTHON setup.py install --single-version-externally-managed --record=record.txt