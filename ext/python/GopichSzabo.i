// SPDX-License-Identifier: BSD-3-Clause
%{
#include "GopichSzabo.h"
#include "CtmcKinetics.h"
%}

namespace tttrlib {};

// `log_likelihood` and `viterbi` cross as array buffers, not as VectorDouble /
// VectorInt32.
//
// SWIG's default `std::vector` typemaps convert through the host language's
// sequence protocol -- one boxed number per element, ~50 ns each, in and out.
// These two are called *per fit iteration* over a photon stream, so that
// conversion sets the runtime of a burst analysis and the compiled likelihood
// is invisible behind it. See okf/bindings/marshalling-cost.md.
//
// These typemap names are not Python-only: ext/r/rarrays.i, ext/java/jarrays.i
// and ext/js/jsarrays.i implement the same IN_ARRAY / ARGOUTVIEWM names against
// R vectors, Java arrays and JS TypedArrays, so one %apply serves all four
// bindings. (`GopichSzabo.i` reaches r/js today and not java -- see
// tools/binding_parity_exceptions.txt.)
%apply (double* IN_ARRAY1, int DIM1) {(double* times, int n_times)}
%apply (int* IN_ARRAY1, int DIM1) {(int* colors, int n_colors_in)}
%apply (long long* IN_ARRAY1, int DIM1) {(long long* offsets, int n_offsets)}
%apply (int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(int** out, int* n_out)}

// `offsets` is optional and means "one burst spanning everything". A NumPy
// IN_ARRAY1 pair cannot simply be left off, so give the pair a `default`
// typemap: SWIG then treats it as optional and runs the `in` typemap only when
// it is actually supplied. Same device as MaxEntTcspc.i's `prior`.
%typemap(default) (long long* offsets, int n_offsets) {
    $1 = nullptr;
    $2 = 0;
}

// The std::vector overloads stay as the C++ surface and are hidden from the
// bindings; the flat ones take their names, so `viterbi(times, colors)` and
// `viterbi(times, colors, offsets)` both still work and now cost a memcpy.
%ignore tttrlib::GopichSzabo::log_likelihood;
%ignore tttrlib::GopichSzabo::viterbi;
%rename(log_likelihood) tttrlib::GopichSzabo::log_likelihood_flat;
%rename(viterbi) tttrlib::GopichSzabo::viterbi_flat;

// Long serial recursions over a photon stream, touching no host-language object.
TTTRLIB_NOGIL(tttrlib::GopichSzabo::log_likelihood_flat)
TTTRLIB_NOGIL(tttrlib::GopichSzabo::viterbi_flat)

%include "GopichSzabo.h"
%include "CtmcKinetics.h"

%clear (double* times, int n_times);
%clear (int* colors, int n_colors_in);
%clear (long long* offsets, int n_offsets);
%clear (int** out, int* n_out);
