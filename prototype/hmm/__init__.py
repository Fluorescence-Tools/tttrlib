"""
Photon-stream HMM prototype — NumPy/Numba development implementation.

The physics and the inference are worked out here first, validated against the
brute-force oracle in ``test/python/hmm/toy.py``, and ported to C++ only once
they are right.  ``prototype/esrrf`` -> ``src/CLSMSuperRes.cpp`` took the same
route.

What is here:

- ``core``        — CSR photon layout and the Numba E-step (the shared kernel)
- ``constraints`` — Dirichlet priors (soft) and fixed entries (hard)
- ``fit``         — maximum likelihood (classic H2MM) and MAP
- ``special``     — digamma / lgamma / Dirichlet KL, without SciPy
- ``vb``          — variational Bayes: posteriors and the ELBO (the workhorse)
- ``gibbs``       — exact blocked Gibbs with tick-level bridges (the reference)
- ``emission``    — lifetime-resolved emission over the product alphabet, and
                    the physical parameterisations that generate it
- ``calibration`` — simulation-based calibration: are the error bars honest?

Run validation:  python -m pytest prototype/hmm/test_prototype.py -q
"""

from .core import PhotonData, e_step, matrix_powers, rho_powers, forward_backward_burst
from .constraints import HmmConstraints, row_normalize
from .fit import HmmModel, fit, random_model
from .vb import HmmVB, fit_vb
from .special import digamma, gammaln, dirichlet_kl
from .gibbs import (HmmPosterior, HmmPhysicalPosterior, gibbs, gibbs_physical,
                    sample_paths_and_counts, split_rhat, ess)
from .calibration import (sbc_ranks, rank_histogram, uniformity_pvalue,
                          outer_mass_ratio, summarize, SbcResult,
                          add_background_photons, SbcPrior,
                          TruncatedNormalPrior1D)
from .special import gammainc_upper, chi2_sf
from .emission import (DecayState, FretEmission, FretDistanceEmission, Optics,
                       Anisotropy,
                       LifetimeEmission, build_emission,
                       donor_lifetime_spectrum, gaussian_distance_distribution,
                       fret_lifetime_spectrum, lifetime_averages,
                       fit_emission, gaussian_irf, multi_exponential,
                       convolve_pattern, simulate_lifetime)

__all__ = [
    "PhotonData",
    "e_step",
    "matrix_powers",
    "rho_powers",
    "forward_backward_burst",
    "HmmConstraints",
    "row_normalize",
    "HmmModel",
    "fit",
    "random_model",
    "HmmVB",
    "fit_vb",
    "digamma",
    "gammaln",
    "dirichlet_kl",
    "HmmPosterior",
    "gibbs",
    "gibbs_physical",
    "HmmPhysicalPosterior",
    "sample_paths_and_counts",
    "split_rhat",
    "ess",
    "DecayState",
    "FretEmission",
    "FretDistanceEmission",
    "Optics",
    "Anisotropy",
    "donor_lifetime_spectrum",
    "gaussian_distance_distribution",
    "fret_lifetime_spectrum",
    "lifetime_averages",
    "LifetimeEmission",
    "build_emission",
    "fit_emission",
    "gaussian_irf",
    "multi_exponential",
    "convolve_pattern",
    "simulate_lifetime",
    "sbc_ranks",
    "rank_histogram",
    "uniformity_pvalue",
    "outer_mass_ratio",
    "summarize",
    "SbcResult",
    "add_background_photons",
    "SbcPrior",
    "TruncatedNormalPrior1D",
    "gammainc_upper",
    "chi2_sf",
]
