// SPDX-License-Identifier: BSD-3-Clause
// SWIG bindings for the additive photon-simulation subsystem (PRD-005).
// All classes are Sim-prefixed and live in namespace tttrlib.

%{
#include "SimRandom.h"
#include "SimDecay.h"
#include "SimSpecies.h"
#include "SimGrid.h"
#include "SimSample.h"
#include "SimScanner.h"
#include "SimSettings.h"
#include "SimMicrotimeEncoder.h"
#include "SimEngine.h"
%}

%include "stdint.i"

// Extra std::vector instantiations not already provided by misc_types.i.
%template(VectorUint8)  std::vector<unsigned char>;
%template(VectorUint16) std::vector<unsigned short>;
%template(VectorInt8)   std::vector<signed char>;

// Hide raw-pointer overloads; Python uses the vector/convenience forms instead.
%ignore tttrlib::SimMicrotimeEncoder::encode;                 // use SimEngine.encode(...)
%ignore tttrlib::SimSample::set_positions;                    // raw-pointer form
%ignore tttrlib::SimSample::set_emitter_grid(const int*, int, int, int, int,
                                             double, double, double, double, double, double);

%include "SimRandom.h"

%include "SimDecay.h"
%template(VectorSimDecay) std::vector<tttrlib::SimDecay>;

%include "SimSpecies.h"
%template(VectorSimSpecies) std::vector<tttrlib::SimSpecies>;

%include "SimGrid.h"
%template(VectorSimGrid) std::vector<tttrlib::SimGrid>;

%include "SimSample.h"
%include "SimScanner.h"
%include "SimSettings.h"
%include "SimMicrotimeEncoder.h"

%newobject tttrlib::SimEngine::from_json;   // Python owns the returned engine
%include "SimEngine.h"
