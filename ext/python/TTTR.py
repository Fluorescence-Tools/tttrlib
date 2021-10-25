
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
#
# @property
# def header(self):
#     return self.get_header()
#
# @property
# def filename(self):
#     return self.get_filename()

def __getattr__(self, item):
    """
    If an attribute `attribute` is accesses that does not exist
    the corresponding getter by calling 'get_attribute' is called

    :param self:
    :param item:
    :return:
    """
    try:
        item = "get_" + str(item)
        call = getattr(self, item)
        return call()
    except:
        raise AttributeError

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

def __add__(self, value):
    t = tttrlib.TTTR(self)
    t.append(value)
    return t

def __init__(self, *args, **kwargs):
    if len(args) > 0:
        import pathlib
        if isinstance(args[0], str) or isinstance(args[0], pathlib.Path):
            if len(args) == 1:
                suffix = str(pathlib.Path(args[0]).suffix)[1:].upper()
                obj = str(pathlib.Path(args[0]).absolute())
                if suffix == 'HT3' or suffix == 'PTU':
                    tttr_container_type = suffix
                elif suffix == 'HDF' or suffix == 'H5' or suffix == 'HDF5':
                    tttr_container_type = 'HDF'
                else:
                    raise ValueError("The file type '{}' does not allow to determine "
                                     "the container format. Specify the 'tttr_container_type' "
                                     "parameter.".format(suffix))
                this = _tttrlib.new_TTTR(obj, tttr_container_type)
            else:
                this = _tttrlib.new_TTTR(*args, **kwargs)
        else:
            this = _tttrlib.new_TTTR(*args, **kwargs)
    else:
        this = _tttrlib.new_TTTR(*args, **kwargs)
    self.this = this

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

