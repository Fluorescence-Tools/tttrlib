# SPDX-License-Identifier: BSD-3-Clause
"""Tests for the Becker & Hickl SPC-QC reader and writer.

The SPC-QC modules write ".spc" files like the classic SPC-130/600 cards, but
with a record layout of their own (Becker & Hickl SPC_data_file_structure.h):

    bit  0-11  macro time (low 12 bit)
    bit 12-15  routing signal, or the marker type on marker records
    bit 16-27  micro time (12 bit ADC, *not* reversed)
    bit 31-28  event selector, and the input channel underneath it:
               QC-x04 uses bits 31-30 as the selector (00 photon, 10 macro time
               overflow, 01 marker, 11 GAP) with the channel in bits 29-28;
               QC-x06 has a three-bit channel and a four-bit selector.

The header word carries flags where the classic one has reserved bits, and only
22 bits of macro time clock -- in femtoseconds when the femto flag (bit 24) is
set. Bit 23 selects the QC-x06 record layout.

The synthetic tests below pin that layout down byte by byte, including the event
kinds the reference recording does not contain (markers, GAP, routing, QC-x06).
The reference-data tests check a real SPCM recording, whose decay curves,
intensity trace and acquisition time are known from the companion ".sdt".
"""
import os
import struct
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
import tttrlib
from test_settings import settings, DATA_AVAILABLE  # type: ignore

# Macro time clock of the reference module, in femtoseconds (2.048131 ns)
MT_CLOCK_FS = 2048131
MT_WRAP = 4096
OVERFLOW = 0x80000000

QC_SPC = settings.get("spcqc_filename", "")
QC_SET = settings.get("spcqc_set_filename", "")
QC_AVAILABLE = DATA_AVAILABLE and os.path.exists(QC_SPC)


def _record(macro_time_low, micro_time, channel, routing=0, event=0):
    """Assemble one SPC-QC record.

    ``event`` is the selector in bits 31-28; ``channel`` is placed underneath
    it, so a QC-x04 photon (selector 0b00) may use two channel bits and a
    QC-x06 photon (selector bit 31 clear) three.
    """
    return (
        (macro_time_low & 0xFFF)
        | ((routing & 0xF) << 12)
        | ((micro_time & 0xFFF) << 16)
        | (((event & 0xF) | (channel & 0xF)) << 28)
    )


def _marker_x04(macro_time_low, marker):
    """QC-x04 marker: selector 0b01, marker type in the routing field."""
    return (macro_time_low & 0xFFF) | ((marker & 0xF) << 12) | (0x1 << 30)


def _gap_x04(macro_time_low, micro_time, channel):
    """QC-x04 GAP: selector 0b11, otherwise a normal photon."""
    return _record(macro_time_low, micro_time, channel, event=0b1100)


# Header flag bits (Becker & Hickl SPC_data_file_structure.h)
HDR_INVALID = 1 << 31
HDR_RAW = 1 << 26
HDR_MARKERS = 1 << 25
HDR_FEMTO = 1 << 24
HDR_SIX_CHANNEL = 1 << 23


def _write_spc(path, records, macro_time_clock=MT_CLOCK_FS, flags=None,
               routing_bits=0):
    if flags is None:
        flags = HDR_INVALID | HDR_RAW | HDR_FEMTO
    header = (macro_time_clock & 0x3FFFFF) | flags | ((routing_bits & 0xF) << 27)
    with open(path, "wb") as fp:
        fp.write(struct.pack("<I", header))
        fp.write(np.asarray(records, dtype="<u4").tobytes())


