
from __future__ import division
import pylab as p
import tttrlib

import scipy.stats
import numpy as np

n_photons = 50
kw = {
    "hist2d_nmax": n_photons,
    "hist2d_nmin": 10,
    "background_ch1": 2.3,
    "background_ch2": 1.2,
}
pda = tttrlib.Pda(**kw)

# The parameters can also be set as attributes
pda.background_ch1 = 2.0
pda.background_ch2 = 5.0

# Here, we use scipy and a poisson distribution to compute an pF.
# For a real analysis pF needs to be estimated from the experiment.
mu = 20 # expectation value for the number of photons
dist = scipy.stats.poisson(mu)
x = np.arange(0, n_photons)
pF = dist.pmf(x)
pda.setPF(pF)

# Now we define a set of species with associated amplitudes
amplitudes = [0.33, 0.33, 0.33]

# probabilities_ch1 is the theoretical probability of detecting a photon
# in the first channel. In the PDA manuscript this is also called pG
probabilities_ch1 = [0.75, 0.4, 0.2]

pda.set_amplitudes(amplitudes)
pda.set_probabilities_ch1(probabilities_ch1)

# Alternatively the spectrum of amplitudes and probabilities
# can be set as an interleaved array [a1, p1, a2, p2, ...]
p_spectrum_ch1 = np.dstack([amplitudes, probabilities_ch1]).flatten()
pda.spectrum_ch1 = p_spectrum_ch1

# The computed distribution of photons in channel 1 and channel 2
# is given by the attribute s1s2. This attribute is convolved with
# the probability of detecting a photon pF and the specified background
s1s2 = pda.s1s2

# A one dimensional representation of the s1s2 matrix if obtain
# by a function that projects the pairs of photons. Any python function
# accepting at least two arguments can be used
proximity_ratio = lambda ch1, ch2: ch2 / (ch1 + ch2)

# The python function is used to set the attribute `histogram_function`
pda.histogram_function = proximity_ratio

# The method get_1dhistogram of the Pda object returns a 1D histogram
# of the s1s2 array for the specified function
x_pr, y_pr = pda.get_1dhistogram(
    log_x=False,
    x_min=0.0,
    x_max=1.0,
    n_bins=21
)

# functions, e.g., the FRET efficiency, that require additional parameters
# can be passed to the Pda object by defining a function with additional
# arguments. Note, potential division by zero need to be handled. Above,
# zero divisions were not handled as overall minimum number of photons was
# set to 10 (hist2d_nmin) and the histogram starts be be computed from the
# first channel.
def fret_efficiency(ch1, ch2, phiD=0.8, phiA=0.32, det_ratio=0.32):
    return 1.0 / (1. + phiD / phiA * det_ratio * ch2 / ch1)


pda.histogram_function = fret_efficiency
x_eff, y_eff = pda.get_1dhistogram(
    log_x=False,
    x_min=0.0,
    x_max=1.0,
    n_bins=31
)

# Histograms with a logarithmic scale are computed by setting `log_x`
# to True. When the option skip_zero_photon is set to False the first
# column and row of the s1s2 matrix (zero photons in ch1 or ch2) is used
# in this case potential division by zeros in the histogram function
# need to be handled. The default value for skip_zero_photon is True.
sg_sr = lambda ch1, ch2: max(1, ch1) / max(1, ch2)
pda.histogram_function = sg_sr
x_sgsr, y_sgsr = pda.get_1dhistogram(
    log_x=True,
    x_min=0.05,
    x_max=80.0,
    n_bins=31,
    skip_zero_photon=False
)

fig, ax = p.subplots(nrows=1, ncols=3)
ax[0].imshow(s1s2)
ax[1].plot(x_pr, y_pr, label='Proximity ratio')
ax[1].plot(x_eff, y_eff, label='FRET efficiency')
ax[1].legend()
ax[2].semilogx(x_sgsr, y_sgsr, label='Sg/Sr')
ax[2].legend()
p.show()
