#!/usr/bin/env python
"""Render cross-version (perf + peak memory) charts and a summary table.

Reads the versioned rows written by ``bench_versions.py`` (rows carrying a
non-null ``version``) from results/*.jsonl and emits, into plots/versions/:

  * ``<task>_time.png`` — wall-time per version (lower is better)
  * ``<task>_mem.png``  — peak RSS per version  (lower is better)
  * ``summary_versions.png`` — memory + speed change of the newest vs the
    oldest version, per task
  * ``summary.md`` — a Markdown table (embedded into the docs / README)

Reuses the palette + white-background rcParams from make_plots.py so the PNGs
match the rest of the suite on light and dark GitHub themes.
"""
import glob
import json
import os
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
RESULTS = os.path.join(HERE, "results")
PLOTS = os.path.join(HERE, "plots", "versions")
os.makedirs(PLOTS, exist_ok=True)

ACCENT = "#2a78d6"      # newest version
ACCENT_DK = "#1b5aa8"
GRAY = "#8a8a86"        # older version(s)
GRAY_LT = "#bcbcb8"
GOOD = "#2e9e6b"        # improvement (lower is better)
BAD = "#d1495b"         # regression

plt.rcParams.update({
    "figure.facecolor": "white", "axes.facecolor": "white",
    "font.size": 11, "axes.edgecolor": "#cfcfca",
    "axes.grid": True, "grid.color": "#ececea", "grid.linewidth": 0.8,
    "axes.axisbelow": True, "svg.fonttype": "none",
})

TASK_TITLE = {
    "file_read": "TTTR file reading (3.5 M-photon PTU)",
    "clsm_fill": "CLSM intensity — fill+structure (512x512 PTU)",
    "clsm_fill_ht3": "CLSM fill — 2.6 M-pixel FLIM image (40x256x256 HT3)",
    "clsm_masked": "CLSM intensity — virtual fill (512x512 PTU)",
    "burst_search": "Burst search (3.5 M photons)",
    "correlation": "Correlation / FCS (3.5 M photons)",
    "fit_map": "Per-pixel reconvolution-MLE map (256x256)",
}
# stable task order for the table
TASK_ORDER = ["file_read", "clsm_fill", "clsm_fill_ht3", "clsm_masked",
              "burst_search", "correlation", "fit_map"]


def load_versions():
    """rows[(task_key, version)] = row, keeping the best (lowest best_s)."""
    rows = {}
    order = []
    for path in glob.glob(os.path.join(RESULTS, "*.jsonl")):
        with open(path) as fh:
            for line in fh:
                line = line.strip()
                if not line:
                    continue
                r = json.loads(line)
                if not r.get("version"):
                    continue
                task = (r.get("extra") or {}).get("task_key") or r.get("task")
                ver = r["version"]
                if ver not in order:
                    order.append(ver)
                key = (task, ver)
                if r.get("status", "ok") != "ok":
                    rows.setdefault(key, r)
                    continue
                prev = rows.get(key)
                if prev is None or (prev.get("best_s") is None) or (
                        r.get("best_s") is not None and r["best_s"] < prev["best_s"]):
                    rows[key] = r
    # sort versions ascending (string tuple compare handles 0.26.2 < 0.27.0)
    order = sorted(order, key=lambda v: [int(x) if x.isdigit() else x
                                         for x in v.replace("=", ".").split(".")])
    return rows, order


def _bar(ax, versions, values, colors, ylabel):
    xs = range(len(versions))
    bars = ax.bar(xs, [v if v is not None else 0 for v in values], color=colors,
                  width=0.6)
    ax.set_xticks(list(xs))
    ax.set_xticklabels([f"v{v}" for v in versions])
    ax.set_ylabel(ylabel)
    for x, v in zip(xs, values):
        if v is None:
            ax.text(x, 0, " n/a\n(new)", ha="center", va="bottom", fontsize=9,
                    color=GRAY)
        else:
            ax.text(x, v, f" {v:.3g}", ha="center", va="bottom", fontsize=9)
    return bars


def per_task_charts(rows, versions):
    for task in TASK_ORDER:
        present = [(task, v) in rows for v in versions]
        if not any(present):
            continue
        colors = [ACCENT if v == versions[-1] else GRAY for v in versions]

        times = [(rows.get((task, v)) or {}).get("best_s") for v in versions]
        times_ms = [t * 1e3 if t is not None else None for t in times]
        fig, ax = plt.subplots(figsize=(5.2, 3.6))
        _bar(ax, versions, times_ms, colors, "wall time (ms) — lower is better")
        ax.set_title(TASK_TITLE.get(task, task), fontsize=11)
        fig.tight_layout()
        fig.savefig(os.path.join(PLOTS, f"{task}_time.png"), dpi=140)
        plt.close(fig)

        mems = [_footprint(rows.get((task, v))) for v in versions]
        if any(m is not None for m in mems):
            fig, ax = plt.subplots(figsize=(5.2, 3.6))
            _bar(ax, versions, mems, colors,
                 "task memory (MB, peak − baseline) — lower is better")
            ax.set_title(TASK_TITLE.get(task, task), fontsize=11)
            fig.tight_layout()
            fig.savefig(os.path.join(PLOTS, f"{task}_mem.png"), dpi=140)
            plt.close(fig)


