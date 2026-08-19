#!/usr/bin/env python
"""Record an INDEPENDENT reference for `TTTR.burst_search_kalman`.

Run in the sciref venv, which has filterpy:

    benchmarks/.venvs/sciref/bin/python \\
        test/python/burstfilter/gen_ab_kalman_burst_filterpy_reference.py

Why this exists
---------------
The search used to be A/B'd against ChiSurf's `KalmanBurstDetector`, and
ChiSurf is not a valid reference: it is a moving target that this library is
also the upstream of, so "we agree" says only that two things that change
together still agree. This reference is built from the **definition in
`BurstSearchKalman.h`** with two independent pieces:

* the filter itself is **filterpy** (`filterpy.kalman.KalmanFilter`, 1.4.5) --
  the standard Python Kalman package, written by someone who has never seen
  this library. Identity transition and observation, process noise `q*dt` on
  the diagonal, measurement noise `r_scale * rate / dt` from Poisson
  statistics, exactly as the header states.
* the detection on top of it -- Mahalanobis distance of the innovation,
  thresholding, minimum run length, gap merging, bins to photon indices -- is
  plain NumPy written from the same prose.

What is deliberately shared is only the binning (a bin width is a convention,
not an algorithm), so a disagreement is a disagreement about the method.

The fixture records the INPUTS (photon stream, settings) and the reference's
burst boundaries, so the A/B runs anywhere without filterpy installed.
"""
import math
import os

import numpy as np

RES = 1e-8            # the macro-time resolution the test suite uses
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "..", "data", "reference",
                   "kalman_burst_filterpy_reference.npz")


def bursty_stream(seed, two_channels):
    """A background with bursts in it -- and the TRUTH: where each burst is.

    The stream is simulated, so the number of bursts and their photon index
    ranges are known exactly. That is a stronger statement than agreement with
    any implementation: a search that agrees with a reference and finds none of
    the injected bursts is wrong, and both would be wrong together.
    """
    rng = np.random.default_rng(seed)
    ticks, channels, truth = [], [], []
    t = 0
    n_photons = 0
    for _ in range(40):
        gap = int(rng.integers(30_000, 80_000))
        n_bg = int(rng.integers(20, 60))
        bg = np.sort(rng.integers(0, gap, n_bg)) + t
        ticks.append(bg)
        channels.append(rng.integers(0, 2, n_bg) if two_channels else np.zeros(n_bg, int))
        t += gap
        n_photons += n_bg
        width = int(rng.integers(3_000, 12_000))
        n_burst = int(rng.integers(60, 200))
        burst = np.sort(rng.integers(0, width, n_burst)) + t
        ticks.append(burst)
        channels.append(rng.integers(0, 2, n_burst) if two_channels else np.zeros(n_burst, int))
        truth.append((n_photons, n_photons + n_burst - 1))   # inclusive photon indices
        t += width
        n_photons += n_burst
    ticks = np.concatenate(ticks).astype(np.uint64)
    order = np.argsort(ticks, kind="stable")
    # the stream is built in time order already, so `order` is the identity and
    # the truth indices are the ones above; assert it rather than assume it
    assert np.array_equal(order, np.arange(order.size)), "stream not in time order"
    return ticks, np.concatenate(channels).astype(np.int8), np.asarray(truth, dtype=np.int64)


def bin_stream(ticks, channels, dt, per_channel):
    """Bins of `dt` from the first photon, one column per used channel."""
    t0 = ticks[0]
    ticks_per_bin = dt / RES
    span = (ticks[-1] - t0) * RES
    n_bins = int(math.ceil(span / dt)) + 1
    b = ((ticks - t0).astype(float) / ticks_per_bin).astype(np.int64)
    b = np.clip(b, 0, n_bins - 1)
    used = np.unique(channels) if per_channel else np.array([0])
    counts = np.zeros((n_bins, used.size))
    for d, c in enumerate(used):
        sel = (channels == c) if per_channel else np.ones_like(channels, bool)
        counts[:, d] = np.bincount(b[sel], minlength=n_bins)
    return b, counts


def mahalanobis_with_filterpy(counts, dt, q, r_scale, warmup):
    """The filter, by filterpy; the distance, by its definition.

    The model, from `BurstSearchKalman.h`: the state and the observation are
    the count RATE per channel, F = H = I ("the rate stays what it was, plus
    process noise"), Q = q*I, and the measurement noise of a rate estimated
    from one bin is Poisson -- `r_scale * rate / dt` -- with the rate taken
    from the *predicted* state, floored so an empty bin cannot make R
    singular. Without a warm-up the filter starts at x = 0 with a diffuse
    P = 1e6*I, which is the "know nothing, believe the first bins" prior.

    filterpy runs predict/update; the innovation, its covariance and the
    Mahalanobis distance are read off the standard definitions.
    """
    from filterpy.kalman import KalmanFilter

    rates = counts / dt
    T, dim = rates.shape
    kf = KalmanFilter(dim_x=dim, dim_z=dim)
    kf.x = np.zeros((dim, 1))
    kf.P = np.eye(dim) * 1e6
    kf.F = np.eye(dim)
    kf.H = np.eye(dim)
    kf.Q = np.eye(dim) * q
    if warmup > 0:
        warm = min(int(warmup), T)
        mean_rate = counts[:warm].sum(axis=0) / (warm * dt)
        kf.x = mean_rate.reshape(dim, 1)
        kf.P = np.diag(np.maximum(mean_rate, 1e-12) / dt)

    distances = np.zeros(T)
    for i in range(T):
        kf.predict()                                   # x unchanged, P += Q
        z = rates[i].reshape(dim, 1)
        rate = np.maximum(kf.x.ravel(), 1e-12)         # the PREDICTED rate
        kf.R = np.diag(r_scale * rate / dt)
        y = z - kf.H @ kf.x                            # innovation
        S = kf.H @ kf.P @ kf.H.T + kf.R                # innovation covariance
        distances[i] = float(np.sqrt((y.T @ np.linalg.inv(S) @ y).item()))
        kf.update(z)
    return distances


