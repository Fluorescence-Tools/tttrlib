"""
Convolving a decay: the recursion or the transform?
===================================================

Before a model decay can be compared against data it has to be convolved with
the instrument response. tttrlib can do that two ways, and this tutorial shows
what each is for — because the obvious reason to pick one of them is the wrong
one.

The received wisdom is "convolution is a multiplication in frequency space, so
use an FFT". That is sound advice for convolving a response with an arbitrary
signal. For a decay built out of exponentials it is not: the periodic decay has
a *closed form* in frequency space, and evaluating it costs one complex division
per rate and per frequency, against one multiply–add per rate and per bin for the
time-domain recursion. Both are ``O(n_bins x n_rates)`` — the transform buys no
better scaling, only worse constants.

So we will do three things: check that the two agree, measure which is faster,
and then look at the situations where the slower one is the only one that works.

This is about *choosing* a backend for a decay built from a rate spectrum. For a
tour of the low-level convolution routines themselves — ``fconv``, ``fconv_per``,
their SIMD variants and what each is for — see the "Convolution routines"
example instead.
"""

# %%
import time

import matplotlib.pylab as plt
import numpy as np

import tttrlib

RECURSIVE, SPECTRAL = 0, 1

# %%
# A decay and an instrument to measure it with
# --------------------------------------------
#
# Rates are given **per bin** — that is ``dt / tau`` — so the numbers below
# describe a decay of 50 bins and one of 12 bins within a 1024-bin excitation
# period. The instrument response is a narrow pulse near the start of the
# period, which is what a real one looks like.

n_bins = 1024
rates = [1.0 / 50.0, 1.0 / 12.0]
weights = [0.7, 0.3]

i = np.arange(n_bins)
irf = np.exp(-0.5 * ((i - 100.0) / 12.0) ** 2)

recursive = np.asarray(
    tttrlib.dfa_convolve(rates, weights, irf.tolist(), n_bins, 0.0, RECURSIVE))
spectral = np.asarray(
    tttrlib.dfa_convolve(rates, weights, irf.tolist(), n_bins, 0.0, SPECTRAL))

fig, ax = plt.subplots(2, 1, sharex=True, figsize=(7, 6),
                       gridspec_kw={"height_ratios": [3, 1]})
ax[0].semilogy(recursive, label="recursive (default)")
ax[0].semilogy(spectral, "--", label="spectral")
ax[0].semilogy(irf / irf.sum(), color="0.7", label="instrument response")
ax[0].set_ylabel("intensity")
ax[0].legend()
ax[1].plot(recursive - spectral)
ax[1].set_xlabel("bin")
ax[1].set_ylabel("difference")
plt.tight_layout()

# %%
# The curves are not merely close — they agree to machine precision.

agreement = np.abs(recursive - spectral).max() / recursive.max()
print("largest relative difference: %.2e" % agreement)

# %%
# That is a stronger statement than it looks, and it did not come for free.
#
# The recursion applies the trapezoid rule to the convolution integral. Work out
# which kernel that leaves and it is ``exp(-k L)`` at every lag ``L``, except at
# ``L = 0`` where it leaves *one half* — the trapezoid rule evaluated across the
# exponential's jump from 0 to 1. Halving one sample of a kernel is subtracting
# half a delta, and a delta has a flat spectrum, so the entire difference between
# the two methods is a constant ``1/2`` subtracted from the periodic spectrum.
#
# Skip that correction and the two differ by ``(1 + exp(-k)) / 2``. That factor
# depends on the **rate**, so it does not divide out of a rate spectrum — it
# reweights it, by 0.5% at ``k = 0.01`` and 5% at ``k = 0.1``. In a model whose
# whole purpose is to measure the *relative* weights of a FRET-rate
# distribution, that would be an error in the answer rather than in the last
# digit.

for k in (0.005, 0.01, 0.05, 0.1, 0.3):
    print("k = %5.3f per bin -> uncorrected discrepancy would be %5.2f%%"
          % (k, 100.0 * (1.0 - (1.0 + np.exp(-k)) / 2.0)))

# %%
# Which is faster?
# ----------------
#
# Time both across a range of rate counts. The interesting quantity is not the
# absolute time — that is hardware — but how the ratio moves.


def timed(method, n_rates, repeat=50):
    """Best-of-`repeat` wall time for one convolution, in microseconds."""
    taus = np.geomspace(10.0, 500.0, n_rates)
    rs, ws = (1.0 / taus).tolist(), [1.0 / n_rates] * n_rates
    best = np.inf
    for _ in range(repeat):
        t0 = time.perf_counter()
        tttrlib.dfa_convolve(rs, ws, irf.tolist(), n_bins, 0.0, method)
        best = min(best, time.perf_counter() - t0)
    return best * 1e6


counts = [1, 2, 4, 8, 16, 32, 64]
t_rec = [timed(RECURSIVE, n) for n in counts]
t_spec = [timed(SPECTRAL, n) for n in counts]

plt.figure(figsize=(7, 4))
plt.plot(counts, t_rec, "o-", label="recursive")
plt.plot(counts, t_spec, "s-", label="spectral")
plt.xlabel("number of rates")
plt.ylabel("time per convolution / µs")
plt.xscale("log", base=2)
plt.yscale("log")
plt.legend()
plt.tight_layout()

