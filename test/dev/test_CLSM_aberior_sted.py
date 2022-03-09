import glob
import numpy as np
import tttrlib
import pylab as plt

fn_pattern = "c:/Science/development/tttr-data/imaging/arberior/16_CLR_overnight/18Nov+16Jan_merged/PQSpcm_*.ptu"
fns = glob.glob(fn_pattern)[:5]
fn = fns[0]
d = tttrlib.TTTR(fn)
print(d.header.get_json())

# issue with default:
# header.tag('ImgHdr_Frame') exists and contains value 2
# The default does 2**(ImgHdr_Frame-1) = 2 but marker_frame_start
# needs to be set to 4
clsm_settings = {
    "marker_frame_start": [4]
}
print(d.used_routing_channels)
plt.semilogy(d.microtime_histogram[0])
plt.show()

d = tttrlib.TTTR(fn)
img1 = tttrlib.CLSMImage(d, **clsm_settings)
img2 = tttrlib.CLSMImage(d, **clsm_settings)

green_ch = [0, 1]
red_ch = [2, 3]
total_ch = green_ch + red_ch
channels = green_ch
gate_range = 5000, 24000

img1.fill(channels=channels)
img2.fill(channels=channels, micro_time_ranges=[gate_range])

fig, axs = plt.subplots(nrows=2, ncols=2, sharex=False, sharey=False)
axs[0, 0].imshow(img1.intensity.sum(axis=0), vmin=3, interpolation=None)
axs[0, 1].imshow(img2.intensity.sum(axis=0), vmin=3, interpolation=None)
frc, _ = img1.frc
axs[1, 0].plot(frc, label="Intensity")
frc, _ = img2.frc
axs[1, 1].plot(frc, label="Intensity")
plt.show()

# Do for all files

frcs = list()
frcs_gated = list()
for i, fn in enumerate(fns):
    d = tttrlib.TTTR(fn)
    img1 = tttrlib.CLSMImage(d, **clsm_settings)
    img2 = tttrlib.CLSMImage(d, **clsm_settings)
    img1.fill(channels=channels)
    img2.fill(channels=channels, micro_time_ranges=[gate_range])
    frcs.append(img1.frc[0])
    frcs_gated.append(img2.frc[0])

frc_avg = np.mean(frcs, axis=0)
frc_gated_avg = np.mean(frcs_gated, axis=0)

plt.plot(frc_avg, label="frc_avg")
plt.plot(frc_gated_avg, label="frc_gated_avg")
plt.legend()
plt.show()

