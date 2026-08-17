#!/usr/bin/env python
"""tttrlib side of the benchmark suite (runs in the base env where tttrlib lives).

Covers file reading, CLSM/FLIM image construction, per-pixel lifetime imaging,
single-curve MLE fitting, burst search, correlation and single-molecule
simulation. Also dumps the *exact* shared inputs (decay image, IRF, single
decay, burst photon stream) to results/shared/ so each competitor fits/searches
identical data in its own isolated venv.
"""
import json
import os

import numpy as np

import tttrlib
from common import bench, record, timeit, RESULTS

SHARED = os.path.join(RESULTS, "shared")
os.makedirs(SHARED, exist_ok=True)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def P(*a):
    return os.path.join(REPO, "tttr-data", *a)


# Datasets
F_SM = P("pq", "ptu", "pq_ptu_hh_t3.ptu")                 # confocal single-molecule, ch 0/2
F_IMG_PTU = P("imaging", "pq", "Microtime200_TH260", "beads.ptu")   # 512x512 CLSM PTU
F_IMG_HT3 = P("imaging", "pq", "ht3", "pq_ht3_clsm.ht3")  # 40x256x256 FLIM HT3
F_SPC = P("bh", "bh_spc132.spc")                           # Becker & Hickl SPC-130 single-molecule
F_SPC630 = P("bh", "bh_spc630_256.spc")                    # Becker & Hickl SPC-600/630, 256-channel 32-bit records
F_SPCQC = P("bh", "bh_spcqc004.spc")                       # Becker & Hickl SPC-QC-104
F_SMFILE = P("sm", "data.sm")                              # Weiss-lab .sm (phconvert smreader)
F_PHT3 = P("imaging", "pq", "PicoHarp_SymPhoTime", "Example_PTU_PicoHarp.ptu")   # PicoHarp T3 PTU


# --------------------------------------------------------------------------- #
# 1. TTTR file reading
# --------------------------------------------------------------------------- #
def bench_read():
    def run():
        d = tttrlib.TTTR(F_SM)
        return d.macro_times, d.micro_times, d.routing_channels
    d = tttrlib.TTTR(F_SM)
    n = len(d.macro_times)
    bench("file_read", "tttrlib", "read PTU T3 (HydraHarp)", run,
          repeat=5, n_items=n, unit="photons", dataset="pq_ptu_hh_t3.ptu")
    # HT3 and SPC-130 too: phconvert (competitors/bench_phconvert.py) reads
    # both, and check_reading.py confirms photon-for-photon identity.
    if os.path.exists(F_IMG_HT3):
        d3 = tttrlib.TTTR(F_IMG_HT3, "HT3")
        bench("file_read", "tttrlib (HT3)", "read HT3 T3 (HydraHarp, CLSM)",
              lambda: tttrlib.TTTR(F_IMG_HT3, "HT3").macro_times, repeat=5,
              n_items=len(d3.macro_times), unit="photons", dataset="pq_ht3_clsm.ht3")
    if os.path.exists(F_SPC):
        ds = tttrlib.TTTR(F_SPC, "SPC-130")
        bench("file_read", "tttrlib (SPC-130)", "read SPC-130 (Becker & Hickl)",
              lambda: tttrlib.TTTR(F_SPC, "SPC-130").macro_times, repeat=5,
              n_items=len(ds.macro_times), unit="photons", dataset="bh_spc132.spc")
    # second reading round (2026-08-17): PicoHarp T3 (ptufile), SPC-630 / SPC-QC / .sm (phconvert)
    for f, ctype, label, desc in ((F_PHT3, "PTU", "tttrlib (PicoHarp T3)", "read PTU T3 (PicoHarp)"),
                                  (F_SPC630, "SPC-600_256", "tttrlib (SPC-630)", "read SPC-600/630 256-ch (Becker & Hickl)"),
                                  (F_SPCQC, "SPC-QC", "tttrlib (SPC-QC)", "read SPC-QC-104 (Becker & Hickl)"),
                                  (F_SMFILE, "SM", "tttrlib (SM)", "read .sm (Weiss lab)")):
        if os.path.exists(f):
            dd = tttrlib.TTTR(f, ctype)
            bench("file_read", label, desc, lambda f=f, ctype=ctype: tttrlib.TTTR(f, ctype).macro_times, repeat=5,
                  n_items=int((np.asarray(dd.event_types) == 0).sum()), unit="photons", dataset=os.path.basename(f))

    # save the photon stream so FRETBursts searches the identical photons
    np.savez(os.path.join(SHARED, "burst_stream.npz"),
             macro_times=np.asarray(d.macro_times, dtype=np.int64),
             channels=np.asarray(d.routing_channels, dtype=np.int16),
             macro_time_resolution=d.header.macro_time_resolution)
    return n


