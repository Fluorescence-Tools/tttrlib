// SPDX-License-Identifier: BSD-3-Clause
//
// The Skilling-Bryan MEM engine is HEADER-ONLY since 2026-09-02: every body
// lives in include/MaxEntQp.h, so a consumer holding only the header (imp.bff
// vendors tttrlib headers byte-identically rather than linking) runs this
// engine and not a second one of its own. This file stays as the pointer --
// and so the module's source list needs no edit -- but deliberately holds no
// code: a body added here and not in the header would fork the engine for
// exactly the consumers the move exists to serve.
#include "MaxEntQp.h"
