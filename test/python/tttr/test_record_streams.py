# SPDX-License-Identifier: BSD-3-Clause
"""Decoding a buffer, reading a container in pieces, and the whole .set sidecar.

Three gaps in the same seam, all of them "the library can do this and
does not expose it":

1. every decoder sat behind ``TTTR(filename)``, so a caller holding a buffer --
   from a card, a socket, a container it unpacked itself -- had to write the
   decoder again;
2. thirteen of the fourteen container types were all-or-nothing on read;
3. the ``.set`` sidecar was read for five tags out of ~120, by a function that
   was in no binding.

The load-bearing test here is :meth:`TestDecodeRecords.test_split_inside_an
_overflow_run`. The decoder state exists for exactly one reason -- an SPC-130
macro time overflow is a record of its own, and a chunk boundary that lands in a
run of them silently shifts every macro time after it if the count is dropped.
Nothing errors when that goes wrong; the times are simply short.
"""
import os
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
import tttrlib
from test_settings import DATA_AVAILABLE, data_file, get_data_path  # type: ignore

# data_file() rather than get_data_path() for the file every test here reads:
# the latter warns and hands back a path to nothing, so a runner whose data set
# lacks it used to run the whole module against a file that is not there. That
# is how this module segfaulted CI -- TTTRHeader dereferenced open_file()'s
# nullptr -- and why it then failed on Windows for a second, unrelated-looking
# reason. Missing data is a skip.
SPC130 = data_file("bh/bh_spc132_sm_dna/m000.spc", module_level=True)

# The rest are resolved here for readability. SPCQC, SPC600_256 and CZ_RAW are
# read through the RANGED table below, which already skips a file it does not
# find; the two .set sidecars gate their own test class.
SPCQC = get_data_path("bh/QC004files/sample_c01.spc")
SPC600_256 = get_data_path("bh/bh_spc630_256.spc")
CZ_RAW = get_data_path("cz/fcs/5a6ce6a348a08e3da9f7c0ab4ee0ce94_R1_P1_K1_Ch1.raw")
SET_IMAGING = get_data_path("imaging/bh/spcm/FocalCheck_A1_20x_8xzoom_750nm_m1.set")
SET_SPCQC = get_data_path("bh/bh_spcqc004.set")

HAVE_SET_IMAGING = os.path.exists(SET_IMAGING)
HAVE_SET_SPCQC = os.path.exists(SET_SPCQC)

# (path, container_type) for every container that can be read in pieces and has
# a file here to read. Container types are named rather than inferred: four
# formats claim ".spc" and detection picks the first that accepts.
RANGED = [
    ("pq/ptu/pq_ptu_hh_t3.ptu", 0),
    ("pq/ht3/pq_ht3_sf-compression.ht3", 1),
    ("bh/bh_spc132_sm_dna/m000.spc", 2),
    ("bh/bh_spc630_256.spc", 3),
    ("cz/fcs/5a6ce6a348a08e3da9f7c0ab4ee0ce94_R1_P1_K1_Ch1.raw", 6),
    ("bh/QC004files/sample_c01.spc", 9),
]


def assert_same_events(test, a, b, what=""):
    """Two TTTRs hold the same events, column for column."""
    test.assertEqual(a.size(), b.size(), what + " event count")
    np.testing.assert_array_equal(a.macro_times, b.macro_times, what + " macro times")
    np.testing.assert_array_equal(a.micro_times, b.micro_times, what + " micro times")
    np.testing.assert_array_equal(a.routing_channels, b.routing_channels,
                                  what + " routing channels")
    np.testing.assert_array_equal(a.event_types, b.event_types, what + " event types")


def decode_in_chunks(path, container_type, boundaries):
    """Decode a whole container by handing `boundaries` back one range at a time."""
    info = tttrlib.container_records(path, container_type)
    data = tttrlib.TTTR()
    state = tttrlib.TTTRDecodeState()
    for first, n in boundaries:
        raw = tttrlib.container_read_records(path, first, n, container_type)
        if not len(raw):
            continue
        tttrlib.decode_records(np.frombuffer(raw, dtype=np.uint8),
                               info.record_type, state, data)
    data.set_header(tttrlib.TTTRHeader(path, container_type))
    data.apply_container_channels(container_type)
    data.find_used_routing_channels()
    return data, state