# --------------------------------------------------------------------------- #
# 2. CLSM intensity image construction (from a PTU both readers support)
# --------------------------------------------------------------------------- #
def bench_clsm_intensity():
    d = tttrlib.TTTR(F_IMG_PTU)
    clsm = tttrlib.CLSMImage(d, build_pixels=False)
    img = np.asarray(clsm.get_intensity_masked(tttr_data=d, channels=[0, 1]))
    shape = img.shape
    npix = int(np.prod(shape))

    # Intensity-only fast path: build_pixels=False skips the eager per-pixel
    # container allocation; get_intensity_masked does a single-pass
    # acceptance-bitmask scatter straight into the image. Byte-identical to fill.
    def run():
        dd = tttrlib.TTTR(F_IMG_PTU)
        c = tttrlib.CLSMImage(dd, build_pixels=False)
        return c.get_intensity_masked(tttr_data=dd, channels=[0, 1])
    bench("clsm_intensity", "tttrlib", "PTU -> intensity image", run,
          repeat=7, n_items=npix, unit="pixels", dataset="beads.ptu (TH260)",
          extra={"image_shape": list(shape), "path": "build_pixels=False+masked"})

    # full fill() also builds the reusable photon-to-pixel structure needed for
    # downstream lifetime / FCS / decay analysis on the same object
    def run_full():
        dd = tttrlib.TTTR(F_IMG_PTU)
        c = tttrlib.CLSMImage(dd)
        c.fill(channels=[0, 1])
        return c.intensity
    bench("clsm_intensity", "tttrlib (fill+structure)",
          "PTU -> intensity + reusable FLIM structure", run_full,
          repeat=5, n_items=npix, unit="pixels", dataset="beads.ptu (TH260)",
          extra={"image_shape": list(shape), "path": "fill"})
    return shape


