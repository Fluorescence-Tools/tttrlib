// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_FDC2D_H
#define TTTRLIB_FDC2D_H

// Fdc2D.h -- the 2D fluorescence-decay correlation (2D-FDC) photon pass.
//
// A 2D-FDC matrix is a **photon-pair histogram over micro-times**: walk a
// macro-time-ordered stream and, for every reference photon, count the photons
// whose macro-time falls in a lag window `dT +/- ddT/2`, binning the pair by the
// two micro-times. `M[a][b]` is then the number of pairs separated by roughly
// `dT` whose earlier photon landed in micro-time bin `a` and later in `b`.
//
// Off-diagonal weight means a molecule changed its decay between the two
// photons, so the lag dependence of the cross-peaks measures the interconversion
// rate. That is the whole method: the matrix is data, and the inversion that
// turns it into lifetimes and rates is the caller's business (Tikhonov, MEM, a
// rate-matrix fit -- all comfortably NumPy/SciPy, and none of them belongs in a
// photon library).
//
// ---------------------------------------------------------------------------
// Why here
// ---------------------------------------------------------------------------
// It touches macro and micro times together and nothing else, which is this
// library's subject, and `TTTR` already carries both. It is also a serial pass
// with a binary search inside -- the shape array languages express badly, and
// the reason the implementation this replaces needed a JIT.
//
// Reference: Toru Kondo (Schlau-Cohen lab, MIT), `TK_Create2DFDC_04.m`.
//
// ---------------------------------------------------------------------------
// The log axis depends on `lint_bin_factor`, and that is the reference's rule
// ---------------------------------------------------------------------------
// `TK_Create2DFDC_04.m` derives the micro-time span as
//
//     t_Imax  = ceil((tMax - tMin)/tStep) + lint_BinFactor
//     lint_Imax = ceil(t_Imax / lint_BinFactor)
//     t_Imax  = lint_Imax * lint_BinFactor        // rounded up to whole bins
//
// and builds the LOG edges from that same `t_Imax`. So the log axis moves when
// the *linear* binning factor changes -- surprising, and load-bearing: the
// lifetime inversion runs on the log axis, so a caller adjusting what reads as
// a resolution knob for the linear matrix shifts the axis underneath it.
//
// `lint_bin_factor` defaults to 1, where the rule collapses to `span + 1` and
// nothing changes for a caller who never binned linearly. Pass the real factor
// to reproduce the reference exactly.
//
// The same `t_Imax` is also the **gate**: line 66 tests `tauI >= t_Imax`, not
// `tauI > tMax`. So the method admits photons above `t_max` when the span is
// not a whole number of linear bins. `fdc_scan_log` and `fdc_log` follow that;
// `fdc_scan_axis` and `fdc_scan_two_axes` take the bound explicitly, because a
// binning artefact is the wrong default for a general kernel.
//
// This library follows the MATLAB, by ruling: where an implementation and the
// paper disagree, the paper wins. The port originally inherited a Python
// implementation's `span + 1` unconditionally, which matches the reference only
// at factor 1.
//
// ---------------------------------------------------------------------------
// Two properties are contracts, not implementation details
// ---------------------------------------------------------------------------
// **The result does not depend on how the stream is partitioned.** Chunking is
// a parallelism decision: each chunk accumulates into a private `int64` matrix
// and the chunks are summed. Integer addition is associative and exact, so
// `n_chunks` changes the runtime and nothing else. A caller that halves its
// chunk count and sees different numbers has found a bug, and there is a test
// that says so.
//
// **A micro-time outside the window is dropped, not clamped.** Clamping would
// pile every out-of-range photon into the edge bin, which is indistinguishable
// from a real feature there. Same for the reference photon: if its micro-time
// is out of range the whole window is skipped.
//
// The log-time axis is the reference's, tick for tick:
// `logt[0] = -1`, then `logt[j] = round(t_imax^(j/(L-1)) - 1)`. It is built in
// integers so a bin edge cannot move with the floating-point mood of the
// machine, and it is exposed (`fdc_log_ticks`) because a caller plotting the
// matrix needs the same axis the counting used.

