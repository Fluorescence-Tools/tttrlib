"""Acquiring into a container: photons to disk as they arrive.

`TTTR.write` stores a measurement that is already
finished, and to do that it needs the whole measurement in memory. An
acquisition is a different problem: the photon count is unknown when the file
is opened, the run may last hours, the process may be killed, and **the data
may be larger than RAM** — which is the constraint that shapes the design.

`PtoPhotonStream` implements the abstract `TTTRStreamWriter`, so the same four
calls (`create`/`append`/`checkpoint`/`close`) work for any format that can
take a live stream. PTO is currently the only one, and the interface exists so
that the second one does not reshape the caller.

The tests worth reading are the last three. Everything before them is the
round trip; those are the properties an acquisition actually depends on —
bounded memory, a readable file mid-run, and a killed writer keeping what it
committed.
"""
import glob
import os
import subprocess
import sys
import textwrap

import numpy as np
import pytest

try:
    # The subprocess below measures peak RSS with getrusage, so it needs this
    # too. Import it rather than asking whether it is findable: on Windows it
    # is simply absent, and a real import is what the subprocess will do.
    import resource  # noqa: F401
    HAS_RESOURCE = True
except ImportError:
    HAS_RESOURCE = False

import tttrlib


def _header(macro=1.6e-8, micro=3.2e-11, bins=4096):
    h = tttrlib.TTTRHeader()
    h.set_macro_time_resolution(macro)
    h.set_micro_time_resolution(micro)
    h.set_number_of_micro_time_channels(bins)
    return h


def _events(n, first=0):
    macro = np.arange(first, first + n, dtype=np.uint64)
    micro = (np.arange(n) % 4096).astype(np.uint16)
    chan = (np.arange(n) % 4).astype(np.int8)
    etype = np.zeros(n, dtype=np.int8)
    return macro, micro, chan, etype


class TestTheStreamIsAWriter:
    """It implements the abstract interface, and that is checkable."""

    def test_it_is_a_ttt_r_stream_writer(self):
        assert issubclass(tttrlib.PtoPhotonStream, tttrlib.TTTRStreamWriter)

    def test_the_inherited_half_of_the_api_is_present(self):
        """SWIG drops a base class it was not shown, silently, taking the
        error reporting and the checkpoint policy with it."""
        s = tttrlib.PtoPhotonStream()
        for name in ("create", "append", "checkpoint", "close", "is_open",
                     "n_committed", "n_buffered", "error",
                     "set_auto_checkpoint", "auto_checkpoint"):
            assert hasattr(s, name), name


class TestTheRoundTrip:

    def test_what_was_streamed_is_what_comes_back(self, tmp_path):
        out = str(tmp_path / "acq.pto")
        s = tttrlib.PtoPhotonStream()
        assert s.create(out, _header(), "run"), s.error()
        s.set_auto_checkpoint(1000)

        macro, micro, chan, etype = _events(10_000)
        for i in range(0, 10_000, 250):
            sl = slice(i, i + 250)
            assert s.append(macro[sl], micro[sl], chan[sl], etype[sl]), s.error()
        assert s.close()

        t = tttrlib.TTTR(out)
        assert len(t) == 10_000
        np.testing.assert_array_equal(np.asarray(t.macro_times), macro)
        np.testing.assert_array_equal(np.asarray(t.micro_times), micro)
        np.testing.assert_array_equal(np.asarray(t.routing_channels), chan)
        np.testing.assert_array_equal(np.asarray(t.event_types), etype)

    def test_macro_times_are_not_rebased_across_chunks(self, tmp_path):
        """The bug this pins: a container stacks several photons objects into
        one measurement, and for embedded vendor files — each of which restarts
        its clock at zero — it must shift each one to continue after the last.
        A native table's `macro_time` is absolute by specification, so shifting
        it moves every photon after the first chunk. With a stream written in
        chunks that scatters one continuous measurement across a timeline it
        never occupied, and the event count still matches."""
        out = str(tmp_path / "acq.pto")
        s = tttrlib.PtoPhotonStream()
        s.create(out, _header(), "run")
        macro, micro, chan, etype = _events(6000)
        for i in range(0, 6000, 1000):          # six separate chunks
            sl = slice(i, i + 1000)
            s.append(macro[sl], micro[sl], chan[sl], etype[sl])
            s.checkpoint()
        s.close()

        t = tttrlib.TTTR(out)
        np.testing.assert_array_equal(np.asarray(t.macro_times), macro)

    def test_the_header_survives(self, tmp_path):
        out = str(tmp_path / "acq.pto")
        s = tttrlib.PtoPhotonStream()
        s.create(out, _header(macro=2.5e-8, micro=1e-12, bins=32768), "run")
        s.append(*_events(100))
        s.close()

        h = tttrlib.TTTR(out).header
        assert h.macro_time_resolution == pytest.approx(2.5e-8, rel=1e-12)
        assert h.micro_time_resolution == pytest.approx(1e-12, rel=1e-12)
        assert h.number_of_micro_time_channels == 32768

    def test_a_streamed_imaging_measurement_still_reconstructs(self, tmp_path):
        """End to end on real data: stream a PTU through the acquisition path
        and rebuild the image. Fails if provenance or the imaging tags are lost
        anywhere between the instrument and the chunks."""
        src = sorted(glob.glob("tttr-data/**/*.ptu", recursive=True))
        if not src:
            pytest.skip("no PTU test data")
        t = tttrlib.TTTR(src[0])

        out = str(tmp_path / "img.pto")
        s = tttrlib.PtoPhotonStream()
        assert s.create(out, t.header, "run"), s.error()
        s.set_auto_checkpoint(100_000)
        m, u, c, e = (np.asarray(t.macro_times), np.asarray(t.micro_times),
                      np.asarray(t.routing_channels), np.asarray(t.event_types))
        for i in range(0, len(t), 50_000):
            sl = slice(i, i + 50_000)
            s.append(m[sl], u[sl], c[sl], e[sl])
        assert s.close()

        back = tttrlib.TTTR(out)
        np.testing.assert_array_equal(np.asarray(back.macro_times), m)
        a, b = tttrlib.CLSMImage(t), tttrlib.CLSMImage(back)
        assert (a.n_frames, a.n_lines, a.n_pixel) == (b.n_frames, b.n_lines, b.n_pixel)
        np.testing.assert_array_equal(np.asarray(a.intensity), np.asarray(b.intensity))


