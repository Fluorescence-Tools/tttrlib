"""
=====================================
Image-based segmentation of FLIM data
=====================================
Overview
--------
In live cell imaging, the conformation or the oligomeric state of proteins can
depend on their subcellular location. In this application we show how to combine
image segmentation with fluorescence-lifetime based analysis and generate fluorescence
intensity of molecular subensembles to resolve molecular states of the protein
of interest for different subcellular compartments. Here, we segment MFIS/FLIM data
into nucleus, cytoplasm and vesicles-like structures.

Note, the data of this sample was acquired in Pulsed Interleaved Excition (PIE) mode,
however, we do not use the microtime information in the red channels to split between
"prompt" and "delay" time window.

"""

# Import all required libraries
import tttrlib
import numpy as np
import matplotlib.pyplot as plt
import skimage as ski
import skimage.filters
import skimage.morphology
import skimage.util
import scipy
import scipy.ndimage

#%%
# Loading data and creating the intensity images
#----------------------------------------------
# First, the data is loaded and a tttrlib.CLSMImage generated
# Based on the used routing channels (green channel 0 and 1 and red channels
# 4 and 5) frame-wise intensity images are generated.
# 1. Load data
filename_data = '../../tttr-data/imaging/pq/ht3/mGBP_DA.ht3'
tttr_data = tttrlib.TTTR(filename_data)

#%%
# 2. Read image
# ^^^^^^^^^^^^^^
# Constructs a container for CLSM images. A CLSM image container contains a ordered
# list of frames. Each frame contains a set of lines. Each line contains a set of pixel.
# Frames, lines, and pixels refer to an photon index range in the TTTR data stream. When
# a new CLSMImage container is created the markers for frames and lines contained in the
# data stream are read. There are usually no pixel markers. Markers are identified in the
# data stream by numbers. There are no default identifier for frames and lines. If no identifier
# for markers are provided, either the default values for a particular setup are used,
# or meta data contained in the TTTR file is read. Creating CLSM container can be time
# consuming, as the entire TTTR data stream is processed in that set. Thus, try to reuse
# CLSM containers when possible.
clsm_image = tttrlib.CLSMImage(tttr_data)

#%%
# 3. Define used channels
print("Used routing channels:", tttr_data.get_used_routing_channels())
green_ch = [0, 1]
red_ch = [4, 5]
sum_all = [0, 1, 4, 5]

#%%
# Intensity analysis
# ------------------
# 4. Create intensity images
# Fills the CLSM image container with intensities
# An intensity image the number of counts in a pixel corresponds to the number of photons
# Red intensity is toatal intenstiy, ie, prompt + delayed excitation
clsm_image.fill_pixels(tttr_data, channels=green_ch)  # green channels
int_green = clsm_image.intensity

clsm_image.fill_pixels(tttr_data, channels=red_ch)  # red channels
int_red = clsm_image.intensity

clsm_image.fill_pixels(tttr_data, channels=sum_all)  # Merged sum of all channels
int_all_channel = clsm_image.intensity

#%%
# Display images
# --------------
# 5. Sum over all frames
# Note: image shape is in the order of t-y-x, thus summation of over axis=0
# results in generating the t-projection
SUM_green = int_green.sum(axis=0)
SUM_red = int_red.sum(axis=0)
SUM_intensity_all = int_all_channel.sum(axis=0)

fig, ax = plt.subplots(nrows=1, ncols=2, sharex=True, sharey=True)
im = ax[0].imshow(SUM_green, cmap='viridis')
ax[0].set_title('Integrated intensity (green)')
fig.colorbar(im, ax=ax[0], fraction=0.046, pad=0.04)
im = ax[1].imshow(SUM_red, cmap='inferno')
ax[1].set_title('Integrated intensity (red)')
fig.colorbar(im, ax=ax[1], fraction=0.046, pad=0.04)
fig.show()


#%%
# Micro time analysis
# -------------------
# Computes mean arrival time of photons
# The mean microtime in the pixels for stacked frames
# pixels that have less than the specified number of photons
# are filled with negative values.
minimum_number_of_photons_per_pixel=30
clsm_image.fill_pixels(tttr_data, channels=green_ch)  # green channels
mean_microtime_green = clsm_image.get_mean_micro_time_image(
    tttr_data,
    minimum_number_of_photons=minimum_number_of_photons_per_pixel,
    stack_frames=True)[0]

