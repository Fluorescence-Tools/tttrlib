.. _photon_distribution_analysis:

============================
Photon distribution analysis
============================

Basics
------
An analysis of photon count histograms can be used to determine FRET
efficiencies, rate-constants and population fractions quantitatively.
:cite:`gopich2003single,gopich2005theory,zhang2005photon, gopich2007single,sisamakis2010accurate`
FRET histograms can be computed for multiple data sources (eg, TIRF-camera traces
or data collected using a confocal setup). Intensity based observables are computed
for binned data measured by two or more detection channels (eg, a “green”
(G) and “red” (R) detection channel in a FRET experiment).

The simplest way of analyzing these histograms is by fitting a sum of normal
distributions to recover the means of the underlying populations :cite:`brunger2011three,mccann2010optimizing`.
However, this simple approach is not exact, as the Poissonian statistics of
the photon distribution and broadening caused by shot-noise are not taken
explicitly into account. Hence, the fitted width has no physical meaning and
overlapping cannot be resolved accurately and minor states may be overlooked.
Furthermore, without proper weighting of the residuals the fit quality cannot
be judged. These complications were overcome by Photon Distribution Analysis :term:`PDA`
and related methods :cite:`gopich2005photon,nir2006shot,antonik2006separating,kalinin2007probability,kalinin2008characterizing,tomov2012disentangling`


In :term:`PDA` the Poisson noise of the data is explicitly accounted for by
computing the probability :math:`P(S_G,S_R)` to observe a certain combination
of photons collected in the "green", G, and "red", R, detection channel for a
defined time-window:

.. math::

   P(S_G,S_R)=\sum_{S_G,S_R} P(F)P(F_G,F_R|F)P(B_G)P(B_R)
   S_G = F_G + B_G
   S_R = F_R + B_R

Here the fluorescence intensity distribution, :math:`P(F)`, is obtained from the
total signal intensity distribution :math:`P(S)` assuming that the background signals
:math:`B_G` and :math:`B_R` follow a Poisson distribution :math:`P(B_G)` and
:math:`P(B_R)`, with known mean intensities, :math:`\langle B_G \rangle`
and :math:`\langle B_R \rangle`. The conditional probability :math:`P(F_G,F_R|F)` is the
the probability of observing a particular combination of green and red fluorescence photons,
:math:`F_G` and :math:`F_R`, provided that the total number of registered fluorescence
photons is :math:`F`, and can be expressed as a binomial distribution :cite:`antonik2006separating`.

.. math::

   p(F_G,F_R|F) = \frac{F!}{F_G! (F-F_G)!} p_{G}^{F_G} (1-p_G)^{F-F_G}

Here, :math:`p_G` is the probability that a detected photon is registered by the "Green"
detection channel. The value of :math:`p_G` is unambiguously related to the FRET efficiency
:math:`E` according to:

.. math::

    p_G = (1 + \alpha + \gamma \cdot \frac{E}{1-1})^{-1}

where :math:`gamma=\Phi_{F,A}^{(A,0)} g_R / (\Phi_{F,D}^{(D,0)} g_G)` and :math:`alpha` is
the crosstalk from :math:`G` to :math:`R` channel.

Knowledge of :math:`P(S_G, S_R)` is sufficient to generate theoretical 1D histograms of any
FRET-related parameter, which can be expresses as a function of :math:`S_G` and :math:`S_R`
(e.g., signal ratio :math:`S_G/S_R` or FRET efficiency :math:`E`) :cite:`kalinin2007probability`. Fitting
such histograms obtained for a single species requires only one floating parameter, :math:`p_G`.

Species mixtures
----------------
In a mixture of fluorescence species the intensity distribution :math:`P(F)` is given by the sum
of the individual species distributions :math:`P^{(j)}(F)` weighted by :math:`x_j`, the species fraction:

.. math::

    p(F) = \sum_{j} x^{(j)}P^{(j)}(F) = \sum_{i} P(q^{(i)}) \sum_{j}x^{(j)} \frac{(q^{(i)}Q^{(j)})^F \exp{(-q^{(i)}Q^{(j)})}}{F!}

with :math:`\sum_j x^{(j)} = 1` is the species fraction of the jth species, :math:`P(q^{(i)})` is the brightness
distribution (determined by the confocal volume), and :math:`Q^{(j)}` is the molecular brightness of the jth state.

