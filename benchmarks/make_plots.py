#!/usr/bin/env python
"""Render benchmark comparison charts from results/*.jsonl into plots/*.png.

White background so the PNGs read on both GitHub light and dark themes.
tttrlib is drawn in the accent blue, every competitor in neutral gray.
"""
import glob
import json
import os
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
PLOTS = os.path.join(HERE, "plots")
os.makedirs(PLOTS, exist_ok=True)

ACCENT = "#2a78d6"      # tttrlib
ACCENT_DK = "#1b5aa8"   # tttrlib secondary (batch/second variant)
GRAY = "#8a8a86"
GRAY_LT = "#bcbcb8"
INK = "#0b0b0b"
INK2 = "#52514e"

plt.rcParams.update({
    "figure.facecolor": "white", "axes.facecolor": "white",
    "font.size": 11, "axes.edgecolor": "#cfcfca",
    "axes.grid": True, "grid.color": "#ececea", "grid.linewidth": 0.8,
    "axes.axisbelow": True, "svg.fonttype": "none",
})

TITLES = {
    "file_read": "TTTR file reading  (PicoQuant HydraHarp T3 PTU, 3.5 M photons)",
    "clsm_intensity": "CLSM intensity image construction  (512x512 confocal PTU)",
    "lifetime_image": "Per-pixel FLIM lifetime image  (256x256, 65k pixels)",
    "fit_curve": "Single-curve lifetime fit  (256-bin decay, single detector)",
    "burst_search": "Single-molecule burst search  (3.5 M photons)",
    "correlation": "Correlation / FCS  (3.5 M photons)",
    "simulation": "Single-molecule diffusion + photon simulation  (20 molecules, 1 s)",
    "h2mm": "Photon-by-photon HMM (H2MM) — Baum-Welch EM  (3-state, 200k photons)",
}
XLABEL = {
    "file_read": "photons / second", "clsm_intensity": "pixels / second",
    "lifetime_image": "pixels / second", "fit_curve": "fits / second",
    "burst_search": "photons / second", "correlation": "photons / second",
    "simulation": "diffusion steps / second", "h2mm": "photons / second",
}


def load():
    # keep the best (highest-throughput / lowest-time) row per (category, tool)
    best = {}
    for f in glob.glob(os.path.join(HERE, "results", "*.jsonl")):
        for line in open(f):
            r = json.loads(line)
            key = (r["category"], r["tool"])
            cur = best.get(key)
            if cur is None or (r.get("throughput") or 0) > (cur.get("throughput") or 0):
                best[key] = r
    by = defaultdict(list)
    for r in best.values():
        by[r["category"]].append(r)
    return by


def human(v):
    for u, s in [(1e9, "G"), (1e6, "M"), (1e3, "k")]:
        if v >= u:
            return f"{v/u:.1f}{s}"
    return f"{v:.0f}"


def color_for(tool):
    if not tool.lower().startswith("tttrlib"):
        return GRAY
    return ACCENT_DK if ("fit_many" in tool or "batch" in tool) else ACCENT