# The mean microtime in the pixels for stacked frames
clsm_image.fill_pixels(tttr_data, channels=red_ch)  # green channels
mean_microtime_red = clsm_image.get_mean_micro_time_image(
    tttr_data,
    minimum_number_of_photons=minimum_number_of_photons_per_pixel,
    stack_frames=True)[0]

fig, ax = plt.subplots(nrows=1, ncols=2, sharex=True, sharey=True)
im = ax[0].imshow(mean_microtime_green, cmap='viridis', vmin=0.0)
ax[0].set_title('Mean microtime (green)')
fig.colorbar(im, ax=ax[0], fraction=0.046, pad=0.04)
im = ax[1].imshow(mean_microtime_red, cmap='inferno', vmin=0.0)
ax[1].set_title('Mean microtime (red)')
fig.colorbar(im, ax=ax[1], fraction=0.046, pad=0.04)
fig.show()


#%%
# Use mean arrival time in each pixel. Visualzed decay for all
# Plot of micro time histograms
# Create new TTTR objects using only photons from green and red channels
# Make histogram over micro times. Counts and time axis
tttr_green = tttr_data.get_tttr_by_channel(green_ch)
tttr_red = tttr_data.get_tttr_by_channel(red_ch)
microtime_hist_green = tttr_green.microtime_histogram()[::-1]
microtime_hist_red = tttr_red.microtime_histogram()[::-1]

fig, ax = plt.subplots(nrows=1, ncols=2, sharex=True, sharey=True)
ax[0].semilogy(*microtime_hist_green, color="green") # unpack hist to plot correct time axis
ax[1].semilogy(*microtime_hist_red, color="red")
fig.show()

#%%
# Reminder: Red intensity is toatal intenstiy, ie, prompt + delayed excitation
# More red signal due to direct excitation. For quantitative FRET prompt and
# delay need to be splitted.

#%%
# Lifetime analysis
# -----------------
# Explaing issued with mean arrival time
# IRF
# More detailed analyis with IRF
# Load IRF and inspect IRF for channels
filename_irf = '../../tttr-data/imaging/pq/ht3/mGBP_IRF.ht3'
irf = tttrlib.TTTR(filename_irf)
tttr_irf_green = irf.get_tttr_by_channel(green_ch)
tttr_irf_red = irf.get_tttr_by_channel(red_ch)

#%
# For detailed analysis we split the data into prompt
# and delay. We plot the decays to define the ranges.
n_micro = tttr_data.header.number_of_micro_time_channels
prompt_range = 0, 11000
delay_range = 11000, 25000
fig, ax = plt.subplots(nrows=1, ncols=2, sharex=True, sharey=True)
ax[0].semilogy(tttr_irf_green.microtime_histogram()[0], label="green: %s" % green_ch, color="green")
ax[0].axvspan(*prompt_range, color='blue', alpha=0.1, label="Prompt")
ax[1].semilogy(tttr_irf_red.microtime_histogram()[0], label="red: %s" % red_ch, color="red")
ax[1].axvspan(*delay_range, color='orange', alpha=0.1, label="Delay")
ax[0].legend()
ax[1].legend()
fig.show()


fig, ax = plt.subplots(nrows=1, ncols=2, sharex=True, sharey=True)
im = ax[0].imshow(mean_microtime_green, cmap='plasma', vmin=0.0, vmax=4e-9)
ax[0].set_title('Mean micro time (green)')
fig.colorbar(im, ax=ax[0], fraction=0.046, pad=0.04)
im = ax[1].imshow(mean_microtime_red, cmap='inferno', vmin=0.0, vmax=4e-9)
ax[1].set_title('Mean micro time (red)')
fig.colorbar(im, ax=ax[1], fraction=0.046, pad=0.04)
fig.show()

#%%
# Select

#%%
# Comparison of mean micro time and IRF corrected lifetime

clsm_image.fill_pixels(tttr_data, channels=green_ch)  # green channels
mean_tau_green = clsm_image.get_mean_lifetime_image(
    tttr_data, minimum_number_of_photons=20, stack_frames=True,
    tttr_irf=tttr_irf_green)[0]

