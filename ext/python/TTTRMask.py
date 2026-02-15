
def __getattr__(self, item):
    """
    If an attribute `attribute` is accessed that does not exist,
    the corresponding getter method ('get_attribute') is called.
    """
    # Robustness: don't double-prepend get_
    if str(item).startswith("get_"):
        lookup = str(item)
    else:
        lookup = "get_" + str(item)

    if hasattr(self, lookup):
        call = getattr(self, lookup)
        return call()
    else:
        raise AttributeError(f"'{self.__class__.__name__}' object has no attribute '{item}'")


def __len__(self):
    return len(self.get_indices())

def get_mask(self):
    """
    Get mask as numpy array (wrapper for get_mask_array)
    
    :return: numpy bool array (view of internal C++ memory)
    """
    return self.get_mask_array()

def set_mask(self, value):
    """
    Set mask from numpy array or list (wrapper for set_mask_array)
    
    :param value: numpy bool array or list
    """
    import numpy as np
    if not isinstance(value, np.ndarray):
        value = np.array(value, dtype=bool)
    self.set_mask_array(value)

@property
def mask(self):
    """
    Get mask as numpy array (optimized, zero-copy view of C++ memory)
    
    :return: numpy bool array (view of internal C++ memory)
    """
    return self.get_mask_array()

@mask.setter
def mask(self, value):
    """
    Set mask from numpy array (optimized)
    
    :param value: numpy bool array or list
    """
    import numpy as np
    if not isinstance(value, np.ndarray):
        value = np.array(value, dtype=bool)
    self.set_mask_array(value)

def get_indices(self, selected=True):
    """
    Get indices of selected or masked elements as a list
    
    :param selected: if True returns selected (unmasked) indices, otherwise masked indices
    :return: list of indices
    """
    # Call the C++ method (which returns a vector converted to tuple by SWIG)
    indices = self._get_indices(selected)
    # Convert tuple to list
    return list(indices) if isinstance(indices, tuple) else indices

def get_selected_ranges(self):
    """
    Get selected ranges as a list of (start, stop) tuples
    
    :return: list of (start, stop) tuples, e.g., [(0, 5), (10, 15)]
    """
    # Call the C++ method (which returns a flat vector: [start1, stop1, start2, stop2, ...])
    flat_ranges = self._get_selected_ranges()
    # Convert tuple to list if needed
    flat_ranges = list(flat_ranges) if isinstance(flat_ranges, tuple) else flat_ranges
    # Convert flat list to list of (start, stop) tuples
    return [(flat_ranges[i], flat_ranges[i+1]) for i in range(0, len(flat_ranges), 2)]