def runs_from_distances(d, z_thresh, min_len, merge_gap, warmup):
    """Bins over the threshold -> runs of at least `min_len` -> merged.

    The ORDER matters and is the method's, not a detail: a run shorter than
    `min_len` is dropped *before* merging, so a single noisy bin cannot bridge
    two gaps and glue a whole measurement into one burst. (Merging first and
    filtering after does exactly that -- it returned one burst spanning
    everything on every case here.)
    """
    flag = d > z_thresh
    flag[:warmup] = False
    runs, start = [], None
    for i, on in enumerate(flag):
        if on and start is None:
            start = i
        elif not on and start is not None:
            if i - start >= min_len:
                runs.append([start, i - 1])
            start = None
    if start is not None and len(flag) - start >= min_len:
        runs.append([start, len(flag) - 1])
    if not runs:
        return []
    if merge_gap <= 0:
        return [(s, e) for s, e in runs]
    merged = [runs[0]]
    for r in runs[1:]:
        if r[0] - merged[-1][1] - 1 <= merge_gap:
            merged[-1][1] = r[1]
        else:
            merged.append(r)
    return [(s, e) for s, e in merged]


def bursts_from_runs(b, runs, min_photons):
    out = []
    for s, e in runs:
        first = int(np.searchsorted(b, s, side="left"))
        last = int(np.searchsorted(b, e + 1, side="left")) - 1
        if last >= first and last - first + 1 >= min_photons:
            out.append((first, last))
    return np.asarray(out, dtype=np.int64).reshape(-1, 2)


def main():
    # The regime matters, and the truth is what showed it: with dt = 1e-4 a
    # burst 30-120 us long is under one bin, and a merge gap of 5 bins (0.5 ms)
    # is longer than the background gaps -- so every search, ours and the
    # reference's, returned 1-4 detections covering all 40 injected bursts and
    # "agreed" perfectly while resolving nothing. Bins of 10 us make a burst
    # 3-12 bins and the gaps 30-80 bins, which is the regime this method is for.
    cases = [
        dict(seed=1, per_channel=False, dt=1e-5, q=1e9, r_scale=1.0, z=3.0,
             min_len=2, gap=3, L=20, warmup=50),
        dict(seed=2, per_channel=True, dt=1e-5, q=1e9, r_scale=1.0, z=3.0,
             min_len=2, gap=3, L=20, warmup=50),
        dict(seed=3, per_channel=False, dt=2e-5, q=5e8, r_scale=1.0, z=2.5,
             min_len=2, gap=2, L=15, warmup=50),
        dict(seed=4, per_channel=True, dt=1e-5, q=2e9, r_scale=0.5, z=4.0,
             min_len=1, gap=4, L=25, warmup=50),
    ]
    saved = {}
    for i, c in enumerate(cases):
        ticks, channels, truth = bursty_stream(c["seed"], c["per_channel"])
        b, counts = bin_stream(ticks, channels, c["dt"], c["per_channel"])
        d = mahalanobis_with_filterpy(counts, c["dt"], c["q"], c["r_scale"], c["warmup"])
        runs = runs_from_distances(d, c["z"], c["min_len"], c["gap"], c["warmup"])
        bursts = bursts_from_runs(b, runs, c["L"])
        saved[f"ticks_{i}"] = ticks
        saved[f"truth_{i}"] = truth
        saved[f"channels_{i}"] = channels
        saved[f"distances_{i}"] = d
        saved[f"bursts_{i}"] = bursts
        saved[f"settings_{i}"] = np.array(
            [c["dt"], c["q"], c["r_scale"], c["z"], c["min_len"], c["gap"],
             c["L"], float(c["per_channel"]), c["warmup"]], dtype=float)
        found = sum(1 for a, b_ in truth
                    if np.any((bursts[:, 0] <= b_) & (bursts[:, 1] >= a)))
        print(f"case {i}: {len(ticks)} photons, {counts.shape[0]} bins, "
              f"{len(truth)} injected, {len(bursts)} found by the reference, "
              f"{found} injected bursts covered, max distance {d.max():.2f}")
    saved["n_cases"] = np.array([len(cases)])
    import filterpy
    saved["filterpy_version"] = np.array([filterpy.__version__])
    np.savez_compressed(os.path.abspath(OUT), **saved)
    print("wrote", os.path.abspath(OUT))


if __name__ == "__main__":
    main()
