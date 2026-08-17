#!/usr/bin/env python
"""Record the vendor decode of the BrightEyes-TTM sample for
``TestBrightEyesTtrAgainstLibttp`` (test_ab_core_reference.py).

Runs under a venv with ``libttp`` (``benchmarks/.venvs/vicidomini``, where it
was pip-installed; ``libttp/ttpCython.timeProcessNewProtocol`` is the
Vicidomini lab's own Cython record parser). Only the first ``N_WORDS`` 16-bit
words are parsed (the full 82 MB file pivots to a 10 GB frame in libttp).
Stored: per photon (record index, channel, TDC code, laser valid, laser code),
the record step counters (libttp's default 16-bit step, ``force_16bit_step``),
and the pixel / line / frame enable bits per record.

    KMP_DUPLICATE_LIB_OK=TRUE benchmarks/.venvs/vicidomini/bin/python \\
        test/python/tttr/gen_ab_brighteyes_libttp_reference.py
"""
import os
from importlib.metadata import version

import numpy as np
import libttp.ttpCython as ttp

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "..", "tttr-data", "brighteyes",
                   "FLIM_80MHz_512x512pixel_120FOV_pixeldwelltime200us.ttr")
OUT = os.path.join(HERE, "..", "..", "data", "reference", "brighteyes_libttp_reference.npz")
CHANNELS, N_WORDS = 25, 4_000_000


def main():
    d = np.asarray(ttp.timeProcessNewProtocol(SRC, CHANNELS=CHANNELS, file_offset=0, file_last=N_WORDS))
    n_rec, W = d.shape[0], CHANNELS + 5
    valid, t = d[:, W:2 * W], d[:, 2 * W:]
    ph = valid[:, :CHANNELS] > 0
    rec, chan = np.nonzero(ph)
    A, B, C = t[:, CHANNELS + 2] & 0x7F, t[:, CHANNELS + 3] & 0x7F, t[:, CHANNELS + 4] & 0x7F
    step16 = ((C & 3).astype(np.int64) << 14) | (B.astype(np.int64) << 7) | A.astype(np.int64)
    wraps = np.concatenate([[0], np.cumsum(np.diff(step16) < 0)])
    cum = step16 + (wraps << 16)
    np.savez_compressed(
        OUT, libttp_version=np.array(version("libttp")), n_records=np.array(n_rec), n_words=np.array(N_WORDS),
        photon_record=rec.astype(np.int64), photon_channel=chan.astype(np.int16),
        photon_code=t[rec, chan].astype(np.int16),
        photon_laser_valid=(valid[rec, CHANNELS + 1] > 0), photon_laser_code=t[rec, CHANNELS + 1].astype(np.int16),
        record_step=cum, pixel_enable=(t[:, CHANNELS + 2] >> 7).astype(np.int8),
        line_enable=(t[:, CHANNELS + 3] >> 7).astype(np.int8), frame_enable=(t[:, CHANNELS + 4] >> 7).astype(np.int8))
    print(n_rec, "records,", rec.size, "photons; wrote", OUT)


if __name__ == "__main__":
    main()
