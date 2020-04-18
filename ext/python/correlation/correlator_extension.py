
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
        tttr=None,
        macro_times=None,
        weights=None,
        channels=None,
        **kwargs
):
    # prepare kwargs
    make_fine = kwargs.pop('make_fine', False)
    if isinstance(tttr, tuple):
        t1, t2 = tttr
        kwargs['tttr'] = t1
    elif isinstance(tttr, tttrlib.TTTR):
        kwargs['tttr'] = tttr
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
    if isinstance(tttr, tuple):
        t1, t2 = tttr
        self.set_tttr(t1, t2, make_fine)
    # The channels argument can contain the channel numbers in
    # the TTTR object that should be correlated.
    if isinstance(channels, tuple):
        ch1, ch2 = channels
        # use the indices to create new TTTR objects these
        self.set_tttr(
            tttrlib.TTTR(tttr, tttr.get_selection_by_channel(ch1)),
            tttrlib.TTTR(tttr, tttr.get_selection_by_channel(ch2)),
            make_fine
        )
