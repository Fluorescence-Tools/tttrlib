

@property
def routing_channels(self):
    return self.get_routing_channel()

@property
def event_types(self):
    return self.get_event_type()

@property
def acquisition_time(self):
    return (self.macro_times[-1] - self.macro_times[0]) * self.header.macro_time_resolution

@property
def micro_times(self):
    return self.get_micro_times()

@property
def macro_times(self):
    return self.get_macro_times()

def apply_channel_luts(self, channel_luts, channel_shifts=None):
    """
    Apply channel LUTs and shifts to this TTTR object.
    
    Parameters:
    - channel_luts: dict or MapIntVectorFloat
        Dictionary mapping channel numbers to LUT arrays (as lists or numpy arrays)
    - channel_shifts: dict or MapSignedCharInt, optional
        Dictionary mapping channel numbers to shift values
    
    Returns:
    - int: Success status
    """
    import tttrlib
    
    # Handle native Python dict types
    if isinstance(channel_luts, dict):
        luts_map = tttrlib.MapIntVectorFloat()
        shifts_map = tttrlib.MapSignedCharInt()
        
        for ch, lut_array in channel_luts.items():
            if isinstance(lut_array, (list, tuple)):
                lut_vec = tttrlib.VectorFloat()
                for val in lut_array:
                    lut_vec.append(float(val))
                luts_map[ch] = lut_vec
            elif hasattr(lut_array, '__iter__'):  # numpy array or similar
                lut_vec = tttrlib.VectorFloat()
                for val in lut_array:
                    lut_vec.append(float(val))
                luts_map[ch] = lut_vec
            else:
                raise TypeError(f"LUT array for channel {ch} must be a list, tuple, or array-like")
        
        # Handle shifts
        if channel_shifts is None:
            channel_shifts = {}
        elif isinstance(channel_shifts, dict):
            shifts_map = tttrlib.MapSignedCharInt()
            for ch, shift_val in channel_shifts.items():
                shifts_map[ch] = int(shift_val)
        else:
            shifts_map = channel_shifts
            
        return _tttrlib.TTTR_apply_channel_luts(self, luts_map, shifts_map)
    else:
        # Assume already proper tttrlib types
        if channel_shifts is None:
            channel_shifts = tttrlib.MapSignedCharInt()
        return _tttrlib.TTTR_apply_channel_luts(self, channel_luts, channel_shifts)

    def load_settings_file(self, settings_file):
        """
        Load TTTR correction settings from a JSON file.

        Parameters:
        - settings_file: str or Path
            Path to the JSON settings file

        Sets attributes:
        - self.settings: dict containing the loaded settings
        - self.channel_luts: dict of channel LUTs (converted to numpy arrays)
        - self.channel_shifts: dict of channel shifts
        """
        import json
        import numpy as np
        import pathlib

        settings_path = pathlib.Path(settings_file)
        if not settings_path.exists():
            raise FileNotFoundError(f"Settings file not found: {settings_file}")

        with open(settings_path, 'r') as f:
            self.settings = json.load(f)

        # Convert LUT lists back to numpy arrays
        self.channel_luts = {}
        if 'channel_luts' in self.settings:
            for ch_str, lut_list in self.settings['channel_luts'].items():
                ch = int(ch_str)
                self.channel_luts[ch] = np.array(lut_list, dtype=np.float64)

        # Store channel shifts
        self.channel_shifts = {}
        if 'channel_shifts' in self.settings:
            for ch_str, shift_val in self.settings['channel_shifts'].items():
                ch = int(ch_str)
                self.channel_shifts[ch] = int(shift_val)

        print(f"Loaded settings from: {settings_file}")
        print(f"  Channels with LUTs: {list(self.channel_luts.keys())}")
        print(f"  Channels with shifts: {list(self.channel_shifts.keys())}")

    def apply_settings(self):
        """
        Apply the loaded settings to this TTTR object.

        Requires that load_settings_file() has been called first.

        Applies LUTs and shifts using apply_channel_luts() and apply_luts_and_shifts().
        """
        if not hasattr(self, 'settings'):
            raise RuntimeError("No settings loaded. Call load_settings_file() first.")

        if not hasattr(self, 'channel_luts'):
            raise RuntimeError("No channel LUTs found in settings.")

        # Apply the corrections
        self.apply_channel_luts(self.channel_luts, self.channel_shifts or {})
        self.apply_luts_and_shifts(-1, True)

        print(f"Applied corrections to {len(self.channel_luts)} channels")

