
####################
## Create event trace based on selection
####################
import tttrlib
import numpy as np

data = tttrlib.TTTR('../../examples/PQ/PTU/PQ_PTU_HH_T3.ptu', 'PTU')
data.get_macro_time()

ch1_indeces = data.get_selection_by_channel(np.array([0]))
p2 = tttrlib.TTTR(data, ch1_indeces)
ch0 = p2.get_macro_time()

