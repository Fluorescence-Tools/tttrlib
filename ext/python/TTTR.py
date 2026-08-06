# SPDX-License-Identifier: BSD-3-Clause


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
            elif hasattr(lut_array, '__iter__') and not isinstance(lut_array, str):  # numpy array or similar
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
    if isinstance(key, (tuple, list)):
        key = np.asarray(key)
    if isinstance(key, slice):
        sel = np.arange(*key.indices(self.get_n_valid_events()), dtype=np.int32)
    elif isinstance(key, np.ndarray):
        sel = key.astype(np.int32).ravel()
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
    - parameters (dict or str, optional): Reader parameters, for the containers
      that cannot describe themselves. What each accepts is declared by its
      ``params_schema`` in ``registry('file_container')``; almost every format
      needs none.
    - settings_file (str or Path, optional): Path to JSON settings file with corrections
    - settings (dict, optional): Dictionary with correction settings
    - channel_luts (dict, optional): Channel LUTs (alternative to settings)
    - channel_shifts (dict, optional): Channel shifts (alternative to settings)

    Examples:
    tttr = TTTR('file.ptu')  # Basic loading
    tttr = TTTR('file.ptu', settings_file='settings.json')  # With corrections from file
    tttr = TTTR('file.ptu', channel_luts={0: lut_array})  # With corrections directly
    tttr = TTTR('scan.ttr', 'BRIGHTEYES-TTR', {'laser_MHz': 80})  # Reader parameters
    """
    import pathlib
    import json
    import os

    # Extract our special parameters
    channel_luts = kwargs.pop('channel_luts', None)
    channel_shifts = kwargs.pop('channel_shifts', None)
    settings_file = kwargs.pop('settings_file', None)
    settings = kwargs.pop('settings', None)
    parameters = kwargs.pop('parameters', None)

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
        if isinstance(args[0], (str, os.PathLike)):
            filename = os.fspath(args[0])
            if not isinstance(filename, str):
                raise TypeError("filename must be a str/pathlike returning str")
            if len(args) > 1:
                container_type = args[1]
            else:
                container_type = -1  # infer

            # A third positional that is text or a mapping is reader parameters.
            # A bool there is the historical read_input flag, which this wrapper
            # has never forwarded, so it stays ignored rather than starting to
            # mean something new.
            if len(args) > 2 and isinstance(args[2], (str, dict)):
                parameters = args[2]
            # A dict is the convenient form and JSON is what crosses the
            # boundary; converting here keeps the C++ signature the same one R
            # and Java call, so no binding needs a special case.
            if isinstance(parameters, dict):
                parameters = json.dumps(parameters)

            if parameters is not None:
                if container_type == -1:
                    container_type = _tttrlib.inferTTTRFileType(filename)
                if isinstance(container_type, str):
                    this = _tttrlib.new_TTTR(filename, container_type, parameters, True)
                else:
                    this = _tttrlib.new_TTTR(filename, int(container_type), parameters, True)
            elif channel_luts is not None or channel_shifts is not None:
                if container_type == -1:
                    # This overload takes no "auto" sentinel, so resolve the
                    # container here. Guessing SPC-130 for every .spc used to
                    # mis-read the other Becker & Hickl flavours (e.g. SPC-QC).
                    container_type = _tttrlib.inferTTTRFileType(filename)
                    if container_type < 0:
                        container_type = 2  # BH_SPC130_CONTAINER
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


@staticmethod
def burst_search_algorithms():
    """The available burst searches as a dict, keyed by algorithm name.

    This is for burst searches what ``container_names`` is for file containers —
    a way to discover what tttrlib can do instead of hard-coding a list — but it
    also describes each algorithm's parameters, so a caller can build a user
    interface for a search it knows nothing about.

    Each entry has ``name``, ``label``, ``summary``, ``description``, the
    ``method`` implementing it, and ``params_schema``: a JSON Schema of the
    parameters, with ``type``, ``title``, ``description``, ``default``,
    ``minimum`` and ``maximum`` per property. Because that schema is standard
    JSON Schema rather than a tttrlib-specific format, anything that can already
    render a JSON Schema can render a burst-search form directly. Non-standard
    presentation hints (``unit``, ``scale``, ``advanced``) ride along as extra
    keys, which a JSON Schema consumer ignores.

    Adding an algorithm to tttrlib therefore adds it to such a UI on upgrade,
    with no change to the consuming code.

    Returns:
        dict: ``{name: {"name", "label", "summary", "description", "method",
        "params_schema"}}``

    Example:
        >>> import tttrlib
        >>> algorithms = tttrlib.TTTR.burst_search_algorithms()
        >>> sorted(algorithms)
        ['cusum_sprt', 'maxtree', 'sliding_window']
        >>> sorted(algorithms["sliding_window"]["params_schema"]["properties"])
        ['L', 'T', 'm']
    """
    import json as _json
    import tttrlib as _tttrlib
    return _json.loads(_tttrlib.TTTR.burst_search_algorithms_json())


@staticmethod
def burst_search_defaults(algorithm):
    """Default parameters of ``algorithm`` as a ``{name: value}`` dict.

    Example:
        >>> import tttrlib
        >>> tttrlib.TTTR.burst_search_defaults("sliding_window")
        {'L': 20, 'm': 10, 'T': 0.0005}
    """
    spec = TTTR._burst_search_spec(algorithm)
    return {
        name: prop["default"]
        for name, prop in spec["params_schema"]["properties"].items()
        if "default" in prop
    }


@staticmethod
def _burst_search_spec(algorithm):
    """Registry entry for ``algorithm``, or a ValueError naming the alternatives."""
    algorithms = TTTR.burst_search_algorithms()
    if algorithm not in algorithms:
        raise ValueError(
            f"unknown burst search {algorithm!r}; available: {sorted(algorithms)}"
        )
    return algorithms[algorithm]


def burst_search_by_name(self, algorithm, /, **parameters):
    """Run a burst search selected by name, filling in the registry defaults.

    Dispatches to whichever method implements ``algorithm``, so a caller can
    drive any search from a name plus a dict of parameters without knowing which
    method implements it or what its full signature is.

    Args:
        algorithm: key from :meth:`burst_search_algorithms`. Positional-only, so
            a search whose own parameters include one called ``algorithm`` — the
            coincident search names its per-group search that way — does not
            collide with it.
        **parameters: overrides; anything omitted takes its registry default.

    Returns:
        numpy.ndarray: ``(n, 2)`` array of inclusive ``[start, stop]`` photon
        indices, reshaped from the flat form the underlying methods return.

    Raises:
        ValueError: unknown algorithm, or a parameter the algorithm does not take.

    Example:
        >>> bursts = tttr.burst_search_by_name("maxtree", L=30)   # doctest: +SKIP
    """
    import numpy as _np
    spec = TTTR._burst_search_spec(algorithm)
    properties = spec["params_schema"]["properties"]

    unknown = set(parameters) - set(properties)
    if unknown:
        raise ValueError(
            f"{algorithm} does not take {sorted(unknown)}; "
            f"available: {sorted(properties)}"
        )

    kwargs = TTTR.burst_search_defaults(algorithm)
    kwargs.update(parameters)
    # Some parameters have no meaningful default — a detector grouping depends on
    # the instrument, so inventing one would silently search the wrong channels.
    required = spec["params_schema"].get("required", ())
    missing = [p for p in required if p not in kwargs]
    if missing:
        raise ValueError(
            f"{algorithm} requires {sorted(missing)}, which have no default "
            f"and must be supplied"
        )
    # Coerce to the declared JSON Schema types: a form hands back whatever its
    # widget produced, and a float where the C++ signature wants an int fails in
    # the SWIG layer rather than here.
    for name, prop in properties.items():
        if name not in kwargs:
            continue
        kind = prop.get("type")
        # Only scalars are coerced: a form hands back whatever its widget
        # produced, and a float where the C++ signature wants an int fails in the
        # SWIG layer. Arrays, objects and strings are passed through as they are.
        if kind == "integer":
            kwargs[name] = int(kwargs[name])
        elif kind == "number":
            kwargs[name] = float(kwargs[name])
        elif kind == "boolean":
            kwargs[name] = bool(kwargs[name])
    # A built-in search is a method and is reached by attribute; one from a
    # plugin has no attribute to reach, because the bindings were generated at
    # build time. Its registry entry says which it is -- a plugin entry carries
    # no "method" -- so the dispatch reads the registry rather than guessing.
    if "method" in spec:
        result = getattr(self, spec["method"])(**kwargs)
    else:
        import json as _json
        result = self.burst_search_plugin(algorithm, _json.dumps(kwargs))
    return _np.asarray(result, dtype=_np.int64).reshape(-1, 2)


def burst_search_coincident(
    self, channel_groups, algorithm="maxtree", min_groups=0, L=20,
    parameters=None,
):
    """Bursts that appear in several detector groups at once.

    A burst search over the pooled photon stream cannot tell a molecule carrying
    every label from one carrying a subset: both are simply bright. Requiring the
    burst to be found *independently* in more than one group of detectors can —
    it is what rejects singly-labelled and photobleached molecules in ALEX/PIE,
    where the classical form is the dual-channel burst search over a
    donor-excitation and an acceptor-excitation stream.

    This is the general form of that idea. Any number of detector groups may be
    given, and ``min_groups`` sets how many must agree, so "2 of 3" is expressible
    as well as "all of 2". It is a *composition*, not a new algorithm: the search
    named by ``algorithm`` runs once per group, so it works with every entry in
    the burst-search registry, including ones added later.

    Coincidence is decided in *time*, not per photon. A burst found in one group
    marks the whole span of photons it covers, so photons belonging to the other
    groups fall inside it; a photon is coincident when at least ``min_groups``
    groups mark it.

    Args:
        channel_groups: sequence of routing-channel groups, e.g.
            ``[[0, 1], [2, 3]]``. A group with no photons is ignored rather than
            forcing an empty result.
        algorithm: which registered burst search to run per group.
        min_groups: how many groups must agree. ``0`` means all of the groups
            that actually contain photons.
        L: minimum photons per burst, applied to the coincident result. The inner
            search applies its own ``L`` per group first.
        parameters: parameters for the inner search; omitted ones take their
            registry defaults.

    Returns:
        numpy.ndarray: ``(n, 2)`` array of inclusive ``[start, stop]`` photon
        indices, in the same convention as every other burst search.

    Raises:
        ValueError: no usable group, or ``min_groups`` exceeding the number of
            groups that contain photons.

    Example:
        >>> # a burst must be seen by both detector pairs
        >>> bursts = tttr.burst_search_coincident([[0, 1], [2, 3]])   # doctest: +SKIP
    """
    import numpy as _np
    import tttrlib as _tttrlib

    groups = [list(g) for g in channel_groups]
    if not groups:
        raise ValueError("channel_groups is empty")

    channels = _np.asarray(self.routing_channels)
    n_photons = len(channels)
    votes = _np.zeros(n_photons, dtype=_np.int32)

    n_used = 0
    for group in groups:
        member = _np.flatnonzero(_np.isin(channels, group))
        if member.size == 0:
            continue          # a detector group with no photons cannot vote
        n_used += 1
        selection = _tttrlib.TTTR(self, member.astype(_np.int32))
        bursts = selection.burst_search_by_name(algorithm, **dict(parameters or {}))
        for first, last in bursts:
            # Sub-indices map back through `member`; marking the enclosed global
            # range is what makes the coincidence temporal rather than per photon.
            votes[member[first]:member[last] + 1] += 1

    if n_used == 0:
        raise ValueError(
            "no channel group contains photons; "
            f"used routing channels are {sorted(set(channels.tolist()))}"
        )
    required = int(min_groups) if min_groups else n_used
    if required > n_used:
        raise ValueError(
            f"min_groups={required} exceeds the {n_used} group(s) containing photons"
        )

    mask = votes >= required
    if not mask.any():
        return _np.zeros((0, 2), dtype=_np.int64)

    # Run-length encode the coincident mask into inclusive intervals.
    padded = _np.concatenate(([False], mask, [False]))
    edges = _np.flatnonzero(padded[1:] != padded[:-1])
    starts, stops = edges[0::2], edges[1::2] - 1
    keep = (stops - starts + 1) >= int(L)
    return _np.stack([starts[keep], stops[keep]], axis=1).astype(_np.int64)
