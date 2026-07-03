// SPDX-License-Identifier: BSD-3-Clause
//
// Java-only convenience additions. Included last by ext/java/tttrlib.i, so
// everything here is Java-specific by construction.
//
// Bulk accessor for the CLSM intensity image. tttrlib's native get_intensity()
// returns through output-pointer parameters, which SWIG-Java cannot marshal to
// an array (a void method has no return to bind). Instead we let the caller
// pass a preallocated int[] which C++ fills in place (jarrays.i marshals
// INPLACE_ARRAY1 with JNI release mode 0, so the writes are copied back).

%{
#include "CLSMImage.h"
#include "TTTR.h"
#include "TTTRHeader.h"
#include "Pda.h"
#include <vector>
#include <set>
%}

%extend Pda {
  // Fill a preallocated double[] (length >= (nmax+1)^2) with the flattened,
  // row-major S1S2 probability matrix (PDA's 2-D histogram), evaluating first if
  // needed. Returns the number of cells written. Java-friendly accessor for the
  // native get_S1S2_matrix output-pointer method.
  int get_S1S2_matrix_into(double* INPLACE_ARRAY1, int DIM1) {
    double* out = 0; int n1 = 0, n2 = 0;
    $self->get_S1S2_matrix(&out, &n1, &n2);
    int n = n1 * n2;
    int m = (DIM1 < n) ? DIM1 : n;
    for (int i = 0; i < m; ++i) INPLACE_ARRAY1[i] = out[i];
    if (out) free(out);
    return n;
  }

  // Fill a preallocated double[] (length >= n_bins) with the y-values of the PDA
  // 1-D histogram (over the current S1S2 model). Returns the number of bins.
  // Java-friendly accessor for the native get_1dhistogram output-pointer method.
  int get_1dhistogram_y_into(double* INPLACE_ARRAY1, int DIM1,
                             double x_max = 1000.0, double x_min = 0.01,
                             int n_bins = 81, bool log_x = true) {
    double* hx = 0; int nx = 0; double* hy = 0; int ny = 0;
    $self->get_1dhistogram(&hx, &nx, &hy, &ny, x_max, x_min, n_bins, log_x);
    int m = (DIM1 < ny) ? DIM1 : ny;
    for (int i = 0; i < m; ++i) INPLACE_ARRAY1[i] = hy[i];
    if (hx) free(hx);
    if (hy) free(hy);
    return ny;
  }
}

// ── PRD-002: generalized 1-D output-array marshalling for Java ──────────────
// SWIG-Java cannot bind a void "output-pointer" getter
//   void CLASS::METHOD(CTYPE** out, int* n)
// to an array return (a void method has no jresult). This macro stamps an
// INPLACE-fill accessor instead:
//   int INTONAME(CTYPE[] arr)   // caller preallocates arr;
//                               // returns the number of elements available
//                               // (values past arr.length are dropped).
// The C++ getter allocates and transfers ownership, so we copy then free().
// This replaces per-method hand-written wrappers for the common ARGOUTVIEWM case.
%define %ARRAY_INTO(CLASS, METHOD, INTONAME, CTYPE)
%extend CLASS {
  int INTONAME(CTYPE* INPLACE_ARRAY1, int DIM1) {
    CTYPE* buf = 0; int n = 0;
    $self->METHOD(&buf, &n);
    int m = (DIM1 < n) ? DIM1 : n;
    for (int i = 0; i < m; ++i) INPLACE_ARRAY1[i] = buf[i];
    if (buf) free(buf);
    return n;
  }
}
%enddef

%ARRAY_INTO(TTTR, get_macro_times,     get_macro_times_into,      unsigned long long)
%ARRAY_INTO(TTTR, get_micro_times,     get_micro_times_into,      unsigned short)
%ARRAY_INTO(TTTR, get_routing_channel, get_routing_channels_into, signed char)
%ARRAY_INTO(TTTR, get_event_type,      get_event_types_into,      signed char)

%extend CLSMImage {
  // Decay-from-mask: return the micro-time histogram of photons in the masked
  // pixels (frame-major uint8 mask, size n_frames*n_lines*n_pixel, nonzero =
  // selected; frames stacked). With `channels` non-empty this returns one decay
  // per routing channel, vstacked as [channel0 bins..., channel1 bins..., ...]
  // (length = channels.size() * n_tac, so n_tac = length / n_channels); empty
  // returns the single combined decay. Wraps CLSMImage::get_decay_of_pixels.
  std::vector<int> get_decay_of_pixels_masked(unsigned char* IN_ARRAY1, int DIM1,
                                              int dmask1, int dmask2, int dmask3,
                                              int tac_coarsening = 1,
                                              const std::vector<int>& channels = std::vector<int>()) {
    unsigned int* out = 0; int d1 = 0, d2 = 0;
    std::vector<int> chv(channels);
    $self->get_decay_of_pixels($self->get_tttr().get(),
        IN_ARRAY1, dmask1, dmask2, dmask3,
        &out, &d1, &d2, tac_coarsening, /*stack_frames=*/true, chv);
    int n = ((d1 > 0) ? d1 : 1) * d2;
    std::vector<int> v(n > 0 ? n : 0);
    for (int i = 0; i < n; ++i) v[i] = (int) out[i];
    if (out) free(out);
    return v;
  }
}

