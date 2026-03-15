import numpy as np

@property
def intensity(self):
    """
    Get the intensity array for this frame.
    
    Returns:
        numpy.ndarray: 2D intensity array with shape (n_lines, n_pixels)
    """
    return self.get_intensity()

@property
def shape(self):
    """
    Return the shape of the frame.
    
    Returns:
        tuple: (n_lines, n_pixels)
    """
    return (self.n_lines, len(self[0]) if self.n_lines > 0 else 0)

def __repr__(self):
    """String representation showing the shape of the frame."""
    n_lines = self.n_lines
    n_pixels = len(self[0]) if n_lines > 0 else 0
    return f'tttrlib.CLSMFrame({n_lines}, {n_pixels})'
