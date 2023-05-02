"""
================================
TTTR writing / Modify CLSM files
===============================

Here it is illustrated how TTTR data of CLSM experiments can be created and written. 
Data of an existing CLSM experiment is read, masked, and the masked data is written to 
a file that can be processed by any other software processing the original TTTR data.

In this example:

1. TTTR data set of a CLSM measurement is read
2. a CLSM image for the TTTR data is created
3. photon in pixels within the CLSM container are selected
4. a new TTTR container is created using the original header as template
5. data is copied into the new TTTR container (masking photons)
6. a new TTTR file is created using the TTTR container
7. the newly create TTTR file is read and a CLSM image is create for the filered data


The source code of this example can be used to build analysis pipelines to mix and match
different software.
"""
#%%
import json
import tttrlib
import numpy as np
import pylab as plt

#%%
filename_tttr = '../../tttr-data/imaging/zeiss/eGFP_bad_background/eGFP_bad_background.ptu'
tttr = tttrlib.TTTR(filename_tttr)

clsm = tttrlib.CLSMImage(tttr, fill=True, channels=tttr.used_routing_channels)

# simple filter that selectes indices in pixels by the number of photons
def pixel_with_less_photons(clsm: tttrlib.CLSMImage, n_min: int = 3):
    idx_del = list()
    for frame in clsm:
        for line in frame:
            for pixel in line:
                if pixel.size() < n_min:
                    idx_del += pixel.tttr_indices
    return idx_del


# Create a new empty TTTR container
d2 = tttrlib.TTTR()

# Load header from original TTTR. 
# At this stage the header content can be edited
header_dict = json.loads(tttr.header.json)

# Set the header information of the empty TTTR container
d2.header.set_json(json.dumps(header_dict))

# Assign events to the empty / new TTTR container
# only selecting pixels with more than 10 photons.
# This selection only serves as an example. Any other selection
# will also work.
n_ph_min = 10
idx_del = pixel_with_less_photons(clsm, n_ph_min)
events = {
    "macro_times":  np.delete(tttr.macro_times, idx_del),
    "micro_times": np.delete(tttr.micro_times, idx_del),
    "routing_channels": np.delete(tttr.routing_channels, idx_del),
    "event_types": np.delete(tttr.event_types, idx_del)
}
d2.append_events(**events)

# Write the new TTTR container to a file
fn_stripped = "o4.ptu"
d2.write(fn_stripped)

# Read the file (to make sure everything worked)
d3 = tttrlib.TTTR(fn_stripped)
clsm_stripped = tttrlib.CLSMImage(d3, fill=True, channels=tttr.used_routing_channels)

# The image can be constructed
plt.imshow(clsm_stripped.intensity[0])
plt.show()

# The slection worked.
plt.hist(clsm_stripped.intensity.flatten(), bins=20, range=(1, 20))
plt.hist(clsm.intensity.flatten(), bins=20, range=(1, 20))
plt.show()

