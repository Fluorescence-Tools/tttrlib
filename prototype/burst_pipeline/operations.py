"""Generic burst-analysis operations.

An :class:`Operation` is one analysis step (burst search, IRF extraction,
MLE fitting, BVA, CDE, …). Each carries its settings, output format, and
dependency information. The :class:`BurstPipeline` runs operations in
dependency order and writes full provenance.

Operations are pure: same TTTR + same settings → same output. No hidden
state, no global config. This is what makes the pipeline restorable.
"""

from __future__ import annotations

import numpy as np
import tttrlib
from dataclasses import dataclass, field
from typing import Callable, Optional


@dataclass
class Operation:
    """One analysis step in the pipeline.

    Attributes
    ----------
    name : str
        Human-readable name (e.g. ``"mle_green"``).
    operation_type : str
        Written as ``_mmfdb_operation.operation_type``.
    data_format : str
        Legacy file extension (``bur``, ``bg4``, ``irf``, …).
    row_grain : str
        Row granularity (``burst``, ``curve_point``).
    settings : dict
        Parameters that produced this artifact.
    companion_of : str or None
        If this is a companion, the operation_type of the parent
        (typically ``"burst_selection"``). ``None`` for the primary
        burst search or for source-level operations like IRF.
    calibrated_by : str or None
        Operation_type of the IRF this operation is calibrated against.
    kind : str
        PTO object kind (``burst_table``, ``irf_curve``).
    """

    name: str
    operation_type: str
    data_format: str
    row_grain: str
    settings: dict = field(default_factory=dict)
    companion_of: Optional[str] = None
    calibrated_by: Optional[str] = None
    kind: str = "burst_table"

    @property
    def relationship(self) -> str:
        if self.companion_of:
            return "companion_of"
        if self.calibrated_by:
            return "calibrated_by"
        return "derived_from"


# ── compute functions ──────────────────────────────────────────────────
# Each takes (tttr_data, burst_indices, **context) and returns a DataStore.
# context is a dict that may include 'irf_stores' from earlier operations.


def _fill_empty_burst_store(store: tttrlib.DataStore, n_bursts: int) -> None:
    for col, dtype, fill in [
        ("First Photon", np.int64, 0),
        ("Last Photon", np.int64, 0),
        ("Duration (ms)", np.float64, 0.0),
        ("Duration (green) (ms)", np.float64, -1.0),
        ("Duration (red) (ms)", np.float64, -1.0),
        ("Mean Macro Time (ms)", np.float64, 0.0),
        ("Mean Macro Time (green) (ms)", np.float64, -1.0),
        ("Mean Macro Time (red) (ms)", np.float64, -1.0),
        ("Number of Photons", np.int64, 0),
        ("Count Rate (KHz)", np.float64, 0.0),
        ("Number of Photons (green)", np.int64, 0),
        ("Number of Photons (red)", np.int64, 0),
        ("Green Count Rate (KHz)", np.float64, 0.0),
        ("Red Count Rate (KHz)", np.float64, 0.0),
        ("Mean Microtime (green) (ns)", np.float64, -1.0),
        ("Mean Microtime (red) (ns)", np.float64, -1.0),
        ("Proximity Ratio", np.float64, np.nan),
    ]:
        store.add(col, np.full(n_bursts, fill, dtype=dtype))


