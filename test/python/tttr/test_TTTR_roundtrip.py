"""Round-trip and transcode tests for TTTR file writing.

Same-format round trips (read -> write -> read) must preserve the decoded
event stream exactly (macro times, micro times, routing channels and --
where the format carries them -- event types).

Cross-format transcodes are lossy in documented ways (bit-depth clipping,
dropped micro times, dropped markers); the tests assert exactly what
survives each trip. See PRDs/PRD-006-tttr-file-roundtrip-io.md and
doc/file_formats.rst.
"""
from __future__ import division

import json
import os
import tempfile
import unittest

import numpy as np
import tttrlib

print("Test: ", __file__)

from test_settings import settings, DATA_AVAILABLE  # type: ignore


def tmp_filename(suffix):
    fd, fn = tempfile.mkstemp(suffix)
    os.close(fd)
    os.unlink(fn)  # writers create the file themselves
    return fn


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found, skipping roundtrip tests")
class SameFormatRoundTripTests(unittest.TestCase):
    """read(A) -> write(A) -> read(A) preserves the decoded event stream."""

    def roundtrip(self, settings_key, container, suffix,
                  micro=True, channels=True, event_types=False):
        fn_in = settings[settings_key]
        if not os.path.isfile(fn_in):
            self.skipTest("missing data file: %s" % fn_in)
        fn_out = tmp_filename(suffix)
        try:
            d = tttrlib.TTTR(fn_in, container)
            self.assertGreater(len(d.macro_times), 0)
            self.assertTrue(d.write(fn_out))
            d2 = tttrlib.TTTR(fn_out, container)
            np.testing.assert_array_equal(d.macro_times, d2.macro_times)
            if micro:
                np.testing.assert_array_equal(d.micro_times, d2.micro_times)
            if channels:
                np.testing.assert_array_equal(d.routing_channels, d2.routing_channels)
            if event_types:
                np.testing.assert_array_equal(d.event_types, d2.event_types)
            return d, d2
        finally:
            if os.path.isfile(fn_out):
                os.unlink(fn_out)

    def test_spc132(self):
        self.roundtrip("spc132_filename", "SPC-130", ".spc", event_types=True)

    def test_spc600_256(self):
        self.roundtrip("spc630_filename", "SPC-600_256", ".spc")

    def test_ptu_hh_t3(self):
        self.roundtrip("ptu_hh_t3_filename", "PTU", ".ptu", event_types=True)

    def test_ptu_hh_t2(self):
        # T2 records carry no micro time; micro times are all zero on read
        d, d2 = self.roundtrip(
            "ptu_hh_t2_filename", "PTU", ".ptu", event_types=True)
        self.assertEqual(d2.micro_times.max(initial=0), 0)

    def test_ht3_sf_compressed(self):
        # SF-compressed HT3 (Suren Felekyan's conversion) is auto-detected
        # from the overflow record payloads and round-trips exactly
        d, d2 = self.roundtrip("ht3_sf_filename", "HT3", ".ht3", event_types=True)
        self.assertEqual(d.header.tttr_record_type, 14)   # PQ_RECORD_TYPE_SF_HT3
        self.assertEqual(d2.header.tttr_record_type, 14)

    def test_ht3_sf_reference_decode(self):
        # Validate the SF decoder against an independent numpy decode of
        # the raw records (semantics of the SF reference implementation:
        # overflow records advance the sync counter by (1 + count) * 1024)
        fn = settings["ht3_sf_filename"]
        if not os.path.isfile(fn):
            self.skipTest("missing data file: %s" % fn)
        d = tttrlib.TTTR(fn, "HT3")
        raw = np.fromfile(fn, dtype="<u4", offset=d.header.end())
        is_ovf = (raw >> 25) == 127
        ov = np.zeros(len(raw), dtype=np.uint64)
        ov[is_ovf] = 1 + (raw[is_ovf] & 0xFFFFFF)
        ov = np.cumsum(ov) - ov  # overflows before each record
        mt_ref = ((raw & 0x3FF) + ov * 1024)[~is_ovf]
        mi_ref = ((raw >> 10) & 0x7FFF)[~is_ovf]
        ch_ref = ((raw >> 25) & 0x3F)[~is_ovf]
        np.testing.assert_array_equal(d.macro_times, mt_ref)
        np.testing.assert_array_equal(d.micro_times, mi_ref)
        np.testing.assert_array_equal(d.routing_channels, ch_ref)

    def test_ht3_v1_header_sf_records(self):
        # pq_ht3v1.0_hh_t3.ht3 carries a HydraHarp v1 header but its record
        # stream is SF-compressed (non-zero overflow payloads, no
        # consecutive overflow records) - it must be detected as SF
        d, d2 = self.roundtrip("ht3_v1_filename", "HT3", ".ht3", event_types=True)
        self.assertEqual(d.header.tttr_record_type, 14)   # PQ_RECORD_TYPE_SF_HT3

    def test_ht3_v1_plain(self):
        # An instrument-written HydraHarp v1 HT3 file (all overflow record
        # payloads zero, overflow runs present) must NOT be detected as SF
        fn = settings["clsm_ht3_irf_filename"]
        if not os.path.isfile(fn):
            self.skipTest("missing data file: %s" % fn)
        d = tttrlib.TTTR(fn, "HT3")
        self.assertEqual(d.header.tttr_record_type, 3)    # PQ_RECORD_TYPE_HHT3v1

    def test_sm(self):
        # SM records carry macro time and channel only
        self.roundtrip("sm_filename", "SM", ".sm", micro=False)

    def test_cz_raw(self):
        # CZ raw records carry macro time deltas; the channel is a header
        # field and constant per file
        self.roundtrip("cz_raw_filename", "CZ-RAW", ".raw", micro=False)

    def test_photon_hdf5(self):
        d, d2 = self.roundtrip("photon_hdf_filename", "PHOTON-HDF5", ".hdf5")
        # resolutions travel via timestamps_specs / nanotimes_specs
        h, h2 = d.header, d2.header
        self.assertAlmostEqual(
            h.macro_time_resolution, h2.macro_time_resolution)
        self.assertAlmostEqual(
            h.micro_time_resolution, h2.micro_time_resolution)
        # /setup metadata read from the source is preserved on write
        import json
        for name in ("setup.num_pixels", "setup.num_spots"):
            t1 = json.loads(h.get_json(name, 0))
            t2 = json.loads(h2.get_json(name, 0))
            if t1:  # only compare tags present in the source file
                self.assertEqual(t1.get("value"), t2.get("value"), name)

    def test_ptu_header_tags_preserved(self):
        # PTU is the richest target: the full tag list is re-written
        fn_in = settings["ptu_hh_t3_filename"]
        if not os.path.isfile(fn_in):
            self.skipTest("missing data file: %s" % fn_in)
        fn_out = tmp_filename(".ptu")
        try:
            d = tttrlib.TTTR(fn_in, "PTU")
            self.assertTrue(d.write(fn_out))
            d2 = tttrlib.TTTR(fn_out, "PTU")
            import json
            for name in (
                    "MeasDesc_GlobalResolution",
                    "MeasDesc_Resolution",
                    "TTResultFormat_TTTRRecType",
                    "HW_Type",
            ):
                t1 = json.loads(d.header.get_json(name))
                t2 = json.loads(d2.header.get_json(name))
                if t1:
                    self.assertEqual(t1.get("value"), t2.get("value"), name)
        finally:
            if os.path.isfile(fn_out):
                os.unlink(fn_out)

    def test_spc600_4096_synthetic(self):
        # No reference SPC-600 4096-channel file is available; validate that
        # the encoder is the exact inverse of the decoder with synthetic data
        n = 1000
        rng = np.random.RandomState(42)
        macro = np.cumsum(rng.randint(0, 2 ** 22, n).astype(np.uint64))
        micro = rng.randint(0, 4096, n).astype(np.uint16)
        channels = rng.randint(0, 8, n).astype(np.int8)
        types = np.zeros(n, dtype=np.int8)

        d = tttrlib.TTTR()
        d.append_events(macro, micro, channels, types)
        header = d.header
        header.tttr_container_type = 4  # BH_SPC600_4096_CONTAINER
        header.tttr_record_type = 9     # BH_RECORD_TYPE_SPC600_4096

        fn_out = tmp_filename(".spc")
        try:
            self.assertTrue(d.write(fn_out))
            d2 = tttrlib.TTTR(fn_out, "SPC-600_4096")
            np.testing.assert_array_equal(macro, d2.macro_times)
            np.testing.assert_array_equal(micro, d2.micro_times)
            np.testing.assert_array_equal(channels, d2.routing_channels)
        finally:
            if os.path.isfile(fn_out):
                os.unlink(fn_out)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found, skipping transcode tests")
