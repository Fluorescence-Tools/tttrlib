.. _fluorescence_intensity_analysis:

===============================
Fluorescence intensity analysis
===============================
Intensity-based approaches are the most intuitive and easiest way to measure :term:`FRET`.
Intensity based approaches record signal intensities. The most common intensity-based
FRET measurement approached record the signal intensity in detection channels for the
donor and acceptor fluorophore. Here, the donor is a "green" fluorophore that is
detected by "green" detection channels. The acceptor is a "red" fluorophore that is
detected by "red" detection channels. The donor is excited by a "green" light source.
The acceptor can be excited by a "red" light source.

A measure of FRET that is commonly used is the :term:`FRET efficiency`, :math:`E`.
Computing a FRET efficiency requires corrected signals. As an indicator for FRET one can
calculate the proximity ratio :math:`PR` according using the uncorrected signal
detected by the green, :math:`S_G`, and red, :math:`S_R`, detection channels,
respectively.

.. math::

   PR = S_R / (S_R + S_G)

For ideal fluorophores and detection systems :math:`PR` equals to the FRET
efficiency, :math:`E`. To obtain a FRET efficiency for non ideal detection
systems and fluorophores the fluorescence intensities need to be corrected for the
fluorescence quantum yield of the donor and acceptor.

.. math::

   E = \left( 1+\gamma` \frac{F_{D|D}^{(D,0)}}{F_{A|D}^{(D,A)}} \right )^{-1}

where, :math:`\gamma'=\Phi_{F,A}^{(A,0)}/\Phi_{F,D}^{(D,0)}` is the ratio
of the acceptor to donor fluorescence quantum yield, :math:`F_{D|D}^{(D,A)` is
the fluorescence intensity of the donor :math:`D|...` in a FRET sample
:math:`(D,A)` given the donor excitation :math:`...|D`, and :math:`F_{A|D}^{(D,A)}`
is the fluorescence intensity of the acceptor :math:`A|...` in a FRET sample
:math:`(D,A)` given the donor excitation :math:`...|D`.

:math:`F_{D|D}^{(D,A)` and :math:`F_{A|D}^{(D,A)`, depend on the detection
efficiencies of green (gG) and red (gR) channels and corresponding background
signals, among other correction factors. For the common case where there is
negligible crosstalk from the acceptor to the donor detection channel

.. math::

   F_D = \frac{S_G - \langle B_G \rangle}{g_G}
   F_{A}=\frac{S_{R}-\alpha F_{G}-\langle B_{R} \rangle}{g_{R}}

where :math:`\langle B_G \rangle` and :math:`\langle B_R \rangle` are the background
signals in green and red channels, :math:`\alpha` is the spectral cross-talk from the donor
fluorescence to the acceptor channel. Note that detection efficiencies are difficult to measure
:cite:`fries1998quantitative`, :cite:`sabanayagam2005using`, but the ratio of detection efficiencies
can be calibrated with a known sample (see section 2.5.1). Equation (5) is not corrected
for direct excitation of the acceptor, therefore care has to be taken to choose the
appropriate donor excitation wavelength. The latter is taken into account in
an alternative intensity-based approach developed by Clegg :cite:`clegg199218`.
Here, the acceptor intensity ratio from signal after direct and FRET mediated
excitation is calculated yielding transfer efficiencies being less prone
to experimental artifacts. As in all ensemble FRET experiments, a proper
knowledge of the degree of labeling is mandatory for a quantitative analysis.