# --------------------------------------------------------------------------- #
# 3. Per-pixel lifetime image + shared decay image for competitors
# --------------------------------------------------------------------------- #
def bench_lifetime_image():
    d = tttrlib.TTTR(F_IMG_HT3)
    clsm = tttrlib.CLSMImage(d)
    clsm.fill(channels=[0, 1])

    # (a) tttrlib fast lifetime (method of moments, straight from photons).
    #     Measured COLD (fresh CLSMImage per iteration) so it is a fair
    #     single-shot compute vs flimlib RLD, not the warm moment cache.
    import gc as _gc
    import time as _time
    ml = clsm.get_mean_lifetime(tttr_data=d, minimum_number_of_photons=20, stack_frames=True)
    npix = int(ml.shape[-1] * ml.shape[-2])
    cold = []
    for _ in range(5):
        cc = tttrlib.CLSMImage(d); cc.fill(channels=[0, 1])   # not timed (cold cache)
        _gc.collect()
        t0 = _time.perf_counter()
        cc.get_mean_lifetime(tttr_data=d, minimum_number_of_photons=20, stack_frames=True)
        cold.append(_time.perf_counter() - t0)
    record("lifetime_image", "tttrlib (moments)", "raw photons -> mean-tau map",
           min(cold), sum(cold) / len(cold), cold, n_items=npix, unit="pixels",
           dataset="pq_ht3_clsm.ht3",
           extra={"image_shape": list(ml.shape), "method": "method-of-moments"})

    # (a2) cached re-tune: after the map is built once, changing the IRF/background
    #      is an O(pixels) correction (interactive FLIM). Reported separately.
    clsm.get_mean_lifetime(tttr_data=d, minimum_number_of_photons=20, stack_frames=True)

    def run_retune():
        return clsm.get_mean_lifetime(tttr_data=d, m0_irf=1.0, m1_irf=5.0,
                                      minimum_number_of_photons=20, stack_frames=True)
    bench("lifetime_retune", "tttrlib (moments, cached IRF re-tune)",
          "re-tune IRF on built map", run_retune, repeat=7, n_items=npix,
          unit="pixels", dataset="pq_ht3_clsm.ht3")

    # Build a shared per-pixel decay image (Y, X, H) for flimlib / FLIMKit and
    # tttrlib's own native reconvolution-MLE per-pixel fitter.
    C = 128   # microtime coarsening -> keeps counts un-saturated, H ~ 256
    dec = clsm.get_fluorescence_decay(tttr_data=d, micro_time_coarsening=C, stack_frames=True)
    dec = np.asarray(dec)                      # (1, Y, X, H) uint8
    dec = dec.reshape(dec.shape[-3], dec.shape[-2], dec.shape[-1]).astype(np.float64)
    H = dec.shape[-1]
    dt_ns = float(d.header.micro_time_resolution) * C * 1e9
    period_ns = dt_ns * H
    tt = np.arange(H)
    irf = np.exp(-0.5 * ((tt - 8) / 1.5) ** 2)
    irf = (irf / irf.sum())
    npx = int(dec.shape[0] * dec.shape[1])

    # (b) tttrlib native per-pixel reconvolution MLE (FitNExp.fit_map, multi-threaded C++)
    fmap = tttrlib.FitNExp(dt=dt_ns, irf=irf, period=period_ns,
                           convolution_stop=H - 1, tau_min=0.1, tau_max=10.0)

    def run_map():
        return fmap.fit_map(dec, initial_lifetimes=[2.0], fixed=[1], minimum_photons=20)
    maps = run_map()
    valid = float(maps["valid"].mean())
    bench("lifetime_image", "tttrlib (MLE fit_map)", "decay image -> reconv. MLE tau map",
          run_map, repeat=3, n_items=npx, unit="pixels", dataset="pq_ht3_clsm.ht3",
          extra={"image_shape": list(dec.shape), "method": "reconvolution-MLE",
                 "valid_fraction": valid})

    # shared inputs (float32 to keep the .npz small; competitors up-cast as needed)
    np.savez(os.path.join(SHARED, "decay_image.npz"),
             decay_image=dec.astype(np.float32), period_ns=period_ns,
             dt_ns=dt_ns, irf=irf.astype(np.float32))
    with open(os.path.join(SHARED, "decay_image_meta.json"), "w") as fh:
        json.dump({"shape": list(dec.shape), "period_ns": period_ns, "dt_ns": dt_ns,
                   "n_pixels": npx}, fh)
    return dec.shape


