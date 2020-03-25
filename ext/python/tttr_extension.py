
@property
def micro_times(self):
    return self.get_micro_time()


@property
def routing_channels(self):
    return self.get_routing_channel()


@property
def macro_times(self):
    return self.get_macro_time()


def __repr__(self):
    return 'tttrlib.TTTR("%s", "%s")' % (
        self.get_filename(),
        self.get_tttr_container_type()
    )


def __str__(self):
    s = "Filename: %s \n" % self.get_filename()
    s += "Number of valid events: %d \n" % self.get_n_events()
    s += "Number of micro time channels: %d \n" % self.get_number_of_tac_channels()
    s += "Used routing channels: %s " % self.get_used_routing_channels()
    return s

