#!/usr/bin/env python3
"""NeuralNet / HMM-surrogate benchmark — tttrlib C++ vs scikit-learn vs EM.

Answers three questions the AD/surrogate design rests on, on the *real*
implementation rather than a standalone probe:

1. How does ``NeuralNet::train`` compare with ``sklearn.MLPRegressor`` in
   wall-clock and in held-out accuracy, on identical data?
2. How fast is the surrogate forward pass (feature extraction + net) compared
   with the scikit-learn path it replaces?
3. Does the surrogate actually beat Baum-Welch EM on wall-clock, and by how
   much does it give up in accuracy? This is the whole premise of the engine.

Run in the base env. Results append to ``results/nn.jsonl``.
"""
import numpy as np

import tttrlib
from common import bench, record, timeit

# ---- problem definition ----------------------------------------------------
N_STATES = 2
N_STREAMS = 2
N_BURSTS = 150
BURST_LEN = 80
MEAN_DT = 4.0
SEED = 12345

# Training-set sizes: small enough to run in a benchmark, large enough that the
# per-sample simulation cost dominates the fixed overheads.
N_TRAIN = 400
HIDDEN = [256, 256, 128]


def _train_options(max_iter=200):
    opt = tttrlib.TrainOptions()
    opt.hidden_layer_sizes = tttrlib.VectorInt32(HIDDEN)
    opt.max_iter = max_iter
    opt.batch_size = 200
    opt.learning_rate = 1e-3
    opt.early_stopping = True
    opt.seed = SEED
    return opt


def _simulate_dataset(e_lo, e_hi, k, rng, n_bursts=N_BURSTS, burst_len=BURST_LEN):
    """Simulate a two-state kinetic dataset and load it into an HMM engine."""
    times, streams = [], []
    for _ in range(n_bursts):
        t = np.concatenate(
            [[0], np.cumsum(rng.poisson(MEAN_DT, burst_len - 1) + 1)]
        ).astype(np.int64)
        # random-telegraph hidden state, switching with probability k per photon
        s = np.empty(burst_len, dtype=np.int32)
        state = rng.random() < 0.5
        for j in range(burst_len):
            if j and rng.random() < k:
                state = not state
            s[j] = rng.random() < (e_hi if state else e_lo)
        times.append(t)
        streams.append(s)

    engine = tttrlib.HMM()
    engine.set_bursts(
        tttrlib.VectorVectorInt64([tttrlib.VectorInt64(t.tolist()) for t in times]),
        tttrlib.VectorVectorInt32([tttrlib.VectorInt32(s.tolist()) for s in streams]),
        N_STREAMS,
    )
    return engine


