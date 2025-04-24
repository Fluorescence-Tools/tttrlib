

@property
def routing_channels(self):
    return self.get_routing_channel()

@property
def event_types(self):
    return self.get_event_type()

def __getattr__(self, item):
    """
    If an attribute `attribute` is accessed that does not exist,
    the corresponding getter method ('get_attribute') is called.
    Works for both instance and static methods.
    """
    item = "get_" + str(item)
    # Check if the static method or instance method exists in the class
    if hasattr(self.__class__, item):
        call = getattr(self.__class__, item)
        if isinstance(call, staticmethod):
            # If it's a static method, call it directly from the class
            return call.__get__(None, self.__class__)()
        else:
            # Otherwise, assume it's an instance method
            return call(self)
    else:
        raise AttributeError(f"'{self.__class__.__name__}' object has no attribute '{item}'")

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
    import pathlib
    import sys
    
    if len(args) > 0:
        # Case 1: First argument is a filename (string or Path)
        if isinstance(args[0], (str, pathlib.Path)):
            if len(args) == 1:
                # Convert to absolute path and use posix separator for consistency
                obj = str(pathlib.Path(args[0]).absolute().as_posix())
                this = _tttrlib.new_TTTR(obj)
            else:
                # Additional arguments - pass them all through
                this = _tttrlib.new_TTTR(*args, **kwargs)
        
        # Case 2: First argument is another TTTR object (for copying or selection)
        elif args[0].__class__.__name__ == 'TTTR':
            this = _tttrlib.new_TTTR(*args, **kwargs)
        
        # Case 3: First argument is a numpy array or other data structure
        else:
            try:
                this = _tttrlib.new_TTTR(*args, **kwargs)
            except Exception as e:
                err_type = str(type(args[0]))
                raise TypeError(f"Cannot create TTTR object from first argument of type {err_type}. "
                                f"Expected a filename (str/Path) or TTTR object. Error: {str(e)}")
    else:
        # Case 4: No arguments - create an empty TTTR object
        this = _tttrlib.new_TTTR(*args, **kwargs)
        
    self.this = this

def __repr__(self):
    return f'TTTR("{self.get_filename()}", "{self.get_tttr_container_type()}")'

def __str__(self):
    return (
        f"Filename: {self.get_filename()}\n"
        f"Number of valid events: {self.get_n_events()}\n"
        f"Number of micro time channels: {self.get_number_of_micro_time_channels()}\n"
        f"Used routing channels: {self.get_used_routing_channels()}"
    )
