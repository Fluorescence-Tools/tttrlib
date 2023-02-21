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

    CLSM
        confocal laser scanning microscopy

    PDA
        Photon Distribution Analysis

    MFD (Multiparameter Fluorescence Detection)
        A MFD experiments is a time-resolved fluorescence experiment which probes the absorption and fluorescence,
        the fluorescence quantum yield, the fluorescence lifetime, and the anisotropy of the studied chromophores
        simultaneously (see :cite:`Kuehnemuth2001`)

    FCS (Fluorescence correlation spectroscopy)
        FCS is a method which relies on fluctuations on the recorded signals to characterize molecular interaction such
        as binding and unbinding, chemical reaction kinetics, diffusion of fluorescent molecules (see :cite:`Elson1974`
        and :cite:`magde1972`)

    FRET efficiency
        The FRET efficiency is the yield of a FRET process. A FRET process transfers energy from the excited state of
        a donor fluorophore to an acceptor fluorophore. The number of donor molecules in an excited state which
        transfers energy to an acceptor defines the yield of this energy transfer.

        .. math::
            E = \frac{transferred}{excited}

        Practically, mostly the donor and acceptor fluorescence intensities are used to obtain an experimental estimate
        for this yield.

    FRET
        FRET stands for (Förster) Resonance Energy Transfer and describes the mechanism of energy transfer between two
        light-sensitive molecules. A donor chromophore, initially in its electronic excited state, may transfer energy
        to an acceptor chromophore, through nonradiative dipole–dipole coupling. The fluorescence absorption and
        emission spectrum of the acceptor are red shifted compared to the respective spectra of the donor fluorophore
        For FRET to occur, three criteria have to be fulfilled: (1) The donor fluorescence emission spectrum has
        to overlap with the absorption spectrum of the acceptor, (2) the fluorophores' dipoles are sufficiently 
        parallel and (3) the fluorophores are in close vicinity (usually < 10 nm).
        The efficiency of this energy transfer is inversely proportional to the sixth power of the distance 
        between donor and acceptor, making FRET extremely sensitive to small changes in distance.

    FRET induced donor decay
        The FRET-induced donor decay is a time-resolved intensity independent measure of FRET similar to the
        time-resolved anisotropy defined by the ratio of the donor fluorescence decay in the presence and the
        absence of FRET (see: :cite:`peulen2017`).

    IRF
        IRF stands for instrument response function. In time-resolved fluorescence measurements the IRF is the temporal
        response of the fluorescence spectrometer to a delta-pulse. Suppose a initially sharp pulse defines the time of
        excitation / triggers the laser, then recorded response of the fluorescence spectrometer is broadened due to:
        (1) the temporal response of the exciting light source, (2) the temporal dispersion due to the optics of the
        instrument, (3) the delay of the light within the sample, and (4) the response of the detector. As the most
        intuitive contribution to the IRF is the excitation profile, the IRF is sometimes called 'lamp function'.
        The IRF is typically recorded by minimising the contribution of (3), e.g., by measuring the response of the
        instrument using a scattering sample, or a short lived dye.

    TTTR
         TTTR stands for time tagged time-resolved data or experiments. In TTTR-datasets the events, e.g., the detection
         of a photon, are tagged by a detection channel number. Moreover, the recording clock usually registers the
         events with a high time resolution of a few picoseconds. For long recording times of the detected events,
         a coarse and a fine clock are combined. The fine clock measures the time of the events relative to the coarse
         clock with a high time resolution. The time of the coarse and the fine clock is usually called macro and
         micro time, respectively.

    FRET positioning system (FPS)
        FRET positioning system, FPS, is an approach to determine structural models based on a set of FRET measurements.
        FPS explicitly considers the spatial distribution of the dyes. This way experimental observables, i.e., FRET
        efficiencies may be predicted precisely.
        The FPS-toolkit is available from the `web page <http://www.mpc.hhu.de/software/fps.html>`_ of the Seidel
        group of the Heinrich Heine University. An implementation of accessible volume simulations
        (`AV <https://github.com/Fluorescence-Tools/LabelLib>`_) used in FPS are available as open source.

    Time correlated single photon counting (TCSPC)
        Time correlated single photon counting (TCPSC) is a technique to measure light intensities with picosecond
        resolution. Its main application is the detection of fluorescent light. A pulsed light source excites a
        fluorescent sample. A single photon detector records the emitted fluorescence photons. Thus, per excitation
        cycle, only a single photon is detected. Fast detection electronics records the time between the excitation
        pulse and the detection of the fluorescence photon. A histogram accumulates multiple detected photons to yield
        a time-resolved fluorescence intensity decay.

    SWIG
        SWIG is a software development tool that connects programs written in C and C++ with a variety of high-level
        programming languages. SWIG can be used with different types of target languages including common scripting
        languages such as Javascript, Perl, PHP, Python, Tcl and Ruby and non-scripting languages such as C#, D,
        Go language, Java, Octave, and R. SWIG is free software and the code that SWIG generates is compatible with
        both commercial and non-commercial projects. ``tttrlib`` is C/C++ based to provide the capability for a
        broad variety of languages to interface its provided functionality.

    Scatter fraction
        The scatter fraction :math:`gamma` is defined by the number of photons
        that

    Anisotropy
        The steady-state anisotropy :math:`r_G` in the detection channel :math:`G`
        is formally given by the fluorescence intensity weighted integral of the
        time-resolved anisotropy.

        :math:`r_G=\int F_G(t) \cdot r(t) dt \cdot \frac{1}{\int F_G(t) dt}`

        where the time-resolved anisotropy is defined by unperturbed the fluorescence
        intensities of an ideal detection system.

        :math:`r_G(t)=\frac{F_{G,p}(t)-F_{G,s}(t)}{F_{G,p}(t)+2F_{G,s}(t)}`

        Through out ``fit2x`` two distinct anisotropies are computed: (1)
        background corrected anisotropies, and (2) anisotropies not accounting for
        the background. In single-molecule experiments the background is mainly
        scattered light (Raman scattering). The uncorrected anisotropy (without
        background correction) is computed by:

        :math:`r = (S_p - g \cdot S_s) / (S_p \cdot (1 - 3 \cdot l_2) + (2 - 3 \cdot l_1) \cdot g \cdot Ss)`

        where :math:`S_p` is the signal in the parallel (German: parallel=p) detection
        channel, :math`S_s` the signal in the perpendicular decection channel
        (German: senkrecht=s), :math:`g` is the g-factor, :math:`l_1` and
        :math:`l_2` are factor mixing that determine the mixing of the parallel
        and perpendicular detection channel, respectively :cite:`koshioka_time-dependent_1995`.

        The scatter corrected steady-state anisotropy is computed using the scatter /
        background corrected signals parallel :math:`F_p = (S_p - \gamma \cdot B_p) / (1. - \gamma)`
        and perpendicular :math:`F_s = (S_s - \gamma \cdot B_s) / (1. - \gamma)`
        fluorescence intensity.
        :math:`r = (F_p - g \cdot F_s) / (F_p \cdot (1 - 3 \cdot l_2) + (2 - 3 \cdot l_1) \cdot g \cdot F_s)`
        The scatter corrected and anisotropy not corrected for scatter are computed
        by most fits of ``fit2x``.

    Jordi-format
        In the Jordi format is a format for fluorescence decays. In the Jordi
        format fluorescence decays are stacked in a one dimensional array.
        In a typical polarization resolved Jordi file the first decay is
        the parallel and the subsequent decay is the perpendicular decay. In the
        Jordi format both decays must have the same length, i.e., the same number
        of micro time counting channels.

    PIE
        Pulsed-Interleaved Excitation (PIE) experiments excite the studied samples
        by multiple pulsed light sources for different dyes. The light sources excite
        the sample interleaved and the photons of the samples are registered by
        time-resolved detectors and electronics.