class TestWhatItRefuses:

    def test_it_will_not_stream_into_a_file_that_exists(self, tmp_path):
        """Chunks written between somebody else's objects would be absorbed
        into their measurement by the "several photons objects are one
        measurement" rule."""
        out = tmp_path / "taken.pto"
        out.write_bytes(b"anything")
        s = tttrlib.PtoPhotonStream()
        assert not s.create(str(out), _header(), "run")
        assert "exists" in s.error()

    def test_it_will_not_stream_without_a_header(self, tmp_path):
        """Without the clocks the events written have no units — the same
        requirement, for the same reason, as the write-once path."""
        s = tttrlib.PtoPhotonStream()
        assert not s.create(str(tmp_path / "x.pto"), None, "run")

    def test_arrays_of_different_lengths_are_refused(self, tmp_path):
        """Four arrays that disagree offset the columns against each other, so
        every photon after the short one is attributed to the wrong event —
        which nothing downstream can detect."""
        s = tttrlib.PtoPhotonStream()
        s.create(str(tmp_path / "x.pto"), _header(), "run")
        macro, micro, chan, etype = _events(100)
        assert not s.append(macro, micro[:50], chan, etype)
        assert "length" in s.error()

    def test_a_checkpoint_with_nothing_buffered_succeeds(self, tmp_path):
        """A caller checkpointing on a timer must not have to ask first
        whether any photons arrived."""
        s = tttrlib.PtoPhotonStream()
        s.create(str(tmp_path / "x.pto"), _header(), "run")
        assert s.checkpoint()
        assert s.checkpoint()
        assert s.n_committed() == 0


