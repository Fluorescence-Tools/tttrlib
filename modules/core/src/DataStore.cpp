// SPDX-License-Identifier: BSD-3-Clause
//
// The one translation unit in tttrlib that compiles ptolib's implementation:
// the DataStore, the expression engine, the .dstore file and the PTO
// container. Every other file includes the header and links this through
// libtttrlib_core. A second PTOLIB_IMPLEMENTATION anywhere in the tree would be
// a duplicate-symbol error at link time, which is the intended alarm.
#define PTOLIB_IMPLEMENTATION
#include "ptolib/ptolib.h"