class TranscodeTests(unittest.TestCase):
    """Cross-format conversions; lossy in documented ways."""

    def transcode(self, d, container_type, record_type, suffix, read_as):
        header = d.header
        header.tttr_container_type = container_type
        header.tttr_record_type = record_type
        fn_out = tmp_filename(suffix)
        try:
            self.assertTrue(d.write(fn_out))
            return tttrlib.TTTR(fn_out, read_as)
        finally:
            if os.path.isfile(fn_out):
                os.unlink(fn_out)

    def read(self, settings_key, container):
        fn = settings[settings_key]
        if not os.path.isfile(fn):
            self.skipTest("missing data file: %s" % fn)
        return tttrlib.TTTR(fn, container)

    def test_spc132_to_ptu_t3(self):
        # SPC-130 fits into HHT3v2 without loss (12 bit micro < 15 bit,
        # 4 bit channel < 6 bit)
        d = self.read("spc132_filename", "SPC-130")
        d2 = self.transcode(d, 0, 4, ".ptu", "PTU")  # PTU / HHT3v2
        np.testing.assert_array_equal(d.macro_times, d2.macro_times)
        np.testing.assert_array_equal(d.micro_times, d2.micro_times)
        np.testing.assert_array_equal(d.routing_channels, d2.routing_channels)
        np.testing.assert_array_equal(d.event_types, d2.event_types)

    def test_ptu_t3_to_spc132(self):
        # SPC-130 micro times clip to 12 bit; markers are not representable
        d = self.read("ptu_hh_t3_filename", "PTU")
        d2 = self.transcode(d, 2, 7, ".spc", "SPC-130")  # SPC-130
        self.assertEqual(len(d.macro_times), len(d2.macro_times))
        photons = np.array(d.event_types) == 0
        np.testing.assert_array_equal(
            d.macro_times[photons], np.array(d2.macro_times)[photons])
        np.testing.assert_array_equal(
            np.minimum(d.micro_times[photons], 4095),
            np.array(d2.micro_times)[photons])
        np.testing.assert_array_equal(
            d.routing_channels[photons] & 0xF,
            np.array(d2.routing_channels)[photons])

    def test_ptu_t3_to_ptu_t2(self):
        # T2 drops the micro times, everything else survives
        d = self.read("ptu_hh_t3_filename", "PTU")
        d2 = self.transcode(d, 0, 1, ".ptu", "PTU")  # PTU / HHT2v2
        np.testing.assert_array_equal(d.macro_times, d2.macro_times)
        np.testing.assert_array_equal(d.routing_channels, d2.routing_channels)
        np.testing.assert_array_equal(d.event_types, d2.event_types)
        self.assertEqual(d2.micro_times.max(initial=0), 0)

    def test_ptu_t3_to_ptu_hht2v1(self):
        # HydraHarp T2 v1 (record type 2): same 6-bit channel field as v2,
        # so the full photon stream survives; micro times are dropped.
        d = self.read("ptu_hh_t3_filename", "PTU")
        d2 = self.transcode(d, 0, 2, ".ptu", "PTU")  # PTU / HHT2v1
        np.testing.assert_array_equal(d.macro_times, d2.macro_times)
        np.testing.assert_array_equal(d.routing_channels, d2.routing_channels)
        np.testing.assert_array_equal(d.event_types, d2.event_types)
        self.assertEqual(d2.micro_times.max(initial=0), 0)

    def test_ptu_t3_to_generic_t2(self):
        # MultiHarp / PicoHarp 330 generic T2 (record type 13)
        d = self.read("ptu_hh_t3_filename", "PTU")
        d2 = self.transcode(d, 0, 13, ".ptu", "PTU")  # PTU / GENERIC_T2
        np.testing.assert_array_equal(d.macro_times, d2.macro_times)
        np.testing.assert_array_equal(d.routing_channels, d2.routing_channels)
        np.testing.assert_array_equal(d.event_types, d2.event_types)
        self.assertEqual(d2.micro_times.max(initial=0), 0)

    def test_ptu_t3_to_ptu_pht2(self):
        # PicoHarp T2 (record type 6): 4-bit channel field. The source photons
        # live on channels 0 and 2, which fit; micro times are dropped.
        d = self.read("ptu_hh_t3_filename", "PTU")
        self.assertLessEqual(int(np.asarray(d.routing_channels).max()), 14)
        d2 = self.transcode(d, 0, 6, ".ptu", "PTU")  # PTU / PHT2
        np.testing.assert_array_equal(d.macro_times, d2.macro_times)
        np.testing.assert_array_equal(d.routing_channels, d2.routing_channels)
        np.testing.assert_array_equal(d.event_types, d2.event_types)
        self.assertEqual(d2.micro_times.max(initial=0), 0)

    def test_ptu_t3_to_generic_t3(self):
        # MultiHarp / PicoHarp 330 generic T3
        d = self.read("ptu_hh_t3_filename", "PTU")
        d2 = self.transcode(d, 0, 12, ".ptu", "PTU")  # PTU / GENERIC_T3
        np.testing.assert_array_equal(d.macro_times, d2.macro_times)
        np.testing.assert_array_equal(d.micro_times, d2.micro_times)
        np.testing.assert_array_equal(d.routing_channels, d2.routing_channels)

    def test_spc132_to_ht3(self):
        # Transcoding into HT3 synthesizes a HydraHarp v2 header
        d = self.read("spc132_filename", "SPC-130")
        d2 = self.transcode(d, 1, 4, ".ht3", "HT3")  # HT3 / HHT3v2
        np.testing.assert_array_equal(d.macro_times, d2.macro_times)
        np.testing.assert_array_equal(d.micro_times, d2.micro_times)
        np.testing.assert_array_equal(d.routing_channels, d2.routing_channels)
        # HT3 carries the macro time calibration as SyncRate; the writer
        # derives it from the global resolution so calibration survives
        self.assertAlmostEqual(
            d.header.macro_time_resolution,
            d2.header.macro_time_resolution,
            delta=abs(d.header.macro_time_resolution) * 1e-6,
        )

    def test_ptu_t3_to_photon_hdf5(self):
        # Photon-HDF5 stores the decoded arrays; lossless for photons
        d = self.read("ptu_hh_t3_filename", "PTU")
        d2 = self.transcode(d, 5, -1, ".hdf5", "PHOTON-HDF5")
        np.testing.assert_array_equal(d.macro_times, d2.macro_times)
        np.testing.assert_array_equal(d.micro_times, d2.micro_times)
        np.testing.assert_array_equal(d.routing_channels, d2.routing_channels)
        self.assertAlmostEqual(
            d.header.macro_time_resolution, d2.header.macro_time_resolution)

    def test_ptu_t3_to_sm(self):
        # SM keeps macro times and channels; micro times drop
        d = self.read("ptu_hh_t3_filename", "PTU")
        d2 = self.transcode(d, 7, 11, ".sm", "SM")  # SM
        np.testing.assert_array_equal(d.macro_times, d2.macro_times)
        np.testing.assert_array_equal(d.routing_channels, d2.routing_channels)
        self.assertEqual(d2.micro_times.max(initial=0), 0)

    def test_sf_ht3_to_ptu_t3(self):
        # SF-compressed HT3 payloads fit HydraHarp v2 T3 records losslessly
        d = self.read("ht3_sf_filename", "HT3")
        d2 = self.transcode(d, 0, 4, ".ptu", "PTU")  # PTU / HHT3v2
        np.testing.assert_array_equal(d.macro_times, d2.macro_times)
        np.testing.assert_array_equal(d.micro_times, d2.micro_times)
        np.testing.assert_array_equal(d.routing_channels, d2.routing_channels)

    def test_ht3_v1_to_sf_ht3(self):
        # Compress a plain (instrument-written) v1 HT3 file with SF
        # compression: values are preserved and the file shrinks because
        # overflow runs collapse into single counted records
        fn = settings["clsm_ht3_irf_filename"]
        if not os.path.isfile(fn):
            self.skipTest("missing data file: %s" % fn)
        d = tttrlib.TTTR(fn, "HT3")
        self.assertEqual(d.header.tttr_record_type, 3)
        header = d.header
        header.tttr_container_type = 1
        header.tttr_record_type = 14  # PQ_RECORD_TYPE_SF_HT3
        fn_out = tmp_filename(".ht3")
        try:
            self.assertTrue(d.write(fn_out))
            d2 = tttrlib.TTTR(fn_out, "HT3")
            self.assertEqual(d2.header.tttr_record_type, 14)
            np.testing.assert_array_equal(d.macro_times, d2.macro_times)
            np.testing.assert_array_equal(d.micro_times, d2.micro_times)
            np.testing.assert_array_equal(d.routing_channels, d2.routing_channels)
            self.assertLess(os.path.getsize(fn_out), os.path.getsize(fn))
        finally:
            if os.path.isfile(fn_out):
                os.unlink(fn_out)

    def test_ptu_t3_to_cz_raw(self):
        # CZ raw keeps macro times only
        d = self.read("ptu_hh_t3_filename", "PTU")
        d2 = self.transcode(d, 6, 10, ".raw", "CZ-RAW")  # CZ-RAW
        np.testing.assert_array_equal(d.macro_times, d2.macro_times)

    def test_invalid_pair_returns_false(self):
        d = self.read("spc132_filename", "SPC-130")
        header = d.header
        header.tttr_container_type = 99
        fn_out = tmp_filename(".xxx")
        try:
            self.assertFalse(d.write(fn_out))
            self.assertFalse(os.path.isfile(fn_out))
        finally:
            if os.path.isfile(fn_out):
                os.unlink(fn_out)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found, skipping write-inference tests")
