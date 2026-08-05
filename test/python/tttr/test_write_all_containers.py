"""Every container can be written, and writing does not disturb the source.

Two invariants:

  * macro times survive a transcode into any container. Micro times need not --
    a 15641-channel micro time does not fit an SPC-130's 12 bits, a ConfoCor3's
    1 bit or an SM file's none at all -- but the arrival times must.
  * writing does not change the object being written. It used to: the target
    container and record type were stamped into `this->header`, so a second
    write started from a header describing the first file.
"""
import os
import tempfile

import numpy as np
import pytest
import tttrlib
from test_settings import DATA_ROOT, DATA_AVAILABLE  # type: ignore

pytestmark = pytest.mark.skipif(not DATA_AVAILABLE, reason="test data not available")

# Formats that cannot represent a micro time at the source's resolution. The
# number is the largest micro time the container can hold.
NARROW_MICRO_TIME = {
    "SPC-130": 4095, "SPC-600_256": 255, "SPC-600_4096": 4095,
    "SPC-QC": 4095, "CZ-RAW": 1, "SM": 0,
}


def _source():
    for dirpath, _, files in os.walk(DATA_ROOT):
        for fn in sorted(files):
            if fn.lower().endswith(".ht3"):
                return tttrlib.TTTR(os.path.join(dirpath, fn))
    return None


@pytest.fixture(scope="module")
def src():
    d = _source()
    if d is None:
        pytest.skip("no .ht3 file in the test data")
    return d


CONTAINERS = sorted(
    tttrlib.registry("file_container").items(), key=lambda kv: kv[1]["container_type"]
)


@pytest.mark.parametrize("name,entry", CONTAINERS, ids=[n for n, _ in CONTAINERS])
def test_every_container_writes_and_reads_back(src, name, entry):
    assert entry["can_write"] is True, f"{name} is advertised as unwritable"
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, f"out.{entry['canonical_extension']}")
        src.write(out, name)
        back = tttrlib.TTTR(out, name)

        assert back.n_valid_events == src.n_valid_events
        assert np.array_equal(back.macro_times, src.macro_times), \
            f"{name} did not preserve arrival times"
        if name in NARROW_MICRO_TIME:
            assert back.micro_times.max() <= NARROW_MICRO_TIME[name]
        else:
            assert np.array_equal(back.micro_times, src.micro_times)


def test_writing_does_not_mutate_the_source(src):
    """Regression: a second write used to inherit the first one's header.

    Writing PTU set the source header to container 0 / record 4. The HT3 write
    that followed then emitted HHT3v2 records under a header the reader resolved
    as SF-compressed, and every macro time after the first overflow came back
    multiplied -- with a matching event count.
    """
    container = src.header.tttr_container_type
    record = src.header.tttr_record_type
    macro = src.macro_times.copy()

    with tempfile.TemporaryDirectory() as tmp:
        for name, entry in CONTAINERS:
            src.write(os.path.join(tmp, f"m.{entry['canonical_extension']}"), name)
            assert src.header.tttr_container_type == container, f"after writing {name}"
            assert src.header.tttr_record_type == record, f"after writing {name}"
            assert np.array_equal(src.macro_times, macro), f"after writing {name}"


def test_a_reused_source_writes_every_container_correctly(src):
    """The matrix above, but with one source object rather than a fresh one."""
    macro = src.macro_times.copy()
    with tempfile.TemporaryDirectory() as tmp:
        for name, entry in CONTAINERS:
            out = os.path.join(tmp, f"r_{entry['container_type']}.{entry['canonical_extension']}")
            src.write(out, name)
            assert np.array_equal(tttrlib.TTTR(out, name).macro_times, macro), name
