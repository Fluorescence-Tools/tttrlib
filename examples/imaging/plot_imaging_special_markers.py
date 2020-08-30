"""
=================
Marker in imaging
=================

"""
import tttrlib
import numpy as np
import pylab as p

events = tttrlib.TTTR('../../test/data/imaging/pq/ht3/pq_ht3_clsm.ht3', 1)
# select special events
e = events.get_event_type()
c = events.get_routing_channel()

special_events = np.where(e == 1)

fig, ax = p.subplots(3, 1, sharey=False)

p.setp(ax[0].get_xticklabels(), visible=False)
ax[0].plot(c[special_events][:7000], 'g')
ax[1].plot(c[special_events][:50], 'g')
ax[2].plot(c[special_events][len(c[special_events])-200:], 'g')


p.show()