# --------------------------------------------------------------------------- #
# 4. Single-curve MLE fit throughput (fit2x)
# --------------------------------------------------------------------------- #
def bench_fit_curve():
    # Single-detector, single-curve mono-exponential reconvolution MLE (FitNExp).
    # This is the "one detector, one decay" case many TCSPC users actually have.
    n = 256
    dt = 0.05                      # ns per bin -> 12.8 ns window
    period = n * dt
    tt = np.arange(n)
    irf = np.exp(-0.5 * ((tt - 20) / 2.0) ** 2)
    irf = irf / irf.sum()
    true_tau = 2.5
    pure = np.exp(-(tt * dt) / true_tau)
    model = np.convolve(pure, irf)[:n]
    model = model / model.sum() * 50_000 + 2.0
    data = np.random.default_rng(0).poisson(model).astype(np.float64)

    fitter = tttrlib.FitNExp(dt=dt, irf=irf, period=period,
                             convolution_stop=n - 1, tau_min=0.2, tau_max=8.0)
    r = fitter(data, initial_lifetimes=[1.5], include_model=True)
    print("   [FitNExp sanity] converged=%s tau=%.3f (true %.2f)"
          % (r["converged"], r["lifetimes"][0], true_tau))

    NFIT = 2000

    def run():
        for _ in range(NFIT):
            fitter(data, initial_lifetimes=[1.5])
    best, mean, allt = timeit(run, repeat=3, warmup=1)
    record("fit_curve", "tttrlib (FitNExp MLE)", "single-curve mono-exp reconv. MLE",
           best / NFIT, mean / NFIT, [t / NFIT for t in allt],
           n_items=1, unit="fits", dataset="synthetic 256-bin decay",
           extra={"n_bins": n, "recovered_tau": float(r["lifetimes"][0]), "n_fits": NFIT})

    # tttrlib native batch fitter (fit_many) on 2000 decays in one multithreaded call
    matrix = np.tile(data, (NFIT, 1))

    def run_batch():
        return fitter.fit_many(matrix, initial_lifetimes=[1.5])
    bb, bm, ball = timeit(run_batch, repeat=3, warmup=1)
    record("fit_curve", "tttrlib (FitNExp fit_many)", "batch mono-exp reconv. MLE",
           bb / NFIT, bm / NFIT, [t / NFIT for t in ball],
           n_items=1, unit="fits", dataset="synthetic 256-bin decay x2000",
           extra={"n_bins": n, "batched": True, "n_fits": NFIT})

    # save the single decay + irf so flimlib fits the identical curve
    np.savez(os.path.join(SHARED, "single_decay.npz"),
             decay=data.astype(np.float32), irf=irf.astype(np.float32),
             period_ns=period, dt_ns=dt, n_fits=NFIT)


# --------------------------------------------------------------------------- #
# 5. Burst search
# --------------------------------------------------------------------------- #
def bench_burst():
    d = tttrlib.TTTR(F_SM)
    n = len(d.macro_times)

    def run():
        return np.asarray(d.burst_search(L=30, m=10, T=1e-3,
                                         mode="sliding_window")).reshape(-1, 2)
    bursts = run()
    bench("burst_search", "tttrlib", "sliding-window burst search", run,
          repeat=5, n_items=n, unit="photons", dataset="pq_ptu_hh_t3.ptu",
          extra={"n_bursts": int(len(bursts)), "L": 30, "m": 10, "T_s": 1e-3})
    return len(bursts)


