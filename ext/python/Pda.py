# SPDX-License-Identifier: BSD-3-Clause

# PDA Implementation constants
PDA_DEFAULT = 0
PDA_OPTIMIZED = 1

def histogram_function(self, cb):
    # type: (Callable) -> None
    """Set the callback that reduces an (S1, S2) cell to a 1D observable.

    The callback is called as ``cb(ch1, ch2)`` with the photon counts in
    channel 1 and channel 2, in that order, and returns the x value the cell
    contributes to. For a proximity ratio Sr / (Sg + Sr) with channel 1 green,
    that is ``lambda ch1, ch2: ch2 / (ch1 + ch2)``.

    :param cb: the callback function
    :return: None
    """
    class PdaCallbackPython(PdaCallback):
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
    return 'Pda(n_species=%d, hist2d_nmin=%d, hist2d_nmax=%d)' % (
        len(self.get_amplitudes()), self.hist2d_nmin, self.hist2d_nmax
    )

def __str__(self):
    # Every line below used to concatenate str + float and raise TypeError,
    # so str(pda) never worked at all.
    s = "Pda:\n"
    s += "Number of species: %d\n" % len(self.get_amplitudes())
    s += "Species amplitudes: %s\n" % self.species_amplitudes
    s += "Probabilities ch1: %s\n" % self.probabilities_ch1
    s += "Background ch1: %s\n" % self.background_ch1
    s += "Background ch2: %s\n" % self.background_ch2
    s += "Histogram 2D valid: %s\n" % self.hist2d_valid
    s += "Maximum number of photons: %s\n" % self.hist2d_nmax
    s += "Minimum number of photons: %s\n" % self.hist2d_nmin
    s += "P(F): %s\n" % self.pf
    return s

