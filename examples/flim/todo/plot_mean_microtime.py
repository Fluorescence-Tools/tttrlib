#
# #%%
# # 5. Micro time imaging
# # ----------------------
# # Mean arrival time
# # ^^^^^^^^^^^^^^^^^
# # Use mean arrival time in each pixel. Visualzed decay for all
# # Plot of micro time histograms
# # Create new TTTR objects using only photons from green and red channels
# # Make histogram over micro times. Counts and time axis
# # Computes mean arrival time of photons
# # The mean microtime in the pixels for stacked frames
# # pixels that have less than the specified number of photons
# # are filled with negative values.
#
# minimum_number_of_photons_per_pixel=30
# clsm_image.fill(tttr_data, channels=green_ch)  # green channels
# mean_microtime_green = clsm_image.get_mean_micro_time(
#     tttr_data,
#     minimum_number_of_photons=minimum_number_of_photons_per_pixel,
#     stack_frames=True)[0]
#
# # The mean microtime in the pixels for stacked frames
# clsm_image.fill(tttr_data, channels=red_ch)  # green channels
# mean_microtime_red = clsm_image.get_mean_micro_time(
#     tttr_data,
#     minimum_number_of_photons=minimum_number_of_photons_per_pixel,
#     stack_frames=True)[0]
#
# fig, _ = plot_images(
#     [SUM_green],
#     titles=['Mean microtime (green)'],
#     cmaps=['viridis', 'inferno'],
#     nrows=1, ncols=2, sharex=True, sharey=True
# )
# fig.show()
#
# #%%
# # Lifetime analysis
# # ^^^^^^^^^^^^^^^^^
# fig, ax = plt.subplots(nrows=1, ncols=2, sharex=True, sharey=True)
# im = ax[0].imshow(mean_microtime_green, cmap='plasma', vmin=0.0)
# ax[0].set_title('Mean micro time (green)')
# fig.colorbar(im, ax=ax[0], fraction=0.046, pad=0.04)
# im = ax[1].imshow(mean_microtime_red, cmap='inferno', vmin=0.0)
# ax[1].set_title('Mean micro time (red)')
# fig.colorbar(im, ax=ax[1], fraction=0.046, pad=0.04)
# fig.show()
#
#
# #%%
# # Comparison of mean micro time and IRF corrected lifetime
#
# clsm_image.fill(tttr_data, channels=green_ch)  # green channels
# mean_tau_green = clsm_image.get_mean_lifetime(
#     tttr_data, minimum_number_of_photons=20, stack_frames=True,
#     tttr_irf=tttr_irf_green)[0]
#
# # The mean microtime in the pixels for stacked frames
# clsm_image.fill(tttr_data, channels=red_ch)  # green channels
# mean_tau_red = clsm_image.get_mean_lifetime(
#     tttr_data, minimum_number_of_photons=20, stack_frames=True,
#     tttr_irf=tttr_irf_red)[0]
#
# fig, ax = plt.subplots(nrows=2, ncols=2, sharex=True, sharey=True)
# ax[1, 0].set_title('Mean micro time (green)')
# fig.colorbar(im, ax=ax[1, 0], fraction=0.046, pad=0.04)
# im = ax[1, 1].imshow(mean_microtime_red, cmap='inferno', vmin=0.0, vmax=4e-9)
# ax[1, 1].set_title('Mean micro time (red)')
# im = ax[1, 0].imshow(mean_microtime_green, cmap='plasma', vmin=0.0, vmax=4e-9)
# fig.colorbar(im, ax=ax[1, 1], fraction=0.046, pad=0.04)
#
# im = ax[0, 0].imshow(mean_tau_green, cmap='plasma')
# ax[0, 0].set_title('Intensity of integrated frames (green)')
# fig.colorbar(im, ax=ax[0, 0], fraction=0.046, pad=0.04)
# im = ax[0, 1].imshow(mean_tau_red, cmap='inferno')
# ax[0, 1].set_title('Intensity of integrated frames (red)')
#
# fig.colorbar(im, ax=ax[0, 1], fraction=0.046, pad=0.04)
# fig.show()
#
