
@property
def intensity(self):
    return self.get_intensity_image()


@property
def mean_tac(self):
    return self.get_mean_tac_image()


def __len__(self):
    return self.n_frames


def __repr__(self):
    return 'tttrlib.CLSMImage(%s, %s, %s)' % (
        self.n_frames,
        self.n_lines,
        self.n_pixel
    )

