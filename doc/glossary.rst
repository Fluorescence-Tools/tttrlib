Glossary
========


.. glossary::

    FRET rate constant
        The FRET rate constant, $latex k_{RET}$, quantifies the FRET process by the number of quanta transferred
        from the donor's excited state to the acceptor per time. It depends on the mutual dipole orientation of the
        donor and the acceptor fluorophore, the distance between the donor and acceptor, :math:`R_{DA}`, the Förster
        radius, :math:`R_0` of the dye pair, and the corresponding fluorescence lifetime of the donor in the absence
        of FRET, :math:`\tau_{0}`. The orientation factor :math:`\kappa^2` captures The mutual dipole orientation.

        .. math:: k_{RET} = \frac{1}{\tau_0} \kappa^2 \left( \frac{R_0}{R_{DA}} \right)^{6}

        Note, for the calculation of the FRET rate constant the fluorescence lifetime has to match the Förster radius.
        Meaning the fluorescence lifetime of the corresponding donor fluorescence quantum yield, :math:`\Phi_{F}^{D0}`
        should be used.

    MFD (Multiparameter Fluorescence Detection)
        A MFD experiments is a time-resolved fluorescence experiment which probes the absorption and fluorescence,
        the fluorescence quantum yield, the fluorescence lifetime, and the anisotropy of the studied chromophores
        simultaneously (see :cite:`Kuehnemuth2001`)

    FCS (Fluorescence correlation spectroscopy)
        FCS is a method which relies on fluctuations on the recorded signals to characterize molecular interaction such
        as binding and unbinding, chemical reaction kinetics, diffusion of fluorescent molecules (see Elson, Magde,
        Fluorescence Correlation Spectroscopy. I. Conceptual Basis and Theory, Biopolymers