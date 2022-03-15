.. _burst_selection:

===============
Single molecule
===============
The main advantage of single-molecule fluorescence spectroscopy
is the ability to resolve static (many states) and dynamic (inter-converting states)
heterogeneities. Depending on measurement technique used (TIRF or confocal-MFD setup)
there is a number of analysis methodologies. Each method is suitable for a certain
time-scale (trace analysis/HMM, histogram methods/PDA, time-window/PDA methods,
correlation, TCSPC). Dynamics can be quantified if changes in the observables are
within the time-scale of the method (time-resolution and observation time).

Burst Analysis
===============

MFD
---
In a single-molecule confocal experiment, single molecules are detected during
their free diffusion through a confocal volume. The resulting photon bursts
are selected from the recorded photon trace after separation of the background.
Usually this is accomplished by applying a threshold for the maximum inter-photon
time and a minimum photon number within a burst (typically 30-160 photons). The
burst-duration is determined mainly by the experimental setup and the molecular
dimensions. As the molecules are freely diffusing a distribution of burst durations
is observed and the fluorescence observables are averaged according to the individual
burst durations. To obtain equal averaging the observation times can be unified :cite:`fries1998quantitative`.

Due to the low number of photons (60-500 photons per burst) detected per burst it
is not reasonable to apply complex models to describe the fluorescence intensity
decays. However, an averaged fluorescence lifetime can be determined for each burst
using maximum likelihood algorithms :cite:`maus2001experimental`. Note that the determined lifetime
is the fluorescence weighted average lifetime, :math:`\langle \tau \rangle_F`, and not the
species weighted average lifetime, :math:`\langle \tau \rangle_x`. Given a mixture of
fluorescence species with the species fractions :math:`x^(i)` and corresponding
lifetimes :math:`\tau^(i)` those averages are given by:

.. math::

   \langle \tau \rangle_x = \sum_i x^{(i)} \tau^{(i)}

    \langle \tau \rangle_F = \frac{1}{\langle \tau \rangle_x} \sum_i x^{(i)} (\tau^{(i)})^2

For each burst several fluorescence parameters are determined, e.g. the green-red intensity ratio (FD/FA),
the FRET efficiency (E) or the scatter-corrected anisotropy (:math:`r_scatter`).
Based on these parameter multi-dimensional frequency histograms can be constructed.

PIE
---
An analysis of a MFD-PIE measurement with multidimensional histograms is in
application :cite:`kudryavtsev2012combining`. The macro time (in seconds) is
used to track changes in sample properties (e.g. degradation, adsorption,
or FRET levels shift) in the course of the measurement. Comparison of burst
length of two PIE-channels :math:`T_{G+R|D}-T_{R|A}`  provides a mean to filter
bursts exhibiting acceptor dye photobleaching, which may be otherwise confused with
FRET dynamics. Here :math:`T_{G+R|D}` is the mean macro time of the photons detected
in the donor- or acceptor channel after donor excitation and :math:`T_{R|A}` is
the mean macro time of red photons after direct excitation of the acceptor
:cite:`kudryavtsev2012combining`. The PIE stoichiometry, :math:`S_{PIE}`, is used to
filter FRET molecules (D,A) from incomplete (D,0) (A,0) or mislabeled molecules (A,A) (D,D).
The PIE stoichiometry is given by:

.. math::

    S_{PIE}=\frac{F_{R|D}-\beta F_{R|A} - \alpha F_{G|D} + \gamma F_{G|D}}{F_{R|D} - \beta F_{R|A} - \alpha F_{G|D} + \gamma F_{G|D} + F_{R|A}}


where :math:`F_{G|D}` , :math:`F_{R|D}`, and :math:`F_{R|A}` are background corrected
fluorescence intensities measured in the donor channel after donor excitation
(:math:`G|D`), in the acceptor channel after donor excitation (:math:`R|D`) and
direct acceptor excitation (:math:`R|A`), respectively, :math:`\alpha` is the
correction factor for donor fluorescence crosstalk into the acceptor channel, :math:`\beta` is the correction
factor for acceptor excitation by the donor excitation source, and
:math:`\gamma = \Phi_{F,A}^{(A,0)}/\Phi_{F,D}^{(D,0)}\cdot g_R /g_G`depend on the fluorescence
quantum yields, :math:`\Phi_{F}` and detection efficiencies :math:`g`.

FRET-lines
----------
In these multidimensional histograms from MFD or MFD-PIE experiments the comparison
between theoretical and observed fluorescence parameters reveal sample properties.
Here the theoretical relation between intensity based FRET-indicators (e.g. FD/FA or E)
and :math:`\langle \tau \rangle_F` are called FRET-lines. In the case of a mono-exponential
donor decay and no conformational dynamics this relation is given by:

.. math::

   E = \left [ 1 + \gamma` \frac{F_{D|D}}{F_{A|D}}\right ]
