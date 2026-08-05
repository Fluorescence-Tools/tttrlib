// SPDX-License-Identifier: BSD-3-Clause
%module tttrlib
%{
#include "FileIO.h"
#include "FileCheck.h"
%}

// Both, and in this order. swig does not follow #include, so the file-open and
// encoding helpers -- which are wrapped API -- stop being generated the moment
// their declarations move to another header, however correctly FileCheck.h
// includes it.
%include "FileIO.h"
%include "FileCheck.h"
