"""Record the reference fixture for ``test_math_ab_probabilistic.py``.

Runs under an environment that has **hmmlearn** and **filterpy** installed
(neither is a tttrlib dependency, and neither belongs in the test
environment), and writes
``test/data/reference/math_ab_probabilistic_reference.npz``. The inputs are
stored in the file alongside the reference outputs, so the test does not
depend on any random stream being reproducible across NumPy versions.

    uv venv /tmp/refenv && uv pip install -p /tmp/refenv/bin/python \\
        hmmlearn filterpy numpy
    /tmp/refenv/bin/python test/python/misc/gen_math_ab_probabilistic_reference.py

What is recorded, and from what:

* HMM lattice cases -- ``hmmlearn._hmmc`` ``forward_log`` (log-likelihood
  and forward lattice), ``backward_log``, the log-normalised posteriors,
  ``exp(compute_log_xi_sum)`` and ``viterbi`` (score and path). Random
  models of 2, 3 and 5 states, one with a forbidden transition and one with
  a frame a state cannot explain, so the ``-inf`` conventions are compared
  as well as the arithmetic.
* Kalman cases -- ``filterpy.kalman.KalmanFilter`` with ``F = H = I`` driven
  the way ``Kalman.h`` documents its model: ``P_pred = P + Q``, the Poisson
  measurement noise ``R = r_scale * max(x, 1e-12) / dt`` rebuilt from the
  current state every bin, and the innovation's Mahalanobis distance. Two-,
  one- and three-channel traces. filterpy uses the Joseph form for the
  covariance update, so this is an independent arrangement of the same
  filter, not a transcription.
"""

import os
import sys

import numpy as np

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..",
                   "data", "reference", "math_ab_probabilistic_reference.npz")


def hmm_cases():
    from hmmlearn import _hmmc
    from hmmlearn.utils import log_normalize

    rng = np.random.default_rng(20260817)
    out = {}
    specs = [
        ("k2_t60", 2, 60, None),
        ("k3_t200", 3, 200, None),
        ("k5_t300", 5, 300, None),
        ("k2_t1", 2, 1, None),
        ("k3_forbidden", 3, 120, "forbidden"),
        ("k3_deadframe", 3, 120, "deadframe"),
    ]
    for name, k, t, twist in specs:
        start = rng.random(k) + 0.1
        start /= start.sum()
        trans = rng.random((k, k)) + 0.1
        if twist == "forbidden":
            trans[0, 2] = 0.0
        trans /= trans.sum(axis=1, keepdims=True)
        frame = rng.random((t, k)) * 0.9 + 0.05
        log_frame = np.log(frame)
        if twist == "deadframe":
            log_frame[t // 2, 1] = -np.inf
        log_frame = np.ascontiguousarray(log_frame)

        log_prob, fwd = _hmmc.forward_log(start, trans, log_frame)
        bwd = _hmmc.backward_log(start, trans, log_frame)
        log_gamma = fwd + bwd
        log_normalize(log_gamma, axis=1)
        with np.errstate(under="ignore"):
            post = np.exp(log_gamma)
            if t > 1:
                xi = np.exp(_hmmc.compute_log_xi_sum(fwd, trans, bwd, log_frame))
            else:
                xi = np.zeros((k, k))
        vscore, vpath = _hmmc.viterbi(start, trans, log_frame)

        with np.errstate(divide="ignore"):
            out[f"hmm/{name}/log_startprob"] = np.log(start)
            out[f"hmm/{name}/log_transmat"] = np.log(trans)
        out[f"hmm/{name}/log_frameprob"] = log_frame
        out[f"hmm/{name}/log_prob"] = np.asarray(log_prob)
        out[f"hmm/{name}/fwd"] = fwd
        out[f"hmm/{name}/bwd"] = bwd
        out[f"hmm/{name}/posteriors"] = post
        out[f"hmm/{name}/xi_sum"] = xi
        out[f"hmm/{name}/viterbi_score"] = np.asarray(vscore)
        out[f"hmm/{name}/viterbi_path"] = np.asarray(vpath, dtype=np.int64)
    return out


def kalman_cases():
    from filterpy.kalman import KalmanFilter

    rng = np.random.default_rng(7)
    out = {}
    specs = [
        ("dim2", 2, 800, 1e-3, 1.0),
        ("dim2_rscale", 2, 300, 2e-3, 1.7),
        ("dim1", 1, 400, 1e-3, 1.0),
        ("dim3", 3, 250, 5e-4, 1.2),
    ]
    for name, dim, T, dt, r_scale in specs:
        rates = rng.uniform(2e3, 1e5, size=dim)
        step = rng.uniform(0.3, 3.0, size=dim)
        true = np.where(np.arange(T)[:, None] < T // 2, rates, rates * step)
        y = rng.poisson(true * dt).astype(np.float64) / dt
        x0 = rates.copy()
        P0 = np.eye(dim) * 1e6
        Q = np.eye(dim) * 100.0

        kf = KalmanFilter(dim_x=dim, dim_z=dim)
        kf.x = x0.copy()
        kf.P = P0.copy()
        kf.F = np.eye(dim)
        kf.H = np.eye(dim)
        kf.Q = Q.copy()
        xs = np.empty((T, dim))
        Ps = np.empty((T, dim, dim))
        Ds = np.empty(T)
        for t in range(T):
            x_now = kf.x.copy()
            kf.R = np.diag(r_scale * np.maximum(x_now, 1e-12) / dt)
            kf.predict()
            kf.update(y[t])
            xs[t] = kf.x
            Ps[t] = kf.P
            Ds[t] = kf.mahalanobis
        out[f"kalman/{name}/y"] = y
        out[f"kalman/{name}/x0"] = x0
        out[f"kalman/{name}/P0"] = P0
        out[f"kalman/{name}/Q"] = Q
        out[f"kalman/{name}/dt"] = np.asarray(dt)
        out[f"kalman/{name}/r_scale"] = np.asarray(r_scale)
        out[f"kalman/{name}/x_filt"] = xs
        out[f"kalman/{name}/P_filt"] = Ps
        out[f"kalman/{name}/D"] = Ds
    return out


def main():
    import hmmlearn
    import filterpy
    data = {}
    data.update(hmm_cases())
    data.update(kalman_cases())
    data["provenance"] = np.asarray(
        f"hmmlearn {hmmlearn.__version__}; filterpy {filterpy.__version__}; "
        f"numpy {np.__version__}; python {sys.version.split()[0]}")
    np.savez_compressed(OUT, **data)
    print(f"wrote {OUT} ({len(data)} arrays): {data['provenance']}")


if __name__ == "__main__":
    main()
