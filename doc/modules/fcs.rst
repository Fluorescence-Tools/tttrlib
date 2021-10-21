.. _fluorescence_correlation_spectroscopy:

=====================================
Fluorescence correlation spectroscopy
=====================================

Introduction
============
Confocal fluorescence correlation spectroscopy (FCS) is a powerful method to analyze molecular
dynamics. FCS has single molecule sensitive, is highly selective, and live-cell compatible.
FCS has a broad measurable time range spanning from ~ns to ∼s. Moreover, FCS provides spatial
selectivity so that membrane, cytoplasmic and nucleus molecular dynamics can be easily distinguished. 

The main underlying principle in correlation spectroscopy is the statistical analysis of
intensity fluctuations emitted by fluorescently labeled biomolecules by correlation analysis. The
resulting auto- or cross-correlation functions then can be further analyzed by curve fitting to 
eventually derive the rate constants of interest. In other words: The statistical methods FCS and 
FCCS do not provide single molecule traces like in single particle tracking, but a dynamic 
pattern or “fingerprint” of a probed specimen with high temporal resolution.

.. math::

   G(t_{c}) = I(t) \cdot I(t + t_{c})



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
