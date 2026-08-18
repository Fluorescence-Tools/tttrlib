# `spectroscopy/hmm` — Hidden Markov Models for TTTR Data

Hidden Markov Model (HMM) algorithms for state transition analysis in single-molecule photon streams and time series.

## Contents

- **`HMM.h` / `HMM.cpp`**: Discrete state HMM algorithms (Viterbi decoding, Baum-Welch training, forward-backward algorithm).
- **`HMMBayes.h` / `HMMBayes.cpp`**: Bayesian HMM analysis and model selection.
- **`HMMVB.h` / `HMMVB.cpp`**: mean-field variational Bayes (`fit_vb`): Dirichlet posterior over (π, A, B) with the same forward-backward engine, `elbo` = Beal's bound at the returned posterior (`elbo_normalised` is the iteration's convergence variable, K(K−1)/2 nat higher). A/B: hmmlearn `VariationalCategoricalHMM` on dense streams — posterior 1e-4, bound 2e-10, 13× faster (`benchmarks/check_sciref.py`, `test/python/hmm/test_ab_hmm_reference.py`). Example: `examples/single_molecule/plot_hmm_variational_bayes.py`.
- **`HMMConstraints.h` / `HMMConstraints.cpp`**: parameter constraints for rate
  matrices and emission probabilities (ties, bounds, fixed entries).
- **`HMMRestraints.h` / `HMMRestraints.cpp`**: soft restraints — a prior on a
  parameter, added to the likelihood rather than imposed on it (the prior kinds
  are [`decay`](../decay)'s `DecayFitPrior`).
- **`HMMEmission.h`**: the emission models a state can carry — channel
  probabilities, micro-time (lifetime) distributions and the product alphabet
  of the two.
- **`HMMState.cpp`**: the state object: its emission, its dwell statistics and
  the sidecar a decoded stream is written back as.
- **`HMMSurrogate.h` / `HMMSurrogate.cpp`**: the neural surrogate that predicts
  a starting HMM from stream features, so EM begins near the answer
  (`NeuralNet` from [`math`](../../math), trained on simulated streams).

## Dependencies

- Depends on `math`, `core`, `burst`, `decay`, `simulation`, nlohmann/json.
