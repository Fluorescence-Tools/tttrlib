.. _fluorescence_correlation_spectroscopy:

=====================================
Fluorescence correlation spectroscopy
=====================================

Introduction
============
General
-------

The most common method to study fluctuations in the time evolution of any signal is to compute the correlation function
of such. In this respect, fluorescence correlation spectroscopy (FCS) :cite:`elson201340,Elson1974,magde1972`
in combination with FRET (FRET-FCS) was developed as a powerful tool :cite:`slaughter2004single,torres2007measuring`.
FRET-FCS allows for the analysis of FRET fluctuations covering a time range of nanoseconds to seconds. Hence,
it is a perfect method to study conformational dynamics of biomolecules, complex formation, folding and catalysis
:cite:`johnson2006calmodulin,price2011fret,price2010detecting,slaughter2004single,slaughter2005single,slaughter2005conformational,
gurunathan2010fret,levitus2010relaxation,al2005fluorescence,torres2007measuring`.

Confocal fluorescence correlation spectroscopy (FCS) is a powerful method to analyze molecular
dynamics. FCS has single molecule sensitivity, is highly selective, and live-cell compatible.
FCS has a broad measurable time range spanning from ~ns to ∼s. Moreover, FCS provides spatial
selectivity so that e. g. in live cells membrane, cytoplasmic and nucleus molecular dynamics can be easily distinguished.

The main underlying principle in correlation spectroscopy is the statistical analysis of
intensity fluctuations emitted by fluorescently labeled biomolecules by correlation analysis. The
resulting auto- (FCS) or cross-correlation functions (FCCS) then can be further analyzed by curve fitting to
eventually derive the rate constants of interest. In other words: The statistical methods FCS and 
FCCS do not provide single molecule traces like in single particle tracking, but a dynamic 
pattern or “fingerprint” of a probed specimen with high temporal resolution.

Structural fluctuations are reflected by the correlation function, which in turn provides restraints on the number of
conformational states. In this section we briefly describe the simplest case of FRET-FCS and discuss various
experimental scenarios. A detailed review was recently published covering all advantages and challenges of these
methods :cite:`felekyan2013analyzing`.

The auto/cross-correlation of two correlation channels :math:`S_A` and :math:`S_B` is given by:

.. math::

   G_{A,B} (t_{c}) = 1 + \frac{\langle \delta S_A(t) \delta S_B (t+t_c) \rangle}{\langle S_A(t) \rangle \langle S_B(t) \rangle}

If :math:`S_A` equals :math:`S_B` the correlation function is called autocorrelation function otherwise it is called
cross-correlation function. If all species are of equal brightness, the amplitude at zero time of the autocorrelation
function, :math:`G(0)`, allows to determine the mean number of molecules :math:`N` in the detection volume,
:math:`V_{det}`, or the concentration, :math:`c`, if the parameters of detection volume are known.

.. math::

    G(t_c=0) = 1 + \frac{1}{N} \cdot G_{diff}(t_c)

with :math:`c = N / V_{det}`. For a 3-dimensional Gaussian shaped detection/illumination volume the normalized
diffusion term is given by

.. math::

    G_{diff}(t_c) = \left( 1 + \frac{t_c}{t_{diff}} \right )^{-1} \left( 1 + \left( \frac{\omega_0}{z_0} \right)^2 \frac{t_c}{t_{diff}} \right)^{-1/2}

whereas :math:`\omega_0` and :math:`z_0` are shape parameters of the detection volume that is defined by
:math:`w(x,y,z)=\exp (-2 (x^2 + y^2) / \omega_0^2) \exp (-2 z^2/z_0^2)`. For 1-photon excitation the characteristic
diffusion time related to the diffusion coefficient :math:`D_{diff}^{(i)}` is given by
:math:`t_{diff}^{(i)} = \omega_0^2 / (4 D_{diff}^{(i)})`. The autocorrelation function allows for a direct assessment of
the diffusion constant :math:`D_{diff}`.

In a mixture of :math:`n` species with corresponding brightnesses, :math:`Q^{(i)}`, diffusion constants,
:math:`D_{diff}^{(i)}`, and fractions, :math:`x^{(i)}`, :math:`i = 1, \dots, n` it is most convenient to define the
autocorrelation function :cite:`kim2005two`.

.. math::

    G(t_c) = 1 + \frac{1}{N} \frac{\sum_i^nx^{(i)} (Q^{(i)})^2 G_{diff}^{(i)}(t_c)}{\left( \sum_i^n x^{(i)} Q^{(i)} \right)^2}

Even if the brightness, :math:`Q^{(i)}`, is known for each species, the diffusion coefficients, :math:`D_{diff}^{(i)}`,
still have to be significantly distinct for successful extraction of :math:`c^{(i)}` values, emphasizing the need of a
proper methodology that can distinguish species in a mixture.

