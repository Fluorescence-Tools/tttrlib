#!/usr/bin/env python
"""Upstream FRET / burst competitors, on the shared inputs of ``bench_fret.py``:

  pda      PAM PDA_histogram.cpp (../chisurf/junk/PAM), compiled natively through the
           mex.h shim in competitors/native/pam_pda/ -- timed inside the process
  burstml  the original FRET_burstML MEX (junk/FRET_burstML/burstMLProject.zip) built
           with test/cpp/burstml_mex_shim + GSL, driver competitors/native/burstml/ --
           timed inside the process
  two_cde  FRETBursts phrates.kde_laplace + Tomov's burst formula, in the fretbursts venv
  fdc2d    Toru Kondo's TK_Create2DFDC_04.m (../chisurf/junk/2D-FLC-code) in Octave,
           tic/toc around the function
  cusum    PAM CUSUM_burstsearch (../chisurf/junk/PAM/PAM.m) in Octave, tic/toc

Run in the base env (numpy only); each pair skips with a printed reason when
its compiler / GSL / Octave / venv / checkout is missing. Reference outputs
are saved to results/shared/fret/reference_outputs.npz for check_fret.py.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE))
from common import record, RESULTS  # noqa: E402

SHARED = os.path.join(RESULTS, "shared", "fret")
CHISURF_JUNK = os.path.abspath(os.path.join(ROOT, "..", "chisurf", "junk"))
FRETBURSTS_PY = os.path.join(os.path.dirname(HERE), ".venvs", "fretbursts", "bin", "python")
OCTAVE = shutil.which("octave") or "/opt/homebrew/bin/octave"
CXX = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
OUT = {}


def skip(name, why):
    print(f"[fret] {name}: skipped -- {why}", file=sys.stderr)


def rec(category, tool, task, best_s, times, n_items, unit, extra=None):
    record(category, tool, task, best_s, float(np.mean(times)), list(times),
           n_items=n_items, unit=unit, dataset="simulated", extra=extra or {}, env="native")


# --------------------------------------------------------------------------- PDA

def bench_pda():
    src = os.path.join(CHISURF_JUNK, "PAM", "functions", "PDAFit", "histogram_library", "PDA_histogram.cpp")
    if not (os.path.exists(src) and CXX):
        return skip("pda", "needs ../chisurf/junk/PAM PDA_histogram.cpp and a C++ compiler")
    d = tempfile.mkdtemp(prefix="pam_pda_bench_")
    nat = os.path.join(HERE, "native", "pam_pda")
    exe = os.path.join(d, "pam_pda")
    r = subprocess.run([CXX, "-std=c++17", "-O2", "-I", nat, os.path.join(nat, "main.cpp"), src, "-o", exe],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return skip("pda", "PAM PDA_histogram.cpp did not compile: " + r.stderr[-300:])
    z = np.load(os.path.join(SHARED, "pda.npz"))
    nmax, pf, amps, probs = int(z["nmax"]), z["pF"], z["amps"], z["probs"]
    repeats = 5
    inp = (f"{nmax}\n" + " ".join(repr(float(x)) for x in pf) + f"\n{len(amps)}\n"
           + "\n".join(f"{a!r} {p!r}" for a, p in zip(amps, probs))
           + f"\n{float(z['bg1'])!r} {float(z['bg2'])!r} {repeats}\n")
    times = []
    for _ in range(3):
        out = subprocess.run([exe], input=inp, capture_output=True, text=True, check=True).stdout.split()
        times.append(float(out[1]))
        vals = out[2:]
    OUT["pda_s1s2"] = np.array([float(v) for v in vals]).reshape(nmax + 1, nmax + 1)
    rec("pda", "PAM PDA_histogram.cpp", f"PDA S1/S2 histogram, nmax={nmax}, 3-species mixture",
        min(times), times, (nmax + 1) ** 2, "cell", {"impl": "PAM (Schrimpf 2018) MEX source, native build"})


# --------------------------------------------------------------------------- BurstML

def find_gsl():
    for prefix in (sys.prefix, "/opt/homebrew", "/usr/local", "/usr"):
        if os.path.exists(os.path.join(prefix, "include", "gsl", "gsl_blas.h")):
            return prefix
    return None


def bench_burstml():
    zip_path = os.path.join(ROOT, "junk", "FRET_burstML", "burstMLProject.zip")
    gsl = find_gsl()
    if not (os.path.exists(zip_path) and CXX and gsl):
        return skip("burstml", "needs junk/FRET_burstML/burstMLProject.zip, a C++ compiler and GSL")
    d = tempfile.mkdtemp(prefix="burstml_bench_")
    with zipfile.ZipFile(zip_path) as zf:
        zf.extract("src/mlhDiffNTRbkg_MT.cpp", d)
        zf.extract("src/mlhDiffNTRbkg_MT.h", d)
    hdr = os.path.join(d, "src", "mlhDiffNTRbkg_MT.h")
    with open(hdr, encoding="utf-8", errors="replace") as f:
        txt = f.read().replace("<gsl/gsl_eigen.h.>", "<gsl/gsl_eigen.h>")
    with open(hdr, "w") as f:
        f.write(txt)
    shim = os.path.join(ROOT, "test", "cpp", "burstml_mex_shim")
    exe = os.path.join(d, "mex_bench")
    cmd = [CXX, "-std=c++17", "-O2", "-w", "-I", shim, "-I", os.path.join(gsl, "include"),
           os.path.join(d, "src", "mlhDiffNTRbkg_MT.cpp"), os.path.join(HERE, "native", "burstml", "driver.cpp"),
           "-L", os.path.join(gsl, "lib"), "-lgsl", "-lgslcblas", f"-Wl,-rpath,{os.path.join(gsl, 'lib')}", "-o", exe]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        return skip("burstml", "MEX did not compile: " + r.stderr[-300:])
    z = np.load(os.path.join(SHARED, "burstml.npz"))
    t100, colours, offsets, params, lb, ub = z["t100"], z["colours"], z["offsets"], z["params"], z["lb"], z["ub"]
    jmax, qmax, t_th, n_th, n_colours = int(z["jmax"]), float(z["qmax"]), float(z["t_th"]), float(z["n_th"]), int(z["n_colours"])
    n_b, n_ph = len(offsets) - 1, len(t100)
    repeats = 3
    lines = [f"{params.shape[1]} {params.shape[0]} {n_colours} {jmax} {qmax!r} {t_th!r} {n_th!r} {n_b} {n_ph}",
             " ".join(repr(float(v)) for p in params for v in p),
             " ".join(map(repr, map(float, lb))) + " " + " ".join(map(repr, map(float, ub))),
             " ".join(map(str, offsets)), " ".join(str(i + 1) for i in range(n_b)),
             " ".join(map(str, t100)), " ".join(str(int(c) + 1) for c in colours), str(repeats)]
    times = []
    for _ in range(3):
        out = subprocess.run([exe], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True).stdout.split()
        times.append(float(out[1]))
        vals = out[2:]
    OUT["burstml_nll"] = np.array([float(v) for v in vals])
    rec("burstml", "FRET_burstML MEX (native)", f"BurstML NLL, {n_b} bursts x {params.shape[0]} parameter sets (2 states, 2 colours, jmax {jmax})",
        min(times), times, params.shape[0], "evaluation", {"impl": "mlhDiffNTRbkg_MT.cpp (Hoffmann et al.), GSL"})


# --------------------------------------------------------------------------- 2CDE

_FB_SCRIPT = r"""
import sys, json, time, numpy as np, warnings
warnings.filterwarnings("ignore")
from fretbursts.phtools import phrates
z = np.load(sys.argv[1])
macro = z["macro"].astype(np.int64); chan = z["chan"]; bounds = z["bounds"]; tau = float(z["tau"])
mask_d, mask_a = chan == 0, chan == 1