# The mean microtime in the pixels for stacked frames
clsm_image.fill_pixels(tttr_data, channels=red_ch)  # green channels
mean_tau_red = clsm_image.get_mean_lifetime_image(
    tttr_data, minimum_number_of_photons=20, stack_frames=True,
    tttr_irf=tttr_irf_red)[0]

fig, ax = plt.subplots(nrows=2, ncols=2, sharex=True, sharey=True)
ax[1, 0].set_title('Mean micro time (green)')
fig.colorbar(im, ax=ax[1, 0], fraction=0.046, pad=0.04)
im = ax[1, 1].imshow(mean_microtime_red, cmap='inferno', vmin=0.0, vmax=4e-9)
ax[1, 1].set_title('Mean micro time (red)')
im = ax[1, 0].imshow(mean_microtime_green, cmap='plasma', vmin=0.0, vmax=4e-9)
fig.colorbar(im, ax=ax[1, 1], fraction=0.046, pad=0.04)

im = ax[0, 0].imshow(mean_tau_green, cmap='plasma')
ax[0, 0].set_title('Intensity of integrated frames (green)')
fig.colorbar(im, ax=ax[0, 0], fraction=0.046, pad=0.04)
im = ax[0, 1].imshow(mean_tau_red, cmap='inferno')
ax[0, 1].set_title('Intensity of integrated frames (red)')

fig.colorbar(im, ax=ax[0, 1], fraction=0.046, pad=0.04)
fig.show()


#%%
# Intensity-based segmentation
#-------------------------------
# Next, we use the generated intensity image and use standard image segmentiation
# techniques like Median or Gaussian filtering, Li- or Otsu-based thresholding to 
# seperate the three regions of interest: cytoplasm, vesicles and nucleus.
# At the end these segmentations represent binary masks, which are filled with "1/TRUE" in
# the region of interest and "0/FALSE" outside.
# 6. Segmentation of the vesicles
# We start with the segmentation of the vesicles.
# Here, first a Gaussian smoothing with a size of 1 sigma is applied, 
# followed by thresholding using the method of Otsu.
# Finally, objects with a size smaller than 5 pixels are removed.

sigma_vesicles = 1  # radius for Gaussian smoothing
min_vesicle_size = 5  # give minimal vesicle size in pixel

binary_img_vesicles = []
for image in range(int_all_channel.shape[0]):
    input_img = int_all_channel[image,:,:]
    # A. Gaussian filtering
    gaussian_img = ski.filters.gaussian(input_img, sigma=sigma_vesicles)
    thresh_vesicles = ski.filters.threshold_otsu(gaussian_img)  # B. Determine Otsu threshold for each frame
    binary_temp = gaussian_img >= thresh_vesicles  # C. Intensities larger than threshold = TRUE /1
    # D. Remove small objects
    cleaned_image = ski.morphology.remove_small_objects(binary_temp, min_vesicle_size)
    binary_img_vesicles.append(cleaned_image)

vesicle_mask = (np.array(binary_img_vesicles, dtype=np.ubyte))

#%%
# 7. Segmentation of the cytoplasm
# Next, the cytoplasm is segmented.
# Here, first a median filtering with a disk size of 3 pixel is applied.
# This is followed by thresholding using the method of Otsu.
# However, for later frames direct usage of the Otsu threshold selected too much of 
# background, thus we slightly increase the threshold by multiplying with 1.03 (Otsu_scaling_factor)
# In the final steps, first small holes inside the cytoplasm area are closed,
# followed by the removal of small detected areas outside the main cell.
# Here, a minimal size threshold of 10'000 pixel is applied.

disk_size_cyto = 3  # smoothing radius in pixel for median filtering
otsu_scaling_factor = 1.03  # increase Otsu threshold by multiplication with this factor
minimal_size = 10000  # give minimal cytoplasm size in pixel

