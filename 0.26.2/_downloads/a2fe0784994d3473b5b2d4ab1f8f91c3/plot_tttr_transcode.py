"""
================================
TTTR writing / Transcode
================================

Save the content of a PTU file in an SPC file

"""
#%%
import os
from pathlib import Path
import json
import tttrlib
import numpy as np
import pylab as plt

#%%
# Use TTTRLIB_DATA if set, otherwise fall back to repository layout
DATA_ROOT = Path(os.environ.get("TTTRLIB_DATA", ".")).resolve()
filename_ptu = str(DATA_ROOT / 'pq/ptu/pq_ptu_hh_t3.ptu')
tttr_ptu = tttrlib.TTTR(filename_ptu)

# Note: SPC has limited header support.
filename_spc_1 = "conv1.spc"
tttr_ptu.write(filename_spc_1)
