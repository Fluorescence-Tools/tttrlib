"""Time-domain recursion vs. frequency-domain product, for the same convolution.

A periodic multiexponential convolved with an instrument response can be
computed two ways, and the received wisdom — "convolution is a multiplication in
frequency space, so use an FFT" — is wrong here. Both paths are
``O(n_bins * n_rates)``:

* the **recursion** (``fconv_per_cs``) costs one multiply-add per rate and bin,
  and is SIMD-optimised;
* the **spectral** path has a closed form for the periodic decay,
  ``1 / (1 - exp(-k) exp(-2 pi i w / n))``, which costs one *complex division*
  per rate and frequency, plus one inverse transform.

The transform is not what dominates — evaluating the closed form is — so the
frequency domain buys no better scaling, only worse constants. This script
measures that across problem sizes and rate counts, and checks the two agree.

Run with ``python bench_convolution.py``; results land in
``results/convolution.jsonl``.
"""
import numpy as np

import tttrlib

from common import record, timeit


def gaussian_irf(n_bins, centre_frac=0.1, width_frac=0.012):
    """A compact instrument response — one pulse, near zero at both ends.

    The width scales with the period so the pulse stays compact at every size.
    A fixed width would make the response *wrap* at small ``n_bins``, and the
    recursion cannot see a response that wraps (it assumes nothing precedes bin
    0), so the two backends would then differ for reasons that have nothing to
    do with the timing being measured.
    """
    i = np.arange(n_bins)
    return np.exp(-0.5 * ((i - centre_frac * n_bins) / (width_frac * n_bins)) ** 2)


def rate_spectrum(n_rates, n_bins):
    """``n_rates`` rates per bin, spread over the decays a period can resolve.

    The fast end is a rate that decays within a tenth of the period; the slow end
    one that barely decays at all. That is the range a real donor(x)FRET product
    spans, and it is where the two methods must agree.
    """
    taus = np.geomspace(0.05 * n_bins, 2.0 * n_bins, n_rates)
    rates = 1.0 / taus
    weights = np.full(n_rates, 1.0 / n_rates)
    return rates.tolist(), weights.tolist()


def relative_difference(a, b):
    """Mean |a - b| over the bins that carry signal, relative to the mean of a."""
    a = np.asarray(a) / np.sum(a)
    b = np.asarray(b) / np.sum(b)
    return float(np.abs(a - b).mean() / np.abs(a).mean())


def main():
    # (n_bins, n_rates): a FLIM pixel decay through to a 64-term donor(x)FRET
    # outer product on a finely binned decay.
    cases = [
        (64, 1), (256, 1), (1024, 1), (4096, 1),
        (1024, 4), (1024, 16), (1024, 64),
        (4096, 64),
    ]

    for n_bins, n_rates in cases:
        irf = gaussian_irf(n_bins).tolist()
        rates, weights = rate_spectrum(n_rates, n_bins)

        def recursive():
            return tttrlib.dfa_convolve(rates, weights, irf, n_bins, 0.0, 0)

        def spectral():
            return tttrlib.dfa_convolve(rates, weights, irf, n_bins, 0.0, 1)

        agreement = relative_difference(recursive(), spectral())

        for label, fn in (("recursive", recursive), ("spectral", spectral)):
            best, mean, times = timeit(fn, repeat=7, number=20)
            record(
                "convolution", "tttrlib", f"convolve-{label}",
                best, mean, times,
                n_items=1, unit="convolution",
                dataset=f"n_bins={n_bins},n_rates={n_rates}",
                extra={
                    "method": label,
                    "n_bins": n_bins,
                    "n_rates": n_rates,
                    "agreement_rel": agreement,
                },
            )

    # The sub-bin timeshift is the one place the spectral machinery is not
    # optional: the recursion cannot express a fractional shift, so it borrows
    # one transform of the *response*. Measure what that borrowing costs, since
    # a fit that floats the shift pays it on every evaluation.
    n_bins, n_rates = 1024, 16
    irf = gaussian_irf(n_bins).tolist()
    rates, weights = rate_spectrum(n_rates, n_bins)
    for shift, label in ((0.0, "no-shift"), (0.37, "sub-bin-shift")):
        best, mean, times = timeit(
            lambda s=shift: tttrlib.dfa_convolve(rates, weights, irf, n_bins, s, 0),
            repeat=7, number=20)
        record(
            "convolution", "tttrlib", f"convolve-recursive-{label}",
            best, mean, times,
            n_items=1, unit="convolution",
            dataset=f"n_bins={n_bins},n_rates={n_rates}",
            extra={"method": "recursive", "shift_bins": shift,
                   "n_bins": n_bins, "n_rates": n_rates},
        )


if __name__ == "__main__":
    main()