binary_img_cytoplasm = []
for image in range(int_all_channel.shape[0]):
    input_img = int_all_channel[image,:,:]
    # A. Median filtering
    median_img = ski.filters.median(input_img, ski.morphology.disk(disk_size_cyto))
    thresh_cyto = ski.filters.threshold_otsu(median_img)  # B. Determine Otsu threshold for each frame
    binary_temp = median_img >= thresh_cyto*otsu_scaling_factor  # C. Intensities larger than threshold = TRUE /1
    # D. Fill holes in the cytoplasm
    bool_img = scipy.ndimage.binary_fill_holes(binary_temp)
    segmentation_filled = np.copy(bool_img.astype(np.uint8))
    # E. Remove small speckles outside the large cell
    small_aggregates_removed = ski.morphology.area_closing(ski.util.invert(segmentation_filled), area_threshold=minimal_size)
    final_segmentation = ski.util.invert(small_aggregates_removed)
    binary_img_cytoplasm.append(final_segmentation)

cytoplasm_mask = (np.array(binary_img_cytoplasm, dtype=np.ubyte))

#%%
# 8. Segmentation of nucleus
# Finally, the nucleus is segmented. 
# In contrast to the cytoplasm and vesicles, here only the green intensity image
# is used as input as here the contrast between cytoplasm/vesicles region and nucleus 
# seemed largest. Please note that the nucleus can be best seen as "region in cytoplasm
# but without vesicles". The segmentation of the nucleus thus proves to be a challenge.
# We first apply a Gaussian smoothing with a radius of 2 sigma, followed by thresholding 
# using the method of Li. The thresholded image is dilated, holes inside the nucleus filled 
# and small areas outside removed. Next all left-overs outside the cytoplasmic (i.e. body
# of the cell) are removed by multiplying with the generated cytoplasm mask, followed by
# another round of dilation to make the nucleus "smoother"
# Finally, as the nucleus is characterized by being outside the way of the diffusing 
# vesicles, we average the such obtained nucleus and generate an average nucleus by
# summing over all 400 frames and threshold this projection using the method of Otsu.

sigma_nucleus = 2  # radius for Gaussian filtering
dilation_radius = 2  # for dilation operation
minimal_hole_size = 1000  # minimal area size (smaller areas will be removed)
minimal_nucleus_size = 2000 # minimal nucleus size

binary_img_nucleus = []
for image in range(int_green.shape[0]):
    input_img = int_green[image,:,:]
    input_img_cytoplasm = cytoplasm_mask[image,:,:]
    # A. Gaussian img
    gaussian_img_nucleus = ski.filters.gaussian(input_img, sigma=sigma_nucleus)
    # B. Li-thresholding
    li_threshold = ski.filters.threshold_li(gaussian_img_nucleus)
    thresholded_image = gaussian_img_nucleus > li_threshold
    # C. Dilation
    dilated_segmentation = ski.morphology.dilation(thresholded_image, ski.morphology.disk(dilation_radius))
    # D. Fill holes
    bool_img = scipy.ndimage.binary_fill_holes(dilated_segmentation)
    segmentation_filled = np.copy(bool_img.astype(np.uint8))
    # E Remove small areas
    small_areas_removed = ski.morphology.area_closing(ski.util.invert(segmentation_filled), area_threshold=minimal_nucleus_size)
    # F. Combine with cytoplasm by multiplication of binary images
    combination = np.zeros(small_areas_removed.shape, dtype=np.uint)
    combination = input_img_cytoplasm * (small_areas_removed - 254)
    # G. Remove small areas at the cytoplasm border
    cleaned_combination = ski.morphology.area_closing(ski.util.invert(combination), area_threshold=minimal_nucleus_size)
    # H. dilate Nucleus to make more "smooth"
    dilated_combination = ski.morphology.dilation(ski.util.invert(cleaned_combination), ski.morphology.disk(dilation_radius))
    binary_img_nucleus.append(dilated_combination)

# I. Average over all frames to get a smoother nucleus
averaged_img_nucleus = np.sum(binary_img_nucleus, axis=0)
Otsu_thresh = ski.filters.threshold_otsu(averaged_img_nucleus)
thresholded_img = averaged_img_nucleus > Otsu_thresh
# J. Maybe required to removed smaller stuff
nucleus_mask_avg = ski.morphology.remove_small_objects(thresholded_img, min_size=minimal_nucleus_size)

