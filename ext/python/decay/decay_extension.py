
@property
def model(self):
    return self.get_model()


@property
def data(self):
    return self.get_data()


@data.setter
def data(self, v):
    self.set_data(v)


@property
def irf(self):
    return self.get_irf()


@irf.setter
def irf(self, v):
    self.set_irf(v)


@property
def weighted_residuals(self):
    return self.get_weighted_residuals()


@property
def time_axis(self):
    return self.get_time_axis()


@property
def irf_background_counts(self):
    return self.get_irf_background_counts()


@irf_background_counts.setter
def irf_background_counts(self, v):
    return self.set_irf_background_counts(v)


@property
def lifetime_spectrum(self):
    return self.get_lifetime_spectrum()


@lifetime_spectrum.setter
def lifetime_spectrum(self, v):
    self.set_lifetime_spectrum(v)


@property
def chi2_mle(self):
    return self.get_chi2_mle()


@property
def chi2(self):
    return self.get_chi2()


def __repr__(self):
    s = 'DECAY \n'
    s += '-- Lifetime spectrum: %s \n' % self.get_lifetime_spectrum()
    s += '-- Constant background / counts: %s \n' % self.get_constant_background()
    s += '-- Areal scatter fraction: %s \n' % self.get_areal_scatter_fraction()
    s += '-- Irf shift / channels: %s \n' % self.get_irf_shift_channels()
    s += '-- Irf background / counts: %s \n' % self.get_irf_background_counts()
    s += '-- Chi2: %s \n' % self.chi2
    return s