class WriteTypeInferenceTests(unittest.TestCase):
    """write() selects the output container from the filename extension, with an
    optional explicit override (by container id or by name)."""

    def read(self, settings_key, container):
        fn = settings[settings_key]
        if not os.path.isfile(fn):
            self.skipTest("missing data file: %s" % fn)
        return tttrlib.TTTR(fn, container)

    def test_extension_infers_cross_family_container(self):
        # SPC-130 source written to a .ptu file must become a PTU file, without
        # touching the header container type beforehand.
        d = self.read("spc132_filename", "SPC-130")
        fn_out = tmp_filename(".ptu")
        try:
            self.assertTrue(d.write(fn_out))
            d2 = tttrlib.TTTR(fn_out)  # auto-detect on read
            self.assertEqual(d2.header.tttr_container_type, 0)  # PQ_PTU_CONTAINER
            self.assertEqual(len(d.macro_times), len(d2.macro_times))
        finally:
            if os.path.isfile(fn_out):
                os.unlink(fn_out)

    def test_generic_spc_extension_preserves_source_flavour(self):
        # The .spc extension is shared by all Becker & Hickl SPC flavours, so a
        # SPC-600/256 round trip must not collapse to SPC-130.
        d = self.read("spc630_filename", "SPC-600_256")
        fn_out = tmp_filename(".spc")
        try:
            self.assertTrue(d.write(fn_out))
            d2 = tttrlib.TTTR(fn_out, "SPC-600_256")
            self.assertEqual(d2.header.tttr_container_type, 3)  # BH_SPC600_256
            np.testing.assert_array_equal(d.macro_times, d2.macro_times)
            np.testing.assert_array_equal(d.micro_times, d2.micro_times)
        finally:
            if os.path.isfile(fn_out):
                os.unlink(fn_out)

    def test_explicit_container_name_overrides_extension(self):
        # An unknown extension is fine when the container name is given.
        d = self.read("spc132_filename", "SPC-130")
        fn_out = tmp_filename(".dat")
        try:
            self.assertTrue(d.write(fn_out, "PTU"))
            d2 = tttrlib.TTTR(fn_out, "PTU")
            self.assertEqual(d2.header.tttr_container_type, 0)
            self.assertEqual(len(d.macro_times), len(d2.macro_times))
        finally:
            if os.path.isfile(fn_out):
                os.unlink(fn_out)

    def test_explicit_container_id_beats_extension(self):
        # The numeric container argument wins over the filename extension.
        d = self.read("spc132_filename", "SPC-130")
        fn_out = tmp_filename(".ptu")
        try:
            self.assertTrue(d.write(fn_out, None, 2))  # 2 == BH_SPC130_CONTAINER
            d2 = tttrlib.TTTR(fn_out, "SPC-130")
            self.assertEqual(d2.header.tttr_container_type, 2)
            np.testing.assert_array_equal(d.macro_times, d2.macro_times)
        finally:
            if os.path.isfile(fn_out):
                os.unlink(fn_out)

    def test_unknown_container_name_returns_false(self):
        d = self.read("spc132_filename", "SPC-130")
        fn_out = tmp_filename(".spc")
        try:
            self.assertFalse(d.write(fn_out, "NOT-A-FORMAT"))
        finally:
            if os.path.isfile(fn_out):
                os.unlink(fn_out)