#%%
# In the next two steps, we clean up the cytoplasm and vesicles masks.
# First, we remove the nucleus and vesicles from the cytoplasm, and second
# we remove the vesicles inside the nucleus region - if any- and the vesicles
# beloging to the neighboring cell from the vesicles mask.
# 9. Remove nucleus and vesicles from cytoplasm
# We invert the average nucleus mask and multiply with each frame, followed
# by inverting the vesicles mask and frame-wise multiplication.
# Remember that all masks are filled with 0 and 1, thus 0*0 or 0*1 = 0,
# only those regions, where both, in the cytoplasm mask and the inverted mask,
# the value 1 is given, stay 1.
inv_nucleus = ski.util.invert(nucleus_mask_avg)  # Invert nucleus_mask

cytoplasm_wo_nucleus_list = []  # Apply nucleus mask on cytoplasm
for image in range(cytoplasm_mask.shape[0]):
    input_img = cytoplasm_mask[image,:,:]
    temp_cyto = input_img * inv_nucleus
    cytoplasm_wo_nucleus_list.append(temp_cyto)

cytoplasm_wo_nucleus = (np.array(cytoplasm_wo_nucleus_list, dtype=np.ubyte))

inv_vesicles = ski.util.invert(vesicle_mask) - 254  # Invert vesicle_mask

cytoplasm_wo_nucleus_vesicles = []  # Apply vesicle mask on cytoplasm_wo_nucleus
for image in range(cytoplasm_mask.shape[0]):
    input_img = cytoplasm_wo_nucleus[image,:,:]
    input_img_inv_vesicle = inv_vesicles[image,:,:]
    temp_cyto2 = input_img * input_img_inv_vesicle
    cytoplasm_wo_nucleus_vesicles.append(temp_cyto2)

# Final cytoplasm mask:
final_cytoplasm = (np.array(cytoplasm_wo_nucleus_vesicles, dtype=np.ubyte))

#%%
# 10. Remove vesicles in nucleus regions and outside cell of interest
# Here, the same strategy as for the cleaned up cytoplasm is used.
# First vesicles inside the nucleus are removed, followed by those outside the 
# main cell by multiplying with inverted masks.
vesicles_wo_nucleus_list = []  # Apply nucleus mask
for image in range(vesicle_mask.shape[0]):
    input_img = vesicle_mask[image,:,:]
    temp_vesicle = input_img * inv_nucleus
    vesicles_wo_nucleus_list.append(temp_vesicle)

vesicles_wo_nucleus = (np.array(vesicles_wo_nucleus_list, dtype=np.ubyte))

vesicles_in_cytoplasm = []  # Apply cytoplasm mask on nucleus-free vesicles
for image in range(vesicle_mask.shape[0]):
    input_img_vesicle = vesicles_wo_nucleus[image,:,:]
    input_img_cytoplasm = cytoplasm_wo_nucleus[image,:,:]
    temp_vesicle_in_cyto = input_img_vesicle * input_img_cytoplasm
    vesicles_in_cytoplasm.append(temp_vesicle_in_cyto)

# Final vesicles mask:
final_vesicles = (np.array(vesicles_in_cytoplasm, dtype=np.ubyte))

#%%
# 11. Apply generated masks to reconstruct green intensity decays
# Now we used the generated masks for our three regions of interest and
# group the pixels belonging to each class. This procedure is applied to 
# every individual frame. Then, based on the micro time information 
# in each pixel the photon arrival time histogram can be reconstructed.
# However, first we ignore our masks and generate the mean arrival time 
# image summed over all frames.
# Next, the summed decays for all pixel and the three pixel classes are
# generated.

# Get the mean photon arrival time for all pixel with 3 or more photons
image_mean_micro_time = clsm_image.get_mean_micro_time_image(
    tttr_data,
    minimum_number_of_photons=3,
    stack_frames=True  # TRUE = all frames are summed up
)

# generate decays for unmasked pixel
n_frames, n_lines, n_pixel = clsm_image.shape
pseudo_mask = np.ones((n_frames, n_lines, n_pixel), dtype=np.uint8)
kw = {
    "tttr_data": tttr_data,
    "mask": pseudo_mask,  # Here a mask filled iwht all 1 is used
    "tac_coarsening": 8,  # binning of the micro times
    "stack_frames": True  # TRUE = all frames are summed up
}
decay_all = clsm_image.get_decay_of_pixels(**kw)
sum_decay_all = decay_all.sum(axis=0)

