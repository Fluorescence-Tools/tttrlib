

@property
def routing_channels(self):
    return self.get_routing_channel()

@property
def event_types(self):
    return self.get_event_type()

def __getattr__(self, item):
    """
    If an attribute `attribute` is accesses that does not exist
    the corresponding getter by calling 'get_attribute' is called

    :param self:
    :param item:
    :return:
    """
    item = "get_" + str(item)
    if hasattr(self.__class__, item):
        call = getattr(self, item)
        return call()
    else:
        raise AttributeError

def __len__(self):
    return self.get_n_valid_events()

def __getitem__(self, key):
    if isinstance(key, tuple):
        key = np.array(key)
    if isinstance(key, slice):
        sel = np.arange(*key.indices(self.get_n_valid_events()), dtype=np.int32)
    elif isinstance(key, np.ndarray):
        sel = key.astype(np.int32)
    else:
        sel = np.array([key], dtype=np.int32)
    return TTTR(self, sel)

def __add__(self, value):
    t = TTTR(self)
    t.append(value)
    return t

def __init__(self, *args, **kwargs):
    if len(args) > 0:
        import pathlib
        if isinstance(args[0], str) or isinstance(args[0], pathlib.Path):
            if len(args) == 1:
                obj = str(pathlib.Path(args[0]).absolute().as_posix())
                this = _tttrlib.new_TTTR(obj)
            else:
                this = _tttrlib.new_TTTR(*args, **kwargs)
        else:
            this = _tttrlib.new_TTTR(*args, **kwargs)
    else:
        this = _tttrlib.new_TTTR(*args, **kwargs)
    self.this = this

def __repr__(self):
    return 'TTTR("%s", "%s")' % (
        self.get_filename(),
        self.get_tttr_container_type()
    )

def __str__(self):
    s = "Filename: %s \n" % self.get_filename()
    s += "Number of valid events: %d \n" % self.get_n_events()
    s += "Number of micro time channels: %d \n" % self.get_number_of_micro_time_channels()
    s += "Used routing channels: %s " % self.get_used_routing_channels()
    return s