def _footprint(r):
    """Task memory footprint in MB: peak RSS minus the post-import baseline.

    Subtracting the interpreter+extension baseline (captured right after
    ``import tttrlib``) removes the difference between the isolated venv of an
    older version and the heavier base env of the working-tree build, so the
    number reflects what the *task* allocated, not the environment. Falls back to
    raw peak when no baseline was recorded (e.g. psutil absent)."""
    if r is None:
        return None
    peak = r.get("peak_rss_mb")
    if peak is None:
        return None
    base = r.get("rss_baseline_mb")
    return max(0.0, peak - base) if base is not None else peak


def _delta(old, new):
    """Signed percent change new vs old; None if either missing."""
    if old is None or new is None or old == 0:
        return None
    return (new - old) / old * 100.0


def summary(rows, versions):
    if len(versions) < 2:
        oldest = newest = versions[0] if versions else None
    else:
        oldest, newest = versions[0], versions[-1]

    lines = [
        f"| Task | v{oldest} time | v{newest} time | Δ time | "
        f"v{oldest} mem MB | v{newest} mem MB | Δ mem |",
        "|------|----------:|----------:|:-----:|----------:|----------:|:-----:|",
    ]
    mem_deltas, time_deltas, labels = [], [], []
    for task in TASK_ORDER:
        ro = rows.get((task, oldest))
        rn = rows.get((task, newest))
        if ro is None and rn is None:
            continue

        def fmt_t(r):
            if r is None:
                return "—"
            if r.get("best_s") is None:
                return "n/a"
            return f"{r['best_s'] * 1e3:.2f} ms"

        def fmt_m(r):
            fp = _footprint(r)
            return "—" if fp is None else f"{fp:.0f}"

        to = ro.get("best_s") if ro and ro.get("best_s") is not None else None
        tn = rn.get("best_s") if rn and rn.get("best_s") is not None else None
        mo = _footprint(ro)
        mn = _footprint(rn)
        dt = _delta(to, tn)
        dm = _delta(mo, mn)
        new_only = (ro is None) or (ro and ro.get("best_s") is None)
        dt_s = "new in v%s" % newest if new_only else (f"{dt:+.0f}%" if dt is not None else "—")
        dm_s = "new" if new_only else (f"{dm:+.0f}%" if dm is not None else "—")
        lines.append(
            f"| {TASK_TITLE.get(task, task)} | {fmt_t(ro)} | {fmt_t(rn)} | {dt_s} "
            f"| {fmt_m(ro)} | {fmt_m(rn)} | {dm_s} |")
        if dm is not None and not new_only:
            mem_deltas.append(dm)
            time_deltas.append(dt if dt is not None else 0.0)
            labels.append(TASK_TITLE.get(task, task))

    with open(os.path.join(PLOTS, "summary.md"), "w") as fh:
        fh.write(f"Wall time and task memory footprint (peak RSS minus the "
                 f"post-import baseline), tttrlib v{newest} vs v{oldest} — same "
                 f"machine, same inputs. Negative Δ is an improvement.\n\n")
        fh.write("\n".join(lines) + "\n")

    if labels:
        fig, ax = plt.subplots(figsize=(7.6, 0.6 * len(labels) + 1.6))
        ys = range(len(labels))
        colors = [GOOD if d < 0 else BAD for d in mem_deltas]
        ax.barh(list(ys), mem_deltas, color=colors, height=0.6)
        ax.set_yticks(list(ys))
        ax.set_yticklabels(labels, fontsize=9)
        ax.axvline(0, color="#888", linewidth=1)
        ax.set_xlabel(f"task memory change, v{newest} vs v{oldest} "
                      f"(%, negative = less memory)")
        for y, d in zip(ys, mem_deltas):
            ax.text(d, y, f" {d:+.0f}%", va="center",
                    ha="left" if d >= 0 else "right", fontsize=9)
        ax.invert_yaxis()
        fig.tight_layout()
        fig.savefig(os.path.join(PLOTS, "summary_versions.png"), dpi=140)
        plt.close(fig)
    print(f"wrote {PLOTS}/summary.md and per-task charts for versions {versions}")


def main():
    rows, versions = load_versions()
    if not versions:
        print("no versioned rows found — run bench_versions.py first")
        return
    per_task_charts(rows, versions)
    summary(rows, versions)


if __name__ == "__main__":
    main()
