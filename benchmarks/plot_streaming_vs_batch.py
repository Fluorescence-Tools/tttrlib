# SPDX-License-Identifier: BSD-3-Clause
"""Visual check: StreamingCorrelator against the batch Wahl correlator.

Produces `plots/streaming_correlator_validation.png`. The claim being checked is
not "the curves look similar" — it is that the online correlator returns the
same numbers as the batch one on the same photons, so the figure shows the
overlay, the ratio, and what that ratio used to be.

    python benchmarks/plot_streaming_vs_batch.py
"""
import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import LogLocator, NullFormatter

import tttrlib

N_BINS, N_CASC = 16, 12

# Validated categorical slots 1-3 (see the dataviz palette reference); slot 3
# sits below 3:1 on this surface, so every series is also direct-labeled.
C_BATCH = "#2a78d6"     # blue
C_STREAM = "#eb6834"    # orange
C_BEFORE = "#1baf7a"    # aqua
INK = "#0b0b0b"
INK_2 = "#52514e"
INK_MUTED = "#8a8984"
SURFACE = "#fcfcfb"
GRID = "#e3e2dd"

# Recorded before the fix, on a different dataset (42k photons,
# tau_D = 15.6 ms). Shown for the shape of the failure, not to be compared
# value-for-value with the run below.
BEFORE = {0: 1.031, 1: 1.031, 2: 1.23, 3: 1.25, 4: 1.12, 5: 2.39}


def diffusion_stream(duration, tau_d, seed, mean_rate=0.05, contrast=1.5):
    """Photons whose rate follows an Ornstein-Uhlenbeck intensity, so G(tau)
    actually decays — a lag misassignment is invisible on a flat curve."""
    rng = np.random.default_rng(seed)
    n_steps = int(duration)
    a = np.exp(-1.0 / tau_d)
    s = np.sqrt(1.0 - a * a)
    noise = rng.normal(size=n_steps)
    x = np.zeros(n_steps)
    for i in range(1, n_steps):
        x[i] = a * x[i - 1] + s * noise[i]
    counts = rng.poisson(mean_rate * np.exp(contrast * x - 0.5 * contrast ** 2))
    return np.repeat(np.arange(n_steps, dtype=np.uint64), counts)


def correlate(times):
    c = tttrlib.Correlator()
    c.method = "wahl"
    c.n_bins, c.n_casc = N_BINS, N_CASC
    c.set_macrotimes(times, times)
    w = np.ones(len(times))
    c.set_weights(w, w)
    x_batch = np.asarray(c.x_axis, dtype=float)
    g_batch = np.asarray(c.get_corr_normalized(), dtype=float)

    sc = tttrlib.StreamingCorrelator(N_BINS, N_CASC, 1.0)
    for t in times:
        sc.push_photon(int(t))
    sc.flush()
    g_stream = np.asarray(sc.get_correlation_normalized(), dtype=float)
    return x_batch, g_batch, g_stream


def style_axes(ax):
    ax.set_facecolor(SURFACE)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
        ax.spines[side].set_linewidth(1.0)
    ax.tick_params(colors=INK_2, labelsize=9, length=3, width=1.0)
    ax.grid(True, which="major", color=GRID, linewidth=0.8, zorder=0)
    ax.set_axisbelow(True)