def even_chunks(n_records, size):
    return [(at, size) for at in range(0, n_records, size)]


class TestRecordTypeMetadata(unittest.TestCase):
    """What a caller holding a buffer has to know before it can decode one."""

    def test_every_record_type_is_named(self):
        for rt in range(1, 20):
            name = tttrlib.record_type_name(rt)
            self.assertTrue(name)
            self.assertNotIn("record type", name, f"{rt} has no name")

    def test_widths(self):
        self.assertEqual(tttrlib.record_bytes(tttrlib.RECORD_SPC130), 4)
        # the one format whose record is not word aligned
        self.assertEqual(tttrlib.record_bytes(tttrlib.RECORD_SPC600_4096), 6)
        # SM interleaves two word widths, so it has no width to state
        self.assertEqual(tttrlib.record_bytes(tttrlib.RECORD_SM), 0)

    def test_decodable_set_is_the_dispatch_table(self):
        """Every registered type is reachable, or declines."""
        decodable = set(tttrlib.decodable_record_types())
        for rt in range(1, 20):
            buffer = np.zeros(32, dtype=np.uint8)
            if rt in decodable:
                tttrlib.decode_records(buffer, rt)      # must not raise
            else:
                # ValueError: the library maps std::invalid_argument to it
                with self.assertRaises(ValueError) as raised:
                    tttrlib.decode_records(buffer, rt)
                # by name, not by number: "record type 11" is not actionable
                self.assertIn(tttrlib.record_type_name(rt), str(raised.exception))

    def test_the_enum_agrees_with_the_readers(self):
        """The enumerators take their values from the macros, so this is cheap
        insurance against someone reintroducing a hand-written copy."""
        self.assertEqual(tttrlib.RECORD_SPC130, 7)
        self.assertEqual(tttrlib.RECORD_SPCQC_X04, 15)
        data = tttrlib.TTTR(SPC130) if DATA_AVAILABLE else None
        if data is not None:
            self.assertEqual(data.get_tttr_record_type(), tttrlib.RECORD_SPC130)