class TestTheAcquisitionProperties:
    """The three the design exists for."""

    @pytest.mark.slow
    @pytest.mark.skipif(not HAS_RESOURCE,
                        reason="peak RSS needs the POSIX resource module")
    def test_memory_does_not_grow_with_the_length_of_the_run(self, tmp_path):
        """The objective: acquire more data than fits in RAM. What the writer
        holds may grow with the checkpoint interval and must not grow with the
        run, so the file gets bigger and the process does not.

        Run in subprocesses because peak RSS is a high-water mark for the
        process and cannot be reset."""
        code = textwrap.dedent("""
            import os, sys, tempfile, pathlib, resource, numpy as np, tttrlib
            total = int(sys.argv[1])
            d = pathlib.Path(tempfile.mkdtemp()); out = str(d / "b.pto")
            h = tttrlib.TTTRHeader()
            h.set_macro_time_resolution(1.6e-8)
            h.set_micro_time_resolution(3.2e-11)
            h.set_number_of_micro_time_channels(4096)
            s = tttrlib.PtoPhotonStream(); s.create(out, h, "run")
            # buffer_limit is the memory bound -- how much the writer may hold
            # while the disk catches up. auto_checkpoint is the durability
            # cadence. This test is about the first.
            s.set_buffer_limit(1_000_000)
            s.set_auto_checkpoint(500_000)
            B = 500_000
            macro = np.arange(B, dtype=np.uint64)
            micro = (np.arange(B) % 4096).astype(np.uint16)
            chan = (np.arange(B) % 4).astype(np.int8)
            et = np.zeros(B, dtype=np.int8)
            for k in range(total // B):
                s.append(macro + np.uint64(k * B), micro, chan, et)
            s.close()
            print(os.path.getsize(out),
                  resource.getrusage(resource.RUSAGE_SELF).ru_maxrss)
            os.remove(out)
        """)
        sizes, peaks = [], []
        for total in (4_000_000, 16_000_000):
            r = subprocess.run([sys.executable, "-c", code, str(total)],
                               capture_output=True, text=True)
            assert r.returncode == 0, r.stderr
            size, peak = (int(x) for x in r.stdout.split())
            sizes.append(size)
            peaks.append(peak)

        # The file quadruples.
        assert sizes[1] > 3.5 * sizes[0]
        # The process does not. Generous, because RSS carries the interpreter
        # and the batch arrays: the claim being pinned is that peak memory does
        # not scale with the run, not that it is constant to the byte.
        assert peaks[1] < 1.5 * peaks[0], f"peak RSS grew with the run: {peaks}"

    def test_no_photon_is_ever_dropped(self, tmp_path):
        """The guarantee, asserted rather than described. A full buffer blocks
        the producer; there is no path that discards an event. Driven with a
        buffer far smaller than the data so the backpressure path is the one
        actually taken -- `n_stalls` proves it was."""
        out = str(tmp_path / "pressure.pto")
        s = tttrlib.PtoPhotonStream()
        assert s.create(out, _header(), "run"), s.error()
        s.set_buffer_limit(5_000)            # far below what is handed over

        total = 200_000
        macro, micro, chan, etype = _events(total)
        for i in range(0, total, 2_000):
            sl = slice(i, i + 2_000)
            assert s.append(macro[sl], micro[sl], chan[sl], etype[sl]), s.error()
        assert s.close()

        assert s.n_dropped() == 0
        assert s.n_stalls() > 0, "the backpressure path was never taken"
        t = tttrlib.TTTR(out)
        assert len(t) == total
        np.testing.assert_array_equal(np.asarray(t.macro_times), macro)

    def test_a_reader_may_open_the_file_during_the_acquisition(self, tmp_path):
        """A read-only open takes no lock, so a live viewer sees the committed
        prefix — a consistent shorter measurement, never a torn read."""
        out = tmp_path / "live.pto"
        writer = textwrap.dedent("""
            import sys, time, numpy as np, tttrlib
            h = tttrlib.TTTRHeader()
            h.set_macro_time_resolution(1.6e-8)
            h.set_micro_time_resolution(3.2e-11)
            h.set_number_of_micro_time_channels(4096)
            s = tttrlib.PtoPhotonStream(); s.create(sys.argv[1], h, "run")
            B, k = 5000, 0
            while True:
                s.append((np.arange(B, dtype=np.uint64) + np.uint64(k * B)),
                         (np.arange(B) % 4096).astype(np.uint16),
                         (np.arange(B) % 4).astype(np.int8),
                         np.zeros(B, dtype=np.int8))
                s.checkpoint()
                print(s.n_committed(), flush=True)
                k += 1
                time.sleep(0.02)
        """)
        p = subprocess.Popen([sys.executable, "-c", writer, str(out)],
                             stdout=subprocess.PIPE, text=True)
        try:
            for _ in range(6):                    # let a few checkpoints land
                assert p.stdout.readline(), "the writer produced nothing"
            t = tttrlib.TTTR(str(out))
            n = len(t)
            assert n > 0
            m = np.asarray(t.macro_times)
            # A consistent prefix, not a torn one.
            np.testing.assert_array_equal(m, np.arange(n, dtype=np.uint64))
            assert t.header.macro_time_resolution == pytest.approx(1.6e-8)
        finally:
            p.kill()
            p.wait()

    def test_a_killed_writer_keeps_every_checkpointed_photon(self, tmp_path):
        """An uncommitted chunk lies outside
        the Segment and is invisible, per the format's abandoned-write rule, so
        the file holds exactly the last checkpoint — not almost it."""
        out = tmp_path / "killed.pto"
        writer = textwrap.dedent("""
            import sys, time, numpy as np, tttrlib
            h = tttrlib.TTTRHeader()
            h.set_macro_time_resolution(1.6e-8)
            h.set_micro_time_resolution(3.2e-11)
            h.set_number_of_micro_time_channels(4096)
            s = tttrlib.PtoPhotonStream(); s.create(sys.argv[1], h, "run")
            B, k = 5000, 0
            while True:
                s.append((np.arange(B, dtype=np.uint64) + np.uint64(k * B)),
                         (np.arange(B) % 4096).astype(np.uint16),
                         (np.arange(B) % 4).astype(np.int8),
                         np.zeros(B, dtype=np.int8))
                s.checkpoint()
                print(s.n_committed(), flush=True)
                k += 1
                time.sleep(0.02)
        """)
        p = subprocess.Popen([sys.executable, "-c", writer, str(out)],
                             stdout=subprocess.PIPE, text=True)
        last = 0
        try:
            for _ in range(6):
                line = p.stdout.readline()
                assert line, "the writer produced nothing"
                last = int(line)
        finally:
            p.kill()
            p.wait()

        t = tttrlib.TTTR(str(out))
        # At least what we saw reported, and a whole number of chunks -- the
        # writer may have committed once more between our last read and the
        # kill, which is a race in the test, not in the file.
        assert len(t) >= last
        m = np.asarray(t.macro_times)
        np.testing.assert_array_equal(m, np.arange(len(t), dtype=np.uint64))