To describe the brightness distribution, we use a predefined set of brightness values, :math:`{q^{(i)}}`, consisting of
100-200 elements. All brightness variations due to FRET can be described in terms of the specified FRET efficiencies
:math:`E^{(j)}` as

.. math::

    Q^{(j)} = \frac{(1-E^{(j)}) \Phi_{F,D}^{(D,0)} g_G (1+\alpha) + E^{(j)} \Phi_{F,A}^{(A,0)} g_R}{\Phi_{F,D}^{(D,0)} g_G (1+\alpha)}

To convert the signal intensity ratio :math:`S_G/S_R` (or fluorescence ratio :math:`F_D/F_A`) into a distance, the
inter-dye distance, :math:`R_{DA}^{(j)}`, is computed by

.. math::

    R_{DA}^{(j)} = R_{0r} \left[ \Phi_{F,A}^{(A,0)} \cdot \frac{F_D^{(j)}}{ F_A^{(j) }} \right]^{1/6}
                 = R_{0r} \left[ \Phi_{F,A}^{(A,0)} \left( \frac{1 - E^{(j)}}{E^{(j)}} \right) \right]^{1/6}


where :math:`\Phi_{F,A}^{(0,A)}` is the position-dependent fluorescence quantum yield and :math:`R_{0r}` is the reduced
Förster radius (:math:`9780[J(\lambda) \kappa^2 n^{-4}]^{1/6}` [Ang.]), :math:`J(\lambda)` is the D and A spectral
overlap integral, :math:`\kappa^2` is the dipolar orientation factor and :math:`n` is the refractive index.

For a single FRET species :math:`j` with a normal distributed inter-dye distance
:math:`p(R_{DA}^{(j)}, \bar{R_DA}, \sigma_{DA})`, the fluorescence distribution :math:`P^{(j)}(F)` is

