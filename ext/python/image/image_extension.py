
@property
def intensity(self):
    return self.get_intensity_image()


@property
def mean_tac(self):
    return self.get_mean_tac_image()


def __len__(self):
    return self.n_frames()


def __repr__(self):
    return 'tttrlib.TTTR("%s", "%s")' % (
        self.get_filename(),
        self.get_tttr_container_type()
    )