class TestBHSPCQCSynthetic(unittest.TestCase):
    """Byte-level tests against a hand-assembled file."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def _path(self, name="qc.spc"):
        return os.path.join(self.tmp, name)

    def test_record_layout(self):
        """Macro time, micro time and channel are decoded from the right bits."""
        fn = self._path()
        _write_spc(fn, [
            _record(10, 2000, 0),
            _record(4095, 1, 1),
            OVERFLOW,
            _record(7, 4095, 2),
            OVERFLOW,
            OVERFLOW,
            _record(0, 0, 3),
        ])
        data = tttrlib.TTTR(fn)
        self.assertEqual(len(data), 4)
        np.testing.assert_array_equal(
            data.macro_times,
            [10, 4095, MT_WRAP + 7, 3 * MT_WRAP + 0],
        )
        np.testing.assert_array_equal(data.micro_times, [2000, 1, 4095, 0])
        # no router in use, so the channel is simply the module input
        np.testing.assert_array_equal(data.routing_channels, [0, 1, 2, 3])
        np.testing.assert_array_equal(data.event_types, [0, 0, 0, 0])

    def test_channel_is_the_module_input_when_no_router_is_used(self):
        """The common case: routing width 0, so the channel is the input."""
        fn = self._path()
        _write_spc(fn, [
            _record(0, 11, channel=0),
            _record(1, 22, channel=1),
            _record(2, 33, channel=2),
        ], routing_bits=0)
        data = tttrlib.TTTR(fn)
        np.testing.assert_array_equal(data.routing_channels, [0, 1, 2])

    def test_routing_and_channel_pack_losslessly(self):
        """With a router both dimensions survive, the input above the routing.

        Becker & Hickl themselves combine the two as "routing channel number,
        bits 6-4 = input channel for QC-x0x modules" (`MeasFCSInfo.chan`). The
        four routing bits are reserved there whether or not a router is
        attached; tttrlib moves the input channel down onto the routing width
        the header declares, so the numbering stays dense.
        """
        fn = self._path()
        _write_spc(fn, [
            _record(0, 11, channel=1, routing=0),
            _record(1, 22, channel=2, routing=1),
            _record(2, 33, channel=3, routing=2),
        ], routing_bits=2)
        data = tttrlib.TTTR(fn)
        np.testing.assert_array_equal(
            data.routing_channels, [1 << 2, (2 << 2) | 1, (3 << 2) | 2]
        )
        np.testing.assert_array_equal(data.micro_times, [11, 22, 33])

    def test_an_understated_routing_width_does_not_merge_detectors(self):
        """A header claiming fewer routing bits than the records use is ignored.

        Compacting onto too narrow a routing field would fold two detectors
        onto the same channel, so the full four bits are kept instead.
        """
        fn = self._path()
        _write_spc(fn, [
            _record(0, 11, channel=1, routing=0),
            _record(1, 22, channel=1, routing=8),  # needs 4 routing bits
        ], routing_bits=1)
        data = tttrlib.TTTR(fn)
        channels = data.routing_channels
        self.assertNotEqual(channels[0], channels[1])
        np.testing.assert_array_equal(channels, [1 << 4, (1 << 4) | 8])

    def test_markers_are_decoded_as_markers(self):
        """Selector 0b01 is a marker, not a photon on some high channel."""
        fn = self._path()
        _write_spc(fn, [
            _record(5, 100, 1),
            _marker_x04(9, 2),
            OVERFLOW,
            _marker_x04(3, 4),
            _record(4, 200, 2),
        ])
        data = tttrlib.TTTR(fn)
        np.testing.assert_array_equal(data.event_types, [0, 1, 1, 0])
        np.testing.assert_array_equal(
            data.macro_times, [5, 9, MT_WRAP + 3, MT_WRAP + 4]
        )
        # marker type rides in the routing field, as it does on SPC-130
        np.testing.assert_array_equal(data.routing_channels, [1, 2, 4, 2])

    def test_gap_records_are_photons(self):
        """Selector 0b11 marks a FIFO overrun but the photon itself is valid."""
        fn = self._path()
        _write_spc(fn, [
            _record(1, 111, 1),
            _gap_x04(2, 222, 2),
            _record(3, 333, 3),
        ])
        data = tttrlib.TTTR(fn)
        self.assertEqual(len(data), 3)
        np.testing.assert_array_equal(data.micro_times, [111, 222, 333])
        np.testing.assert_array_equal(data.routing_channels, [1, 2, 3])
        np.testing.assert_array_equal(data.macro_times, [1, 2, 3])

    def test_qc_x06_layout_widens_the_channel(self):
        """With the six-channel header flag the channel takes bit 30 as well."""
        fn = self._path("qc06.spc")
        _write_spc(
            fn,
            [
                _record(1, 10, channel=0b111),   # only reachable with 3 bits
                _record(2, 20, channel=0b100),
                OVERFLOW,
                (3 & 0xFFF) | (7 << 12) | (0xA << 28),  # QC-x06 marker
            ],
            flags=HDR_INVALID | HDR_RAW | HDR_FEMTO | HDR_SIX_CHANNEL,
        )
        data = tttrlib.TTTR(fn)
        # channels 7 and 4 need the third bit; the marker keeps its type (7)
        np.testing.assert_array_equal(data.routing_channels, [7, 4, 7])
        np.testing.assert_array_equal(data.event_types, [0, 0, 1])
        np.testing.assert_array_equal(data.macro_times, [1, 2, MT_WRAP + 3])

    def test_x04_channel_3_is_not_confused_with_a_marker(self):
        """Channel 3 fills both QC-x04 channel bits without touching bit 30."""
        fn = self._path()
        _write_spc(fn, [_record(0, 500, 3)])
        data = tttrlib.TTTR(fn)
        np.testing.assert_array_equal(data.event_types, [0])
        np.testing.assert_array_equal(data.routing_channels, [3])

    def test_clock_unit_follows_the_femto_flag(self):
        """Without the femto flag the clock is in the classic 0.1 ns units."""
        fn = self._path("coarse.spc")
        _write_spc(fn, [_record(0, 1, 0)],
                   macro_time_clock=500,
                   flags=HDR_INVALID | HDR_RAW)
        data = tttrlib.TTTR(fn)
        self.assertAlmostEqual(
            data.header.macro_time_resolution, 500 * 1e-10, places=16
        )

    def test_micro_times_are_not_reversed(self):
        """Unlike SPC-130, SPCM stores the micro time the way it histograms it."""
        fn = self._path()
        _write_spc(fn, [_record(0, 100, 0)])
        data = tttrlib.TTTR(fn)
        self.assertEqual(data.micro_times[0], 100)

    def test_container_is_auto_detected(self):
        fn = self._path()
        _write_spc(fn, [_record(1, 2, 0), OVERFLOW])
        data = tttrlib.TTTR(fn)
        self.assertEqual(data.get_tttr_container_type(), "SPC-QC")
        self.assertEqual(
            tttrlib.registry("file_container")["SPC-QC"]["container_type"],
            9,
        )

    def test_header_resolutions(self):
        """The macro time clock is read in fs; the micro time falls back to 16 ps."""
        fn = self._path()
        _write_spc(fn, [_record(1, 2, 0)])
        # keep the TTTR alive: the header does not own the object it came from
        data = tttrlib.TTTR(fn)
        header = data.header
        self.assertAlmostEqual(
            header.macro_time_resolution, MT_CLOCK_FS * 1e-15, places=18
        )
        # No .set sidecar next to the file -> default TAC range of 65.54 ns
        self.assertAlmostEqual(
            header.micro_time_resolution, 6.554e-8 / 4096, places=16
        )
        self.assertEqual(header.number_of_micro_time_channels, 4096)

    def test_classic_spc_is_not_claimed(self):
        """A classic SPC-130 file must not be taken for an SPC-QC file."""
        fn = self._path("classic.spc")
        # macro time clock 135 (13.5 ns in units of 0.1 ns) and an SPC-130
        # overflow record (invalid + mtov set)
        with open(fn, "wb") as fp:
            fp.write(struct.pack("<I", 135 | (1 << 31)))
            fp.write(np.asarray([0xC0000005, 0x00010001], dtype="<u4").tobytes())
        self.assertEqual(
            tttrlib.TTTR(fn).get_tttr_container_type(), "SPC-130"
        )

    def test_write_round_trip(self):
        """Writing and re-reading reproduces macro time, micro time and channel."""
        n = 2000
        rng = np.random.RandomState(0)
        macro_times = np.cumsum(rng.randint(1, 5000, n)).astype(np.uint64)
        micro_times = rng.randint(0, 4096, n).astype(np.uint16)
        channels = rng.randint(0, 4, n).astype(np.int8)
        event_types = np.zeros(n, dtype=np.int8)
        src = tttrlib.TTTR(macro_times, micro_times, channels, event_types)
        src.header.set_macro_time_resolution(MT_CLOCK_FS * 1e-15)

        fn = self._path("out.spc")
        self.assertTrue(src.write(fn, "SPC-QC"))

        back = tttrlib.TTTR(fn)
        self.assertEqual(back.get_tttr_container_type(), "SPC-QC")
        np.testing.assert_array_equal(back.macro_times, macro_times)
        np.testing.assert_array_equal(back.micro_times, micro_times)
        np.testing.assert_array_equal(back.routing_channels, channels)
        self.assertAlmostEqual(
            back.header.macro_time_resolution, MT_CLOCK_FS * 1e-15, places=18
        )

    def test_transcoded_channels_survive_up_to_the_record_width(self):
        """Data from another container has no routing width to inherit.

        The full four routing bits are assumed then, which is what keeps a
        6-bit PicoQuant channel intact: 4 routing bits plus the 2 input bits of
        a QC-x04 record hold 0..63. Anything wider than the record cannot be
        represented and clips.
        """
        n = 200
        rng = np.random.RandomState(1)
        macro_times = np.cumsum(rng.randint(1, 900, n)).astype(np.uint64)
        micro_times = rng.randint(0, 4096, n).astype(np.uint16)
        channels = rng.randint(0, 64, n).astype(np.int8)
        src = tttrlib.TTTR(
            macro_times, micro_times, channels, np.zeros(n, dtype=np.int8)
        )
        src.header.set_macro_time_resolution(MT_CLOCK_FS * 1e-15)

        fn = self._path("wide.spc")
        self.assertTrue(src.write(fn, "SPC-QC"))
        back = tttrlib.TTTR(fn)
        np.testing.assert_array_equal(back.routing_channels, channels)
        np.testing.assert_array_equal(back.macro_times, macro_times)
        np.testing.assert_array_equal(back.micro_times, micro_times)

    def test_markers_round_trip(self):
        """Markers must come back as markers, not as photons."""
        macro_times = np.array([10, 4200, 4300, 9000], dtype=np.uint64)
        micro_times = np.array([100, 0, 200, 0], dtype=np.uint16)
        channels = np.array([1, 2, 3, 4], dtype=np.int8)
        event_types = np.array([0, 1, 0, 1], dtype=np.int8)
        src = tttrlib.TTTR(macro_times, micro_times, channels, event_types)
        src.header.set_macro_time_resolution(MT_CLOCK_FS * 1e-15)

        fn = self._path("markers.spc")
        self.assertTrue(src.write(fn, "SPC-QC"))
        back = tttrlib.TTTR(fn)
        np.testing.assert_array_equal(back.event_types, event_types)
        np.testing.assert_array_equal(back.macro_times, macro_times)
        np.testing.assert_array_equal(back.routing_channels, channels)
        # micro times survive on the photons; markers have none by definition
        photons = event_types == 0
        np.testing.assert_array_equal(
            back.micro_times[photons], micro_times[photons]
        )


@unittest.skipUnless(QC_AVAILABLE, "SPC-QC reference data not available")
class TestBHSPCQCReferenceData(unittest.TestCase):
    """Tests against an SPCM recording of an SPC-QC-004 module.

    The file is the first 512k records of a FIFO measurement; the expectations
    below were cross-checked against the decay curves and the acquisition time
    SPCM wrote into the companion ".sdt".
    """

    # Photons per CFD channel in the trimmed reference file
    # Keyed by the decoded routing channel. No router in this measurement, so
    # the channel is the module input; SPCM records these as chan 0, 16, 32,
    # which is the same pair of fields with the four routing bits still in.
    COUNTS = {0: 7813, 1: 24547, 2: 284}

    # The first 32 points of the 1 ms intensity trace SPCM wrote into the
    # companion ".sdt" for each channel. Binning the decoded macro times must
    # reproduce these -- this is what validates the macro times per photon
    # rather than only in aggregate.
    TRACE_HEAD = {
        0: [3, 0, 1, 4, 4, 0, 1, 2, 1, 1, 2, 0, 1, 2, 3, 0,
            1, 2, 3, 2, 2, 2, 1, 1, 1, 3, 2, 0, 4, 2, 2, 2],
        1: [7, 2, 6, 5, 5, 9, 2, 7, 2, 4, 6, 5, 3, 3, 4, 3,
            1, 6, 3, 5, 2, 3, 6, 1, 3, 3, 2, 2, 5, 8, 2, 3],
        2: [0] * 32,
    }

    def setUp(self):
        self.data = tttrlib.TTTR(QC_SPC)

    def test_container_detection(self):
        self.assertEqual(self.data.get_tttr_container_type(), "SPC-QC")

    def test_photon_counts_per_channel(self):
        self.assertEqual(len(self.data), sum(self.COUNTS.values()))
        channels = self.data.routing_channels
        np.testing.assert_array_equal(
            np.unique(channels), sorted(self.COUNTS)
        )
        for channel, count in self.COUNTS.items():
            self.assertEqual(int((channels == channel).sum()), count)

    def test_macro_times_are_monotonic(self):
        macro_times = self.data.macro_times.astype(np.int64)
        self.assertTrue(np.all(np.diff(macro_times) >= 0))

    def test_macro_times_reproduce_the_spcm_intensity_trace(self):
        """Per-photon check: 1 ms bins of the decoded macro times vs SPCM.

        Monotonicity and total duration only constrain the macro times in
        aggregate -- a wrong overflow weight or a misplaced macro time field
        could still integrate to the right total. Binning against the intensity
        trace SPCM recorded alongside the decay pins every photon down to 1 ms.
        """
        times = self.data.macro_times * self.data.header.macro_time_resolution
        channels = self.data.routing_channels
        for channel, expected in self.TRACE_HEAD.items():
            counts = np.bincount(
                np.floor(times[channels == channel] / 1e-3).astype(np.int64),
                minlength=len(expected),
            )[: len(expected)]
            np.testing.assert_array_equal(
                counts, expected, err_msg=f"channel {channel}"
            )

    def test_micro_times_span_the_laser_period(self):
        """20 MHz excitation over a 16 ps TAC bin -> the decay ends near bin 3125."""
        micro_times = self.data.micro_times
        self.assertLess(micro_times.max(), 4096)
        self.assertGreater(micro_times.max(), 3000)
        # The decay starts at low bins: SPCM does not reverse the micro time
        counts = np.bincount(micro_times.astype(np.int64), minlength=4096)
        self.assertGreater(counts[:512].sum(), counts[2048:].sum())

    def test_resolutions_from_the_set_sidecar(self):
        header = self.data.header
        self.assertAlmostEqual(
            header.macro_time_resolution, MT_CLOCK_FS * 1e-15, places=18
        )
        # SP_TAC_R / SP_ADC_RE of the .set: 65.54 ns over 4096 channels
        self.assertAlmostEqual(
            header.micro_time_resolution, 6.554e-8 / 4096, places=16
        )

    def test_acquisition_time(self):
        """512k records at 2.048131 ns per macro time unit are ~4.3 s."""
        duration = self.data.macro_times[-1] * self.data.header.macro_time_resolution
        self.assertAlmostEqual(duration, 4.11, places=1)

    def test_written_records_are_byte_identical_to_spcm(self):
        """The writer must reproduce SPCM's own record stream, not merely a
        stream that reads back the same.

        Round-tripping through tttrlib cannot catch a writer that swaps two
        fields, because the reader undoes the swap symmetrically -- it only
        shows up against the bytes SPCM wrote. The tail is allowed to differ:
        SPCM keeps emitting overflow records after the last photon, which carry
        no information to preserve.
        """
        with tempfile.TemporaryDirectory() as tmp:
            fn = os.path.join(tmp, "written.spc")
            self.assertTrue(self.data.write(fn))
            written = np.fromfile(fn, dtype="<u4")
            original = np.fromfile(QC_SPC, dtype="<u4")
            self.assertLessEqual(len(written), len(original))
            np.testing.assert_array_equal(written, original[: len(written)])
            # whatever the original has past that point is padding overflows
            np.testing.assert_array_equal(
                np.unique(original[len(written):]), [OVERFLOW]
            )

    def test_round_trip_through_spc_qc(self):
        with tempfile.TemporaryDirectory() as tmp:
            fn = os.path.join(tmp, "round_trip.spc")
            self.assertTrue(self.data.write(fn))
            back = tttrlib.TTTR(fn)
            np.testing.assert_array_equal(back.macro_times, self.data.macro_times)
            np.testing.assert_array_equal(back.micro_times, self.data.micro_times)
            np.testing.assert_array_equal(
                back.routing_channels, self.data.routing_channels
            )

    def test_round_trip_through_ptu(self):
        """Transcoding to PTU preserves the decoded stream."""
        with tempfile.TemporaryDirectory() as tmp:
            fn = os.path.join(tmp, "round_trip.ptu")
            self.assertTrue(self.data.write(fn))
            back = tttrlib.TTTR(fn)
            np.testing.assert_array_equal(back.macro_times, self.data.macro_times)
            np.testing.assert_array_equal(back.micro_times, self.data.micro_times)
            np.testing.assert_array_equal(
                back.routing_channels, self.data.routing_channels
            )


if __name__ == "__main__":
    unittest.main()
