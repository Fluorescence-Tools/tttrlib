
@property
def correlation(self):
    return self.get_corr_normalized()


@property
def x_axis(self):
    return self.get_x_axis_normalized()


def __repr__(self):
    return 'tttrlib.Correlator()'


def __str__(self):
    s = "Number of evenly spaced correlation channels: %d \n" % self.get_n_bins()
    s += "Number of correlation blocks: %d \n" % self.get_n_casc()
    return s

