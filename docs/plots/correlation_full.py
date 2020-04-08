import tttrlib
import pylab as p
import numpy as np

data = tttrlib.TTTR('../../test/data/BH/BH_SPC132.spc', 'SPC-130')

# get the indices of the two channels
ch1_indeces = data.get_selection_by_channel([8])
ch2_indeces = data.get_selection_by_channel([0])

# use the indices to create new TTTR objects
tttr_ch1 = tttrlib.TTTR(data, ch1_indeces)
tttr_ch2 = tttrlib.TTTR(data, ch2_indeces)
# correlate with the new TTTR objects
correlator = tttrlib.Correlator(
    method='default',
    n_casc=35,
    n_bins=6,
    tttr=(tttr_ch1, tttr_ch2),
    make_fine=True
)
# Alternatively instead of the constructor use method below
# correlator.set_tttr(data_ch1, data_ch2, make_fine=True)
x = correlator.x_axis
y = correlator.correlation

# use pylab to plot the data
p.semilogx(x, y)
# p.show()

# the above should produce the same result as the code below
correlator_ref = tttrlib.Correlator(
    tttr=data,
    n_casc=35,
    n_bins=6
)
t1 = data.macro_times[ch1_indeces]
t2 = data.macro_times[ch2_indeces]
mt1 = data.micro_times[ch1_indeces]
mt2 = data.micro_times[ch2_indeces]
w1 = np.ones_like(t1, dtype=np.float)
w2 = np.ones_like(t2, dtype=np.float)
correlator_ref.set_macrotimes(t1, t2)
correlator_ref.set_weights(w1, w2)
n_microtime_channels = data.get_number_of_micro_time_channels()
correlator_ref.set_microtimes(mt1, mt2, n_microtime_channels)
x_ref = correlator_ref.x_axis
y_ref = correlator_ref.correlation

p.semilogx(x_ref, y_ref)
p.show()
