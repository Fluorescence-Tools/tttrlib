"""
==============================================
Estimating the noise of the correlation curves
==============================================

There are many approaches to calculate the noise in correlation functions
(see: :cite:`Wohland2001`, :cite:`Qian1990`, :cite:`Starchev2001`). An
analytical calculation of the noise is not possible because it involves diverging
integrals for the correlations decaying functions :cite:`Wohland2001`. For TTTR
data an analytical solution of the noise in the correlation is however not
necessary, as the data contained in the photon stream can be split to yield a set
of correlation functions that is used to yield an estimate for the expected
correlation function and the associated noise by the mean and the standard
deviation.

TODO:

.. code-block:: python

    import tttrlib

The data passed to the correlator is splitted into @param n_split pieces. The
individual correlations are used to calculate a set of correlation curves. In
subsequent processing steps, the set of correlation curves can be used to
estimate the the mean correlation and the standard deviation of the correlation
curves.


:cite:`Kapusta2007`
:cite:`Enderlein1997`
:cite:`Bohmer2002`
"""