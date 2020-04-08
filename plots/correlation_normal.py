import pylab as p
import tttrlib
import numpy as np

fig, ax = p.subplots(nrows=1, ncols=2)


#  Read the data data
data = tttrlib.TTTR('../../test/data/BH/BH_SPC132.spc', 'SPC-130')

# Create correlator
# Caution: x-axis in units of macro time counter
# tttrlib.Correlator is unaware of the calibration in the TTTR object
correlator = tttrlib.Correlator()

# Select the green channels (channel number 0 and 8)
ch1_indeces = data.get_selection_by_channel([0])
ch2_indeces = data.get_selection_by_channel([8])

# Select macro times for Ch1 and Ch2 and create array of weights
t = data.macro_times
t1 = t[ch1_indeces]
w1 = np.ones_like(t1, dtype=np.float)
t2 = t[ch2_indeces]
w2 = np.ones_like(t2, dtype=np.float)
correlator.set_events(t1, w1, t2, w2)

x = correlator.get_x_axis_normalized()
# scale the x-axis to have units in milliseconds
x *= (data.header.macro_time_resolution / 1e6)
y = correlator.get_corr_normalized()

ax[0].semilogx(x, y)


# green-red cross correlation

correlator = tttrlib.Correlator(
    tttr=(
        tttrlib.TTTR(data, data.get_selection_by_channel([0, 8])),
        tttrlib.TTTR(data, data.get_selection_by_channel([1, 9]))
    )
)
# no need to scale axis - correlator aware of macro time units
ax[1].semilogx(
    correlator.x_axis,
    correlator.correlation
)


p.show()