# --------------------------------------------------------------------------- #
# 6. Correlation (FCS)
# --------------------------------------------------------------------------- #
def bench_correlation():
    d = tttrlib.TTTR(F_SM)
    n = len(d.macro_times)

    def run():
        c = tttrlib.Correlator(channels=([0], [2]), tttr=d,
                               n_bins=7, n_casc=25, make_fine=False)
        return c.x, c.y
    bench("correlation", "tttrlib", "cross-correlation (FCS, multi-tau)", run,
          repeat=5, n_items=n, unit="photons", dataset="pq_ptu_hh_t3.ptu")

    # Save the two channel timestamp streams + matching log-spaced lag bins so
    # pycorrelate cross-correlates the identical photons over the same lag grid.
    macro = np.asarray(d.macro_times, dtype=np.int64)
    ch = np.asarray(d.routing_channels)
    t0 = macro[ch == 0]
    t2 = macro[ch == 2]
    # ~175 log-spaced lag bins (matching the multi-tau 7x25 lag resolution) from
    # 1 tick up to a tenth of the acquisition span.
    span = int(max(t0.max(), t2.max()) - min(t0.min(), t2.min()))
    n_lags = 7 * 25
    bins = np.unique(np.geomspace(1, max(2, span // 10), num=n_lags + 1).astype(np.int64))
    np.savez(os.path.join(SHARED, "correlation.npz"),
             t0=t0, t2=t2, bins=bins, n_photons=n)


# --------------------------------------------------------------------------- #
# 7. Single-molecule diffusion + photon simulation
# --------------------------------------------------------------------------- #
def bench_simulation():
    def _vd(x):
        return tttrlib.VectorDouble([float(v) for v in x])

    # Time unit for tttrlib Sim* is ms; D in um^2/ms. 0.3 um^2/ms = 300 um^2/s,
    # a realistic small-dye diffusion coefficient (== 3e-10 m^2/s for PyBroMo).
    N = 20            # molecules
    T_ms = 1000.0     # 1 s of simulated time
    dt = 1e-3         # ms window (1 us), == PyBroMo t_step below
    D = 0.3           # um^2/ms

    def build(coasting):
        s = tttrlib.SimSystem()
        sp = tttrlib.SimSpecies(); sp.D = D; sp.q = _vd([200.0, 20.0])
        s.add_species(sp)
        s.set_rate_matrices(_vd([0.0]), _vd([0.0]))
        s.set_background(_vd([2.0, 2.0]))
        s.set_box(5.0, 5.0)
        s.set_population(0, float(N))
        exc = tttrlib.SimGrid.gaussian3d(0.3, 0.9, 5.0, 5.0, 0.05, 1.0)
        st = tttrlib.SimIntegrator(); st.dt = dt; st.n_channels = 2
        st.n_ph_max = 10 ** 9
        st.max_windows = int(T_ms / dt)
        st.per_molecule_skip = coasting   # fast mode: coast molecules far from focus
        return tttrlib.SimEngine(s, exc, tttrlib.VectorSimGrid([]), st)

    n_steps = int(T_ms / dt)
    for coasting, label in [(False, "tttrlib (SimEngine)"),
                            (True, "tttrlib (SimEngine, coasting)")]:
        eng = build(coasting)
        eng.run()
        nph = eng.n_photons()
        print("   [sim] %s: %d photons, %d molecules, %.0fms sim-time"
              % (label, nph, N, T_ms))

        def run(_c=coasting):
            e = build(_c)
            e.run()
            return e.n_photons()
        best, mean, allt = timeit(run, repeat=3, warmup=0)
        record("simulation", label, "%d molecules, 1s diffusion+photons" % N,
               best, mean, allt, n_items=n_steps, unit="steps", dataset="synthetic",
               extra={"n_molecules": N, "T_s": 1.0, "n_photons": nph,
                      "n_steps": n_steps, "coasting": coasting})
        if not coasting:
            fixed_best = best
    # record simulation spec so PyBroMo runs a comparable workload
    with open(os.path.join(SHARED, "sim_spec.json"), "w") as fh:
        json.dump({"n_molecules": N, "T_s": 1.0, "dt_s": dt * 1e-3,
                   "D_m2_per_s": D * 1e-9, "box_half_um": 2.5,
                   "psf_w0_um": 0.3, "max_rate_cps": 200e3, "bg_cps": 2000.0,
                   "n_steps": n_steps, "tttrlib_n_photons": nph,
                   "tttrlib_wall_s": fixed_best}, fh)


def main():
    print("=" * 70)
    print("tttrlib benchmarks (base env)")
    print("=" * 70)
    for fn in [bench_read, bench_clsm_intensity, bench_lifetime_image,
               bench_fit_curve, bench_burst, bench_correlation, bench_simulation]:
        try:
            fn()
        except Exception as e:
            import traceback
            print(f"!! {fn.__name__} failed: {type(e).__name__}: {e}")
            traceback.print_exc()


if __name__ == "__main__":
    main()
