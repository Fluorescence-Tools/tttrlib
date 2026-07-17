#!/usr/bin/env python3
"""H2MM benchmark — tttrlib engine (base env) + shared-input writer.

Writes a deterministic simulated burst dataset and a fixed initial model to
``results/shared/h2mm/`` so every competitor (the reference C library
``H2MM_C`` and the ChiSurf numba engine) optimises the *identical* problem from
the *identical* starting point.  Then times the tttrlib C++ engine's EM
optimisation (fixed iteration budget, acceleration off for an apples-to-apples
per-map comparison — the SQUAREM win is reported separately) and Viterbi.

Run this first (base env), then the competitors in their venvs.
"""
import os

import numpy as np

import tttrlib
from common import bench, RESULTS

SHARED = os.path.join(RESULTS, "shared", "h2mm")
os.makedirs(SHARED, exist_ok=True)

# ---- problem definition (kept small enough to fit any env, large enough to
# make the hot loop dominate) --------------------------------------------------
N_STATES = 3
N_STREAMS = 2
N_BURSTS = 400
BURST_LEN = 500
MEAN_GAP = 40          # mean inter-photon macro-time gap (ticks)
MAX_ITER = 500         # EM-map cap
TOL = 1e-7             # convergence threshold on the log-likelihood increment
SEED = 12345


def _make_true_model():
    prior = np.full(N_STATES, 1.0 / N_STATES)
    trans = np.full((N_STATES, N_STATES), 5e-4)
    np.fill_diagonal(trans, 1.0 - 5e-4 * (N_STATES - 1))
    # three FRET states with distinct acceptor fractions
    e = np.linspace(0.2, 0.8, N_STATES)
    obs = np.column_stack([1 - e, e])
    return prior, trans, obs


def _simulate():
    rng = np.random.default_rng(SEED)
    times = [
        np.cumsum(rng.integers(1, 2 * MEAN_GAP, size=BURST_LEN)).astype(np.int64)
        for _ in range(N_BURSTS)
    ]
    prior, trans, obs = _make_true_model()
    true = tttrlib.H2mmModel(list(prior.ravel()), list(trans.ravel()), list(obs.ravel()))
    streams = tttrlib.H2MM.simulate_bursts(true, [list(t) for t in times], SEED + 1)
    streams = [np.asarray(s, dtype=np.int8) for s in streams]
    return times, streams


def _write_shared(times, streams, init):
    # CSR-style concatenation so numpy/H2MM_C competitors can rebuild per-burst.
    offsets = np.zeros(len(times) + 1, dtype=np.int64)
    offsets[1:] = np.cumsum([len(t) for t in times])
    np.savez(
        os.path.join(SHARED, "data.npz"),
        times=np.concatenate(times).astype(np.int64),
        streams=np.concatenate(streams).astype(np.int8),
        offsets=offsets,
        n_streams=N_STREAMS,
        init_prior=init.prior_np,
        init_trans=init.trans_np,
        init_obs=init.obs_np,
        max_iter=MAX_ITER,
        tol=TOL,
    )


def main():
    times, streams = _simulate()
    init = tttrlib.H2MM.factory_model(N_STATES, N_STREAMS, 1e-3, 7)
    _write_shared(times, streams, init)

    eng = tttrlib.H2MM()
    eng.set_bursts([list(map(int, t)) for t in times],
                   [list(map(int, s)) for s in streams], N_STREAMS)
    n_phot = eng.get_n_photons()
    print(f"[h2mm] {eng.get_n_bursts()} bursts, {n_phot} photons, "
          f"{len(eng.get_unique_dt())} unique Δt")

    # Plain EM to convergence (tol) — directly comparable to H2MM_C / numba
    # plain EM (identical init + tol -> identical EM trajectory & map count).
    fit_pl = eng.optimize(init, MAX_ITER, TOL, 1e-12, False)
    row = bench("h2mm", "tttrlib (plain EM)", f"EM {N_STATES}-state (plain, tol={TOL:g})",
                lambda: eng.optimize(init, MAX_ITER, TOL, 1e-12, False),
                repeat=5, warmup=1,
                n_items=n_phot, unit="photon", dataset="simulated",
                extra={"n_states": N_STATES, "n_bursts": N_BURSTS,
                       "n_iter": fit_pl.n_iter, "accelerate": False})

    # SQUAREM acceleration: same fixed point, far fewer maps (algorithmic win).
    fit_sq = eng.optimize(init, MAX_ITER, TOL, 1e-12, True)
    bench("h2mm", "tttrlib (SQUAREM)", f"EM {N_STATES}-state (SQUAREM)",
          lambda: eng.optimize(init, MAX_ITER, TOL, 1e-12, True),
          repeat=5, warmup=1,
          n_items=n_phot, unit="photon", dataset="simulated",
          extra={"n_iter": fit_sq.n_iter, "accelerate": True})
    print(f"[h2mm] SQUAREM converged in {fit_sq.n_iter} maps "
          f"vs plain EM {fit_pl.n_iter} maps "
          f"(logL {fit_sq.loglik:.2f} / {fit_pl.loglik:.2f})")

    def run_viterbi():
        eng.viterbi_path(fit_sq)
    bench("h2mm_viterbi", "tttrlib", "Viterbi path decode", run_viterbi, repeat=5,
          n_items=n_phot, unit="photon", dataset="simulated")

    print(f"[h2mm] tttrlib plain-EM best={row['best_s']*1e3:.1f} ms")


if __name__ == "__main__":
    main()