def bar_chart(cat, rows):
    rows = [r for r in rows if r.get("throughput")]
    rows.sort(key=lambda r: r["throughput"])
    tools = [r["tool"] for r in rows]
    vals = [r["throughput"] for r in rows]
    colors = [color_for(t) for t in tools]

    fig, ax = plt.subplots(figsize=(9.2, 0.62 * len(rows) + 1.7))
    y = range(len(rows))
    ax.barh(list(y), vals, color=colors, height=0.66, zorder=3)
    ax.set_yticks(list(y))
    ax.set_yticklabels(tools, fontsize=10.5)
    ax.set_xscale("log")
    ax.set_xlabel(XLABEL.get(cat, "throughput"), color=INK2)
    ax.set_title(TITLES.get(cat, cat), fontsize=12.5, color=INK, pad=12, loc="left",
                 fontweight="bold")

    tmax = max(vals)
    for yi, r in zip(y, rows):
        v = r["throughput"]
        speed = f"  {human(v)}/s   ({v/tmax*100:.0f}% of fastest)" if v < tmax \
            else f"  {human(v)}/s   ← fastest"
        ax.text(v * 1.05, yi, speed, va="center", ha="left", fontsize=9.5,
                color=INK2, zorder=4)
    ax.set_xlim(min(vals) * 0.4, tmax * 6)
    for s in ("top", "right", "left"):
        ax.spines[s].set_visible(False)
    ax.tick_params(length=0)
    fig.tight_layout()
    out = os.path.join(PLOTS, f"{cat}.png")
    fig.savefig(out, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print("wrote", out)


# Algorithm-matched head-to-heads for the summary (honest same-method pairs).
SUMMARY = [
    ("File reading (PTU)", "file_read", "tttrlib", "ptufile"),
    ("Fast lifetime map", "lifetime_image", "tttrlib (moments)", "flimlib (RLD)"),
    ("CLSM intensity image", "clsm_intensity", "tttrlib", "ptufile"),
    ("Per-pixel reconv. MLE (vs GPU)", "lifetime_image", "tttrlib (MLE fit_map)", "FLIMKit (per-pixel, GPU)"),
    ("Diffusion simulation", "simulation", "tttrlib (SimEngine, coasting)", "PyBroMo"),
    ("Burst search", "burst_search", "tttrlib", "FRETBursts"),
    ("Single-curve MLE fit", "fit_curve", "tttrlib (FitNExp MLE)", "flimlib (LMA)"),
    ("H2MM Baum-Welch (vs C ref)", "h2mm", "tttrlib (SQUAREM)", "H2MM_C"),
    ("Correlation / FCS", "correlation", "tttrlib", "pycorrelate"),
]


def summary_chart(by):
    def get(cat, tool):
        for r in by.get(cat, []):
            if r["tool"] == tool:
                return r["throughput"]
        return None

    labels, ratios, comp_names = [], [], []
    for label, cat, ttool, ctool in SUMMARY:
        t, c = get(cat, ttool), get(cat, ctool)
        if t and c:
            labels.append(label)
            ratios.append(t / c)
            comp_names.append(ctool)
    order = sorted(range(len(ratios)), key=lambda i: ratios[i])
    labels = [labels[i] for i in order]
    ratios = [ratios[i] for i in order]
    comp_names = [comp_names[i] for i in order]

    fig, ax = plt.subplots(figsize=(9.6, 0.62 * len(labels) + 1.9))
    y = range(len(labels))
    colors = [GRAY if r < 1 else ACCENT for r in ratios]
    ax.barh(list(y), ratios, color=colors, height=0.66, zorder=3)
    ax.axvline(1.0, color="#e34948", lw=1.4, zorder=2)
    ax.text(1.0, len(labels) - 0.35, " parity", color="#e34948", fontsize=9, va="bottom")
    ax.set_yticks(list(y))
    ax.set_yticklabels(labels, fontsize=10.5)
    ax.set_xscale("log")
    ax.set_xlabel("tttrlib speed relative to competitor  (×, log scale)", color=INK2)
    ax.set_title("How much faster is tttrlib on CPU?  (same data; incl. vs a GPU competitor)",
                 fontsize=12.5, color=INK, pad=12, loc="left", fontweight="bold")
    for yi, r, c in zip(y, ratios, comp_names):
        txt = f"  {r:.1f}× vs {c}" if r >= 1 else f"  {r:.2f}× vs {c} (slower)"
        ax.text(r * 1.06, yi, txt, va="center", ha="left", fontsize=9.5, color=INK2)
    ax.set_xlim(0.4, max(ratios) * 6)
    for s in ("top", "right", "left"):
        ax.spines[s].set_visible(False)
    ax.tick_params(length=0)
    fig.tight_layout()
    out = os.path.join(PLOTS, "summary_speedup.png")
    fig.savefig(out, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print("wrote", out)


FAST_METHODS = {"method-of-moments", "rapid-lifetime-determination", "phasor"}


def main():
    by = load()
    for cat, rows in by.items():
        if cat == "lifetime_image":
            fast = [r for r in rows if r.get("extra", {}).get("method") in FAST_METHODS]
            mle = [r for r in rows if r.get("extra", {}).get("method") not in FAST_METHODS]
            _titled(fast, "lifetime_fast",
                    "Fast lifetime map  (moments / RLD / phasor, 65k pixels)")
            _titled(mle, "lifetime_mle",
                    "Per-pixel reconvolution-MLE lifetime map  (65k pixels)")
            continue
        if cat == "fit_curve":
            # RLD is a rough no-reconvolution estimator, not a comparable MLE fit;
            # keep the single-curve chart to reconvolution fitters only.
            rows = [r for r in rows if "RLD" not in r["tool"]]
        if len([r for r in rows if r.get("throughput")]) >= 2:
            bar_chart(cat, rows)
    summary_chart(by)


def _titled(rows, name, title):
    TITLES[name] = title
    XLABEL[name] = "pixels / second"
    if len([r for r in rows if r.get("throughput")]) >= 1:
        bar_chart(name, rows)


if __name__ == "__main__":
    main()
