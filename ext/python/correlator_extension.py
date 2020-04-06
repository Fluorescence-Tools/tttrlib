
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

def __init__(
        self,
        macro_times=None,
        weights=None,
        n_bins=17,
        n_casc=25,
        **kwargs
):
    this = _tttrlib.new_Correlator(**kwargs)
    try:
        self.this.append(this)
    except:
        self.this = this
    if weights:
        w1, w2 = weights
        self.set_weights(w1, w2)
    if macro_times:
        t1, t2 = macro_times
        self.set_macrotimes(t1, t2)
    self.n_bins = n_bins
    self.n_casc = n_casc