.. math::

    P^{(j)}(F) = \sum_i P(q^{(i)}) \int p(R_{DA}^{(j)} \frac{1}{F!} q^{(i)} Q(R_{DA}^{(j)})^F \exp(-q^{(i)} Q(R_{DA}^{(j)})) dR_{DA}^{(j)}

where, :math:`Q(R_{DA}^{(j)})` is the molecular brightness as a function of the distance obtained by combining the
equations above.

Fluorescence intensities are averaged over the integration time (typically milliseconds). Thus, a distance derived from
a FRET efficiency, :math:`E`, here addressed by :math:`R_E`, is for dynamic systems an average distance, that contains
information on dynamics :cite:`gopich2003single`.

Acceptor induced broadening
---------------------------
In addition to conformational dynamics, variations of the fluorescence quantum yield :math:`\Phi_{F,A}^{(A0)(j)}` result
in a broadening of FRET efficiency distributions. For a single distance, :math:`R_{DA}^{(j)}`, the broadening caused by
a variation of the acceptor fluorescence quantum yield can be approximated by a normal distance distribution with a mean
distance :math:`\tilde{R}^{(j)}` and apparent width, :math:`\sigma_{app}` :cite:`kalinin2010origin`.

.. math::

    \tilde{R}^{(j)}=R_{RDA}^{(j)} \left ( \frac{\langle \Phi_{F,A}^{(A,0)} \rangle}{\Phi_{F,A}^{(A,0)(j)}} \right )^{1/6}
    \sigma_{app}=\sqrt{\text{var}(\tilde{R})}=R_{DA}^{(j)}\sqrt{\frac{\langle \Phi_{F,A}^{(A,0)} \rangle}{\text{var}(\Phi_{F,A}^{(A,0)})^{1/6}}}=R_{DA}^{(j)}\theta_{A}

The apparent width :math:`\sigma_{app}` caused by variations of the acceptor quantum yield must not be confused with
the physical width of the distribution of distances. :math:`\sigma_{app}` is linearly proportional to
:math:`\langle \tilde{R} \rangle` with a single fixed proportionality factor :math:`\theta_A` given as a constant percentage
of the mean distance.  The dyes Cy5 and Alexa647, commonly used as acceptor, are known to show this phenomenon. The
expected broadening factor :math:`\theta_{A}` can conveniently be estimated by :term:`TCSPC` experiments assuming
:math:`\Phi_{F,A}^{(A,0)}` is proportional to the fluorescence lifetime. For example, if coupled Alexa 647 has two
fluorescence lifetimes of 1.17 ns and 1.76 ns with corresponding amplitudes of 0.62 and 0.38, the proportionality factor
is :math:`\theta_A \approx 0.06`.

Dynamic PDA
-----------
In dynamic systems, the shape of the measured FRET-histograms depends on the length of the binning time (time window).
This is exploited to characterize dynamic processes between multiple states :cite:`palo2006calculation,gopich2003single,gopich2007single`.
To analyze dynamic processes quantitatively Kalinin et al. :cite:`kalinin2010detection` developed a toolkit, referred
to as dynamic PDA, that combines the mathematical formalism of Palo et al. :cite:`palo2006calculation` and Gopich
et al. :cite:`gopich2007single` with all essential steps towards the complete description of experimental data
(including brightness variations, shortening of the observation time due to diffusion and contribution of
multimolecular events). Aforementioned authors laid the groundwork for dynamic PDA. It was applied to characterize
exchange between two or more FRET states using global analysis of multiple time windows. Finally, the Kapanidis group
combined dynamic PDA :cite:`santoso2010characterizing` with burst variance analysis (BVA) :cite:`torella2011identifying`
to detect dynamics by comparing the standard deviation of FRET from individual molecules over time to that expected
from theory.


Consider a molecule with two interconverting states :math:`S_1` and :math:`S_2` (:math:`S_1 \to_{k_{12}}  S_2` and
:math:`S_2 \to_{k_{21}}  S_1`) being observed for time :math:`\Delta t`. During the observation time the molecule spends
a time :math:`t_1` in state :math:`S_1` and :math:`t=\Delta t - t_1` in state :math:`S_2`. The probability of being a
time :math:`t_1` in the state :math:`S_1` is given by:

.. math::

    p(t_1) =
    \delta(t_1) \frac{k_{12}}{k_{12} + k_{21}} \exp{(-k_{21} \Delta t)} +
    \delta(\Delta t - t_1) \frac{k_{21}}{k_{12} + k_{21}} \exp{(-k_{12} \Delta t)} +
    \left[
        2\frac{k_{12} k_{21}}{k_{12} + k_{21}} I_0(2\sqrt{k_{12} k_{21} t_1 t_2}) +
        \frac{k_{21}t_1+k_{12}t_2}{k_{12} + k_{21}} \cdot \sqrt{\frac{k_{12}k_{21}}{t_1 t_2}} I_1(2\sqrt{k_{12} k_{21} t_1 t_2})
    \right]
    \exp{(-k_{12} t_1 - k_{21} t_2)}


where :math:`\delta` is the Dirac :math:`delta`-function, :math:`I_0` and :math:`I_1` are the Bessel functions of the
order 0 and 1, :math:`k_{12}` and :math:`k_{21}` are rate constants. The probability :math:`p(t_1)` consists of three
terms. The first two terms give the probability of a molecule spending the entirety of the time :math:`\Delta t` in
:math:`S_1` and :math:`S_2`, respectively. The third term describes the probability of the molecule for spending a
time :math:`t_1: in the state :math:`S_1`, where :math:`0 < t_1 < \Delta t`. The probability :math:`P(S_G,S_R)` to
observe a certain combination of photons is calculated the same way as described previously.

Gopich and Szabo derived an analytical alternative to dynamic PDA :cite:`gopich2010fret`. Here, FRET efficiency histograms
(FEH) are approximated by a sum of normal distributions. Hence, experimental data can be conveniently fitted using any
spreadsheet application without any programming. The parameters of the normal distributions are explicitly determined
by the FRET efficiencies of the states and their exchange rate constants. This approach has been validated by
simulations of systems with two, three and four conformational states and accurately describes how maxima in the
histograms collapse as the binning time or the transition rate constants increases (for details see original publication
:cite:`gopich2010fret`). In a two state system (as described above for PDA) the experimental FEH is given by:

.. math::

    FEH(E) =
        c_1 \frac{1}{\sqrt{2\pi \sigma_1^2}} \exp{\left(-\frac{(E-E_1)^2}{2\sigma_1^2}\right)} +
        c_2 \frac{1}{\sqrt{2\pi \sigma_2^2}} \exp{\left(-\frac{(E-E_2)^2}{2\sigma_2^2}\right)} +
        c_{12} \frac{1}{\sqrt{2\pi \sigma_{12}^2}} \exp{\left(-\frac{(E-E_{12})^2}{2\sigma_{12}^2}\right)}

here, :math:`E_1` and :math:`E_2` corresond to the FRET efficiencies of the states :math:`S_1` and :math:`S_2`,
respectively. The pre-factors are given by:

.. math::

    c_{1} = p_{1} e^{-k_{12} \Delta t}
    c_{2} = p_{2} e^{-k_{21} \Delta t}
    c_{12} = 1 - c_1 - c_2

Note, here, the definition of the rate constants are the inverse of the constants in the original publication
:cite:`gopich2010fret`. The normalization factors :math:`p_i` are the equilibrium fractions determined by the rate
constants:

.. math::

    p_1 = \frac{k_{21}}{k_{12} + k_{21}}
    p_2 = \frac{k_{12}}{k_{12} + k_{21}}

The width :math:`\sigma_i` of the distributions, describing molecules static within the time :math:`\Delta t`, is
governed only by shot noise:

.. math::

    \sigma_i^2 = E_i (1-E_i) \langle n^{-1} \rangle

Here, :math:`\langle n^{-1} \rangle` is the inverse of the average of number of photons in a time bin calculated using
the experimental number of photons per bin. The mean of the third normal distribution :math:`E_{12}` representing the
mixed state is given by:

.. math::

    E_{12} = E_1 f_{12} + E_2 f_{21}

with :math:`f_{12} = (p_1-c_1)/c_{12}` and :math:`f_{21} = 1 - f_{12}`. The width of the dynamic mixing peak is
determined by the shot noise, the FRET efficiencies of the individual states, and the rate constants:

.. math::

    \sigma_{12}^2 =
      E_{12} (1 - E_{12}) \langle n^{-1} \rangle +
      \sigma^2_{c_{12}}(1-\langle n^{-1} \rangle)

where

.. math::

    \sigma^2_{c_{12}}=(E_1-E_2)^2
    \left(
      f_{12} f_{21} - \frac{p_1 p_2}{c_{12}}
      \left[
        1 - 2 \frac{(k_{12} + k_{21}) \Delta t + e^{-(k_{12}+k_{21}) \Delta t)} - 1}{((k_{12} + k_{21})\Delta t)^2}
      \right]
    \right)

In the Gopich Szabo theory corrected experimental data should be analyzed.

In experiments on freely diffusing molecules the shape of experimental FEHs is influenced by the binning time
:math:`\Delta t` and the actual time spend by the molecules within the observation volume. Neither dynamic PDA nor
the analytical approach by Gopich et al.:cite:`gopich2010fret` take diffusion effects explicitly, causing incomplete
averaging or variations of the count rate, into account. Resulting systematic deviation can be decreased either by
(i) a preselection of single-molecule bursts for analysis or (ii) an application of an empirical approach suggested by
Kalinin et al. (Eq. 11 in :cite:`kalinin2010detection`). The second approach uses the FCS diffusion time :math:`t_{diff}`
to correct for incomplete averaging given the short diffusion time. Note that these procedures still produce systematic
errors. Gopich et al. introduced an approach termed "recoloring method" in time bins that bypasses the diffusion
problems at the cost of lack of an analytical solution :cite:`gopich2009decoding,chung2011extracting`. Although the
analytical approach without applied corrections produces systematic deviations, it is a reliable quantitative method
available to all researchers using smFRET.

In conclusion, complementary to the 2D histograms and the FRET lines, quantitative analysis of FRET histograms can be
used to identify limiting states and corresponding exchange rate constants. Dynamic PDA can quantify the processes
with relaxation times of 0.1-10 fold of the selected time window, ranging from 0.1 ms (limited by photon statistics)
up to 10 ms (limited by diffusion time) :cite:`kalinin2010detection`. In order to complement PDA and its variations FCS
and lifetime filtered FCS is an additional powerful tool that extends the possible range of kinetics studies by six
decades in time.
