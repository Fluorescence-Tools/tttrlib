%{
#include "BurstSignificance.h"
%}

// Exact low-count detection statistics, shared by the max-tree and Bayesian
// Blocks burst searches. Exposed because they are useful on their own -- anyone
// scoring their own burst candidates needs the same arithmetic -- and because
// exposing them is what makes the math testable from the Python test suite.
//
// The header is inline-only, so SWIG generates thin wrappers with no library
// symbols to link. Only the user-facing functions are wrapped; log_gamma_p() is
// an implementation detail of the Poisson tail and is deliberately left out.
%include "BurstSignificance.h"
