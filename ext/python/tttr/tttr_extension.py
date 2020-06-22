
@property
def micro_times(self):
    return self.get_micro_time()


@property
def routing_channels(self):
    return self.get_routing_channel()


@property
def macro_times(self):
    return self.get_macro_time()


@property
def event_types(self):
    return self.get_event_type()


@property
def header(self):
    return self.get_header()


def __len__(self):
    return self.get_n_valid_events()


def __getitem__(self, key):
    if isinstance(key, slice):
        sel = np.arange(*key.indices(self.get_n_valid_events()), dtype=np.int32)
    elif isinstance(key, np.ndarray):
        sel = key.astype(np.int32)
    else:
        sel = np.array([key], dtype=np.int32)
    return tttrlib.TTTR(self, sel)


def __repr__(self):
    return 'tttrlib.TTTR("%s", "%s")' % (
        self.get_filename(),
        self.get_tttr_container_type()
    )


def __str__(self):
    s = "Filename: %s \n" % self.get_filename()
    s += "Number of valid events: %d \n" % self.get_n_events()
    s += "Number of micro time channels: %d \n" % self.get_number_of_micro_time_channels()
    s += "Used routing channels: %s " % self.get_used_routing_channels()
    return s