def __getattr__(self, item):
    """
    If an attribute `attribute` is accessed that does not exist,
    the corresponding getter method ('get_attribute') is called.
    Works for both instance and static methods.
    """
    # Special case for apply_luts_and_shifts which is not exposed in current build
    if item == "apply_luts_and_shifts":
        return self._apply_luts_and_shifts_impl

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
    import numpy as np
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
    """
    Initialize a TTTR object.
    
    Parameters:
    - filename (str or Path): Path to TTTR file to load
    - container_type (int, optional): TTTR container type (-1 for auto-detect)
    - settings_file (str or Path, optional): Path to JSON settings file with corrections
    - settings (dict, optional): Dictionary with correction settings
    - channel_luts (dict, optional): Channel LUTs (alternative to settings)
    - channel_shifts (dict, optional): Channel shifts (alternative to settings)
    
    Examples:
    tttr = TTTR('file.ptu')  # Basic loading
    tttr = TTTR('file.ptu', settings_file='settings.json')  # With corrections from file
    tttr = TTTR('file.ptu', channel_luts={0: lut_array})  # With corrections directly
    """
    import pathlib
    import json
    
    # Extract our special parameters
    channel_luts = kwargs.pop('channel_luts', None)
    channel_shifts = kwargs.pop('channel_shifts', None)
    settings_file = kwargs.pop('settings_file', None)
    settings = kwargs.pop('settings', None)
    
    # Handle settings parameters
    if settings_file is not None or settings is not None:
        if settings_file is not None:
            # Load settings from file
            settings_path = pathlib.Path(settings_file)
            if not settings_path.exists():
                raise FileNotFoundError(f"Settings file not found: {settings_file}")
            with open(settings_path, 'r') as f:
                settings = json.load(f)
        
        # Extract corrections from settings
        if 'channel_luts' in settings:
            channel_luts = {}
            for ch_str, lut_list in settings['channel_luts'].items():
                ch = int(ch_str)
                channel_luts[ch] = lut_list
        if 'channel_shifts' in settings:
            channel_shifts = {}
            for ch_str, shift_val in settings['channel_shifts'].items():
                ch = int(ch_str)
                channel_shifts[ch] = shift_val
    
    if len(args) > 0:
        # Case 1: First argument is a filename (string or Path)
        if isinstance(args[0], (str, pathlib.Path)):
            filename = str(pathlib.Path(args[0]).absolute().as_posix())
            if len(args) > 1:
                container_type = args[1]
            else:
                container_type = -1  # infer
            
            if channel_luts is not None or channel_shifts is not None:
                if container_type == -1:
                    container_type = 2  # assume BH_SPC130_CONTAINER for .spc files
                this = _tttrlib.new_TTTR(filename, container_type, channel_luts or {}, channel_shifts or {}, True)
            else:
                if len(args) == 1:
                    this = _tttrlib.new_TTTR(filename)
                else:
                    # Check if container_type is a string or int
                    if isinstance(container_type, str):
                        this = _tttrlib.new_TTTR(filename, container_type)
                    else:
                        this = _tttrlib.new_TTTR(filename, container_type, True)
        
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
    
    # Apply channel LUTs and shifts if provided and TTTR has macro times
    if (channel_luts is not None or channel_shifts is not None) and hasattr(self, 'macro_times') and len(self.macro_times) > 0:
        self.apply_channel_luts(channel_luts or {}, channel_shifts or {})
        self.apply_luts_and_shifts(-1, True)

def __repr__(self):
    return f'TTTR("{self.get_filename()}", "{self.get_tttr_container_type()}")'

def __str__(self):
    return (
        f"Filename: {self.get_filename()}\n"
        f"Number of valid events: {self.get_n_events()}\n"
        f"Number of micro time channels: {self.get_number_of_micro_time_channels()}\n"
        f"Used routing channels: {self.get_used_routing_channels()}"
    )
