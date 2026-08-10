# `spectroscopy/hmm` — Hidden Markov Models for TTTR Data

Hidden Markov Model (HMM) algorithms for state transition analysis in single-molecule photon streams and time series.

## Contents

- **`HMM.h` / `HMM.cpp`**: Discrete state HMM algorithms (Viterbi decoding, Baum-Welch training, forward-backward algorithm).
- **`HMMBayes.h` / `HMMBayes.cpp`**: Bayesian HMM analysis and model selection.
- **`HMMConstraints.h` / `HMMConstraints.cpp`**: Parameter constraints for rate matrices and emission probabilities.

## Dependencies

- Depends on `core`, `util`.
