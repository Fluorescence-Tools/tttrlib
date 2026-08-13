"""One photon stream, one file, and live analysis — in a single pass.

A live measurement usually has to be *both* recorded and looked at. Reading the
file back to analyse it costs a second pass over data that may be larger than
RAM, and cannot show anything until the run ends.

`PhotonStreamHub` fans one stream out to every consumer. The file writer is a
consumer like any other, which is what makes this one pass instead of two.

    source ─▶ PhotonStreamHub ─┬─▶ RecordStreamWriter / PtoPhotonStream  (file)
                               ├─▶ your correlator
                               └─▶ your burst search

The hub gives no consumer its own queue, deliberately. Buffering belongs to
whoever needs it, and only the file writer is waiting on a disk — a correlator
is arithmetic on the events and is faster without a thread hop. A slow consumer
therefore slows the source, which is the honest failure: the alternative is
discarding events for whoever cannot keep up, and missing photons are something
no later stage can detect.
"""
import glob
import os
import tempfile

import numpy as np

import tttrlib

PQ_HT3 = 1


class CountRate(tttrlib.PhotonSink):
    """A live consumer, written in Python. Counts photons per channel and per
    time bin — the cheapest thing a live view needs."""

    def __init__(self, bin_width_macro=1_000_000):
        super().__init__()
        self.bin_width = bin_width_macro
        self.per_channel = {}
        self.bins = {}
        self.n = 0

    def submit(self, macro, micro, chan, etype, n):
        # `chan` and the rest are borrowed for this call only; anything kept
        # must be copied. Here only counts are kept.
        photons = etype == 0
        self.n += int(photons.sum())
        for c in np.unique(chan[photons]):
            self.per_channel[int(c)] = (self.per_channel.get(int(c), 0)
                                        + int((chan[photons] == c).sum()))
        for b in np.unique(macro[photons] // self.bin_width):
            self.bins[int(b)] = (self.bins.get(int(b), 0)
                                 + int((macro[photons] // self.bin_width == b).sum()))
        return True

    def sink_name(self):
        return "count rate"


class MicroTimeHistogram(tttrlib.PhotonSink):
    """A second consumer on the same photons: the TCSPC decay, accumulating."""

    def __init__(self, n_bins=4096):
        super().__init__()
        self.hist = np.zeros(n_bins, dtype=np.int64)

    def set_header(self, header):
        # Where a consumer picks up the clocks. Called once, before any events.
        self.micro_resolution = header.micro_time_resolution

    def submit(self, macro, micro, chan, etype, n):
        photons = etype == 0
        self.hist += np.bincount(micro[photons], minlength=len(self.hist))[:len(self.hist)]
        return True

    def sink_name(self):
        return "decay"


def main():
    hits = sorted(glob.glob("tttr-data/**/*.ht3", recursive=True))
    if not hits:
        raise SystemExit("no test data; run test/download_test_data.py first")
    src = tttrlib.TTTR(hits[0])
    n = min(500_000, len(src))
    macro = np.asarray(src.macro_times)[:n].copy()
    micro = np.asarray(src.micro_times)[:n].copy()
    chan = np.asarray(src.routing_channels)[:n].copy()
    etype = np.asarray(src.event_types)[:n].copy()

    tmp = tempfile.mkdtemp()
    path = os.path.join(tmp, "acquisition.ht3")

    hub = tttrlib.PhotonStreamHub()

    writer = tttrlib.RecordStreamWriter(PQ_HT3)
    if not writer.create(path, src.header, "run"):
        raise SystemExit(f"cannot open {path}: {writer.error()}")
    writer.set_auto_checkpoint(100_000)   # how much a crash may cost
    hub.add_sink(writer)

    rate = CountRate()
    decay = MicroTimeHistogram()
    hub.add_sink(rate)
    hub.add_sink(decay)

    hub.set_header(src.header)
    print(f"{hub.n_sinks()} consumers attached; streaming {n:,} events\n")

    for i in range(0, n, 25_000):
        s = slice(i, i + 25_000)
        if not hub.submit_events(macro[s], micro[s], chan[s], etype[s]):
            # A broken consumer does not stop the others -- the hub says which.
            print("  a consumer failed:", hub.error(), list(hub.failed_sinks()))
    hub.flush()
    writer.close()

    back = tttrlib.TTTR(path)
    print(f"file        : {len(back):,} events in {os.path.getsize(path) / 1e6:.1f} MB")
    print(f"count rate  : {rate.n:,} photons, "
          f"channels {sorted(rate.per_channel)}, {len(rate.bins)} time bins")
    print(f"decay       : {int(decay.hist.sum()):,} photons, "
          f"peak at micro-time bin {int(decay.hist.argmax())}")
    print(f"\nhub saw {hub.n_events():,} events; every consumer saw the same ones,")
    print("and the file was written in the same pass.")


if __name__ == "__main__":
    main()
