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
#include "Histogram.h"
#include "BurstFilter.h"
#include "TiffArrayIO.h"
#include "Correlator.h"
#include "TTTRMask.h"
#include "DataStore.h"
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

// The correlation curve. Both getters are the ARGOUTVIEWM shape, so Java sees
// opaque pointers without these and cannot read a correlation at all.
// The per-event selection mask, one byte per event. ARGOUTVIEW shape, so Java
// sees an opaque pointer without this.
//
// NOT %ARRAY_INTO: that free()s what it copied, and get_mask_array hands back a
// VIEW into a cached snapshot the mask still owns (see the note in
// ext/python/TTTRMask.i). Copy, do not free. Unqualified name because
// TTTRMask.i wraps the class that way -- `%shared_ptr(TTTRMask)`.
%extend TTTRMask {
  int get_mask_array_into(unsigned char* INPLACE_ARRAY1, int DIM1) {
    unsigned char* buf = 0; int n = 0;
    $self->get_mask(&buf, &n);   // get_mask_array is TTTRMask.i's %rename of it
    const int m = (DIM1 < n) ? DIM1 : n;
    for (int i = 0; i < m; ++i) INPLACE_ARRAY1[i] = buf[i];
    return n;
  }
}

%ARRAY_INTO(Correlator, get_x_axis,          get_x_axis_into,          double)
%ARRAY_INTO(Correlator, get_corr_normalized, get_corr_normalized_into, double)

%ARRAY_INTO(TTTR, get_macro_times,     get_macro_times_into,      unsigned long long)
%ARRAY_INTO(TTTR, get_micro_times,     get_micro_times_into,      unsigned short)
%ARRAY_INTO(TTTR, get_routing_channel, get_routing_channels_into, signed char)
%ARRAY_INTO(TTTR, get_event_type,      get_event_types_into,      signed char)

// ── Histogram counts, for Java ─────────────────────────────────────────────
// get_histogram is the ARGOUTVIEWM shape (it malloc()s and hands over
// ownership), so this is %ARRAY_INTO's job -- but Histogram<double> is a
// template and the macro takes a plain class name, so it is stamped by hand on
// the instantiation. Pair it with n_total_bins() to size the array.
%extend Histogram<double> {
  int get_histogram_into(double* INPLACE_ARRAY1, int DIM1) {
    double* buf = 0; int n = 0;
    $self->get_histogram(&buf, &n);
    const int m = (DIM1 < n) ? DIM1 : n;
    for (int i = 0; i < m; ++i) INPLACE_ARRAY1[i] = buf[i];
    if (buf) free(buf);
    return n;
  }
}

// ── TIFF, for Java ─────────────────────────────────────────────────────────
// read_tiff is a free function with an output-pointer block, so neither
// %ARRAY_INTO (which extends a class) nor an ARGOUTVIEW typemap (which Java
// does not have) applies. A free helper is the whole answer: the caller
// preallocates and gets back the true element count, exactly like the %*_INTO
// accessors. The dimensions come back through a second call because a Java
// method has one return; tiff_info() already reports them.
%inline %{
namespace tttrlib {
int tiff_read_f64_into(const std::string& path,
                       double* INPLACE_ARRAY1, int DIM1) {
    double* buf = 0; int d1 = 0, d2 = 0, d3 = 0;
    read_tiff<double>(path, &buf, &d1, &d2, &d3);
    const size_t n = (size_t) d1 * (size_t) d2 * (size_t) d3;
    const size_t m = ((size_t) DIM1 < n) ? (size_t) DIM1 : n;
    for (size_t i = 0; i < m; ++i) INPLACE_ARRAY1[i] = buf[i];
    if (buf) free(buf);
    return (int) n;
}
}  // namespace tttrlib
%}

// ── Column data, for Java (PRD-019) ────────────────────────────────────────
// Column's typed getters have the same "output pointer" shape as the ones
// above, with one difference that matters: a *view* points INTO the column and
// the column keeps owning it. %ARRAY_INTO free()s what it copied, which here
// would hand the allocator a pointer into the middle of a live std::vector.
// Hence a separate macro that copies and does not free.
//
// Without these, a DataStore column is unreadable from Java -- jarrays.i
// defines no ARGOUTVIEW typemaps, because a void-returning method has no
// jresult to assign, so the view getters wrap as opaque SWIGTYPE_p_double.
%define %VIEW_INTO(METHOD, INTONAME, CTYPE)
%extend tttrlib::data::Column {
  int INTONAME(CTYPE* INPLACE_ARRAY1, int DIM1) {
    CTYPE* buf = 0; int n = 0;
    $self->METHOD(&buf, &n);
    const int m = (DIM1 < n) ? DIM1 : n;
    for (int i = 0; i < m; ++i) INPLACE_ARRAY1[i] = buf[i];
    return n;   // the true length, so a caller can detect truncation
  }
}
%enddef