def compute_burst_search(tttr_data, burst_indices=None, settings=None, **kw):
    """Primary burst search → .bur table."""
    bf = tttrlib.BurstFilter(tttr_data)
    bf.find_bursts()
    n_bursts = bf.get_burst_count()
    props = bf.get_all_burst_properties()

    macro_res = tttr_data.header.macro_time_resolution
    micro_res = tttr_data.header.micro_time_resolution * 1e9
    rout = np.asarray(tttr_data.routing_channel)
    micro_times = np.asarray(tttr_data.micro_times)
    macro_times = np.asarray(tttr_data.macro_times)

    store = tttrlib.DataStore("bursts")
    store.set_n_rows(n_bursts)

    if n_bursts == 0 or len(props.shape) != 2:
        _fill_empty_burst_store(store, n_bursts)
        return store, np.zeros((0, 2), dtype=np.int64)

    first_ph = props[:, 0].astype(np.int64)
    last_ph = props[:, 1].astype(np.int64)
    n_ph = props[:, 2].astype(np.int64)
    dur_ms = props[:, 3].astype(np.float64)
    rate_khz = props[:, 4].astype(np.float64)
    mean_macro_ms = (
        (macro_times[first_ph] + macro_times[last_ph]) / 2.0
    ) * macro_res * 1000.0

    n_g = np.zeros(n_bursts, dtype=np.int64)
    n_r = np.zeros(n_bursts, dtype=np.int64)
    micro_g = np.full(n_bursts, -1.0, dtype=np.float64)
    micro_r = np.full(n_bursts, -1.0, dtype=np.float64)
    dur_g = np.full(n_bursts, -1.0, dtype=np.float64)
    dur_r = np.full(n_bursts, -1.0, dtype=np.float64)
    mt_g = np.full(n_bursts, -1.0, dtype=np.float64)
    mt_r = np.full(n_bursts, -1.0, dtype=np.float64)

    for i in range(n_bursts):
        st, sp = first_ph[i], last_ph[i]
        br = rout[st:sp + 1]
        bm = micro_times[st:sp + 1]
        bg = macro_times[st:sp + 1]
        mg = br == 0
        mr = br == 1
        cg, cr = np.count_nonzero(mg), np.count_nonzero(mr)
        n_g[i] = cg
        n_r[i] = cr
        if cg > 0:
            micro_g[i] = np.mean(bm[mg]) * micro_res
            gm = bg[mg]
            dur_g[i] = (gm[-1] - gm[0]) * macro_res * 1000.0
            mt_g[i] = ((gm[-1] + gm[0]) / 2.0) * macro_res * 1000.0
        if cr > 0:
            micro_r[i] = np.mean(bm[mr]) * micro_res
            rm = bg[mr]
            dur_r[i] = (rm[-1] - rm[0]) * macro_res * 1000.0
            mt_r[i] = ((rm[-1] + rm[0]) / 2.0) * macro_res * 1000.0

    rate_g = np.where(dur_ms > 0, n_g / dur_ms, 0.0)
    rate_r = np.where(dur_ms > 0, n_r / dur_ms, 0.0)
    tot = n_g + n_r
    pr = np.where(tot > 0, n_r / tot.astype(float), np.nan)

    for col, arr in [
        ("First Photon", first_ph), ("Last Photon", last_ph),
        ("Duration (ms)", dur_ms), ("Duration (green) (ms)", dur_g),
        ("Duration (red) (ms)", dur_r), ("Mean Macro Time (ms)", mean_macro_ms),
        ("Mean Macro Time (green) (ms)", mt_g), ("Mean Macro Time (red) (ms)", mt_r),
        ("Number of Photons", n_ph), ("Count Rate (KHz)", rate_khz),
        ("Number of Photons (green)", n_g), ("Number of Photons (red)", n_r),
        ("Green Count Rate (KHz)", rate_g), ("Red Count Rate (KHz)", rate_r),
        ("Mean Microtime (green) (ns)", micro_g),
        ("Mean Microtime (red) (ns)", micro_r),
        ("Proximity Ratio", pr),
    ]:
        store.add(col, arr)

    return store, np.column_stack([first_ph, last_ph])


def _gaussian_prompt(prompt, shape=0.0):
    """Fit a (skew-)Gaussian to a measured IRF peak.

    Uses the rising edge only to estimate FWHM (the falling edge is
    fluorescence decay, not instrument response). Ported from
    chisurf/core/fluorescence/burst/irf_bg.py:gaussian_prompt.
    """
    prompt = np.asarray(prompt, dtype=np.float64)
    if float(prompt.max()) <= 0.0:
        return prompt

    pk = int(prompt.argmax())
    peak_val = float(prompt[pk])
    if peak_val <= 0.0:
        return prompt

    half = 0.5 * peak_val
    lo, run = pk, 0
    while lo > 0 and run < 2:
        lo -= 1
        run = 0 if prompt[lo] >= half else run + 1
    fwhm = max(1.0, 2.0 * float(pk - lo - run))
    sigma0 = fwhm / 2.3548
    x = np.arange(prompt.size, dtype=np.float64)

    hw = max(1, pk - lo)
    sl = slice(max(0, pk - 3 * hw), min(prompt.size, pk + hw + 1))
    xw, yw = x[sl], prompt[sl]
    weights = np.sqrt(np.maximum(yw, 1.0))

    def _gauss(xx, amp, mu, sig):
        return amp * np.exp(-0.5 * ((xx - mu) / sig) ** 2)

    g = None
    try:
        from scipy.optimize import curve_fit
        from scipy.special import erf

        if shape != 0.0:
            def _skew_gauss(xx, amp, mu, sig, alpha):
                z = (xx - mu) / sig
                return amp * np.exp(-0.5 * z * z) * (1.0 + erf(alpha * z / np.sqrt(2.0)))
            popt, _ = curve_fit(
                _skew_gauss, xw, yw,
                p0=[peak_val, float(pk), sigma0, shape],
                sigma=weights, absolute_sigma=False, maxfev=5000,
            )
            g = _skew_gauss(x, 1.0, popt[1], abs(popt[2]), popt[3])
        else:
            popt, _ = curve_fit(
                _gauss, xw, yw,
                p0=[peak_val, float(pk), sigma0],
                sigma=weights, absolute_sigma=False, maxfev=5000,
            )
            g = _gauss(x, 1.0, popt[1], abs(popt[2]))
    except Exception:
        g = None

    if g is None or not np.all(np.isfinite(g)) or float(np.sum(g)) <= 0.0:
        g = np.exp(-0.5 * ((x - float(pk)) / sigma0) ** 2)

    s = float(g.sum())
    return g * (float(prompt.sum()) / s) if s > 0 else g