Two-state system
----------------
In a two state system given donor excitation an analytical solution assuming equal diffusion coefficients of the states
:math:`D_{diff}^{(1)}=D_{diff}^{(2)}`, sole presence of DA-molecules, and absence of bleaching (:math:`G_{G,R}=G_{R,G}`)
can be calculated. The number of molecules per species are defined by the equilibrium constant :math:`K`:

.. math::

    \\
    K = \frac{k_{21}}{k_{12}} \\
    N^{(1)}=N \cdot \frac{k_{21}}{k_{12}+k_{12}} \\
    N^{(2)}=N \cdot \frac{k_{12}}{k_{12}+k_{12}}

Assuming that the characteristic time of the triplet state is much smaller than the relaxation time given by the
exchange rate constants (:math:`t_R=(1 / (k_{12} + k_{21})`)

.. math::

    \\
    G_{G,G}(t_c) = 1 + \frac{1}{N} G_{diff}(t_c) \left[ 1+ AC_{G,G} \exp\left( -\frac{t_c}{t_R} \right)\right ] \\
    G_{R,R}(t_c) = 1 + \frac{1}{N} G_{diff}(t_c) \left[ 1+ AC_{R,R} \exp\left( -\frac{t_c}{t_R} \right)\right ] \\
    G_{G,R}(t_c) = 1 + \frac{1}{N} G_{diff}(t_c) \left[ 1 - (AC_{G,G} \cdot AC_{R,R})^{1/2} \exp\left( -\frac{t_c}{t_R} \right)\right ]

Here :math:`G_{diff}(t_c)` :math:`AC_{G,G}` , :math:`AC_{R,R}`, and :math:`AC_{G,R}` are the amplitudes of the kinetic
reaction terms, which depend on the molecular brightnesses :math:`Q(i)`` of the FRET states.

The molecular brightness corresponds to the observed photon count rate per molecule, :math:`Q^{(i)}`, where
:math:`F^{(i)}` is the total number of fluorescence photons of the :math:`N(i)` molecules of species :math:`i`. The
molecular brightness is an intrinsic molecular property of the dyes. It is proportional to the product of the focal
excitation irradiance, :math:`I_0`, the extinction coefficient, :math:`\epsilon^{(i)}`, fluorescence quantum yield,
:math:`\Phi_F^{(i)}`, and spectral dependent detection efficiencies :math:`g_G` or :math:`g_R` for green and
red detectors, respectively, as :math:`Q_{G,R}^{(i)} \propto I_0 \epsilon^{(i)} \Phi_F^{(i)} g_{G,R}` :cite:`eggeling2001data,fries1998quantitative`.
The FRET efficiency :math:`E^(i)` of species :math:`i` is related to the molecular brightness by the relationship:

.. math::

    E^{(i)} = \frac{Q_R^{(i)} - \alpha Q_G^{(i)}}{Q_R^{(i)} - \alpha Q_G^{(i)} + Q_G^{(i)} \gamma'}

where :math:`\gamma' = \Phi_{F,A}^{(A,0)} g_R / (\Phi_{F,D}^{(D,0)} g_G)` and :math:`\alpha` is the spectral cross-talk
from the green to the red channel.

Given the brightnesses :math:`Q^{(i)}`, the pre-exponential factors are defined by:

.. math::

    AC_{G,G} = \frac{(Q_G^{(1)} - Q_G^{(2)})^2}{(K Q_G^{(1)} + Q_G^{(2)})^2}\cdot K \\
    AC_{R,R} = \frac{(Q_R^{(2)} - Q_R^{(1)})^2}{(K Q_R^{(2)} + Q_R^{(1)})^2}\cdot K \\
    CC_{G,G} = \frac{(Q_G^{(1)} - Q_G^{(2)})}{(K Q_G^{(1)} + Q_G^{(2)})}\cdot \frac{(Q_R^{(2)} - Q_R^{(1)})}{(Q_R^{(2)} + K Q_R^{(1)})}K

The brightnesses can be neglected if each of the two species contributes only to one of the correlation channels.
The contrast can be increased by lifetime filtered FCS (fFCS).

Analysis
========
As described previously there are several analogous possibilities to compute
correlations. Two possibilities are shown in the example below.

Analysing such correlation functions informs on diffusion and fast kinetics. Such
correlation functions can be analyzed by dedicated open tools for fluorescence
such as `ChiSurf <https://github.com/fluorescence-tools/chisurf/>`_,
`PyCorrFit <https://github.com/FCS-analysis/PyCorrFit>`_, and
`PAM <https://github.com/fluorescence-tools/pam>`_ or generic curve analysis
software.


.. _gated_correlation:

Gated fluorescence correlation spectroscopy
===========================================


Filtered fluorescence correlation spectroscopy
==============================================