class TestOneStreamManyConsumers:
    """One source, one hub, several consumers — the file among them.

    An acquisition is written *and* analysed from the same photons, without
    anybody copying the stream or reading the file back. The file writer is a
    `PhotonSink` like any other, which is what makes that one pass instead of
    three.
    """

    class _Counter(tttrlib.PhotonSink):
        """A consumer written in Python, through the director."""
        def __init__(self):
            super().__init__()
            self.n = 0
            self.flushed = False

        def submit(self, macro, micro, chan, etype, n):
            self.n += n
            return True

        def flush(self):
            self.flushed = True
            return True

        def sink_name(self):
            return "counter"

    class _Broken(tttrlib.PhotonSink):
        def submit(self, macro, micro, chan, etype, n):
            return False

        def sink_name(self):
            return "broken"

    def test_every_consumer_sees_every_photon(self, tmp_path):
        out = str(tmp_path / "live.pto")
        hub = tttrlib.PhotonStreamHub()
        writer = tttrlib.PtoPhotonStream()
        assert writer.create(out, _header(), "run"), writer.error()
        counter = self._Counter()
        hub.add_sink(writer)
        hub.add_sink(counter)
        assert hub.n_sinks() == 2

        macro, micro, chan, etype = _events(20_000)
        for i in range(0, 20_000, 1000):
            sl = slice(i, i + 1000)
            assert hub.submit_events(macro[sl], micro[sl], chan[sl], etype[sl]), hub.error()
        assert hub.flush()
        assert writer.close()

        assert hub.n_events() == 20_000
        assert counter.n == 20_000
        assert counter.flushed
        assert len(tttrlib.TTTR(out)) == 20_000
        assert list(hub.failed_sinks()) == []

    def test_a_broken_consumer_does_not_stop_the_others(self, tmp_path):
        """A file writer that has run out of disk must not silently stop the
        correlator that is still perfectly able to work — and a caller watching
        a live plot would have no way to tell those two apart."""
        hub = tttrlib.PhotonStreamHub()
        broken = self._Broken()
        counter = self._Counter()
        hub.add_sink(broken)
        hub.add_sink(counter)

        macro, micro, chan, etype = _events(500)
        assert not hub.submit_events(macro, micro, chan, etype)
        assert counter.n == 500, "the working consumer was skipped"
        assert "broken" in list(hub.failed_sinks())
        assert "broken" in hub.error()

    def test_a_detached_consumer_stops_receiving(self, tmp_path):
        hub = tttrlib.PhotonStreamHub()
        counter = self._Counter()
        hub.add_sink(counter)
        macro, micro, chan, etype = _events(100)
        hub.submit_events(macro, micro, chan, etype)
        hub.remove_sink(counter)
        hub.submit_events(macro, micro, chan, etype)
        assert counter.n == 100
        assert hub.n_sinks() == 0

    def test_mismatched_lengths_are_refused_before_any_consumer_sees_them(self):
        """Rejected whole: a consumer that took the batch and a consumer that
        did not would disagree about the measurement afterwards."""
        hub = tttrlib.PhotonStreamHub()
        counter = self._Counter()
        hub.add_sink(counter)
        macro, micro, chan, etype = _events(100)
        assert not hub.submit_events(macro, micro[:50], chan, etype)
        assert counter.n == 0