def _tag_names(tttr):
    import json
    return [t.get("name") for t in json.loads(tttr.header.json).get("tags", [])]


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found, skipping metadata tests")
class WriteMetadataFidelityTests(unittest.TestCase):
    """write() must transfer header metadata as faithfully as the target format
    allows, and always emit the minimal metadata that makes a file valid."""

    def read(self, settings_key, container):
        fn = settings[settings_key]
        if not os.path.isfile(fn):
            self.skipTest("missing data file: %s" % fn)
        return tttrlib.TTTR(fn, container)

    def test_resolution_survives_same_format_roundtrip(self):
        d = self.read("spc132_filename", "SPC-130")
        fn_out = tmp_filename(".spc")
        try:
            self.assertTrue(d.write(fn_out))
            d2 = tttrlib.TTTR(fn_out, "SPC-130")
            self.assertAlmostEqual(
                d.header.macro_time_resolution,
                d2.header.macro_time_resolution)
            self.assertEqual(
                d.header.number_of_micro_time_channels,
                d2.header.number_of_micro_time_channels)
        finally:
            if os.path.isfile(fn_out):
                os.unlink(fn_out)

    def test_source_resolution_carried_into_ptu(self):
        # The source resolution tags must survive a cross-format transcode.
        d = self.read("spc132_filename", "SPC-130")
        fn_out = tmp_filename(".ptu")
        try:
            self.assertTrue(d.write(fn_out))
            d2 = tttrlib.TTTR(fn_out)
            self.assertAlmostEqual(
                d.header.macro_time_resolution,
                d2.header.macro_time_resolution)
            self.assertAlmostEqual(
                d.header.micro_time_resolution,
                d2.header.micro_time_resolution)
        finally:
            if os.path.isfile(fn_out):
                os.unlink(fn_out)

    def test_ptu_gets_minimal_mandatory_tags(self):
        # A PTU file is only valid with the record encoding and record count.
        d = self.read("spc132_filename", "SPC-130")
        fn_out = tmp_filename(".ptu")
        try:
            self.assertTrue(d.write(fn_out))
            d2 = tttrlib.TTTR(fn_out)
            names = _tag_names(d2)
            for mandatory in (
                "TTResultFormat_TTTRRecType",
                "TTResultFormat_BitsPerRecord",
                "TTResult_NumberOfRecords",
                "MeasDesc_GlobalResolution",
            ):
                self.assertIn(mandatory, names)
            # The record count tag must match the number of events written.
            tags = json.loads(d2.header.json)["tags"]
            n_records = next(t["value"] for t in tags
                             if t["name"] == "TTResult_NumberOfRecords")
            self.assertEqual(n_records, len(d.macro_times))
            self.assertEqual(len(d2.macro_times), len(d.macro_times))
        finally:
            if os.path.isfile(fn_out):
                os.unlink(fn_out)

    def test_fresh_container_writes_valid_files(self):
        # A TTTR built from bare arrays (no source header) must still produce
        # valid, readable files thanks to the injected minimal metadata.
        src = self.read("spc132_filename", "SPC-130")
        d = tttrlib.TTTR()
        d.append_events(
            macro_times=np.asarray(src.macro_times).copy(),
            micro_times=np.asarray(src.micro_times).copy(),
            routing_channels=np.asarray(src.routing_channels).copy(),
            event_types=np.asarray(src.event_types).copy(),
        )
        for suffix, container in ((".ptu", "PTU"), (".ht3", "HT3"),
                                  (".spc", "SPC-130")):
            fn_out = tmp_filename(suffix)
            try:
                self.assertTrue(d.write(fn_out), "write failed for %s" % suffix)
                d2 = tttrlib.TTTR(fn_out, container)
                self.assertEqual(len(d2.macro_times), len(d.macro_times),
                                 "event count changed for %s" % suffix)
                np.testing.assert_array_equal(d.macro_times, d2.macro_times)
            finally:
                if os.path.isfile(fn_out):
                    os.unlink(fn_out)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found, skipping .set tests")
