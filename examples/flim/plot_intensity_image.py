"""
===============
Intensity image
===============

"""
import pylab as plt
import tttrlib

ht3_filename_img = '../../tttr-data/imaging/pq/ht3/crn_clv_img.ht3'
data = tttrlib.TTTR(ht3_filename_img, 'HT3')
clsm_image = tttrlib.CLSMImage(tttr_data=data, fill=True, channels=[0])
plt.imshow(clsm_image.intensity.sum(axis=0))
plt.show()