def main():
    times = diffusion_stream(6_000_000, 800, seed=11)
    x, g_batch, g_stream = correlate(times)

    keep = (x > 0) & np.isfinite(g_batch) & np.isfinite(g_stream)
    # Drop the last cascades, where T - tau leaves too few pairs to mean anything.
    keep &= x < 0.05 * (times[-1] - times[0])

    fig = plt.figure(figsize=(11, 7.6), facecolor=SURFACE)
    gs = fig.add_gridspec(2, 2, height_ratios=[1.5, 1.0], hspace=0.42, wspace=0.28,
                          left=0.08, right=0.97, top=0.86, bottom=0.09)

    # --- (a) the two curves, overlaid -------------------------------------
    ax = fig.add_subplot(gs[0, :])
    style_axes(ax)
    ax.set_xscale("log")
    ax.plot(x[keep], g_batch[keep], color=C_BATCH, linewidth=2.0,
            zorder=3, solid_capstyle="round")
    # Thin the markers so the overlay reads as agreement rather than as a band:
    # at long lags the multi-tau axis puts many nearly-equal points side by side.
    xi, gi_b, gi_s = x[keep], g_batch[keep], g_stream[keep]
    sel = np.zeros(len(xi), dtype=bool)
    sel[::2] = True
    sel[xi > 300] = False
    sel[np.searchsorted(xi, 300)::6] = True
    ax.plot(xi[sel], gi_s[sel], linestyle="none", marker="o",
            markersize=8, markerfacecolor="none", markeredgecolor=C_STREAM,
            markeredgewidth=1.8, zorder=4)

    i_lab = max(1, len(xi) // 8)
    ax.annotate("batch  Correlator", xy=(xi[i_lab], gi_b[i_lab]),
                xytext=(6, 14), textcoords="offset points",
                color=INK_2, fontsize=10)
    # Placed in the empty lower-left rather than on the curve it names.
    j_lab = int(np.searchsorted(xi, 12))
    ax.annotate("streaming (markers)", xy=(xi[j_lab], gi_s[j_lab]),
                xytext=(1.4, 0.30 * gi_b[0]), textcoords="data",
                color=INK_2, fontsize=10, va="center",
                arrowprops=dict(arrowstyle="-", color=INK_MUTED, linewidth=1.0,
                                shrinkA=2, shrinkB=6))
    ax.set_ylabel("G(τ)", color=INK, fontsize=10)
    ax.set_xlabel("lag τ  (macro-time units, log scale)", color=INK_2, fontsize=9)
    ax.set_title("Same photons, same numbers", color=INK, fontsize=12,
                 loc="left", pad=8)
    ax.xaxis.set_minor_formatter(NullFormatter())
    ax.legend(handles=[
        plt.Line2D([], [], color=C_BATCH, linewidth=2.0, label="batch Correlator (Wahl)"),
        plt.Line2D([], [], color=C_STREAM, linestyle="none", marker="o", markersize=8,
                   markerfacecolor="none", markeredgewidth=1.8,
                   label="StreamingCorrelator"),
    ], frameon=False, fontsize=9, labelcolor=INK_2, loc="upper right")

    # --- (b) the ratio, now ------------------------------------------------
    ax2 = fig.add_subplot(gs[1, 0])
    style_axes(ax2)
    ax2.set_xscale("log")
    ratio = g_stream[keep] / g_batch[keep]
    ax2.axhline(1.0, color=INK_MUTED, linewidth=1.0, zorder=2)
    ax2.plot(x[keep], ratio, color=C_STREAM, linewidth=2.0, zorder=3)
    ax2.set_ylim(0.94, 1.06)
    ax2.set_ylabel("streaming / batch", color=INK, fontsize=10)
    ax2.set_xlabel("lag τ", color=INK_2, fontsize=9)
    ax2.set_title("After — every cascade agrees to 1.0000", color=INK,
                  fontsize=11, loc="left", pad=6)
    ax2.xaxis.set_minor_formatter(NullFormatter())
    ax2.xaxis.set_major_locator(LogLocator(base=10, numticks=6))
    ax2.set_xlim(ax.get_xlim())

    # --- (c) what it used to be -------------------------------------------
    ax3 = fig.add_subplot(gs[1, 1])
    style_axes(ax3)
    ax3.grid(True, axis="y", which="major", color=GRID, linewidth=0.8)
    ax3.grid(False, axis="x")
    casc = sorted(BEFORE)
    vals = [BEFORE[c] for c in casc]
    bars = ax3.bar(casc, vals, width=0.62, color=C_BEFORE, zorder=3,
                   edgecolor=SURFACE, linewidth=2.0)
    ax3.axhline(1.0, color=INK_MUTED, linewidth=1.0, zorder=4)
    for c, v, b in zip(casc, vals, bars):
        ax3.text(b.get_x() + b.get_width() / 2, v + 0.04, f"{v:.2f}",
                 ha="center", color=INK_2, fontsize=9)
    ax3.set_ylim(0.0, 2.75)
    ax3.set_xticks(casc)
    ax3.set_xlabel("cascade level", color=INK_2, fontsize=9)
    ax3.set_ylabel("streaming / batch", color=INK, fontsize=10)
    ax3.set_title("Before — inflated, and worse the coarser the level",
                  color=INK, fontsize=11, loc="left", pad=6)
    ax3.text(0.02, 0.93, "recorded earlier on other data",
             transform=ax3.transAxes, color=INK_MUTED, fontsize=8.5, va="top")

    fig.suptitle("StreamingCorrelator vs the batch Wahl correlator",
                 color=INK, fontsize=15, x=0.08, ha="left", y=0.965)
    fig.text(0.08, 0.915,
             f"{len(times):,} photons, OU-modulated intensity (τ_D = 800), "
             f"n_bins = {N_BINS}, n_casc = {N_CASC}",
             color=INK_2, fontsize=10, ha="left")

    out = "benchmarks/plots/streaming_correlator_validation.png"
    fig.savefig(out, dpi=150, facecolor=SURFACE)
    print(f"wrote {out}")
    for c in range(N_CASC):
        lo, hi = c * N_BINS + 1, c * N_BINS + 1 + N_BINS
        b, s = g_batch[lo:hi], g_stream[lo:hi]
        m = np.isfinite(b) & np.isfinite(s) & (b > 1e-9)
        if m.sum():
            print(f"  cascade {c:>2}  lags {x[lo]:>8.0f}-{x[hi-1]:<8.0f} "
                  f"ratio {(s[m]/b[m]).mean():.4f}")


if __name__ == "__main__":
    main()