%extend TTTR {
  // Fill a preallocated int[] (preallocate >= 256) with the distinct routing
  // channel numbers present in the data (ascending); returns how many there are.
  int get_used_routing_channels_into(int* INPLACE_ARRAY1, int DIM1) {
    std::set<int> used;
    size_t n = (size_t) $self->n_valid_events;
    for (size_t i = 0; i < n; ++i) used.insert((int) $self->get_routing_channel_at(i));
    int i = 0;
    for (std::set<int>::const_iterator it = used.begin(); it != used.end(); ++it, ++i)
      if (i < DIM1) INPLACE_ARRAY1[i] = *it;
    return (int) used.size();
  }

  // Fill a preallocated double[] with the aggregate micro-time decay histogram
  // (photon counts per micro-time channel) for the given routing channels (empty
  // = all). Preallocate DIM1 >= get_number_of_micro_time_channels(); returns the
  // number of bins written. Pair with get_micro_time_resolution_s() to build a
  // time axis (bin * coarsening * resolution). This is the "decay export".
  int get_microtime_histogram_into(double* INPLACE_ARRAY1, int DIM1,
                                   const std::vector<int>& channels = std::vector<int>(),
                                   int micro_time_coarsening = 1,
                                   int minlength = -1) {
    double* hist = 0; int nh = 0; double* tax = 0; int nt = 0;
    $self->get_microtime_histogram(&hist, &nh, &tax, &nt,
                                   (unsigned short) micro_time_coarsening, channels,
                                   minlength);
    int m = (DIM1 < nh) ? DIM1 : nh;
    for (int i = 0; i < m; ++i) INPLACE_ARRAY1[i] = hist[i];
    if (hist) free(hist);
    if (tax) free(tax);
    return nh;
  }

  // Micro-time resolution in seconds (header value); convenience for building a
  // decay time axis without walking the shared_ptr<TTTRHeader> chain from Java.
  double get_micro_time_resolution_s() {
    return $self->get_header()->get_micro_time_resolution();
  }
}

%extend CLSMImage {
  // PIE gating: (re)fill this image using only photons whose micro time falls in
  // the inclusive range [mt_start, mt_stop) on the given routing channels
  // (clearing any previous fill). Pass mt_stop <= mt_start to fill with no
  // micro-time gate (all arrival times). Construct the image with fill=false,
  // then call this once per prompt/delayed window.
  void fill_micro_time_range(const std::vector<int>& channels,
                             int mt_start = 0, int mt_stop = 0) {
    std::vector<std::pair<int,int> > ranges;
    if (mt_stop > mt_start) ranges.push_back(std::make_pair(mt_start, mt_stop));
    $self->fill($self->get_tttr(), channels, /*clear=*/true, ranges);
  }

  // Fill a preallocated int[] (length >= n_frames*n_lines*n_pixel) with the
  // frame-major intensity image and return the number of elements written:
  //   value(frame, line, pixel) = arr[(frame*n_lines + line)*n_pixel + pixel]
  // Counts are widened from unsigned short so the full 0..65535 range survives.
  // The image must have been filled (construct the CLSMImage with fill = true).
  int get_intensity_into(int* INPLACE_ARRAY1, int DIM1) {
    unsigned short* buf = 0;
    int d1 = 0, d2 = 0, d3 = 0;
    $self->get_intensity(&buf, &d1, &d2, &d3);
    size_t n = (size_t) d1 * (size_t) d2 * (size_t) d3;
    size_t m = ((size_t) DIM1 < n) ? (size_t) DIM1 : n;
    for (size_t i = 0; i < m; ++i) INPLACE_ARRAY1[i] = (int) buf[i];
    if (buf) free(buf);
    return (int) n;
  }

  // Fill a preallocated double[] (length >= n_frames*n_lines*n_pixel) with the
  // frame-major mean photon arrival time per pixel (the "FastLifetime" image, in
  // the TTTR micro-time unit). Same layout as get_intensity_into. The IRF-offset
  // correction (decay rise) is done in C++ (CLSMImage::get_mean_micro_time) so
  // all language bindings share it; here we just forward the flag.
  int get_mean_micro_time_into(double* INPLACE_ARRAY1, int DIM1,
                               double microtime_resolution = -1.0,
                               int minimum_number_of_photons = 2,
                               bool correct_irf_offset = false,
                               bool stack_frames = false) {
    double* buf = 0;
    int d1 = 0, d2 = 0, d3 = 0;
    $self->get_mean_micro_time($self->get_tttr().get(), &buf, &d1, &d2, &d3,
                               microtime_resolution, minimum_number_of_photons,
                               stack_frames, correct_irf_offset);
    size_t n = (size_t) d1 * (size_t) d2 * (size_t) d3;
    size_t m = ((size_t) DIM1 < n) ? (size_t) DIM1 : n;
    for (size_t i = 0; i < m; ++i) INPLACE_ARRAY1[i] = buf[i];
    if (buf) free(buf);
    return (int) n;
  }

  // Fill a preallocated float[] (length >= n_frames*n_lines*n_pixel*2) with the
  // interleaved phasor coordinates per pixel: [..., g(cos), s(sin), ...] in the
  // same frame-major pixel order as get_intensity_into. Returns the element
  // count written (= n_frames*n_lines*n_pixel*2).
  int get_phasor_into(float* INPLACE_ARRAY1, int DIM1,
                      double frequency = -1.0, int minimum_number_of_photons = 2,
                      bool correct_irf_offset = false, bool stack_frames = false) {
    float* buf = 0;
    int d1 = 0, d2 = 0, d3 = 0, d4 = 0;
    $self->get_phasor(&buf, &d1, &d2, &d3, &d4, $self->get_tttr().get(),
                      /*tttr_irf=*/0, frequency, minimum_number_of_photons,
                      stack_frames, correct_irf_offset);
    size_t n = (size_t) d1 * (size_t) d2 * (size_t) d3 * (size_t) d4;
    size_t m = ((size_t) DIM1 < n) ? (size_t) DIM1 : n;
    for (size_t i = 0; i < m; ++i) INPLACE_ARRAY1[i] = buf[i];
    if (buf) free(buf);
    return (int) n;
  }
}