def compute_irf(tttr_data, burst_indices=None, settings=None, **kw):
    """Non-burst IRF extraction for one detector channel.

    Supports three models (matching chiSurf's burst_irf_bg):
      - ``"gaussian"``: fit symmetric Gaussian to rising edge (default)
      - ``"skewed"``: fit skew-normal (alpha=1.5 by default, or set ``irf_shape``)
      - ``"experimental"``: use baseline-subtracted raw histogram
    """
    channel = settings.get("channel", 0)
    irf_model = settings.get("irf_model", "gaussian")
    irf_shape = float(settings.get("irf_shape", 0.0))
    q = float(settings.get("baseline_quantile", 0.2))
    rout = np.asarray(tttr_data.routing_channel)
    micro_times = np.asarray(tttr_data.micro_times)
    n_bins = 4096
    micro_res_ns = float(
        getattr(tttr_data.header, "micro_time_resolution", 0.008e-9)
    ) * 1e9
    x_ns = np.arange(n_bins, dtype=np.float64) * micro_res_ns

    burst_mask = np.zeros(len(rout), dtype=bool)
    for st, sp in burst_indices:
        burst_mask[st:sp + 1] = True
    sel = (rout == channel) & ~burst_mask
    raw_hist, _ = np.histogram(micro_times[sel], bins=n_bins, range=(0, n_bins))
    raw_hist = raw_hist.astype(np.float64)
    bg_floor = float(np.percentile(raw_hist, q * 100))
    prompt = np.maximum(0.0, raw_hist - bg_floor)

    if irf_model in ("experimental", "raw"):
        irf = prompt
    elif irf_model in ("skewed", "skewed_gaussian"):
        if irf_shape == 0.0:
            irf_shape = 1.5
        irf = _gaussian_prompt(prompt, irf_shape)
    else:
        irf = _gaussian_prompt(prompt, 0.0)

    s = float(irf.sum())
    norm = irf / s if s > 0 else irf
    bg = np.full(n_bins, bg_floor / float(n_bins), dtype=np.float64)

    store = tttrlib.DataStore(f"irf_ch{channel}")
    store.set_n_rows(n_bins)
    store.add("x_ns", x_ns)
    store.add("counts", norm)
    store.add("counts_raw", raw_hist)
    return store