%VIEW_INTO(get_f64_view,   get_f64_into,   double)
%VIEW_INTO(get_f32_view,   get_f32_into,   float)
%VIEW_INTO(get_i64_view,   get_i64_into,   long long)
%VIEW_INTO(get_i32_view,   get_i32_into,   int)
%VIEW_INTO(get_i16_view,   get_i16_into,   short)
%VIEW_INTO(get_i8_view,    get_i8_into,    signed char)
%VIEW_INTO(get_u64_view,   get_u64_into,   unsigned long long)
%VIEW_INTO(get_u32_view,   get_u32_into,   unsigned int)
%VIEW_INTO(get_u16_view,   get_u16_into,   unsigned short)
%VIEW_INTO(get_u8_view,    get_u8_into,    unsigned char)
%VIEW_INTO(get_codes_view, get_codes_into, int)

// ── Multi-dimensional output-array marshalling ─────────────────────────────
// Same idea as %ARRAY_INTO, for the far more common tttrlib shape
//   void CLASS::METHOD(<pre-args>, CTYPE** out, int* d1, ..., int* dN, <post-args>)
// which returns an N-dimensional block flattened row-major. The Java caller
// preallocates a 1-D array of d1*...*dN and gets back the element count.
//
// Most of these getters take extra arguments before and/or after the output
// block, and the SWIG preprocessor cannot splice a comma-containing argument
// list into a call. So the caller passes the COMPLETE call, parenthesised --
// `(...)` keeps the commas inside one macro argument, and a parenthesised
// expression statement is valid C++. Inside the call, use `buf` for the output
// pointer and `d1`..`dN` for the dimensions.
//
// NOTE: the C++ getter allocates the whole block regardless of the array it is
// handed, so an undersized array truncates but saves nothing. The return value
// is always the TRUE element count, so a caller detects truncation by comparing
// it against arr.length.
//
//   %ARRAY_INTO_3D(CLSMImage, get_mean_lifetime_into, double,
//                  SWIG_ARGS(, int minimum_number_of_photons, bool stack_frames),
//                  ($self->get_mean_lifetime($self->get_tttr().get(),
//                                            &buf, &d1, &d2, &d3,
//                                            minimum_number_of_photons, 0,
//                                            1.0, 1.0, stack_frames)))
%define SWIG_ARGS(...) __VA_ARGS__ %enddef

%define %ARRAY_INTO_2D(CLASS, INTONAME, CTYPE, EXTRA_PARAMS, CALL)
%extend CLASS {
  int INTONAME(CTYPE* INPLACE_ARRAY1, int DIM1 EXTRA_PARAMS) {
    CTYPE* buf = 0; int d1 = 0, d2 = 0;
    CALL;
    size_t n = (size_t) d1 * (size_t) d2;
    size_t m = ((size_t) DIM1 < n) ? (size_t) DIM1 : n;
    for (size_t i = 0; i < m; ++i) INPLACE_ARRAY1[i] = buf[i];
    if (buf) free(buf);
    return (int) n;
  }
}
%enddef

%define %ARRAY_INTO_3D(CLASS, INTONAME, CTYPE, EXTRA_PARAMS, CALL)
%extend CLASS {
  int INTONAME(CTYPE* INPLACE_ARRAY1, int DIM1 EXTRA_PARAMS) {
    CTYPE* buf = 0; int d1 = 0, d2 = 0, d3 = 0;
    CALL;
    size_t n = (size_t) d1 * (size_t) d2 * (size_t) d3;
    size_t m = ((size_t) DIM1 < n) ? (size_t) DIM1 : n;
    for (size_t i = 0; i < m; ++i) INPLACE_ARRAY1[i] = buf[i];
    if (buf) free(buf);
    return (int) n;
  }
}
%enddef

%define %ARRAY_INTO_4D(CLASS, INTONAME, CTYPE, EXTRA_PARAMS, CALL)
%extend CLASS {
  int INTONAME(CTYPE* INPLACE_ARRAY1, int DIM1 EXTRA_PARAMS) {
    CTYPE* buf = 0; int d1 = 0, d2 = 0, d3 = 0, d4 = 0;
    CALL;
    size_t n = (size_t) d1 * (size_t) d2 * (size_t) d3 * (size_t) d4;
    size_t m = ((size_t) DIM1 < n) ? (size_t) DIM1 : n;
    for (size_t i = 0; i < m; ++i) INPLACE_ARRAY1[i] = buf[i];
    if (buf) free(buf);
    return (int) n;
  }
}
%enddef

// First real user: the IRF-corrected mean lifetime map, which the plugin's
// "Lifetime Map" command needs. get_mean_micro_time (already wrapped by hand
// above) is only the mean arrival time.
// Burst ranges. find_bursts both computes the bursts and returns them as an
// (n_bursts, 2) block of start/stop indices; Java has no 2-D output typemap, so
// without this the only wrapped form takes three opaque pointers and cannot be
// called at all.
%ARRAY_INTO_2D(tttrlib::BurstFilter, find_bursts_into, long long,
               SWIG_ARGS(),
               ($self->find_bursts(&buf, &d1, &d2)))

