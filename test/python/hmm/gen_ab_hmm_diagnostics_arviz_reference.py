#!/usr/bin/env python
"""Record ArviZ's split R-hat and ESS for ``TestPosteriorDiagnosticsAgainstArviz``.

Runs under the sciref venv (``benchmarks/.venvs/sciref/bin/python``). The draws
are synthetic AR(1) chains built here with NumPy (stored beside the outputs) in
the ``HmmPosterior`` parameter layout ``[prior | trans | obs]`` for two states
and two symbols, with the symbol-0 emissions of the two states well separated
so tttrlib's canonical relabelling is the identity. Two cases: chains that
agree in mean, and chains offset by half a standard deviation (where an ESS
that ignores between-chain variance reports ~N).

    benchmarks/.venvs/sciref/bin/python test/python/hmm/gen_ab_hmm_diagnostics_arviz_reference.py
"""
import os
import warnings

import numpy as np

warnings.simplefilter("ignore")
import arviz as az  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "data", "reference",
                   "hmm_diagnostics_arviz_reference.npz")


def draws(offset, seed, n_chains=2, n_draws=1000, n_par=10):
    rng = np.random.default_rng(seed)
    x = np.zeros((n_chains, n_draws, n_par))
    phis = np.linspace(0.0, 0.9, n_par)
    for c in range(n_chains):
        for k in range(n_par):
            e = rng.normal(size=n_draws)
            z = np.empty(n_draws)
            z[0] = e[0]
            for t in range(1, n_draws):
                z[t] = phis[k] * z[t - 1] + e[t]
            x[c, :, k] = z * 0.02 + (offset if c == 1 else 0.0)
    x[:, :, 6] += 0.9          # obs[0, 0]
    x[:, :, 8] += 0.2          # obs[1, 0]  -> canonical order fixed
    return x


def main():
    out = {"cases": np.array(["agreeing", "offset", "single_chain"]), "arviz_version": np.array(az.__version__)}
    for name, x in (("agreeing", draws(0.0, 0)), ("offset", draws(0.01, 1)), ("single_chain", draws(0.0, 2)[:1])):
        out[f"{name}/draws"] = x
        n_par = x.shape[2]
        out[f"{name}/rhat_split"] = np.array([float(az.rhat(x[:, :, k], method="split")) if x.shape[0] > 1 else np.nan
                                              for k in range(n_par)])
        out[f"{name}/ess_mean"] = np.array([float(az.ess(x[:, :, k], method="mean")) for k in range(n_par)])
        print(name, "rhat", np.round(out[f"{name}/rhat_split"], 4), "ess", np.round(out[f"{name}/ess_mean"], 1))
    np.savez(OUT, **out)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