class BHSetSidecarRoundTripTests(unittest.TestCase):
    """Becker & Hickl .set sidecar files carry the CLSM imaging settings that
    the .spc record stream cannot. Writing an .spc emits a companion .set, and
    the settings survive a full .spc+.set -> .ptu -> .spc+.set round trip."""

    def setUp(self):
        self.spc = settings.get("bh_spcm_clsm_m1_filename")
        self.set = settings.get("bh_spcm_set_m1_filename")
        if not self.spc or not os.path.isfile(self.spc):
            self.skipTest("missing BH SPC imaging data")
        if not self.set or not os.path.isfile(self.set):
            self.skipTest("missing BH .set sidecar")
        self.work = tempfile.mkdtemp()

    def tearDown(self):
        import shutil
        if getattr(self, "work", None) and os.path.isdir(self.work):
            shutil.rmtree(self.work)

    def test_reading_spc_captures_set_sidecar(self):
        d = tttrlib.TTTR(self.spc, "SPC-130")
        names = _tag_names(d)
        # imaging geometry parsed, plus the raw .set preserved
        self.assertIn("ImgHdr_PixX", names)
        self.assertIn("ImgHdr_PixY", names)
        self.assertIn("BH_SPC_SetFile", names)

    def test_writing_spc_emits_set_sidecar(self):
        d = tttrlib.TTTR(self.spc, "SPC-130")
        out_spc = os.path.join(self.work, "out.spc")
        out_set = os.path.join(self.work, "out.set")
        self.assertTrue(d.write(out_spc))
        self.assertTrue(os.path.isfile(out_set), "no .set sidecar written")
        # Byte-identical to the original .set (verbatim preservation).
        with open(self.set, "rb") as f:
            original = f.read()
        with open(out_set, "rb") as f:
            written = f.read()
        self.assertEqual(written, original)

    def test_full_roundtrip_through_ptu(self):
        d = tttrlib.TTTR(self.spc, "SPC-130")

        ptu = os.path.join(self.work, "conv.ptu")
        self.assertTrue(d.write(ptu))
        d2 = tttrlib.TTTR(ptu)
        # The raw .set rides along inside the PTU header.
        self.assertIn("BH_SPC_SetFile", _tag_names(d2))

        back_spc = os.path.join(self.work, "back.spc")
        back_set = os.path.join(self.work, "back.set")
        self.assertTrue(d2.write(back_spc))
        self.assertTrue(os.path.isfile(back_set))

        with open(self.set, "rb") as f:
            original = f.read()
        with open(back_set, "rb") as f:
            regenerated = f.read()
        self.assertEqual(regenerated, original,
                         "the .set was not preserved through the PTU round trip")

        # And the regenerated pair still reconstructs the imaging geometry.
        d3 = tttrlib.TTTR(back_spc, "SPC-130")
        t0 = {t["name"]: t.get("value")
              for t in json.loads(d.header.json)["tags"]}
        t3 = {t["name"]: t.get("value")
              for t in json.loads(d3.header.json)["tags"]}
        self.assertEqual(t3.get("ImgHdr_PixX"), t0.get("ImgHdr_PixX"))
        self.assertEqual(t3.get("ImgHdr_PixY"), t0.get("ImgHdr_PixY"))

    def test_imaging_ptu_auto_opens_as_clsm_image(self):
        # A BH SPC image converted to PTU keeps its frame/line markers, and the
        # reading-routine hint makes it auto-open as the identical CLSM image.
        d = tttrlib.TTTR(self.spc, "SPC-130")
        ref = tttrlib.CLSMImage(tttr_data=d, reading_routine="BH_SPC130")
        self.assertGreater(ref.n_frames, 0)

        ptu = os.path.join(self.work, "img.ptu")
        self.assertTrue(d.write(ptu))
        d2 = tttrlib.TTTR(ptu)
        self.assertIn("BH_SPC_ReadingRoutine", _tag_names(d2))

        # No reading_routine specified: the header hint selects BH_SPC130.
        clsm = tttrlib.CLSMImage(tttr_data=d2)
        self.assertEqual(clsm.intensity.shape, ref.intensity.shape)
        np.testing.assert_array_equal(clsm.intensity, ref.intensity)


if __name__ == '__main__':
    unittest.main()
