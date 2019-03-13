Correlation
===========

How to calculate correlation functions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:cite:`Wahl2003`
:cite:`Laurence2006`


Normal correlations
+++++++++++++++++++

.. plot:: plots/correlation_normal.py
   :include-source:


Estimating the noise of the correlation curves
++++++++++++++++++++++++++++++++++++++++++++++

There are many approaches to calculate the noise in correlation functions (see: :cite:`Wohland2001`, :cite:`Qian1990`,
:cite:`Starchev2001`). An analytical calculation of the noise is not possible because it involves diverging integrals
for the correlations decaying functions :cite:`Wohland2001`. For TTTR data an analytical solution of the noise in
the correlation is however not necessary, as the data contained in the photon stream can be split to yield a set
of correlation functions that is used to yield an estimate for the expected correlation function and the associated
noise by the mean and the standard deviation.

.. code-block:: python
    import tttrlib
    data =

 The data passed to the correlator is splitted into @param n_split pieces. The individual correlations
 are used to calculate a set of correlation curves. In subsequent processing steps, the set of correlation
 curves can be used to estimate the the mean correlation and the standard deviation of the correlation curves.










Count rate filer
++++++++++++++++

.. plot:: plots/correlation_cr_filter.py
   :include-source:


Full and fine correlations
++++++++++++++++++++++++++

To consider the micro time in the correlation, the micro time can be

Filtering background
--------------------

Filtering aggregates
--------------------

Lifetime filters
----------------

:cite:`Kapusta2007`
:cite:`Enderlein1997`
:cite:`Bohmer2002`



.. code-block:: python

    def calc_lifetime_filter(decays, experimental_decay, normalize_patterns=True):
        """Calculates filters for lifetime filtered correlations according to Enderlein

        :param decays: a list of fluorescence decays
        :param experimental_decay: the experimental fluorescence decay
        :return: an array of the filters

        Examples
        --------

        >>> lifetime_1 = 1.0
        >>> lifetime_2 = 3.0
        >>> t = np.linspace(0, 20, num=10)
        >>> d1 = np.exp(-t/lifetime_1)
        >>> d2 = np.exp(-t/lifetime_2)
        >>> decays = [d1, d2]
        >>> w1 = 0.8  # weight of first component
        >>> experimental_decay = w1 * d1 + (1.0 - w1) * d2
        >>> filters = calc_lifetime_filter(decays, experimental_decay)
        >>> calc_lifetime_filter(decays, experimental_decay)
        array([[ 1.19397553, -0.42328685, -1.94651679, -2.57788423, -2.74922322,
                -2.78989942, -2.79923872, -2.80136643, -2.80185031, -2.80196031],
               [-0.19397553,  1.42328685,  2.94651679,  3.57788423,  3.74922322,
                 3.78989942,  3.79923872,  3.80136643,  3.80185031,  3.80196031]])


        Using a structure to generate lifetime filters

        >>> import mfm
        >>> from mfm.fluorescence.general import calculate_fluorescence_decay
        >>> import numpy as np

        >>> time_axis = np.linspace(0, 10, num=100)
        >>> structure = mfm.structure.Structure('./sample_data/modelling/pdb_files/hGBP1_closed.pdb')
        >>> donor_description = {'residue_seq_number': 344, 'atom_name': 'CB'}
        >>> acceptor_description = {'residue_seq_number': 496, 'atom_name': 'CB'}
        >>> donor_lifetime_spectrum = np.array([1., 4.])
        >>> lifetime_spectrum = structure.av_lifetime_spectrum(donor_lifetime_spectrum, donor_description, acceptor_description)
        >>> t, decay_1 = calculate_fluorescence_decay(lifetime_spectrum, time_axis)

        >>> donor_description = {'residue_seq_number': 18, 'atom_name': 'CB'}
        >>> acceptor_description = {'residue_seq_number': 577, 'atom_name': 'CB'}
        >>> donor_lifetime_spectrum = np.array([1., 4.])
        >>> lifetime_spectrum = structure.av_lifetime_spectrum(donor_lifetime_spectrum, donor_description, acceptor_description)
        >>> t, decay_2 = calculate_fluorescence_decay(lifetime_spectrum, time_axis)

        >>> fraction_1 = 0.1
        >>> experimental_decay = fraction_1 * decay_1 + (1. - fraction_1) * decay_2
        >>> decays = [decay_1, decay_2]
        >>> filters = calc_lifetime_filter(decays, experimental_decay, normalize_patterns=False)

        >>> a0 = np.dot(filters[0], experimental_decay)
        >>> a1 = np.dot(filters[1], experimental_decay)

        References
        ----------

        .. [1] Fluorescence Lifetime Correlation Spectroscopy, Peter Kapusta,
           Michael Wahl, Ales Benda, Martin Hof, Jorg Enderlein,
           Journal of Fluorescence, 2007, 17:43-48
        .. [2] Fast fitting of multi-exponential decay curves, Jorg Enderlein,
           Rainer Erdmann, 1997, 134, 371-378, Optics Communications
        .. [3] Time-resolved fluorescence correlation spectroscopy
           Martin Bohmer, Michaek Wahl, Hans-Jurgen Rahn, Rainer Erdmann, Jorg Enderlein,
           Chemical Physics Letters 353 (2002), 439-445
        """
        # normalize the fluorescence decays which serve as reference
        if normalize_patterns:
            decay_patterns = [decay / decay.sum() for decay in decays]
        else:
            decay_patterns = decays
        d = np.diag(1. / experimental_decay)
        m = np.stack(decay_patterns)
        iv = np.linalg.pinv(np.dot(m, np.dot(d, m.T)))
        r = np.dot(np.dot(iv, m), d)
        if normalize_patterns:
            w = np.array([np.dot(fi, experimental_decay) for fi in r]).sum()
            r /= w
        return r

