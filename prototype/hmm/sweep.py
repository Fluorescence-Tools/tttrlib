"""Degradation sweep: what breaks under misspecification, when, and by how much.

Each row generates photons through `SimEngine` with one imperfection switched
on, then fits with a model that does **not** know about it, and records what it
costs.  The deliverable is a table, not a pass/fail — the point is to learn which
terms are load-bearing so the model earns each parameter it carries, rather than
adding all of them and hoping.

Two controls make a row interpretable:

* the **matched** fit, which does know about the imperfection.  The gap between
  naive and matched is the cost of the misspecification; the matched row's own
  residual is the noise floor of the measurement.
* the **oracle** state assignment, using the true parameters.  It separates "the
  fit could not find it" from "the information was never there".

The transition matrix is **fitted**, not supplied: transition-rate bias under
misspecification is one of the things worth measuring, and a supplied `A` would
hide it.
"""
from __future__ import annotations

import itertools
import json
import os
import time
from dataclasses import replace

import numpy as np

from .core import PhotonData
from .emission import (FretDistanceEmission, Optics, fit_emission,
                       lifetime_averages, fret_lifetime_spectrum)
from .instrument import InstrumentConfig, KineticScheme, Photophysics, simulate

__all__ = ["Condition", "CONDITIONS", "run_condition", "run_sweep", "format_table"]

_NB = 1024                     # micro-time bins; converged (see plan 2a-bis)
_TRUE_R = (45.0, 62.0)


class Condition:
    """One row of the sweep: how to generate, and what the naive model omits."""

    def __init__(self, name, cfg_kw=None, scheme=None, photophysics=None,
                 naive_kw=None, note=""):
        # `naive_kw` empty means the model carries **no term** for this effect,
        # so "naive" and "matched" are the same fit and the pair is not a
        # comparison -- the number is the damage, full stop.
        self.name = name
        self.cfg_kw = cfg_kw or {}
        self.scheme = scheme
        self.photophysics = photophysics
        self.naive_kw = naive_kw or {}      # what the naive model wrongly assumes
        self.note = note


def _base_cfg(**kw):
    base = dict(optics=Optics(tau_a=3.0), donor=4.0, sigma=0.0, n_molecules=1,
                n_photons=150_000, max_windows=40_000, brightness=100.0)
    base.update(kw)
    return InstrumentConfig(**base)


CONDITIONS = [
    Condition("baseline (nothing on)", note="the noise floor of the measurement"),

    Condition("crosstalk: alpha=0.08",
              cfg_kw=dict(optics=Optics(alpha=0.08, tau_a=3.0)),
              naive_kw=dict(alpha=0.0)),
    Condition("crosstalk: full (a=.08 d=.06 g=.65)",
              cfg_kw=dict(optics=Optics(alpha=0.08, delta=0.06, gamma=0.65, tau_a=3.0)),
              naive_kw=dict(alpha=0.0, delta=0.0, gamma=1.0)),

    Condition("linker width sigma=6",
              cfg_kw=dict(sigma=6.0), naive_kw=dict(sigma=0.0)),
    Condition("multi-exponential donor",
              cfg_kw=dict(donor=([0.7, 0.3], [4.0, 1.2])),
              naive_kw=dict(donor=3.16)),          # tau_x of the true spectrum
    Condition("sigma=6 + multi-exp donor",
              cfg_kw=dict(sigma=6.0, donor=([0.7, 0.3], [4.0, 1.2])),
              naive_kw=dict(sigma=0.0, donor=3.16)),

    Condition("background 5% uniform",
              cfg_kw=dict(background=(5.0, 5.0), background_kind="uniform")),
    Condition("background 5% scatter",
              cfg_kw=dict(background=(5.0, 5.0), background_kind="scatter")),

    Condition("kinetics: slow", scheme="slow"),
    Condition("kinetics: fast", scheme="fast",
              note="transitions faster than the photon rate blur the states"),

    Condition("blinking acceptor",
              photophysics=Photophysics(blink_off=0.5, blink_on=2.0),
              note="a dark acceptor is the state FRET cannot fake"),
    # Anisotropy is deliberately absent.  SimEngine's anisotropy branch picks the
    # detection channel purely from the parallel/perpendicular split
    # (SimEngine.h:671-674) and never reads the species `q` row, so channels 0/1
    # *are* the polarisation pair.  Measured: with anisotropy on, a species with
    # q=[0,100] emits into channel 0 49% of the time -- colour is overwritten by
    # polarisation.  Simulating colour *and* polarisation needs four channels
    # (green-par, green-perp, red-par, red-perp), which that branch cannot
    # express.  A row here would measure the mis-configuration, not anisotropy.

    Condition("everything at once",
              cfg_kw=dict(optics=Optics(alpha=0.08, delta=0.06, gamma=0.65, tau_a=3.0),
                          sigma=6.0, donor=([0.7, 0.3], [4.0, 1.2]),
                          background=(5.0, 5.0), background_kind="scatter"),
              naive_kw=dict(alpha=0.0, delta=0.0, gamma=1.0, sigma=0.0, donor=3.16)),
]