@unittest.skipUnless(DATA_AVAILABLE, "test data not available")
class TestDecodeRecords(unittest.TestCase):
    """Criteria 1 and 2: a buffer decodes to what the file reader produces."""

    def test_whole_file_in_one_buffer(self):
        reference = tttrlib.TTTR(SPC130)
        info = tttrlib.container_records(SPC130)
        raw = tttrlib.container_read_records(SPC130, 0, 0)
        self.assertEqual(len(raw), info.n_records * info.bytes_per_record)
        data, state = tttrlib.decode_records(
            np.frombuffer(raw, dtype=np.uint8), info.record_type)
        assert_same_events(self, data, reference, "one buffer")
        self.assertEqual(state.n_records, info.n_records)
        self.assertEqual(state.n_events, reference.size())

    def test_uint32_buffer_is_accepted(self):
        """A 32-bit record format's natural array shape, not just bytes."""
        reference = tttrlib.TTTR(SPC130)
        raw = tttrlib.container_read_records(SPC130, 0, 0)
        words = np.frombuffer(raw, dtype=np.uint32)
        data, _ = tttrlib.decode_records(words, tttrlib.RECORD_SPC130)
        assert_same_events(self, data, reference, "uint32 buffer")

    def test_chunked_equals_whole(self):
        reference = tttrlib.TTTR(SPC130)
        info = tttrlib.container_records(SPC130)
        # deliberately not a round number, and not a divisor of the record count
        data, _ = decode_in_chunks(SPC130, 2, even_chunks(info.n_records, 4999))
        assert_same_events(self, data, reference, "4999-record chunks")

    def test_split_inside_an_overflow_run(self):
        """The only thing the carried state exists for.

        An SPC-130 macro time overflow is a record of its own -- invalid=1,
        mark=0, mtov=1 -- carrying a 28-bit count, and a gap in the photon
        stream produces a run of them. A chunk boundary that lands in such a run
        drops the count if the state is not handed back, and every macro time
        after it is short, with nothing failing anywhere. So: cut immediately
        before and immediately after overflow records, and at the boundaries of
        the longest run of them.
        """
        reference = tttrlib.TTTR(SPC130)
        raw = tttrlib.container_read_records(SPC130, 0, 0)
        words = np.frombuffer(raw, dtype=np.uint32)
        # invalid=1 (bit 31), mtov=1 (bit 30), mark=0 (bit 28)
        is_overflow = (((words >> 31) & 1) == 1) & (((words >> 30) & 1) == 1) \
            & (((words >> 28) & 1) == 0)
        overflow_at = np.flatnonzero(is_overflow)
        self.assertGreater(len(overflow_at), 0, "no overflow records to split at")

        # the longest consecutive run, so a cut also lands strictly *inside* one
        runs, start = [], None
        for i, flag in enumerate(is_overflow):
            if flag and start is None:
                start = i
            elif not flag and start is not None:
                runs.append((start, i - start))
                start = None
        longest = max(runs, key=lambda r: r[1])

        cuts = set()
        for at in overflow_at[::4001]:          # a spread of them, not all 125k
            cuts.update({int(at), int(at) + 1})
        cuts.update(range(longest[0], longest[0] + longest[1] + 1))
        cuts.discard(0)                          # not a split at all

        n = len(words)
        for cut in sorted(cuts):
            data, _ = decode_in_chunks(SPC130, 2, [(0, cut), (cut, n - cut)])
            assert_same_events(self, data, reference, f"cut at record {cut}")

    def test_a_state_is_not_optional(self):
        """Same two chunks without the state: the second one starts over.

        The negative control. Without it the test above passes even if the
        state does nothing.
        """
        reference = tttrlib.TTTR(SPC130)
        info = tttrlib.container_records(SPC130)
        half = info.n_records // 2
        data = tttrlib.TTTR()
        for first, n in [(0, half), (half, info.n_records - half)]:
            raw = tttrlib.container_read_records(SPC130, first, n)
            tttrlib.decode_records(np.frombuffer(raw, dtype=np.uint8),
                                   info.record_type, None, data)
        self.assertEqual(data.size(), reference.size())
        self.assertFalse(np.array_equal(data.macro_times, reference.macro_times),
                         "dropping the state has to be visible, or the state is a lie")

    def test_one_record_at_a_time(self):
        """The pathological chunking: every boundary is a boundary."""
        reference = tttrlib.TTTR(SPC130)
        data, _ = decode_in_chunks(SPC130, 2, [(i, 1) for i in range(20000)])
        np.testing.assert_array_equal(
            data.macro_times, reference.macro_times[:data.size()])

    def test_a_trailing_partial_record_is_ignored(self):
        """A caller reading off a socket hands back whatever arrived."""
        raw = tttrlib.container_read_records(SPC130, 0, 10)
        clipped = np.frombuffer(raw, dtype=np.uint8)[:-2]   # 9.5 records
        whole, _ = tttrlib.decode_records(np.frombuffer(raw, dtype=np.uint8)[:-4],
                                          tttrlib.RECORD_SPC130)
        partial, _ = tttrlib.decode_records(clipped, tttrlib.RECORD_SPC130)
        assert_same_events(self, partial, whole, "trailing partial record")

    def test_decoding_into_a_tttr_that_already_holds_events(self):
        info = tttrlib.container_records(SPC130)
        data, state = tttrlib.decode_records(
            np.frombuffer(tttrlib.container_read_records(SPC130, 0, 1000), np.uint8),
            info.record_type)
        first = data.size()
        tttrlib.decode_records(
            np.frombuffer(tttrlib.container_read_records(SPC130, 1000, 1000), np.uint8),
            info.record_type, state, data)
        self.assertGreater(data.size(), first)
        self.assertEqual(state.n_records, 2000)


