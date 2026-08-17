#!/usr/bin/env python
"""ptufile benchmarks (isolated 'read' venv).

ptufile (Christoph Gohlke) is the reference pure-Python/Cython PTU reader that
FLIMKit and many others depend on. Compared against tttrlib.TTTR reading and
tttrlib.CLSMImage construction on the identical PTU files.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from common import bench, timeit, record   # noqa: E402

from ptufile import PtuFile   # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
F_SM = os.path.join(REPO, "tttr-data", "pq", "ptu", "pq_ptu_hh_t3.ptu")
F_IMG = os.path.join(REPO, "tttr-data", "imaging", "pq", "Microtime200_TH260", "beads.ptu")
F_PHT3 = os.path.join(REPO, "tttr-data", "imaging", "pq", "PicoHarp_SymPhoTime", "Example_PTU_PicoHarp.ptu")
SHARED = os.path.join(REPO, "benchmarks", "results", "shared", "reading")
ENV = "venv-read"


def bench_read():
    with PtuFile(F_SM) as ptu:
        n = int(ptu.number_photons)

    def run():
        with PtuFile(F_SM) as ptu:
            return ptu.decode_records()
    bench("file_read", "ptufile", "read PTU T3 (HydraHarp)", run,
          repeat=5, n_items=n, unit="photons", dataset="pq_ptu_hh_t3.ptu", env=ENV)


def bench_read_picoharp():
    with PtuFile(F_PHT3) as ptu:
        r = ptu.decode_records()
    ph = (r["channel"] >= 0) & (r["marker"] == 0)
    os.makedirs(SHARED, exist_ok=True)
    np.savez(os.path.join(SHARED, "ptufile_pht3_outputs.npz"), time=r["time"], dtime=r["dtime"],
             channel=r["channel"], marker=r["marker"])

    def run():
        with PtuFile(F_PHT3) as ptu:
            return ptu.decode_records()
    bench("file_read", "ptufile (PicoHarp T3)", "read PTU T3 (PicoHarp)", run,
          repeat=5, n_items=int(ph.sum()), unit="photons", dataset="Example_PTU_PicoHarp.ptu", env=ENV)


def bench_clsm_intensity():
    with PtuFile(F_IMG) as ptu:
        print("   [ptufile] shape", ptu.shape, "dims", ptu.dims)
        img = ptu.decode_image(dtime=-1, channel=-1, dtype="uint16")  # integrate H+channels
        npix = int(np.prod(img.shape))

    def run():
        with PtuFile(F_IMG) as ptu:
            return ptu.decode_image(dtime=-1, channel=-1, dtype="uint16")
    bench("clsm_intensity", "ptufile", "PTU -> intensity image", run,
          repeat=5, n_items=npix, unit="pixels", dataset="beads.ptu (TH260)",
          env=ENV, extra={"image_shape": list(img.shape)})


if __name__ == "__main__":
    print("=" * 70)
    print("ptufile benchmarks (%s)" % ENV)
    print("=" * 70)
    for fn in [bench_read, bench_read_picoharp, bench_clsm_intensity]:
        try:
            fn()
        except Exception as e:
            import traceback
            print("!! %s failed: %s: %s" % (fn.__name__, type(e).__name__, e))
            traceback.print_exc()