def _model(cfg, n_states, irf_bins, **override):
    """A `FretDistanceEmission` matching `cfg`, with `override` applied.

    `override` is how a *naive* model is built: it states what the fit wrongly
    assumes, so each row records the omission explicitly rather than by omission.
    """
    o = cfg.optics
    kw = dict(donor=cfg.donor, tau_a=o.tau_a, r0=cfg.r0, sigma=cfg.sigma,
              alpha=o.alpha, delta=o.delta, gamma=o.gamma)
    kw.update(override)
    opt = Optics(alpha=kw.pop("alpha"), delta=kw.pop("delta"),
                 gamma=kw.pop("gamma"), tau_a=kw.pop("tau_a"))
    return FretDistanceEmission(
        n_states, optics=opt, n_bins=_NB,
        dt=cfg.microtime_resolution * (cfg.n_microtime_channels // _NB),
        irf=irf_bins, **kw)


def _true_efficiency(cfg, r):
    amp, tau = fret_lifetime_spectrum(r, cfg.donor, cfg.r0, cfg.sigma, 15)
    from .emission import donor_lifetime_spectrum
    d_amp, d_tau = donor_lifetime_spectrum(cfg.donor)
    return 1.0 - lifetime_averages(amp, tau)[0] / lifetime_averages(d_amp, d_tau)[0]


def _accuracy(data, model, truth, n_states):
    from .core import forward_backward_burst
    g = np.concatenate([forward_backward_burst(data, model.prior, model.trans,
                                               model.obs, b)[0]
                        for b in range(data.n_bursts)]).argmax(axis=1)
    ok = truth >= 0
    return max((g[ok] == np.array([p[t] for t in truth[ok]])).mean()
               for p in itertools.permutations(range(n_states)))


def run_condition(cond: Condition, seed=1, tttrlib=None, burst_ms=2.0):
    """Generate, fit naive and matched, and return one row of results."""
    if tttrlib is None:
        import tttrlib as _t
        tttrlib = _t

    cfg = _base_cfg(**cond.cfg_kw)
    if cond.scheme in ("slow", "fast", "intermediate", "static"):
        scheme = KineticScheme.from_regime(_TRUE_R, cond.scheme, burst_ms)
    else:
        scheme = KineticScheme.two_state(_TRUE_R, 0.4, 0.4)

    t0 = time.perf_counter()
    d = simulate(scheme, cfg, cond.photophysics, seed=seed,
                 n_micro_bins=_NB, burst_ms=burst_ms)
    data = PhotonData(d.times, d.streams, 2, micro=d.micro, n_micro_bins=_NB)
    truth = np.concatenate([np.array(s) for s in d.states])

    fine = np.array(list(tttrlib.SimDecay.gaussian_irf(
        cfg.n_microtime_channels, cfg.microtime_resolution,
        cfg.irf_mean, cfg.irf_fwhm)))
    irf = fine.reshape(_NB, -1).sum(axis=1)

    true_e = np.sort([_true_efficiency(cfg, r) for r in _TRUE_R])
    k = 0.4 * cfg.macro_resolution
    A0 = np.array([[1 - k, k], [k, 1 - k]])
    pri = np.array([0.5, 0.5])
    init = np.array([[50.0], [57.0]])

    out = {"condition": cond.name, "note": cond.note,
           "has_model_term": bool(cond.naive_kw),
           "n_bursts": data.n_bursts, "n_photons": data.n_photons,
           "true_E": true_e.tolist()}

    for label, override in (("matched", {}), ("naive", cond.naive_kw)):
        if label == "naive" and not override:
            out["naive_dE"] = out.get("matched_dE")
            out["naive_acc"] = out.get("matched_acc")
            continue
        em = _model(cfg, 2, irf, **override)
        model, par = fit_emission(data, em, init, A0, pri, max_iter=200, tol=1e-9)
        got_e = np.sort([em.efficiency(r) for r in par.ravel()])
        out[f"{label}_dE"] = float(np.abs(got_e - true_e).max())
        out[f"{label}_acc"] = float(_accuracy(data, model, truth, 2))

    out["seconds"] = round(time.perf_counter() - t0, 1)
    return out


def run_sweep(conditions=None, seed=1, verbose=True):
    rows = []
    for c in (conditions or CONDITIONS):
        try:
            r = run_condition(c, seed=seed)
        except Exception as exc:                      # keep the sweep going
            r = {"condition": c.name, "error": f"{type(exc).__name__}: {exc}"}
        rows.append(r)
        if verbose:
            print(_format_row(r), flush=True)
    return rows


def _format_row(r):
    if "error" in r:
        return f"{r['condition']:38s}  ERROR  {r['error'][:60]}"
    tag = "" if r.get("has_model_term") else "   [no model term]"
    return ("{condition:38s}  dE naive {naive_dE:.4f} / matched {matched_dE:.4f}"
            "   acc {naive_acc:.3f} / {matched_acc:.3f}   {seconds:5.1f}s").format(**r) + tag


def format_table(rows):
    head = (f"{'condition':38s}  {'dE naive':>9s} {'dE matched':>11s}"
            f" {'acc naive':>10s} {'acc matched':>12s}")
    lines = [head, "-" * len(head)]
    for r in rows:
        if "error" in r:
            lines.append(f"{r['condition']:38s}  ERROR {r['error'][:50]}")
        else:
            lines.append(f"{r['condition']:38s}  {r['naive_dE']:9.4f} {r['matched_dE']:11.4f}"
                         f" {r['naive_acc']:10.3f} {r['matched_acc']:12.3f}")
    return "\n".join(lines)


def write_results(rows, path=None):
    path = path or os.path.join("benchmarks", "results", "hmm_degradation.jsonl")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as fh:
        for r in rows:
            fh.write(json.dumps(r) + "\n")
    return path


if __name__ == "__main__":
    rows = run_sweep()
    print()
    print(format_table(rows))
    print()
    print("written to", write_results(rows))