@unittest.skipUnless(DATA_AVAILABLE, "test data not available")
class TestRangedReads(unittest.TestCase):
    """Criterion 4: the composition reproduces the whole-file read, and the
    containers that cannot be read in pieces say so."""

    def test_every_ranged_container_round_trips(self):
        for rel, container_type in RANGED:
            path = get_data_path(rel)
            if not os.path.exists(path):
                continue
            with self.subTest(container=rel):
                reference = tttrlib.TTTR(path, container_type)
                info = tttrlib.container_records(path, container_type)
                self.assertTrue(info.ranged, info.reason)
                data, _ = decode_in_chunks(
                    path, container_type, even_chunks(info.n_records, 7777))
                assert_same_events(self, data, reference, rel)

    def test_n_records_without_decoding(self):
        reference = tttrlib.TTTR(SPC130)
        self.assertEqual(tttrlib.container_n_records(SPC130),
                         reference.n_records_in_file)
        # more records than events: an overflow record is not an event
        self.assertGreater(tttrlib.container_n_records(SPC130), reference.size())

    def test_the_containers_that_decline_say_which_they_are(self):
        for rel in ["hdf/1a_1b_Mix.hdf5", "sm/tl_sm.sm"]:
            path = get_data_path(rel)
            if not os.path.exists(path):
                continue
            with self.subTest(container=rel):
                info = tttrlib.container_records(path)
                self.assertFalse(info.ranged)
                self.assertTrue(info.reason)
                names = tttrlib.TTTR.get_supported_container_names()
                self.assertTrue(any(n in info.reason for n in names),
                                f"the decline names no format: {info.reason}")
                self.assertRaises(ValueError,
                                  tttrlib.container_read_records, path, 0, 10)

    def test_a_short_read_comes_back_short(self):
        info = tttrlib.container_records(SPC130)
        raw = tttrlib.container_read_records(SPC130, info.n_records - 5, 1000)
        self.assertEqual(len(raw), 5 * info.bytes_per_record)
        self.assertEqual(len(tttrlib.container_read_records(SPC130, info.n_records, 10)), 0)

    def test_ranged_reads_are_declared_in_the_registry(self):
        registry = tttrlib.registry()["file_container"]
        self.assertTrue(registry["PTU"]["ranged_reads"])
        self.assertTrue(registry["SPC-130"]["ranged_reads"])
        self.assertTrue(registry["PTO"]["ranged_reads"])
        self.assertFalse(registry["PHOTON-HDF5"]["ranged_reads"])
        # the range is a reader parameter, so it is in the schema too
        self.assertIn("first_record", registry["PTU"]["params_schema"]["properties"])

    def test_the_range_as_a_reader_parameter(self):
        """TTTR(spec, first, n) cannot be a constructor -- it is ambiguous with
        TTTR(const char*, int, bool). The PTO reader hit this and went through
        set_container_parameters; so does this."""
        data = tttrlib.TTTR()
        data.set_container_parameters('{"first_record": 0, "n_records": 1000}')
        data.read_file(SPC130, 2)
        composed, _ = tttrlib.decode_records(
            np.frombuffer(tttrlib.container_read_records(SPC130, 0, 1000), np.uint8),
            tttrlib.RECORD_SPC130)
        assert_same_events(self, data, composed, "reader-parameter range")

    def test_container_events_named_function(self):
        composed, _ = tttrlib.decode_records(
            np.frombuffer(tttrlib.container_read_records(SPC130, 0, 1000), np.uint8),
            tttrlib.RECORD_SPC130)
        assert_same_events(self, tttrlib.container_events(SPC130, 0, 1000), composed)

    def test_chunk_generator(self):
        reference = tttrlib.TTTR(SPC130)
        seen = []
        for data, done, total in tttrlib.container_chunks(SPC130, 50000):
            seen.append((done, total))
        self.assertEqual(seen[-1][0], seen[-1][1])
        assert_same_events(self, data, reference, "container_chunks")


@unittest.skipUnless(HAVE_SET_IMAGING and HAVE_SET_SPCQC,
                     "the .set sidecars are not in this data set")
