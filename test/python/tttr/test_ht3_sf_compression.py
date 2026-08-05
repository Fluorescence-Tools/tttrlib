"""SF-compressed HT3: Suren Felekyan's macro-time compression.

A plain HydraHarp v1 T3 file spends one 32-bit overflow record per macro-time
wraparound. The SF variant puts a 24-bit *count* in that record's payload, so a
long gap costs one record instead of thousands. The header is the ordinary
HydraHarp v1 header -- there is no flag -- so the encoding is recognised from
the record stream: a v1 file whose overflow records carry a non-zero payload is
SF-compressed, because a genuine v1 overflow payload is always zero.

The compression is in the macro times, so that is what has to survive.
"""
import os
import tempfile

import numpy as np
import pytest
import tttrlib
from test_settings import DATA_ROOT, DATA_AVAILABLE  # type: ignore

pytestmark = pytest.mark.skipif(not DATA_AVAILABLE, reason="test data not available")

SF_HT3 = 14
HHT3v1 = 3
HHT3v2 = 4
HT3 = 1


def _sf_files():
    out = []
    for dirpath, _, files in os.walk(DATA_ROOT):
        for fn in files:
            if not fn.lower().endswith(".ht3"):
                continue
            path = os.path.join(dirpath, fn)
            raw = np.fromfile(path, dtype=np.uint32)
            overflow = raw[(raw >> 25) == 0x7F]
            if overflow.size and int((overflow & 0xFFFFFF != 0).sum()):
                out.append(path)
    return out


@pytest.fixture(scope="module")
def sf_path():
    files = _sf_files()
    if not files:
        pytest.skip("no SF-compressed HT3 file in the test data")
    return files[0]


def test_sf_is_detected_from_the_record_stream(sf_path):
    """No header flag says SF; the counted overflow records do."""
    d = tttrlib.TTTR(sf_path)
    assert tttrlib.inferTTTRFileType(sf_path) == HT3
    assert d.header.tttr_record_type == SF_HT3


def test_macro_times_are_expanded_and_monotonic(sf_path):
    d = tttrlib.TTTR(sf_path)
    mt = d.macro_times
    assert d.n_valid_events > 0
    assert np.all(np.diff(mt.astype(np.int64)) >= 0), "arrival times must not go backwards"
    # A counted overflow spans many wraparounds, so the expanded range is far
    # larger than the number of records -- that is the compression.
    assert int(mt.max()) > 10 * d.n_valid_events


@pytest.mark.parametrize("record_type,label", [
    (SF_HT3, "SF"), (HHT3v1, "HHT3v1"), (HHT3v2, "HHT3v2"),
])
def test_every_ht3_encoding_round_trips_the_macro_times(sf_path, record_type, label):
    """Writing an SF source as any HT3 encoding preserves arrival times.

    Regression: the HT3 header writer inherited FormatVersion from the source
    instead of deriving it from the records being written, so an SF source
    written as HHT3v2 kept "1.0". The reader then chose HHT3v1, ran SF
    detection, and an HHT3v2 overflow -- which legitimately carries a count --
    looked exactly like an SF one.
    """
    src = tttrlib.TTTR(sf_path)
    header = tttrlib.TTTRHeader(src.header)
    header.tttr_record_type = record_type
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, f"as_{label}.ht3")
        src.write(out, header, HT3)
        back = tttrlib.TTTR(out)
        assert back.header.tttr_record_type == record_type, "wrong encoding on read-back"
        assert np.array_equal(back.macro_times, src.macro_times)
        assert np.array_equal(back.micro_times, src.micro_times)


def test_sf_is_smaller_than_the_uncompressed_encoding(sf_path):
    """The point of SF: the same photons in fewer bytes.

    How many fewer depends entirely on how sparse the stream is -- one counted
    overflow replaces as many plain ones as the gap is long. In this project's
    test data the same code gives 1.45x on a dense file and 8.9x on a sparse
    one, so only the direction is worth asserting.

    HHT3v2 is deliberately not compared: it also counts its overflows, so it
    comes out byte-for-byte the same size as SF. The saving is against v1.
    """
    src = tttrlib.TTTR(sf_path)
    sizes = {}
    with tempfile.TemporaryDirectory() as tmp:
        for record_type, label in ((SF_HT3, "sf"), (HHT3v1, "v1")):
            header = tttrlib.TTTRHeader(src.header)
            header.tttr_record_type = record_type
            out = os.path.join(tmp, f"{label}.ht3")
            src.write(out, header, HT3)
            sizes[label] = os.path.getsize(out)
    assert sizes["sf"] < sizes["v1"], sizes
