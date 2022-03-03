
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