def two_cde():
    kde_d = phrates.kde_laplace(macro[mask_d], tau, macro)
    kde_a = phrates.kde_laplace(macro[mask_a], tau, macro)
    out = np.empty(len(bounds))
    for i, (s, e) in enumerate(bounds):
        sl = slice(int(s), int(e) + 1)
        md, ma = mask_d[sl], mask_a[sl]
        if not md.any() or not ma.any():
            out[i] = np.nan; continue
        kde_adi = kde_a[sl][md]; kde_ddi = kde_d[sl][md]
        kde_dai = kde_d[sl][ma]; kde_aai = kde_a[sl][ma]
        n_d, n_a = md.sum(), ma.sum()
        kde_ddi = (1 + 2 / n_d) * (kde_ddi - 1)
        kde_aai = (1 + 2 / n_a) * (kde_aai - 1)
        ed = np.mean(kde_adi / (kde_adi + kde_ddi)); ea = np.mean(kde_dai / (kde_dai + kde_aai))
        out[i] = 110 - 100 * (ed + ea)
    return out

res = two_cde()
times = []
for _ in range(5):
    t0 = time.perf_counter(); two_cde(); times.append(time.perf_counter() - t0)
np.savez(sys.argv[2], two_cde=res, times=np.array(times))
"""


def bench_two_cde():
    if not os.path.exists(FRETBURSTS_PY):
        return skip("two_cde", "fretbursts venv not built (benchmarks/build_envs.sh)")
    d = tempfile.mkdtemp(prefix="fb_2cde_")
    script = os.path.join(d, "run.py"); out = os.path.join(d, "out.npz")
    with open(script, "w") as f:
        f.write(_FB_SCRIPT)
    r = subprocess.run([FRETBURSTS_PY, script, os.path.join(SHARED, "two_cde.npz"), out], capture_output=True, text=True)
    if r.returncode != 0:
        return skip("two_cde", "FRETBursts run failed: " + r.stderr[-300:])
    z = np.load(out)
    OUT["two_cde"] = z["two_cde"]
    n_b = np.load(os.path.join(SHARED, "two_cde.npz"))["bounds"].shape[0]
    times = z["times"].tolist()
    rec("two_cde", "FRETBursts kde_laplace + 2CDE", f"FRET-2CDE (Laplace KDE, tau 30), {n_b} bursts x 120 photons",
        min(times), times, n_b, "burst", {"impl": "phrates.kde_laplace (cython) + numpy Tomov formula"})


# --------------------------------------------------------------------------- Octave

def octave(cmd, cwd, timeout=1800):
    return subprocess.run([OCTAVE, "--no-gui", "-q", "--eval", cmd], cwd=cwd, capture_output=True, text=True, timeout=timeout)


def bench_fdc2d():
    m = os.path.join(CHISURF_JUNK, "2D-FLC-code", "MatlabCodes", "TK_Create2DFDC_04.m")
    if not (os.path.exists(m) and os.path.exists(OCTAVE)):
        return skip("fdc2d", "needs Octave and ../chisurf/junk/2D-FLC-code")
    z = np.load(os.path.join(SHARED, "fdc2d.npz"))
    macro, micro, lags, ddT, t_min, t_max, L = z["macro"], z["micro"], z["lags"], int(z["ddT"]), int(z["t_min"]), int(z["t_max"]), int(z["logt_imax"])
    d = tempfile.mkdtemp(prefix="fdc_octave_")
    shutil.copy(m, d)
    np.savetxt(os.path.join(d, "macro.txt"), macro, fmt="%d")
    np.savetxt(os.path.join(d, "micro.txt"), micro, fmt="%d")
    # the seek-bar display in the .m is a no-op for timing; run once per lag, tic/toc around the call
    cmd = ("pkg load statistics; " if False else "") + \
          ("tt1=load('macro.txt'); kin1=load('micro.txt'); lags=[%s]; ts=[]; " % ",".join(map(str, lags))
           + "for i=1:numel(lags); t0=tic; [~,~,M,~]=TK_Create2DFDC_04(tt1,kin1,lags(i),%d,0,1e18,%d,%d,1,1,%d); "
             "ts(i)=toc(t0); dlmwrite(sprintf('log_%%d.txt',lags(i)),M,' '); end; dlmwrite('times.txt',ts,' ');"
           % (ddT, t_min, t_max, L))
    r = octave(cmd, d)
    if r.returncode != 0:
        return skip("fdc2d", "Octave failed: " + r.stderr[-300:])
    ts = np.loadtxt(os.path.join(d, "times.txt"), ndmin=1)
    mats = np.stack([np.loadtxt(os.path.join(d, f"log_{lag}.txt"), ndmin=2) for lag in lags]).astype(np.int64)
    OUT["fdc2d_log"] = mats
    total = float(ts.sum())
    rec("fdc2d", "TK_Create2DFDC_04.m (Octave)", f"2D-FDC log matrices, {macro.size} photons x {lags.size} lags",
        total, [total], macro.size * lags.size, "photon-lag", {"impl": "original MATLAB (Kondo), Octave, one call per lag; tic/toc inside"})


def bench_cusum():
    pam = os.path.join(CHISURF_JUNK, "PAM", "PAM.m")
    if not (os.path.exists(pam) and os.path.exists(OCTAVE)):
        return skip("cusum", "needs Octave and ../chisurf/junk/PAM/PAM.m")
    z = np.load(os.path.join(SHARED, "cusum.npz"))
    ticks, IB, IT, res = z["ticks"], float(z["IB_khz"]), float(z["IT_khz"]), float(z["res"])
    src = open(pam, encoding="utf-8", errors="replace").read().splitlines()
    i0 = next(i for i, l in enumerate(src) if l.startswith("function [START,STOP] = CUSUM_burstsearch"))
    i1 = next(i for i in range(i0 + 1, len(src)) if src[i].startswith("function BurstSearch_Preview"))
    body = [l for l in src[i0 + 1:i1] if not l.startswith("global FileInfo")]
    body = [l.replace("FileInfo.ClockPeriod", "ClockPeriod") for l in body]
    d = tempfile.mkdtemp(prefix="pam_cusum_")
    with open(os.path.join(d, "pam_cusum_ref.m"), "w") as f:
        f.write("function [START,STOP] = pam_cusum_ref(Photons,IB,IT,ClockPeriod)\n" + "\n".join(body) + "\n")
    np.savetxt(os.path.join(d, "photons.txt"), ticks, fmt="%d")
    cmd = (f"Photons=load('photons.txt'); ts=[]; for r=1:3; t0=tic; [S,E]=pam_cusum_ref(Photons,{IB},{IT},{res}); ts(r)=toc(t0); end; "
           f"dlmwrite('out.txt',[S E],' '); dlmwrite('times.txt',ts,' ');")
    r = octave(cmd, d)
    if r.returncode != 0:
        return skip("cusum", "Octave failed: " + r.stderr[-300:])
    ts = np.loadtxt(os.path.join(d, "times.txt"), ndmin=1).tolist()
    OUT["cusum_bursts"] = np.loadtxt(os.path.join(d, "out.txt"), dtype=np.int64, ndmin=2) - 1   # 1-based -> 0-based
    rec("cusum", "PAM CUSUM_burstsearch (Octave)", f"CUSUM/SPRT burst search, {ticks.size} photons",
        min(ts), ts, ticks.size, "photon", {"impl": "PAM (Schrimpf 2018) MATLAB, Octave; tic/toc inside"})


def main():
    for name, fn in (("pda", bench_pda), ("burstml", bench_burstml), ("two_cde", bench_two_cde),
                     ("fdc2d", bench_fdc2d), ("cusum", bench_cusum)):
        try:
            fn()
        except Exception as e:
            print(f"[fret] {name} failed: {type(e).__name__}: {e}", file=sys.stderr)
    np.savez(os.path.join(SHARED, "reference_outputs.npz"), **OUT)


if __name__ == "__main__":
    main()