def main():
    rng = np.random.default_rng(SEED)

    # ---- 1. training-set generation (the dominant cost of building a surrogate)
    gen = lambda: tttrlib.HmmSurrogate.generate_training_set(
        N_STATES, N_STREAMS, n_samples=N_TRAIN, n_bursts=N_BURSTS,
        burst_len=BURST_LEN, mean_dt=MEAN_DT, seed=SEED)
    bench("nn", "tttrlib", "generate_training_set", gen,
          repeat=3, warmup=0, n_items=N_TRAIN, unit="datasets",
          extra={"n_bursts": N_BURSTS, "burst_len": BURST_LEN})
    X, Y = gen()
    print(f"    training set: X{X.shape} Y{Y.shape}")

    # ---- 2. training: tttrlib vs scikit-learn on identical data
    opt = _train_options()
    bench("nn", "tttrlib", "train_mlp",
          lambda: tttrlib.NeuralNet.train_np(X, Y, opt),
          repeat=3, warmup=0, n_items=N_TRAIN, unit="samples",
          extra={"hidden": HIDDEN, "max_iter": opt.max_iter})
    net_cpp = tttrlib.NeuralNet.train_np(X, Y, opt)

    try:
        from sklearn.neural_network import MLPRegressor
        from sklearn.preprocessing import StandardScaler
    except ImportError:
        MLPRegressor = None
        record("nn", "sklearn", "train_mlp", None, None, None, status="skipped")

    if MLPRegressor is not None:
        xs, ys = StandardScaler().fit(X), StandardScaler().fit(Y)
        Xs, Ys = xs.transform(X), ys.transform(Y)

        def fit_sklearn():
            m = MLPRegressor(hidden_layer_sizes=tuple(HIDDEN), activation="relu",
                             max_iter=opt.max_iter, early_stopping=True,
                             random_state=SEED)
            m.fit(Xs, Ys)
            return m

        bench("nn", "sklearn", "train_mlp", fit_sklearn,
              repeat=3, warmup=0, n_items=N_TRAIN, unit="samples",
              extra={"hidden": HIDDEN, "max_iter": opt.max_iter})
        mlp = fit_sklearn()

        # held-out accuracy on freshly simulated data, same metric for both
        Xte, Yte = tttrlib.HmmSurrogate.generate_training_set(
            N_STATES, N_STREAMS, n_samples=100, n_bursts=N_BURSTS,
            burst_len=BURST_LEN, mean_dt=MEAN_DT, seed=SEED + 999)
        mae_cpp = float(np.abs(net_cpp.predict_batch_np(Xte) - Yte).mean())
        mae_skl = float(np.abs(ys.inverse_transform(mlp.predict(xs.transform(Xte))) - Yte).mean())
        print(f"    held-out MAE   tttrlib={mae_cpp:.5f}   sklearn={mae_skl:.5f}")
        record("nn", "compare", "heldout_mae", None, None, None,
               extra={"tttrlib": mae_cpp, "sklearn": mae_skl})

    # ---- 3. forward pass: feature extraction + net, on a real dataset
    surrogate = tttrlib.HmmSurrogate(net_cpp, N_STATES, N_STREAMS)
    engine = _simulate_dataset(0.25, 0.75, 0.02, rng)
    print(f"    eval dataset: {engine.get_n_bursts()} bursts, "
          f"{engine.get_n_photons()} photons")

    bench("nn", "tttrlib", "extract_features",
          lambda: tttrlib.HmmSurrogate.features(engine),
          repeat=7, n_items=engine.get_n_photons(), unit="photons")
    bench("nn", "tttrlib", "surrogate_predict",
          lambda: surrogate.predict(engine),
          repeat=7, n_items=engine.get_n_photons(), unit="photons")

    # ---- 4. the premise: surrogate vs Baum-Welch EM, speed and accuracy
    bench("nn", "tttrlib", "em_fit",
          lambda: engine.fit(N_STATES, 1, 500, 1e-7, SEED, True, False),
          repeat=3, warmup=0, n_items=engine.get_n_photons(), unit="photons")

    t_sur, _, _ = timeit(lambda: surrogate.predict(engine), repeat=7)
    t_em, _, _ = timeit(lambda: engine.fit(N_STATES, 1, 500, 1e-7, SEED, True, False),
                        repeat=3, warmup=0)
    print(f"    surrogate is {t_em / t_sur:.1f}x faster than EM "
          f"({t_sur*1e3:.2f} ms vs {t_em*1e3:.1f} ms)")

    # ---- 5. accuracy against ground truth, reported per trial
    #
    # Read this carefully rather than as a headline. EM maximises likelihood,
    # which is NOT the same as being close to the truth on finite data: when the
    # two states are barely separated the likelihood surface is flat and the MLE
    # drifts, so more restarts can find a *higher* likelihood that is *less*
    # accurate (verified: loglik -8052.873 -> -8052.838 while |E| error went
    # 0.027 -> 0.080). The surrogate, being regressed over a prior on models,
    # falls back toward that prior on ambiguous data and can look better there.
    # So an averaged win for the surrogate is dominated by the hard trials and
    # should not be quoted as "the surrogate is more accurate than EM".
    rows = []
    for trial in range(8):
        r = np.random.default_rng(SEED + 7000 + trial)
        e_lo, e_hi = sorted(r.uniform(0.15, 0.85, 2))
        eng = _simulate_dataset(e_lo, e_hi, 0.02, r)
        truth = np.array([e_lo, e_hi])
        m_em = eng.fit(N_STATES, 1, 500, 1e-7, SEED, True, False)
        e_sur = float(np.abs(np.sort(surrogate.predict(eng).obs_np[:, 1]) - truth).mean())
        e_em = float(np.abs(np.sort(m_em.obs_np[:, 1]) - truth).mean())
        rows.append({"separation": float(e_hi - e_lo), "surrogate": e_sur, "em": e_em})

    print(f"    {'separation':>10} {'surrogate':>10} {'EM':>10}")
    for row in sorted(rows, key=lambda d: d["separation"]):
        print(f"    {row['separation']:>10.3f} {row['surrogate']:>10.4f} {row['em']:>10.4f}")
    n_em_better = sum(1 for row in rows if row["em"] < row["surrogate"])
    print(f"    EM more accurate in {n_em_better}/{len(rows)} trials")
    record("nn", "compare", "fret_error", None, None, None,
           extra={"trials": rows, "em_better": n_em_better, "speedup": t_em / t_sur})


if __name__ == "__main__":
    main()