%ARRAY_INTO_3D(CLSMImage, get_mean_lifetime_into, double,
               SWIG_ARGS(, int minimum_number_of_photons = 3,
                           bool stack_frames = false),
               ($self->get_mean_lifetime($self->get_tttr().get(),
                                         &buf, &d1, &d2, &d3,
                                         minimum_number_of_photons,
                                         0, 1.0, 1.0, stack_frames)))

// Per-pixel FCS: one correlation curve per pixel, straight from the photon
// stream. Layout is frame-major like the other image getters, with the lag axis
// innermost: value(f, y, x, tau) = arr[((f*n_lines + y)*n_pixel + x)*n_tau + tau].
// n_tau = returned_count / (n_frames*n_lines*n_pixel).
%ARRAY_INTO_4D(CLSMImage, get_fcs_image_into, float,
               SWIG_ARGS(, const std::string& correlation_method = "default",
                           int n_bins = 50, int n_casc = 1,
                           bool stack_frames = false,
                           bool normalized_correlation = false,
                           int min_photons = 2),
               ($self->get_fcs_image(&buf, &d1, &d2, &d3, &d4,
                                     $self->get_tttr(), 0,
                                     correlation_method, n_bins, n_casc,
                                     stack_frames, normalized_correlation,
                                     min_photons)))

%extend CLSMImage {
  // Per-pixel micro-time decays: a 4-D block (frame, line, pixel, tac) flattened
  // row-major, so pixel (f, y, x) occupies
  //   [(((f*n_lines + y)*n_pixel + x) * n_tac) ... + n_tac)
  // and n_tac = returned_count / (n_frames*n_lines*n_pixel).
  //
  // Hand-written rather than macro-stamped because the C++ getter fills
  // unsigned char (counts saturate at 255 per bin) while Java wants int[]; the
  // macro copies through a single CTYPE.
  int get_fluorescence_decay_into(int* INPLACE_ARRAY1, int DIM1,
                                  int micro_time_coarsening = 1,
                                  bool stack_frames = false,
                                  int max_micro_time_channels = -1) {
    unsigned char* buf = 0;
    int d1 = 0, d2 = 0, d3 = 0, d4 = 0;
    $self->get_fluorescence_decay($self->get_tttr().get(), &buf,
                                  &d1, &d2, &d3, &d4,
                                  micro_time_coarsening, stack_frames,
                                  max_micro_time_channels);
    size_t n = (size_t) d1 * (size_t) d2 * (size_t) d3 * (size_t) d4;
    size_t m = ((size_t) DIM1 < n) ? (size_t) DIM1 : n;
    for (size_t i = 0; i < m; ++i) INPLACE_ARRAY1[i] = (int) buf[i];
    if (buf) free(buf);
    return (int) n;
  }

  // Image correlation spectroscopy over this image. compute_ics is static in
  // C++; exposing it as a member that passes $self is the only way it is ever
  // used, and keeps the Java call site free of raw pointers.
  //
  // Hand-written rather than stamped with %ARRAY_INTO_3D: the x/y ranges must be
  // the {0, -1} ("whole image") sentinels, and an EMPTY vector segfaults inside
  // compute_ics. The macro emits the call as a single expression statement and
  // so cannot declare the locals this needs.
  int compute_ics_into(double* INPLACE_ARRAY1, int DIM1,
                       const std::string& subtract_average = "") {
    double* buf = 0;
    int d1 = 0, d2 = 0, d3 = 0;
    std::vector<int> x_range; x_range.push_back(0); x_range.push_back(-1);
    std::vector<int> y_range; y_range.push_back(0); y_range.push_back(-1);
    std::vector<std::pair<int,int> > frame_pairs;
    CLSMImage::compute_ics(&buf, &d1, &d2, &d3,
                           $self->get_tttr(), $self,
                           0, -1, -1, 1,
                           x_range, y_range, frame_pairs,
                           subtract_average);
    size_t n = (size_t) d1 * (size_t) d2 * (size_t) d3;
    size_t m = ((size_t) DIM1 < n) ? (size_t) DIM1 : n;
    for (size_t i = 0; i < m; ++i) INPLACE_ARRAY1[i] = buf[i];
    if (buf) free(buf);
    return (int) n;
  }
}

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
  // Sourced from the 32-bit counter path (get_intensity_u32), so pixels above
  // 65535 photons are neither truncated nor wrapped -- the 16-bit
  // get_intensity() would silently wrap during accumulation.
  // The image must have been filled (construct the CLSMImage with fill = true).
  int get_intensity_into(int* INPLACE_ARRAY1, int DIM1) {
    unsigned int* buf = 0;
    int d1 = 0, d2 = 0, d3 = 0;
    $self->get_intensity_u32(&buf, &d1, &d2, &d3);
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