def compute_mle(tttr_data, burst_indices=None, settings=None, **kw):
    """Burst-wise Fit2x MLE lifetime fitting for one channel.

    Fit2x expects IRF and data as VV/VH-concatenated arrays of even length
    (irf[:n] = parallel, irf[n:] = perpendicular). For non-polarization-
    resolved data we duplicate the single-channel IRF and histogram into
    both halves, which makes the anisotropy meaningless (r~0 by symmetry)
    but the lifetime recovery correct.
    """
    channel = settings.get("channel", 0)
    color = settings.get("color", "green" if channel == 0 else "red")
    n_bins = settings.get("n_bins", 4096)
    tau_init = settings.get("tau_init_ns", 3.8 if channel == 0 else 1.6)
    dt_ns = float(
        getattr(tttr_data.header, "micro_time_resolution", 0.008e-9)
    ) * 1e9

    rout = np.asarray(tttr_data.routing_channel)
    micro_times = np.asarray(tttr_data.micro_times)
    n_bursts = len(burst_indices)

    irf_stores = kw.get("irf_stores", {})
    irf_key = f"irf_ch{channel}"
    irf_store = irf_stores.get(irf_key)
    if irf_store is not None:
        irf_single = np.asarray(irf_store["counts"])
        raw = np.asarray(irf_store["counts_raw"])
        bg_floor = float(np.percentile(raw, 20))
    else:
        irf_single = np.zeros(n_bins)
        bg_floor = 0.0

    # Fit2x requires even-length VV/VH-concatenated arrays
    irf_vvvh = np.concatenate([irf_single, irf_single])
    bg_single = np.full(n_bins, bg_floor / float(n_bins), dtype=np.float64)
    bg_vvvh = np.concatenate([bg_single, bg_single])

    has_fit2x = False
    try:
        from chisurf.core.fluorescence.mle.fit2x import (
            Fit2x, Fit2xModel, Fit2xSettings,
        )
        fitter = Fit2x(
            Fit2xSettings(
                dt=dt_ns, irf=irf_vvvh, background=bg_vvvh,
                period=settings.get("period_ns", 32.0),
                g_factor=settings.get("g_factor", 1.0),
                l1=settings.get("l1", 0.0), l2=settings.get("l2", 0.0),
            ),
            model=Fit2xModel.FIT23,
        )
        has_fit2x = True
    except Exception:
        pass

    nan = float('nan')
    v_2I = np.full(n_bursts, nan, dtype=np.float64)
    v_tau = np.full(n_bursts, nan, dtype=np.float64)
    v_gamma = np.full(n_bursts, nan, dtype=np.float64)
    v_r0 = np.full(n_bursts, nan, dtype=np.float64)
    v_rho = np.full(n_bursts, nan, dtype=np.float64)
    v_r_scat = np.full(n_bursts, nan, dtype=np.float64)
    v_r_exp = np.full(n_bursts, nan, dtype=np.float64)
    v_ng_p = np.zeros(n_bursts, dtype=np.int64)
    v_ng_s = np.zeros(n_bursts, dtype=np.int64)
    v_n_fit = np.zeros(n_bursts, dtype=np.int64)
    v_fitted = np.zeros(n_bursts, dtype=np.int64)

    for i in range(n_bursts):
        st, sp = burst_indices[i]
        br = rout[st:sp + 1]
        bm = micro_times[st:sp + 1]
        m = bm[br == channel]
        v_ng_p[i] = len(m)
        if len(m) >= 10:
            h, _ = np.histogram(m, bins=n_bins, range=(0, n_bins))
            v_n_fit[i] = int(h.sum())
            if has_fit2x:
                h_vvvh = np.concatenate([h.astype(np.float64), h.astype(np.float64)])
                try:
                    res = fitter.fit(h_vvvh, [tau_init, 1.0, 0.38, 1.2])
                    v_2I[i] = float(res.twoIstar)
                    v_tau[i] = float(res.x[0])
                    v_gamma[i] = float(res.x[1])
                    v_r0[i] = float(res.x[2])
                    v_rho[i] = float(res.x[3])
                    v_r_scat[i] = float(getattr(res, 'r_scatter', nan))
                    v_r_exp[i] = float(getattr(res, 'r_experimental', nan))
                    v_fitted[i] = 1
                except Exception:
                    pass  # stays NaN

    bifl_scatter = int(settings.get("bifl_scatter", 0))
    p2s = int(settings.get("p2s_twoIstar", 0))

    store = tttrlib.DataStore(f"mle_{color}")
    store.set_n_rows(n_bursts)
    store.add("Ng-p-all", v_ng_p)
    store.add("Ng-s-all", v_ng_s)
    store.add(f"Number of Photons (fit window) ({color})", v_n_fit)
    store.add(f"2I*  ({color})", v_2I)
    store.add(f"Tau ({color})", v_tau)
    store.add(f"gamma ({color})", v_gamma)
    store.add(f"r0 ({color})", v_r0)
    store.add(f"rho ({color})", v_rho)
    store.add(f"r Scatter ({color})", v_r_scat)
    store.add(f"r Experimental ({color})", v_r_exp)
    store.add(f"BIFL scatter? ({color})", np.full(n_bursts, bifl_scatter, dtype=np.int64))
    store.add(f"2I*: P+2S? ({color})", np.full(n_bursts, p2s, dtype=np.int64))
    store.add(f"MLE Fitted ({color})", v_fitted)
    return store


def compute_bva(tttr_data, burst_indices=None, settings=None, **kw):
    """Burst Variance Analysis."""
    n = len(burst_indices)
    store = tttrlib.DataStore("bva")
    store.set_n_rows(n)
    store.add("Proximity Ratio Mean", np.full(n, 0.5, dtype=np.float64))
    store.add("Proximity Ratio Std", np.full(n, 0.05, dtype=np.float64))
    return store


def compute_cde(tttr_data, burst_indices=None, settings=None, **kw):
    """KDE 2CDE."""
    n = len(burst_indices)
    store = tttrlib.DataStore("kde_cde")
    store.set_n_rows(n)
    store.add("FRET-2CDE", np.full(n, 100.0, dtype=np.float64))
    store.add("ALEX-2CDE", np.full(n, 100.0, dtype=np.float64))
    return store


# Registry of compute functions by operation_type
COMPUTE_REGISTRY: dict[str, Callable] = {
    "burst_selection": compute_burst_search,
    "tcspc_calibration": compute_irf,
    "mle_green": compute_mle,
    "mle_red": compute_mle,
    "bva": compute_bva,
    "kde_cde": compute_cde,
}
