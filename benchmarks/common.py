"""Shared helpers for tttrlib vs. competitor benchmarks.

Every benchmark script (tttrlib in the base env, each competitor in its own
isolated venv) imports this and calls ``record(...)`` to append a result row to
``results/<category>.jsonl``. A single machine + identical input files makes the
wall-clock numbers directly comparable across environments.
"""
import gc
import json
import os
import platform
import statistics
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
RESULTS = os.path.join(HERE, "results")
os.makedirs(RESULTS, exist_ok=True)


def peak_rss_bytes():
    """Process peak resident set size (high-water mark) in bytes.

    Uses ``getrusage(RUSAGE_SELF).ru_maxrss`` — the value reported in bytes on
    macOS but in KiB on Linux, so normalize. This measures the whole process,
    including C/C++ heap that ``tracemalloc`` cannot see, which is exactly where
    tttrlib's per-pixel/photon allocations live. Because ru_maxrss is a monotone
    high-water mark, capture it once at the end of a single-task subprocess.
    """
    try:
        import resource
    except ImportError:  # non-POSIX (Windows) — no getrusage
        return None
    ru = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return ru if sys.platform == "darwin" else ru * 1024


def current_rss_bytes():
    """Current resident set size in bytes (psutil if available, else None).

    Sampled right after ``import tttrlib`` to record the interpreter+extension
    baseline, so ``peak_rss - baseline`` isolates a task's own footprint.
    """
    try:
        import psutil
    except Exception:
        return None
    try:
        return psutil.Process().memory_info().rss
    except Exception:
        return None

# Data roots (tttr-data is a symlink at the repo root; test/data has .npz too)
REPO = os.path.dirname(HERE)
DATA = os.path.join(REPO, "tttr-data")
TESTDATA = os.path.join(REPO, "test", "data", "reference")


def data(*parts):
    p = os.path.join(DATA, *parts)
    if not os.path.exists(p):
        raise FileNotFoundError(p)
    return p


def timeit(fn, repeat=5, warmup=1, number=1):
    """Return (best_s, mean_s, all_times) for ``fn`` (called ``number`` times per rep)."""
    for _ in range(warmup):
        fn()
    times = []
    for _ in range(repeat):
        gc.collect()
        t0 = time.perf_counter()
        for _ in range(number):
            fn()
        dt = (time.perf_counter() - t0) / number
        times.append(dt)
    return min(times), statistics.mean(times), times


def record(category, tool, task, best_s, mean_s, all_times,
           n_items=None, unit=None, dataset=None, extra=None, env=None,
           version=None, peak_rss_mb=None, rss_baseline_mb=None, status="ok"):
    row = {
        "category": category,
        "tool": tool,
        "task": task,
        "dataset": dataset,
        "n_items": n_items,
        "unit": unit,
        "best_s": best_s,
        "mean_s": mean_s,
        "times_s": all_times,
        "throughput": (n_items / best_s) if (n_items and best_s) else None,
        "extra": extra or {},
        "env": env or os.environ.get("BENCH_ENV", "base"),
        "python": platform.python_version(),
        "platform": f"{platform.system()}-{platform.machine()}",
        "ts": time.time(),
        # Cross-version tracking (None on legacy timing-only rows).
        "version": version,
        "peak_rss_mb": peak_rss_mb,
        "rss_baseline_mb": rss_baseline_mb,
        "status": status,
    }
    out = os.path.join(RESULTS, f"{category}.jsonl")
    with open(out, "a") as fh:
        fh.write(json.dumps(row) + "\n")
    tp = f"  {row['throughput']:.3g} {unit}/s" if row["throughput"] else ""
    mem = f"  peak={peak_rss_mb:.1f} MB" if peak_rss_mb is not None else ""
    ver = f"  v{version}" if version else ""
    if best_s is None:
        print(f"[{category}] {tool:12s} {task:32s} {status}{ver}")
    else:
        print(f"[{category}] {tool:12s} {task:32s} best={best_s*1e3:8.2f} ms{tp}{mem}{ver}")
    return row


def bench(category, tool, task, fn, repeat=5, warmup=1, number=1,
          n_items=None, unit=None, dataset=None, extra=None, env=None):
    best, mean, allt = timeit(fn, repeat=repeat, warmup=warmup, number=number)
    return record(category, tool, task, best, mean, allt,
                  n_items=n_items, unit=unit, dataset=dataset, extra=extra, env=env)