#include <cstdint>

namespace tttrlib {

/*!
 * \brief The reference's micro-time span, in ticks: what the log edges are
 *        built from, and the size of the linear axis in whole bins.
 *
 * `t_Imax = ceil((span + f) / f) * f`, straight from `TK_Create2DFDC_04.m`.
 * Exposed because a caller assembling its own axis for `fdc_scan_axis` needs
 * the same number the built-in log axis uses, and because a formula only
 * present inside a loop is one nobody can test.
 *
 * \param span_ticks  `t_max - t_min`.
 * \param lint_bin_factor  the reference's `lint_BinFactor`; 1 gives `span + 1`.
 */
long long fdc_t_imax(long long span_ticks, long long lint_bin_factor);

/*!
 * \brief The logarithmic micro-time bin edges, in TCSPC ticks.
 *
 * `out[0] = -1` and `out[j] = round(t_imax^(j/(n_ticks-1)) - 1)`, saturating
 * rather than overflowing. Exposed so a caller can label the axis it is given;
 * `fdc_scan_log` builds the same edges internally from the same inputs.
 *
 * \param t_imax   micro-time span in ticks (`t_max - t_min + 1`).
 * \param out      [n_ticks], written. `n_ticks` is `logt_imax + 1`.
 */
void fdc_log_ticks(long long t_imax, long long* out, int n_out);

/*!
 * \brief The logarithmic bin a micro-time falls in, or -1 if it falls outside.
 *
 * The lookup both matrices are built on, exposed because a bin-edge convention
 * that only exists inside a loop is a convention nobody can test.
 */
int fdc_log_bin(long long tau_ticks, long long* logt_ticks, int n_ticks);

/*!
 * \brief One log-binned 2D-FDC matrix per lag, in a single pass over the stream.
 *
 * Every reference photon visits every lag window once, so the stream is
 * traversed once no matter how many lags are asked for -- which is the reason
 * this is one call and not one call per lag.
 *
 * \param macro_times  [n_macro], ascending. **Rejected if not sorted**: the
 *                     window bounds come from a binary search, and on unsorted
 *                     input that silently returns the wrong photons rather
 *                     than failing.
 * \param micro_times  [n_micro], same length as \p macro_times.
 * \param lags         [n_lags], the lag centres `dT` in macro ticks.
 * \param ddT_ticks    full width of the lag window; `half = ddT_ticks / 2`.
 * \param t_min,t_max  micro-time window in ticks; outside is dropped.
 * \param logt_imax    number of log bins; the internal axis has one more edge.
 * \param n_chunks     parallel chunks. Affects speed only -- see the header.
 * \param out          [n_lags * logt_imax * logt_imax], written (not accumulated).
 * \param lint_bin_factor  the reference's `lint_BinFactor`. It sizes the span
 *                     the log edges are built from (see the header); 1 leaves
 *                     the axis at `span + 1`.
 */
void fdc_scan_log(
        long long* macro_times, int n_macro,
        long long* micro_times, int n_micro,
        long long* lags, int n_lags,
        long long ddT_ticks,
        long long t_min, long long t_max,
        int logt_imax, int n_chunks,
        long long* out, int n_out,
        long long lint_bin_factor = 1);

/*!
 * \brief One matrix per lag on a **caller-supplied** micro-time axis.
 *
 * The general form: `fdc_scan_log` is this with the log axis filled in. The
 * axis is an ascending array of bin edges and the bin is
 * `lower_bound(edges, tau) - 1`, so anything expressible as edges works --
 * including the *linear* binning of the reference implementation, which looks
 * like integer ceiling division but is exactly this lookup with edges
 * `[-1, 0, f, 2f, 3f, ...]`. Checked for every `f` and `tau` rather than
 * assumed: `ceil(tau/f) == lower_bound([-1,0,f,2f,...], tau) - 1`.
 *
 * That is why there is no separate linear kernel. A caller that wants the
 * linear-binned matrix beside the log-binned one calls this twice with two
 * axes; if the second pass over the photons ever costs more than it is worth,
 * the fix is a variant taking several axes at once, not a second copy of the
 * loop.
 *
 * \param ticks  [n_ticks], ascending bin edges. The matrix has `n_ticks - 1`
 *               bins per side, and `out` is sized accordingly.
 * \param t_imax exclusive upper bound on `tau = micro - t_min`, in ticks.
 *               **0 means `t_max - t_min + 1`**, the physical gate.
 *
 * The bound is separate from `t_max` on purpose. The reference gates on its
 * `t_Imax` (`TK_Create2DFDC_04.m:66` tests `tauI >= t_Imax`), and that value
 * rounds the span *up* to whole linear bins — so the published method admits
 * photons **above** `t_max` whenever the span is not a whole number of bins.
 * That is a binning artefact, not a physical gate, and defaulting a general
 * kernel to it would be wrong for every caller not reproducing this paper. A
 * caller who *is* reproducing it passes `fdc_t_imax(t_max - t_min, factor)`;
 * `fdc_scan_log` does exactly that for you.
 *
 * Worth knowing how invisible this is: it changes nothing unless the data
 * reaches past `t_max`, so a fixture of well-behaved streams, or any suite
 * using `lint_bin_factor = 1`, cannot see it. It was found at gate `[15, 25]`
 * with factor 4 (`t_imax = 16` against a span of 10): 972 pairs against 432.
 */
void fdc_scan_axis(
        long long* macro_times, int n_macro,
        long long* micro_times, int n_micro,
        long long* lags, int n_lags,
        long long ddT_ticks,
        long long t_min, long long t_max,
        long long* ticks, int n_ticks,
        int n_chunks,
        long long* out, int n_out,
        long long t_imax = 0);

/*!
 * \brief Two axes, one pass over the photons.
 *
 * The two axes share the photon walk and the window binary search, which is the
 * whole reason to ask for both at once. Measured on 1M photons at comparable bin
 * counts (log 100, linear 101): one axis 144.8 ms, two axes as two separate
 * calls 329.1 ms. The second pass is a real 2.27x, not noise, and it is the
 * common case -- a caller wanting the log matrix for a lifetime inversion
 * usually wants the linearly-binned decay beside it.
 *
 * The saving shrinks as an axis gets finer: at 1501 linear bins the accumulator
 * dominates and the extra pass is a smaller share of a much larger cost. So the
 * win is at coarse-to-comparable binning, which is where callers live.
 *
 * Two and not N: two is what the callers need, and a ragged
 * array-of-axes signature would cost every caller clarity to serve none of
 * them. The internals take a list, so a third axis is a signature away if one
 * ever turns up.
 *
 * A photon an axis cannot place is skipped for that axis only -- one coarse
 * axis does not veto a fine one.
 */
void fdc_scan_two_axes(
        long long* macro_times, int n_macro,
        long long* micro_times, int n_micro,
        long long* lags, int n_lags,
        long long ddT_ticks,
        long long t_min, long long t_max,
        long long* ticks_a, int n_ticks_a,
        long long* ticks_b, int n_ticks_b,
        int n_chunks,
        long long* out_a, int n_out_a,
        long long* out_b, int n_out_b,
        long long t_imax = 0);

/*!
 * \brief The single-lag matrix -- `fdc_scan_log` with one lag, spelled out.
 *
 * Kept because it is what a caller exploring one lag actually wants, and
 * because a scan that agrees with it is a scan whose lag loop is right.
 *
 * \param out  [logt_imax * logt_imax], written.
 */
void fdc_log(
        long long* macro_times, int n_macro,
        long long* micro_times, int n_micro,
        long long dT_ticks, long long ddT_ticks,
        long long t_min, long long t_max,
        int logt_imax, int n_chunks,
        long long* out, int n_out,
        long long lint_bin_factor = 1);

}  // namespace tttrlib

#endif  // TTTRLIB_FDC2D_H
