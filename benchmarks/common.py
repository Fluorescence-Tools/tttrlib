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
           n_items=None, unit=None, dataset=None, extra=None, env=None):
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
    }
    out = os.path.join(RESULTS, f"{category}.jsonl")
    with open(out, "a") as fh:
        fh.write(json.dumps(row) + "\n")
    tp = f"  {row['throughput']:.3g} {unit}/s" if row["throughput"] else ""
    print(f"[{category}] {tool:12s} {task:32s} best={best_s*1e3:8.2f} ms{tp}")
    return row


def bench(category, tool, task, fn, repeat=5, warmup=1, number=1,
          n_items=None, unit=None, dataset=None, extra=None, env=None):
    best, mean, allt = timeit(fn, repeat=repeat, warmup=warmup, number=number)
    return record(category, tool, task, best, mean, allt,
                  n_items=n_items, unit=unit, dataset=dataset, extra=extra, env=env)