speedup = np.asarray(t_spec) / np.asarray(t_rec)
for n, s in zip(counts, speedup):
    print("%3d rates: spectral is %.1fx slower" % (n, s))

# %%
# The recursion wins everywhere, and the gap *widens* with the rate count. That
# is exactly the regime a donor ⊗ FRET ⊗ anisotropy model lives in — its rate
# spectrum is an outer product, so tens of rates is normal — which means the
# frequency domain is at its worst precisely where such a model would want it.
#
# **Use the recursion.** It is the default, so this is also the "do nothing"
# answer.
#
# When you do need the transform
# ------------------------------
#
# There are three, and they are about capability rather than speed.
#
# **1. A response that wraps.** The recursion starts at bin 0 as though nothing
# preceded it, so it cannot see a response whose tail has wrapped around the
# period boundary. With a compact pulse that never happens; with a broad one only
# the spectral path is right.

broad = np.exp(-0.5 * ((i - 100.0) / 300.0) ** 2)
a = np.asarray(tttrlib.dfa_convolve(rates, weights, broad.tolist(), n_bins, 0.0,
                                    RECURSIVE))
b = np.asarray(tttrlib.dfa_convolve(rates, weights, broad.tolist(), n_bins, 0.0,
                                    SPECTRAL))
wrapped_gap = np.abs(a - b).max() / a.max()
print("compact response: %.1e   broad (wrapping) response: %.1e"
      % (agreement, wrapped_gap))

plt.figure(figsize=(7, 4))
plt.plot(a, label="recursive — loses the wrapped photons")
plt.plot(b, "--", label="spectral — keeps them")
plt.plot(broad / broad.sum() * a.max() / (broad / broad.sum()).max(),
         color="0.8", label="the broad response (scaled)")
plt.xlabel("bin")
plt.ylabel("intensity")
plt.legend()
plt.tight_layout()

# %%
# **2. An arbitrary measured pattern.** An autofluorescence or scatter reference
# is not a sum of exponentials, so there is no recursion to run — only the
# transform can convolve it.
#
# **3. An independent check.** Two different algorithms computing the same
# physics is a real test; the agreement printed above is what makes it one.
#
# A sub-bin timeshift is *not* on the list
# ----------------------------------------
#
# A shift of a whole bin is a roll, but a fractional one is not, and rounding it
# to the nearest bin biases the fitted lifetime when bins are coarse. This does
# not force you onto the spectral path: pass a fractional ``shift_bins`` and the
# recursion borrows a single transform of the *response*, at a cost that does not
# grow with the number of rates.

shifted = np.asarray(
    tttrlib.dfa_convolve(rates, weights, irf.tolist(), n_bins, 0.5, RECURSIVE))
unshifted = np.asarray(
    tttrlib.dfa_convolve(rates, weights, irf.tolist(), n_bins, 0.0, RECURSIVE))
one_bin = np.asarray(
    tttrlib.dfa_convolve(rates, weights, irf.tolist(), n_bins, 1.0, RECURSIVE))

plt.figure(figsize=(7, 4))
plt.plot(unshifted[80:140], "o-", label="shift 0")
plt.plot(shifted[80:140], "^-", label="shift 0.5 bin")
plt.plot(one_bin[80:140], "s-", label="shift 1 bin")
plt.xlabel("bin (offset 80)")
plt.ylabel("intensity")
plt.legend()
plt.tight_layout()

# %%
# The half-bin curve tracks between its neighbours, and — the part that matters —
# it stays **positive**.
#
# "Between" is not exact, and it should not be: a phase ramp is band-limited
# interpolation, so it rings a little rather than staying strictly within the two
# integer shifts. The overshoot printed below is around 2e-4 of the peak here —
# the signature of a genuine interpolation rather than a rounding to the nearest
# bin, which is what would bias a fitted lifetime on coarsely binned data. It
# grows with how sharp the curve is, so a fast decay through a narrow response
# rings more than a slow one.
#
# That is why the shift is applied to the response and never to the decay. A
# phase ramp is band-limited interpolation, so it rings wherever the signal has a
# step, and the decay has one: it jumps at the period boundary, the moment the
# next pulse arrives. Shift the decay by half a bin and its tail oscillates and
# goes negative — and a Poisson likelihood cannot take the logarithm of a
# negative model. The instrument response is a compact pulse near zero at both
# ends, so it shifts cleanly.

tolerance = 1.0e-3 * unshifted.max()
overshoot = max(
    float(np.max(np.minimum(unshifted, one_bin) - shifted)),
    float(np.max(shifted - np.maximum(unshifted, one_bin))),
    0.0)
interpolates = overshoot < tolerance
print("stays positive: %s   interpolates between neighbours: %s"
      % (shifted.min() > 0, interpolates))
print("overshoot beyond the neighbours: %.1e of the peak"
      % (overshoot / unshifted.max()))

# %%
# Convolution is commutative, so the fitted model is the same either way. Only
# the numerics differ — and only one of the two is usable.

plt.show()