# Generate decay from masked pixels for cytoplasm, nucleus and vesicles
kw_cytoplasm = {
    "tttr_data": tttr_data,
    "mask": final_cytoplasm,  # cyotplasm mask
    "tac_coarsening": 8,
    "stack_frames": True
}
decay_cytoplasm = clsm_image.get_decay_of_pixels(**kw_cytoplasm)
sum_decay_cytoplasm = decay_cytoplasm.sum(axis=0)

kw_vesicles = {
    "tttr_data": tttr_data,
    "mask": final_vesicles,  # vesicles mask
    "tac_coarsening": 8,
    "stack_frames": True
}
decay_vesicles = clsm_image.get_decay_of_pixels(**kw_vesicles)
sum_decay_vesicles = decay_vesicles.sum(axis=0)

# Caution: for nucleus only single average image mask was generated
# "multiply" 400x to have a mask for each frame
nucleus_mask = np.empty((n_frames, n_lines, n_pixel), dtype=np.uint8)
for i in range(n_frames):
    nucleus_mask[i] = nucleus_mask_avg

kw_nucleus = {
    "tttr_data": tttr_data,
    "mask": nucleus_mask,  # nucleus mask
    "tac_coarsening": 8,
    "stack_frames": True
}
decay_nucleus = clsm_image.get_decay_of_pixels(**kw_nucleus)
sum_decay_nucleus = decay_nucleus.sum(axis=0)

#%%
# Apart from reconstructing pixel-classwise photon arrival time histograms, 
# several other characteristics can be calculated pixel-wise. The most commonly 
# used FRET indicators are e.g. the mean arrival time in green (tau_green), 
# green-to-red signal (sg_sr) or the proximity ratio (in PIE experiments).
# TODO: Scatterplot
# 12. Generate Scatterplots (TODO)

# 13. Calculate e(t)?

#%%
# 14. Plot results
# In the first row the intensity sum of the green and red channels is shown,
# next to the mean fluroescence lifetime in the green channels.
# The vesicles show clearly a smeared out shape, indicative for their diffusion 
# through the cytoplasm, thus using a frame-wise analysis and pixel-selection routine 
# is strongly advised.
# In the middle row exemplary the masks for cytoplasm and vesicle for the first frame,
# and the average nucleus mask are shown.
# The last row shows possible data to export based on the generated pixel classes:
# The reconstructed photon arrival time histograms....
fig, ax = plt.subplots(3, 3, sharex=False, sharey=False)
im = ax[0, 0].imshow(SUM_green, cmap='plasma')
ax[0, 0].set_title('Intensity of integrated frames (green)')
fig.colorbar(im, ax=ax[0, 0])
im = ax[0, 1].imshow(SUM_red, cmap='plasma')
ax[0, 1].set_title('Intensity of integrated frames (red)')
fig.colorbar(im, ax=ax[0, 1])
im = ax[0, 2].imshow(SUM_red)
ax[0, 2].set_title('Mean green fluorescence lifetime')
fig.colorbar(im, ax=ax[0, 2])

im = ax[1, 0].imshow(final_cytoplasm[0])
ax[1, 0].set_title('Cytoplasm mask (frame 0)')
im = ax[1, 1].imshow(final_vesicles[0])
ax[1, 1].set_title('Vesicle mask (frame 0)')
im = ax[1, 2].imshow(nucleus_mask_avg)
ax[1, 2].set_title('Nucleus mask (avg over all frames)')

im = ax[2, 0].semilogy(sum_decay_all, label='all pixel')
im = ax[2, 0].semilogy(sum_decay_nucleus, label='Nucleus')
im = ax[2, 0].semilogy(sum_decay_cytoplasm, label='Cytoplasm')
im = ax[2, 0].semilogy(sum_decay_vesicles, label='Vesicles')
ax[2, 0].legend()
ax[2, 0].set(xlabel='TCSPC bin', ylabel='Counts',
       title='Donor fluorescence intensity decay')

plt.show()

#%%
# To conclude, applying classical image segmentation tools
# on fluorescence-lifetime resolved fluorescence spectroscopy data 
# can help understanding cellular processes.