

def histogram_function(self, cb):
    # type: (Callable) -> None
    """Set the callback function that is used to compute 1D histograms

    :param cb: the callback function
    :return: None
    """
    class PdaCallbackPython(tttrlib.PdaCallback):
        def __init__(
                self,
                cb_function,
                *args, **kwargs
        ):
        # type: (typing.Callable) -> None
            super().__init__(*args, **kwargs)
            self._cb = cb_function

        def run(self, *args, **kwargs):
            return self._cb(*args, **kwargs)
    cb_instance = PdaCallbackPython(cb_function=cb)
    cb_instance.__disown__()
    self.set_callback(cb_instance)


histogram_function = property(None, histogram_function)


@property
def pf(self):
    return self.getPF()


@pf.setter
def pf(self, v):
    self.setPF(v)


@property
def spectrum_ch1(self):
    return self.get_probability_spectrum_ch1()


@spectrum_ch1.setter
def spectrum_ch1(self, v):
    self.set_probability_spectrum_ch1(v)


@property
def species_amplitudes(self):
    return self.get_amplitudes()


@species_amplitudes.setter
def species_amplitudes(self, v):
    self.set_amplitudes(v)


@property
def probabilities_ch1(self):
    return self.get_probabilities_ch1()


@probabilities_ch1.setter
def probabilities_ch1(self, v):
    self.set_probabilities_ch1(v)


@property
def s1s2(self):
    return self.get_S1S2_matrix()


def __repr__(self):
    return 'Pda("n_species: %s")' % (
        len(self.get_amplitudes())
    )


def __str__(self):
    s = "Pda: \n"
    s += "Number of species: %d \n" % len(self.get_amplitudes())
    s += "Probability spectrum: %s \n" % self.spectrum_ch1()
    s += "Background Channel 1:" + self.background_ch1
    s += "Background Channel 2:" + self.background_ch2
    s += "Histogram 2D valid:" + self.hist2d_valid
    s += "Maximum number of photons:" + self.hist2d_nmax
    s += "Minimum number of photons:" + self.hist2d_nmin
    s += "P(F):" + self.pf
    return s

