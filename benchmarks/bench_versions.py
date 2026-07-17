#!/usr/bin/env python
"""Cross-version performance + peak-memory monitor for tttrlib.

Measures wall-time AND peak resident memory for the same workloads across
tttrlib versions on one machine, so regressions/improvements are tracked over
releases. Peak memory is process RSS (``getrusage``), NOT ``tracemalloc``:
tttrlib's memory wins live in the C/C++ heap (per-pixel/per-photon containers),
which ``tracemalloc`` cannot see. One task runs per process so ``ru_maxrss``
(a monotone high-water mark) isolates cleanly.

Two modes
---------
worker        ``--run-task NAME --version LABEL``
              Runs ONE task in the current interpreter, records time + peak RSS.

orchestrator  ``--versions LABEL[=SPEC] ...`` (default)
              For each version, prepares an environment (the current base env
              for the working-tree build, a fresh ``uv`` venv + pip install for a
              released version) and spawns one worker subprocess per (version,
              task). SPEC is a pip requirement (e.g. ``0.26.2=tttrlib==0.26.2``);
              ``LABEL=local`` (or the highest label) uses the current interpreter.

Example
-------
    python bench_versions.py --versions 0.26.2 0.27.0=local
    python make_version_plots.py
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from common import timeit, record, peak_rss_bytes, current_rss_bytes  # noqa: E402

REPO = os.path.dirname(HERE)


def P(*a):
    return os.path.join(REPO, "tttr-data", *a)


F_SM = P("pq", "ptu", "pq_ptu_hh_t3.ptu")                          # single-molecule
F_IMG_PTU = P("imaging", "pq", "Microtime200_TH260", "beads.ptu")  # 512x512 CLSM PTU
F_IMG_HT3 = P("imaging", "pq", "ht3", "pq_ht3_clsm.ht3")           # 40x256x256 FLIM HT3


class Unsupported(Exception):
    """Raised by a task builder when the running tttrlib lacks the needed API."""


# --------------------------------------------------------------------------- #
# Task registry. Each builder receives the imported tttrlib module and returns
# (run_callable, meta_dict, timeit_kwargs). It may raise Unsupported for APIs
# that a compared (older) version does not have. run() rebuilds its objects each
# call so peak RSS reflects the task, exactly as bench_tttrlib.py does.
# --------------------------------------------------------------------------- #
def _b_file_read(tt):
    d = tt.TTTR(F_SM)
    n = len(d.macro_times)

    def run():
        dd = tt.TTTR(F_SM)
        return dd.macro_times, dd.micro_times, dd.routing_channels

    return run, dict(category="file_read", task="read PTU T3 (HydraHarp)",
                     n_items=n, unit="photons", dataset="pq_ptu_hh_t3.ptu"), \
        dict(repeat=5, warmup=1)


def _b_clsm_fill(tt):
    # Standard materialized fill — supported by every version. The 0.27 lazy
    # stream-mask fill makes THIS same call cost far less resident memory.
    import numpy as np
    d = tt.TTTR(F_IMG_PTU)
    c = tt.CLSMImage(d)
    c.fill(channels=[0, 1])
    npix = int(np.asarray(c.intensity).size)

    def run():
        dd = tt.TTTR(F_IMG_PTU)
        cc = tt.CLSMImage(dd)
        cc.fill(channels=[0, 1])
        return cc.intensity

    return run, dict(category="clsm_intensity", task="PTU -> intensity (fill+structure)",
                     n_items=npix, unit="pixels", dataset="beads.ptu (TH260)"), \
        dict(repeat=3, warmup=1)


def _b_clsm_masked(tt):
    # Virtual fill (build_pixels=False + get_intensity_masked) — new in 0.27.
    import numpy as np
    d = tt.TTTR(F_IMG_PTU)
    try:
        c = tt.CLSMImage(d, build_pixels=False)
    except TypeError:
        raise Unsupported("build_pixels")
    if not hasattr(c, "get_intensity_masked"):
        raise Unsupported("get_intensity_masked")
    img = np.asarray(c.get_intensity_masked(tttr_data=d, channels=[0, 1]))
    npix = int(img.size)

    def run():
        dd = tt.TTTR(F_IMG_PTU)
        cc = tt.CLSMImage(dd, build_pixels=False)
        return cc.get_intensity_masked(tttr_data=dd, channels=[0, 1])

    return run, dict(category="clsm_intensity", task="PTU -> intensity (virtual fill)",
                     n_items=npix, unit="pixels", dataset="beads.ptu (TH260)"), \
        dict(repeat=7, warmup=1)


def _b_clsm_fill_ht3(tt):
    # Fill the memory-heavy 40x256x256 (2.6 M-pixel) FLIM image. Both versions
    # support fill(); 0.27's lazy per-event stream-mask fill replaces 0.26's
    # eagerly materialized per-pixel index vectors, so this is where the
    # cross-version peak-RSS gap is largest.
    import numpy as np
    d = tt.TTTR(F_IMG_HT3)
    c = tt.CLSMImage(d)
    c.fill(channels=[0, 1])
    npix = int(np.asarray(c.intensity).size)

    def run():
        dd = tt.TTTR(F_IMG_HT3)
        cc = tt.CLSMImage(dd)
        cc.fill(channels=[0, 1])
        return cc.intensity

    return run, dict(category="clsm_fill_ht3", task="HT3 40x256x256 -> intensity (fill)",
                     n_items=npix, unit="pixels", dataset="pq_ht3_clsm.ht3"), \
        dict(repeat=3, warmup=1)


def _b_burst(tt):
    import numpy as np
    d = tt.TTTR(F_SM)
    n = len(d.macro_times)

    def run():
        return np.asarray(d.burst_search(L=30, m=10, T=1e-3, mode="sliding_window"))

    return run, dict(category="burst_search", task="sliding-window burst search",
                     n_items=n, unit="photons", dataset="pq_ptu_hh_t3.ptu"), \
        dict(repeat=5, warmup=1)


def _b_correlation(tt):
    d = tt.TTTR(F_SM)
    n = len(d.macro_times)

    def run():
        c = tt.Correlator(channels=([0], [2]), tttr=d, n_bins=7, n_casc=25,
                          make_fine=False)
        return c.x, c.y

    return run, dict(category="correlation", task="cross-correlation (FCS, multi-tau)",
                     n_items=n, unit="photons", dataset="pq_ptu_hh_t3.ptu"), \
        dict(repeat=5, warmup=1)


def _b_fit_map(tt):
    # Per-pixel reconvolution-MLE lifetime map — new in 0.27 (FitNExp.fit_map).
    import numpy as np
    if not hasattr(tt, "FitNExp"):
        raise Unsupported("FitNExp")
    d = tt.TTTR(F_IMG_HT3)
    clsm = tt.CLSMImage(d)
    clsm.fill(channels=[0, 1])
    C = 128
    dec = np.asarray(clsm.get_fluorescence_decay(
        tttr_data=d, micro_time_coarsening=C, stack_frames=True))
    dec = dec.reshape(dec.shape[-3], dec.shape[-2], dec.shape[-1]).astype(np.float64)
    H = dec.shape[-1]
    dt_ns = float(d.header.micro_time_resolution) * C * 1e9
    period_ns = dt_ns * H
    tt_ax = np.arange(H)
    irf = np.exp(-0.5 * ((tt_ax - 8) / 1.5) ** 2)
    irf = irf / irf.sum()
    npx = int(dec.shape[0] * dec.shape[1])
    fmap = tt.FitNExp(dt=dt_ns, irf=irf, period=period_ns,
                      convolution_stop=H - 1, tau_min=0.1, tau_max=10.0)

    def run():
        return fmap.fit_map(dec, initial_lifetimes=[2.0], fixed=[1],
                            minimum_photons=20)

    return run, dict(category="lifetime_image", task="decay image -> reconv. MLE tau map",
                     n_items=npx, unit="pixels", dataset="pq_ht3_clsm.ht3"), \
        dict(repeat=3, warmup=1)


TASKS = {
    "file_read": _b_file_read,
    "clsm_fill": _b_clsm_fill,
    "clsm_fill_ht3": _b_clsm_fill_ht3,
    "clsm_masked": _b_clsm_masked,
    "burst_search": _b_burst,
    "correlation": _b_correlation,
    "fit_map": _b_fit_map,
}


# --------------------------------------------------------------------------- #
# Worker: run ONE task in the current interpreter, capture time + peak RSS.
# --------------------------------------------------------------------------- #
def run_worker(task_name, version):
    import tttrlib as tt
    baseline = current_rss_bytes()
    builder = TASKS[task_name]
    tool = f"tttrlib {version}"
    try:
        run, meta, kw = builder(tt)
    except Unsupported as exc:
        record(TASKS_CATEGORY.get(task_name, "misc"), tool, task_name,
               None, None, [], version=version, status=f"unsupported:{exc}")
        print(f"[{task_name}] v{version}: unsupported ({exc})")
        return
    except Exception as exc:  # API drift / data issue on an older version
        record(TASKS_CATEGORY.get(task_name, "misc"), tool, task_name,
               None, None, [], version=version, status=f"error:{type(exc).__name__}")
        print(f"[{task_name}] v{version}: error ({type(exc).__name__}: {exc})")
        return
    try:
        best, mean, allt = timeit(run, **kw)
    except Exception as exc:
        record(meta["category"], tool, meta["task"], None, None, [],
               version=version, status=f"error:{type(exc).__name__}")
        print(f"[{task_name}] v{version}: run error ({type(exc).__name__}: {exc})")
        return
    peak = peak_rss_bytes()
    record(meta["category"], tool, meta["task"], best, mean, allt,
           n_items=meta.get("n_items"), unit=meta.get("unit"),
           dataset=meta.get("dataset"), version=version,
           peak_rss_mb=(peak / 1e6) if peak is not None else None,
           rss_baseline_mb=(baseline / 1e6) if baseline is not None else None,
           extra={"task_key": task_name})


# category lookup for unsupported rows (builders may not run to return meta)
TASKS_CATEGORY = {
    "file_read": "file_read",
    "clsm_fill": "clsm_intensity",
    "clsm_fill_ht3": "clsm_fill_ht3",
    "clsm_masked": "clsm_intensity",
    "burst_search": "burst_search",
    "correlation": "correlation",
    "fit_map": "lifetime_image",
}


# --------------------------------------------------------------------------- #
# Orchestrator: prepare an environment per version, spawn one worker per task.
# --------------------------------------------------------------------------- #
def _venv_python(venv_dir):
    return os.path.join(venv_dir, "bin", "python")


def prepare_env(label, spec):
    """Return the python executable for a version, preparing a venv if needed.

    spec == "local"  -> the current interpreter (working-tree build).
    spec == pip req  -> a uv venv named .venvs/tttrlib-<label> with that req.
    Returns (python_exe, status). status "ok" or "install-failed".
    """
    if spec == "local":
        return sys.executable, "ok"

    venvs = os.path.join(HERE, ".venvs")
    os.makedirs(venvs, exist_ok=True)
    venv_dir = os.path.join(venvs, f"tttrlib-{label}")
    py = _venv_python(venv_dir)

    have_uv = shutil.which("uv") is not None
    if not os.path.exists(py):
        print(f"[env] creating venv for {label} ({spec}) ...")
        if have_uv:
            subprocess.run(["uv", "venv", "--python", "3.10", venv_dir], check=True)
        else:
            subprocess.run([sys.executable, "-m", "venv", venv_dir], check=True)

    # Install the requested tttrlib + numpy (+ psutil for the RSS baseline).
    installer = (["uv", "pip", "install", "--python", py]
                 if have_uv else [py, "-m", "pip", "install"])
    try:
        subprocess.run(installer + [spec, "numpy", "psutil"], check=True)
    except subprocess.CalledProcessError:
        print(f"[env] !! install failed for {label} ({spec})")
        return py, "install-failed"
    return py, "ok"


def run_orchestrator(versions, tasks):
    env = dict(os.environ)
    env.setdefault("OMP_NUM_THREADS", "4")   # pin: affects both time and peak RSS
    env["BENCH_ENV"] = "versions"

    for label, spec in versions:
        py, status = prepare_env(label, spec)
        if status != "ok":
            record("misc", f"tttrlib {label}", "install", None, None, [],
                   version=label, status=status)
            continue
        # report the actually-installed version for provenance
        try:
            shown = subprocess.run(
                [py, "-c", "import tttrlib,importlib.metadata as m;"
                 "print(m.version('tttrlib'))"],
                capture_output=True, text=True, env=env).stdout.strip()
            print(f"[env] {label}: installed tttrlib {shown or '(unknown)'}")
        except Exception:
            pass
        for task in tasks:
            print(f"--- {label} :: {task} ---")
            subprocess.run([py, os.path.abspath(__file__),
                            "--run-task", task, "--version", label], env=env)


def parse_version_arg(item):
    """`LABEL` -> (LABEL, 'tttrlib==LABEL'); `LABEL=SPEC` -> (LABEL, SPEC)."""
    if "=" in item:
        label, spec = item.split("=", 1)
        return label, spec
    return item, f"tttrlib=={item}"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--run-task", help="worker mode: run one task in this interpreter")
    ap.add_argument("--version", help="worker mode: version label for the row")
    ap.add_argument("--versions", nargs="+",
                    help="orchestrator: version labels (LABEL or LABEL=SPEC)")
    ap.add_argument("--tasks", nargs="+", default=list(TASKS),
                    help="subset of tasks to run (default: all)")
    args = ap.parse_args()

    if args.run_task:
        if args.run_task not in TASKS:
            ap.error(f"unknown task {args.run_task}; choose from {list(TASKS)}")
        run_worker(args.run_task, args.version or "unknown")
        return

    if not args.versions:
        ap.error("orchestrator mode needs --versions (e.g. --versions 0.26.2 0.27.0=local)")
    versions = [parse_version_arg(v) for v in args.versions]
    bad = [t for t in args.tasks if t not in TASKS]
    if bad:
        ap.error(f"unknown tasks {bad}; choose from {list(TASKS)}")
    run_orchestrator(versions, args.tasks)


if __name__ == "__main__":
    main()