class TestSetSidecar(unittest.TestCase):
    """Criterion 5: the whole sidecar, not the five tags a photon reader needs.

    Gated on the two .set files themselves rather than on the data directory:
    a data set that has the directory but not these files should skip, not
    fail on a path to nothing.
    """

    def test_parameter_counts(self):
        # The PRD counted #SP + #PR: 115 and 121. Everything else -- the #DI
        # display block, the indexed trace and window rows, the identification
        # header -- is over and above that.
        for path, floor in [(SET_IMAGING, 115), (SET_SPCQC, 121)]:
            with self.subTest(file=os.path.basename(path)):
                self.assertGreaterEqual(len(tttrlib.read_set_file(path)), floor)

    def test_the_five_tags_agree_with_the_photon_reader(self):
        """read_bh_set_file keeps its scope; this must not contradict it."""
        sidecar = tttrlib.bh_set(SET_IMAGING)["SYS_PARA"]
        # keep the TTTR alive: `.header` is a pointer into it, and reading a tag
        # off the header of a temporary is a use-after-free
        data = tttrlib.TTTR(
            get_data_path("imaging/bh/spcm/FocalCheck_A1_20x_8xzoom_750nm_m1.spc"),
            "SPC-130", read_input=False)
        header = data.header
        self.assertEqual(int(sidecar["SP_IMG_X"]), header.tag("ImgHdr_PixX")["value"])
        self.assertEqual(int(sidecar["SP_IMG_Y"]), header.tag("ImgHdr_PixY")["value"])
        self.assertEqual(int(sidecar["SP_PIX_CLK"]) == 1,
                         bool(header.tag("BH_UsePixelClock")["value"]))

    def test_the_spcqc_micro_time_resolution_comes_from_the_sidecar(self):
        sidecar = tttrlib.bh_set(SET_SPCQC)["SYS_PARA"]
        expected = float(sidecar["SP_TAC_R"]) / int(sidecar["SP_ADC_RE"])
        data = tttrlib.TTTR(get_data_path("bh/bh_spcqc004.spc"), "SPC-QC",
                            read_input=False)
        self.assertAlmostEqual(data.header.micro_time_resolution, expected, places=15)

    def test_sections_are_preserved(self):
        parsed = tttrlib.bh_set(SET_SPCQC)
        self.assertIn("IDENTIFICATION", parsed)
        self.assertIn("SYS_PARA", parsed)
        self.assertEqual(parsed["IDENTIFICATION"]["ID"], "SPC Setup Script File")
        # The identification block runs straight out of the binary preamble with
        # no newline in front of it, so anchoring the marker on column zero
        # loses the whole block.
        self.assertEqual(parsed["IDENTIFICATION"]["Author"], "System")

    def test_values_stay_text(self):
        """A .set is a device configuration file and its types are
        per-parameter; a parser that guesses is wrong about one field in a
        hundred and silent about it."""
        by_name = {p.name: p for p in tttrlib.read_set_file(SET_SPCQC)
                   if p.group == "SP"}
        self.assertEqual(by_name["SP_TAC_R"].type, "F")
        self.assertEqual(by_name["SP_TAC_R"].value, "6.554e-08")
        self.assertEqual(by_name["SP_ADC_RE"].type, "I")
        self.assertIsInstance(by_name["SP_ADC_RE"].value, str)

    def test_bytes_and_path_agree(self):
        with open(SET_SPCQC, "rb") as f:
            content = f.read()
        self.assertEqual(len(tttrlib.parse_set(content.decode("latin-1"))),
                         len(tttrlib.read_set_file(SET_SPCQC)))
        self.assertEqual(tttrlib.bh_set(content=content), tttrlib.bh_set(SET_SPCQC))

    def test_a_file_that_is_not_a_set_yields_nothing(self):
        self.assertEqual(len(tttrlib.read_set_file(SPC130)), 0)
        self.assertEqual(len(tttrlib.read_set_file("no-such-file.set")), 0)

    def test_binary_tail_is_not_parsed(self):
        """Everything after BIN_PARA_BEGIN: is a blob of window geometry, and a
        line parser run over binary finds parameters that are not there."""
        for p in tttrlib.read_set_file(SET_SPCQC):
            self.assertIn(p.section,
                          {"IDENTIFICATION", "SYS_PARA", "TRACE_PARA", "WIND_PARA"})


if __name__ == "__main__":
    unittest.main()
