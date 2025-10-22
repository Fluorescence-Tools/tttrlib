
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

